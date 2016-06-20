// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "core_io.h"
#include "main.h"
#include "init.h"
#include "chainparams.h"

#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"

// 12.1 - which are required?
//#include "flat-database.h"
#include "governance.h"
#include "governance-classes.h"
#include "masternode.h"
#include "governance.h"
#include "darksend.h"
#include "masternodeman.h"
//#include "masternode-sync.h"
#include "util.h"
#include "addrman.h"
#include <boost/lexical_cast.hpp>
#include <univalue.h>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

using namespace std;

class CNode;

// DECLARE GLOBAL VARIABLES FOR GOVERNANCE CLASSES
CGovernanceTriggerManager triggerman;

// SPLIT UP STRING BY DELIMITER

/*  
    NOTE : SplitBy can be simplified via:
    http://www.boost.org/doc/libs/1_58_0/doc/html/boost/algorithm/split_idp202406848.html
*/

std::vector<std::string> SplitBy(std::string strCommand, std::string strDelimit)
{
    std::vector<std::string> vParts;
    boost::split(vParts, strCommand, boost::is_any_of(strDelimit));

    for(int q=0; q<(int)vParts.size(); q++)
    {
        if(strDelimit.find(vParts[q]) != std::string::npos)
        {
            vParts.erase(vParts.begin()+q);
            --q;
        }
    }

   return vParts;
}


/**
*   Add Governance Object
*/

bool CGovernanceTriggerManager::AddNewTrigger(uint256 nHash)
{
    if(!mapTrigger.count(nHash))
    {
        if(IsValidTrigger(nHash))
        {
            mapTrigger.insert(make_pair(nHash, SEEN_OBJECT_IS_VALID));
            return true;
        }
    }

    return false;
}

/**
*   Is Valid Trigger
*/

bool CGovernanceTriggerManager::IsValidTrigger(uint256 nHash)
{
    CGovernanceObject* pObj = governance.FindGovernanceObject(nHash);

    if(pObj)
    {
        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) return false;

        // CHECK TRIGGER SUBTYPES, DO PER-TYPE EVALUATION

        if(pObj->GetObjectSubtype() == TRIGGER_SUPERBLOCK)
        {
            CSuperblock t(pObj);
            if(t.IsExecuted()) return false;
        }

        /*
            IS IT ACTIVATED?
            - this shouldn't be important, because votes can change over time
        */
    } else {
        // it's gone...
        return false;
    }

    return true;
}

/**
*
*   Clean And Remove
*
*/

void CGovernanceTriggerManager::CleanAndRemove()
{
    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    std::map<uint256, int>::iterator it1 = mapTrigger.begin();
    while(it1 != mapTrigger.end())
    {
        bool fErase = IsValidTrigger((*it1).first);

        if(fErase)
        {
            mapTrigger.erase(it1++);
        } else {
            it1++;
        }
    }
}


/**
*   Get Active Triggers
*
*   - Look through triggers and scan for active ones
*   - Return the triggers in a list
*/

std::vector<CGovernanceObject*> CGovernanceTriggerManager::GetActiveTriggers()
{
    std::vector<CGovernanceObject*> vecResults;

    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    std::map<uint256, int>::iterator it1 = mapTrigger.begin();
    while(it1 != mapTrigger.end()){

        CGovernanceObject* pObj = governance.FindGovernanceObject((*it1).first);
        bool fErase = true;

        if(pObj)
        {
            fErase = false;
            if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) fErase = true;

            // 12.1 - todo
            // if(pObj->IsExecuted()) fErase = true;
        }       

        if(fErase)
        {
            mapTrigger.erase(it1);
        } else {
            CSuperblock t(pObj);
            vecResults.push_back(&t);
        }

    }

    return vecResults;
} 

/**
*   Is Valid Superblock Height
*
*   - See if this block can be a superblock
*/

bool CSuperblockManager::IsValidSuperblockHeight(int nBlockHeight)
{
    // SUPERBLOCKS CAN HAPPEN ONCE PER DAY
    return nBlockHeight % Params().GetConsensus().nSuperblockCycle;
}

/**
*   Is Superblock Triggered
*
*   - Does this block have a non-executed and actived trigger?
*/

bool CSuperblockManager::IsSuperblockTriggered(int nBlockHeight)
{
    // GET ALL ACTIVE TRIGGERS
    std::vector<CGovernanceObject*> vecTriggers = triggerman.GetActiveTriggers();

    BOOST_FOREACH(CGovernanceObject* pObj, vecTriggers)
    {
        if(pObj)
        {
            bool fIsTriggered = true;
            CSuperblock superblock(pObj);
            
            // 12.1 - todo 
            // if(!superblock.IsActivated(nBlockHeight))
            // {
            //     fIsTriggered = false;
            // }

            // TODO : MORE CHECKS?
            
            return fIsTriggered;
        }       
    }

    return false;
}


bool CSuperblockManager::GetBestSuperblock(CSuperblock& block)
{
    // 12.1 - TODO
    return true;
}

/**
*   Create Superblock Payments
*
*   - Create the correct payment structure for a given superblock
*/

void CSuperblockManager::CreateSuperblock(CMutableTransaction& txNew, CAmount nFees)
{
    AssertLockHeld(cs_main);
    if(!chainActive.Tip()) return;

    /*
        - resize txNew.size() to superblock vecPayments + 1
        - loop through each payment in superblock and set txNew
    */

    CSuperblock superblock;
    if(!CSuperblockManager::GetBestSuperblock(superblock))
    {
        // 12.1 - todo --logging
        return;
    }

    // CONFIGURE SUPERBLOCK OUTPUTS 

    txNew.vout.resize(superblock.CountPayments());
    for(int i = 0; i <= superblock.CountPayments(); i++)
    {
        CGovernancePayment payment;
        if(superblock.GetPayment(i, payment))
        {
            // SET COINBASE OUTPUT TO SUPERBLOCK SETTING

            txNew.vout[i].scriptPubKey = payment.script;
            txNew.vout[i].nValue = payment.nAmount;

            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);

            // TODO: PRINT NICE N.N DASH OUTPUT

            LogPrintf("SUPERBLOCK: output n %d payment %d to %s\n", payment.nAmount, address2.ToString());
        }
    }
}

bool CSuperblockManager::IsValid(const CTransaction& txNew, int nBlockHeight)
{
    // LOOP THROUGH VALID SUPERBLOCKS TO TRY AND FIND A MATCH
    // 12.1 - todo

    return true;
}

/**
*   Is Transaction Valid
*
*   - Does this transaction match the superblock?
*/

bool CSuperblock::IsTransactionValid(const CTransaction& txNew)
{
    // TODO : LOCK(cs);
    std::string strPayeesPossible = "";

    // CONFIGURE SUPERBLOCK OUTPUTS 

    int nPayments = CountPayments();    
    for(int i = 0; i <= nPayments; i++)
    {
        CGovernancePayment payment;
        if(GetPayment(i, payment))
        {
            // SET COINBASE OUTPUT TO SUPERBLOCK SETTING

            if(payment.script == txNew.vout[i].scriptPubKey && payment.nAmount == txNew.vout[i].nValue)
            {
                // WE FOUND THE CORRECT SUPERBLOCK OUTPUT!
            } else {
                // MISMATCHED SUPERBLOCK OUTPUT!

                CTxDestination address1;
                ExtractDestination(payment.script, address1);
                CBitcoinAddress address2(address1);

                // TODO: PRINT NICE N.N DASH OUTPUT

                LogPrintf("SUPERBLOCK: output n %d payment %d to %s\n", payment.nAmount, address2.ToString());

                return false;
            }
        }
    }

    return true;
}

/**
*   Get Required Payment String
*
*   - Get a string representing the payments required for a given superblock
*/

std::string CSuperblockManager::GetRequiredPaymentsString(int nBlockHeight)
{
    // TODO : LOCK(cs);

    std::string ret = "Unknown";

    // GET BEST SUPERBLOCK

    CSuperblock superblock;
    if(!CSuperblockManager::GetBestSuperblock(superblock))
    {
        // 12.1 - todo --logging
        return "error";
    }    

    // LOOP THROUGH SUPERBLOCK PAYMENTS, CONFIGURE OUTPUT STRING 

    for(int i = 0; i <= superblock.CountPayments(); i++)
    {
        CGovernancePayment payment;
        if(superblock.GetPayment(i, payment))
        {
            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);

            // RETURN NICE OUTPUT FOR CONSOLE

            if(ret != "Unknown"){
                ret += ", " + address2.ToString();
            } else {
                ret = address2.ToString();
            }
        }
    }

    return ret;
}
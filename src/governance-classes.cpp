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

#include "governance.h"
#include "governance-classes.h"
#include "masternode.h"
#include "governance.h"
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

        CGovernanceObject* pObj = governance.FindGovernanceObject(nHash);

        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER)
        {
            mapTrigger.insert(make_pair(nHash, SEEN_OBJECT_IS_VALID));
            return true;
        }
    }

    return false;
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
        int nNewStatus = -1;
        CGovernanceObject* pObj = governance.FindGovernanceObject((*it1).first);

        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER)
        {
            UpdateStatus((*it1).first, SEEN_OBJECT_ERROR_INVALID);
            it1++;
            continue;
        }

        it1++;
    }
}

/**
*
*   Update Status
*
*/

bool CGovernanceTriggerManager::UpdateStatus(uint256 nHash, int nNewStatus)
{

    if(mapTrigger.count(nHash))
    {
        mapTrigger[nHash] = nNewStatus;
        return true;
    } else {
        LogPrintf("CGovernanceTriggerManager::UpdateStatus - nHash %s nNewStatus %d\n", nHash.ToString(), nNewStatus);
        return false;
    }
}

/**
*
*   Get Status
*
*/

int CGovernanceTriggerManager::GetStatus(uint256 nHash)
{

    if(mapTrigger.count(nHash))
    {
        return mapTrigger[nHash];
    } else {
        LogPrintf("CGovernanceTriggerManager::GetStatus - nHash %s\n", nHash.ToString());
        return false;
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
    int nYesCount = 0;

    BOOST_FOREACH(CGovernanceObject* pObj, vecTriggers)
    {
        if(pObj)
        {
            CSuperblock t(pObj);
            if(nBlockHeight != t.GetBlockStart()) continue;

            // MAKE SURE THIS TRIGGER IS ACTIVE VIA FUNDING CACHE FLAG

            if(pObj->fCachedFunding)
            {
                return true;
            }
        }       
    }

    return nYesCount > 0;
}


bool CSuperblockManager::GetBestSuperblock(CSuperblock* pBlock, int nBlockHeight)
{
    std::vector<CGovernanceObject*> vecTriggers = triggerman.GetActiveTriggers();
    int nYesCount = 0;

    BOOST_FOREACH(CGovernanceObject* pObj, vecTriggers)
    {
        if(pObj)
        {
            CSuperblock t(pObj);
            if(nBlockHeight != t.GetBlockStart()) continue;

            // DO WE HAVE A NEW WINNER?

            int nTempYesCount = pObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
            if(nTempYesCount > nYesCount)
            {
                nYesCount = nTempYesCount;
                pBlock = &t;
            }
        }       
    }

    return nYesCount > 0;
}

/**
*   Create Superblock Payments
*
*   - Create the correct payment structure for a given superblock
*/

void CSuperblockManager::CreateSuperblock(CMutableTransaction& txNew, CAmount nFees, int nBlockHeight)
{
    AssertLockHeld(cs_main);
    if(!chainActive.Tip()) return;

    /*
        - resize txNew.size() to superblock vecPayments + 1
        - loop through each payment in superblock and set txNew
    */

    CSuperblock* pBlock = NULL;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))
    {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        return;
    }

    // CONFIGURE SUPERBLOCK OUTPUTS 

    txNew.vout.resize(pBlock->CountPayments());
    for(int i = 0; i <= pBlock->CountPayments(); i++)
    {
        CGovernancePayment payment;
        if(pBlock->GetPayment(i, payment))
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
    // GET BEST SUPERBLOCK, SHOULD MATCH

    CSuperblock* pBlock = NULL;
    if(CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))
    {
        return pBlock->IsValid(txNew);
    }    
    
    return false;
}

/**
*   Is Transaction Valid
*
*   - Does this transaction match the superblock?
*/

bool CSuperblock::IsValid(const CTransaction& txNew)
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

    CSuperblock* pBlock = NULL;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))
    {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        return "error";
    }    

    // LOOP THROUGH SUPERBLOCK PAYMENTS, CONFIGURE OUTPUT STRING 

    for(int i = 0; i <= pBlock->CountPayments(); i++)
    {
        CGovernancePayment payment;
        if(pBlock->GetPayment(i, payment))
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
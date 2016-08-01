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

    printf("AddNewTrigger 1");

    // IF WE DON'T HAVE THIS HASH, WE SHOULD ADD IT

    if(!mapTrigger.count(nHash))
    {
        printf("2");
        CGovernanceObject* pObj = governance.FindGovernanceObject(nHash);

        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER)
        {
            printf("3\n");
            mapTrigger.insert(make_pair(nHash, SEEN_OBJECT_IS_VALID));
            return true;
        }
    }

    printf("4\n");
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
        //int nNewStatus = -1;
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
        //bool fErase = true;

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
    printf("IsSuperblockTriggered\n");
    std::vector<CGovernanceObject*> vecTriggers = triggerman.GetActiveTriggers();
    //int nYesCount = 0;

    printf("1");

    BOOST_FOREACH(CGovernanceObject* pObj, vecTriggers)
    {
        if(pObj)
        {
            printf("2");

            // note : 12.1 - is epoch calculation correct?

            CSuperblock t(pObj);
            if(nBlockHeight != t.GetBlockStart()) continue;

            // MAKE SURE THIS TRIGGER IS ACTIVE VIA FUNDING CACHE FLAG

            if(pObj->fCachedFunding)
            {
                printf("3\n");
                return true;
            }
        }       
    }

    printf("4\n");
    return false;
}


bool CSuperblockManager::GetBestSuperblock(CSuperblock* pBlock, int nBlockHeight)
{
    std::vector<CGovernanceObject*> vecTriggers = triggerman.GetActiveTriggers();
    int nYesCount = 0;

    printf("GetBestSuperblock\n");

    BOOST_FOREACH(CGovernanceObject* pObj, vecTriggers)
    {
        printf("1");
        if(pObj)
        {
            printf("2");
            CSuperblock t(pObj);
            if(nBlockHeight != t.GetBlockStart()) continue;

            // DO WE HAVE A NEW WINNER?

            int nTempYesCount = pObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
            if(nTempYesCount > nYesCount)
            {
                printf("3");

                nYesCount = nTempYesCount;
                pBlock = &t;
            }
        }       
    }

    printf("4\n");

    return nYesCount > 0;
}

/**
*   Create Superblock Payments
*
*   - Create the correct payment structure for a given superblock
*/

void CSuperblockManager::CreateSuperblock(CMutableTransaction& txNew, CAmount nFees, int nBlockHeight)
{
    printf("CreateSuperblock");

    AssertLockHeld(cs_main);
    if(!chainActive.Tip()) return;

    // GET THE BEST SUPERBLOCK FOR THIS BLOCK HEIGHT

    CSuperblock* pBlock = NULL;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))
    {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        printf("2\n");
        return;
    }

    // CONFIGURE SUPERBLOCK OUTPUTS 

    txNew.vout.resize(pBlock->CountPayments());
    for(int i = 0; i <= pBlock->CountPayments(); i++)
    {
        printf("3");
        CGovernancePayment payment;
        if(pBlock->GetPayment(i, payment))
        {
            printf("4");
            // SET COINBASE OUTPUT TO SUPERBLOCK SETTING

            txNew.vout[i].scriptPubKey = payment.script;
            txNew.vout[i].nValue = payment.nAmount;

            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT

            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);

            // TODO: PRINT NICE N.N DASH OUTPUT

            LogPrintf("NEW Superblock : output %d (addr %s, amount %d)\n", i, address2.ToString(), payment.nAmount);
        }
    }

    printf("5\n");
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
    printf("IsValid");
    // TODO : LOCK(cs);
    std::string strPayeesPossible = "";

    printf("1");

    // CONFIGURE SUPERBLOCK OUTPUTS 

    int nPayments = CountPayments();    
    for(int i = 0; i <= nPayments; i++)
    {
        printf("2");
        CGovernancePayment payment;
        if(GetPayment(i, payment))
        {
            printf("3");
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

                printf("4\n");

                return false;
            }
        }
    }

    printf("5\n");

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
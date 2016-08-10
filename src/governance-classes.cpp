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
    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: Start" << endl; );
    AssertLockHeld(governance.cs);

    // IF WE ALREADY HAVE THIS HASH, RETURN
    if(mapTrigger.count(nHash))  {
        DBG( 
            cout << "CGovernanceTriggerManager::AddNewTrigger: Already have hash"
                 << ", nHash = " << nHash.GetHex()
                 << ", count = " << mapTrigger.count(nHash)
                 << ", mapTrigger.size() = " << mapTrigger.size()
                 << endl; );
        return false;
    }

    CSuperblock_sptr superblock(new CSuperblock(nHash));

    if(superblock->GetErrorState()) {
        DBG( cout << "AddNewTrigger: error in superblock: " << superblock->GetErrorMessage() << endl; );
        LogPrint("superblock", "CGovernanceTriggerManager::AddNewTrigger: Error creating superblock: %s\n", superblock->GetErrorMessage());
        return false;
    }

    superblock->SetStatus(SEEN_OBJECT_IS_VALID);

    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: Inserting trigger" << endl; );
    mapTrigger.insert(make_pair(nHash, superblock));

    DBG( cout << "CGovernanceTriggerManager::AddNewTrigger: End" << endl; );

    return true;
}

/**
*
*   Clean And Remove
*
*/

void CGovernanceTriggerManager::CleanAndRemove()
{
    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: Start" << endl; );
    AssertLockHeld(governance.cs);

    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    for(trigger_m_it it = mapTrigger.begin(); it != mapTrigger.end(); ++it)  {
        //int nNewStatus = -1;
        CGovernanceObject* pObj = governance.FindGovernanceObject((*it).first);
        if(!pObj)  {
            continue;
        }
        CSuperblock_sptr& superblock = it->second;
        if(!superblock)  {
            continue;
        }
        // IF THIS ISN'T A TRIGGER, WHY ARE WE HERE?
        if(pObj->GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) {
            superblock->SetStatus(SEEN_OBJECT_ERROR_INVALID);
        }
    }

    // Remove triggers that are invalid or already executed
    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: mapTrigger.size() = " << mapTrigger.size() << endl; );
    trigger_m_it it = mapTrigger.begin();
    while(it != mapTrigger.end())  {
        bool remove = false;
        CSuperblock_sptr& superblock = it->second;
        if(!superblock)  {
            DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: NULL superblock marked for removal " << endl; );
            remove = true;
        }
        else  {
            DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: superblock status = " << superblock->GetStatus() << endl; );
            switch(superblock->GetStatus())  {
            case SEEN_OBJECT_ERROR_INVALID:
            case SEEN_OBJECT_UNKNOWN:
                remove = true;
                break;
            case SEEN_OBJECT_EXECUTED:
                {
                    CGovernanceObject* govobj = superblock->GetGovernanceObject();
                    if(govobj)  {
                        govobj->fExpired = true;
                    }
                }
                remove = true;
                break;
            case SEEN_OBJECT_IS_VALID:
                {
                    // Rough approximation: 30 days per month * 576 blocks per day
                    static const int nMonthlyBlocks = 30*576;
                    int nTriggerBlock = superblock->GetBlockStart();
                    int nExpirationBlock = nTriggerBlock + nMonthlyBlocks;
                    if(governance.GetCachedBlockHeight() > nExpirationBlock)  {
                        remove = true;
                        CGovernanceObject* govobj = superblock->GetGovernanceObject();
                        if(govobj)  {
                            govobj->fExpired = true;
                        }
                    }
                }
            default:
                break;
            }
        }
        
        if(remove)  {
            DBG( 
                string strdata = "NULL";
                CGovernanceObject* govobj = superblock->GetGovernanceObject();
                if(govobj)  {
                    strdata = govobj->GetDataAsString();
                }
                cout << "CGovernanceTriggerManager::CleanAndRemove: Removing object: " 
                     << strdata
                     << endl;
               );
            mapTrigger.erase(it++);
        }
        else  {
            ++it;
        }
    }

    DBG( cout << "CGovernanceTriggerManager::CleanAndRemove: End" << endl; );
}

/**
*   Get Active Triggers
*
*   - Look through triggers and scan for active ones
*   - Return the triggers in a list
*/

std::vector<CSuperblock_sptr> CGovernanceTriggerManager::GetActiveTriggers()
{
    AssertLockHeld(governance.cs);
    std::vector<CSuperblock_sptr> vecResults;

    DBG( cout << "GetActiveTriggers: mapTrigger.size() = " << mapTrigger.size() << endl; );

    // LOOK AT THESE OBJECTS AND COMPILE A VALID LIST OF TRIGGERS
    trigger_m_it it = mapTrigger.begin();
    while(it != mapTrigger.end()) {

        CGovernanceObject* pObj = governance.FindGovernanceObject((*it).first);

        if(pObj) {
            DBG( cout << "GetActiveTriggers: pObj->GetDataAsString() = " << pObj->GetDataAsString() << endl; );
            vecResults.push_back(it->second);
        }
        ++it;
    }

    DBG( cout << "GetActiveTriggers: vecResults.size() = " << vecResults.size() << endl; );

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
    LOCK(governance.cs);
    // GET ALL ACTIVE TRIGGERS
    std::vector<CSuperblock_sptr> vecTriggers = triggerman.GetActiveTriggers();
    //int nYesCount = 0;

    DBG( cout << "IsSuperblockTriggered Number triggers = " << vecTriggers.size() << endl; );

    BOOST_FOREACH(CSuperblock_sptr superblock, vecTriggers)
    {
        if(!superblock)  {
            DBG( cout << "IsSuperblockTriggered Not a superblock, continuing " << endl; );
            continue;
        }

        CGovernanceObject* pObj = superblock->GetGovernanceObject();

        if(!pObj)  {
            DBG( cout << "IsSuperblockTriggered pObj is NULL, continuing" << endl; );
            continue;
        }

        // note : 12.1 - is epoch calculation correct?

        if(nBlockHeight != superblock->GetBlockStart()) {
            DBG( cout << "IsSuperblockTriggered Not the target block, continuing" 
                      << ", nBlockHeight = " << nBlockHeight
                      << ", superblock->GetBlockStart() = " << superblock->GetBlockStart()
                      << endl; );
            continue;
        }

        // MAKE SURE THIS TRIGGER IS ACTIVE VIA FUNDING CACHE FLAG

        if(pObj->fCachedFunding)  {
            DBG( cout << "IsSuperblockTriggered returning true" << endl; );
            return true;
        }       
        else  {
            DBG( cout << "IsSuperblockTriggered No fCachedFunding, continuing" << endl; );
        }
    }

    return false;
}


bool CSuperblockManager::GetBestSuperblock(CSuperblock_sptr& pBlock, int nBlockHeight)
{
    AssertLockHeld(governance.cs);
    std::vector<CSuperblock_sptr> vecTriggers = triggerman.GetActiveTriggers();
    int nYesCount = 0;

    BOOST_FOREACH(CSuperblock_sptr superblock, vecTriggers)
    {
        if(!superblock)  {
            DBG( cout << "GetBestSuperblock Not a superblock, continuing" << endl; );
            continue;
        }

        CGovernanceObject* pObj = superblock->GetGovernanceObject();

        if(!pObj)  {
            DBG( cout << "GetBestSuperblock pObj is NULL, continuing" << endl; );
            continue;
        }

        if(nBlockHeight != superblock->GetBlockStart()) {
            DBG( cout << "GetBestSuperblock Not the target block, continuing" << endl; );
            continue;
        }
        
        // DO WE HAVE A NEW WINNER?

        int nTempYesCount = pObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING);
        DBG( cout << "GetBestSuperblock nTempYesCount = " << nTempYesCount << endl; );
        if(nTempYesCount > nYesCount)  {
            nYesCount = nTempYesCount;
            pBlock = superblock;
            DBG( cout << "GetBestSuperblock Valid superblock found, pBlock set" << endl; );
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
    DBG( cout << "CSuperblockManager::CreateSuperblock Start" << endl; );

    LOCK(governance.cs);
    AssertLockHeld(cs_main);

    if(!chainActive.Tip()) {
        DBG( cout << "CSuperblockManager::CreateSuperblock No active tip, returning" << endl; );
        return;
    }

    // GET THE BEST SUPERBLOCK FOR THIS BLOCK HEIGHT

    CSuperblock_sptr pBlock;
    if(!CSuperblockManager::GetBestSuperblock(pBlock, nBlockHeight))  {
        LogPrint("superblock", "CSuperblockManager::CreateSuperblock: Can't find superblock for height %d\n", nBlockHeight);
        DBG( cout << "CSuperblockManager::CreateSuperblock Failed to get superblock for height, returning" << endl; );
        return;
    }

    // CONFIGURE SUPERBLOCK OUTPUTS 

    DBG( cout << "CSuperblockManager::CreateSuperblock Number payments: " << pBlock->CountPayments() << endl; );

    txNew.vout.resize(pBlock->CountPayments());
    for(int i = 0; i <= pBlock->CountPayments(); i++)  {
        CGovernancePayment payment;
        DBG( cout << "CSuperblockManager::CreateSuperblock i = " << i << endl; );
        if(pBlock->GetPayment(i, payment))  {
            DBG( cout << "CSuperblockManager::CreateSuperblock Payment found " << endl; );
            // SET COINBASE OUTPUT TO SUPERBLOCK SETTING
            
            txNew.vout[i].scriptPubKey = payment.script;
            txNew.vout[i].nValue = payment.nAmount;
            
            // PRINT NICE LOG OUTPUT FOR SUPERBLOCK PAYMENT
            
            CTxDestination address1;
            ExtractDestination(payment.script, address1);
            CBitcoinAddress address2(address1);
            
            // TODO: PRINT NICE N.N DASH OUTPUT
            
            DBG( cout << "CSuperblockManager::CreateSuperblock Before LogPrintf call " << endl; );
            LogPrintf("NEW Superblock : output %d (addr %s, amount %d)\n", i, address2.ToString(), payment.nAmount);
            DBG( cout << "CSuperblockManager::CreateSuperblock After LogPrintf call " << endl; );
            pBlock->SetExecuted();
        }
        else  {
            DBG( cout << "CSuperblockManager::CreateSuperblock Payment not found " << endl; );
        }
    }

    DBG( cout << "CSuperblockManager::CreateSuperblock End" << endl; );
}

bool CSuperblockManager::IsValid(const CTransaction& txNew, int nBlockHeight)
{
    // GET BEST SUPERBLOCK, SHOULD MATCH
    LOCK(governance.cs);

    CSuperblock_sptr pBlock;
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

                LogPrintf("SUPERBLOCK: output n %d payment %d to %s\n", i, payment.nAmount, address2.ToString());

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
    LOCK(governance.cs);
    std::string ret = "Unknown";

    // GET BEST SUPERBLOCK

    CSuperblock_sptr pBlock;
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

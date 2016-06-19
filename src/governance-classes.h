
// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOVERANCE_CLASSES_H
#define GOVERANCE_CLASSES_H

#include "main.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "masternode.h"
#include <boost/lexical_cast.hpp>
#include "init.h"

using namespace std;

#define TRIGGER_UNKNOWN             -1
#define TRIGGER_SUPERBLOCK          1000

class CSuperblock;
class CGovernanceTrigger;
class CGovernanceTriggerManager;

// DECLARE GLOBAL VARIABLES FOR GOVERNANCE CLASSES
extern CGovernanceTriggerManager triggerman;

// SPLIT A STRING UP - USED FOR SUPERBLOCK PAYMENTS
std::vector<std::string> SplitBy(std::string strCommand, std::string strDelimit);

/**
*   Trigger Mananger
*
*   - Track governance objects which are triggers
*   - After triggers are activated and executed, they can be removed
*/

class CGovernanceTriggerManager
{

private:
    std::map<uint256, int> mapTrigger;

public:
    std::vector<CGovernanceObject*> GetActiveTriggers();
    bool AddNewTrigger(uint256 nHash);
    void CleanAndRemove();
    bool IsValidTrigger(uint256);
};


class CSuperblockManager
{
public:
    static bool IsBlockValid(const CTransaction& txNew)
    {
        // 12.1 -- todo
        // for(unsigned int i = 0; i < (int)txNew.vout.size(); i++)
        // {
        //     BOOST_FOREACH(const CTxOut& v, txNew[i].vout)
        //     {
        //         txNew.vout.push_back(v);
        //     }
        // }

        // bool nExecuted = true;
        return true;
    }

    static bool IsValidSuperblockHeight(int nBlockHeight);
    static bool IsSuperblockTriggered(int nBlockHeight);
    static void CreateSuperblock(CMutableTransaction& txNew, CAmount nFees);
    static std::string GetRequiredPaymentsString(int nBlockHeight);
    static bool IsValid(const CTransaction& txNew, int nBlockHeight);
    static bool GetBestSuperblock(CSuperblock& block);
};

/**
*   Governance Object Payment
*
*/

class CGovernancePayment
{
public:
    CScript script;
    CAmount nAmount;
    bool fValid; 

    CGovernancePayment()
    {
        SetNull();
    }

    void SetNull()
    {
        script = CScript();
        nAmount = 0;
        fValid = false;
    }

    bool IsValid()
    {
        return fValid;
    }

    CGovernancePayment(CBitcoinAddress addrIn, CAmount nAmountIn)
    {
        try
        {
            CTxDestination dest = addrIn.Get();
            script = GetScriptForDestination(dest);
            nAmount = nAmountIn;
        } catch(...) {
            SetNull(); //set fValid to false
        }
    }
};


/**
*   Trigger : Superblock
*
*   - Create payments on the network
*/

class CSuperblock : public CGovernanceObject
{

    /*

        object structure: 
        {
            "governance_object_id" : last_id,
            "type" : govtypes.trigger,
            "subtype" : "superblock",
            "superblock_name" : superblock_name,
            "start_epoch" : start_epoch,
            "payment_addresses" : "addr1|addr2|addr3",
            "payment_amounts"   : "amount1|amount2|amount3"
        }
    */

private:
    CGovernanceObject* pGovObj;

    bool fError;
    std::string strError;

    int nEpochStart;
    bool nExecuted;
    std::vector<CGovernancePayment> vecPayments;

public:

    CSuperblock(){}

    CSuperblock(CGovernanceObject* pGovObjIn)
    {
        if(!pGovObj) return;
        pGovObj = pGovObjIn;

        UniValue obj(UniValue::VOBJ);
        
        if(pGovObj->GetData(obj))
        {
            vecPayments.clear();

            // FIRST WE GET THE START EPOCH, THE DATE WHICH THE PAYMENT SHALL OCCUR
            strError = "Error parsing start epoch";
            nEpochStart = obj["start_epoch"].get_int();

            // NEXT WE GET THE PAYMENT INFORMATION AND RECONSTRUCT THE PAYMENT VECTOR
            strError = "Missing payment information";
            std::string strAddresses = obj["payment_addresses"].get_str();
            std::string strAmounts = obj["payment_amounts"].get_str();
            ParsePaymentSchedule(strAddresses, strAmounts);

            nExecuted = false;
            fError = false;
            strError = "";
        } else {
            fError = true;
            strError = "Unparsable";
        }
    }

    bool ParsePaymentSchedule(std::string& strPaymentAddresses, std::string& strPaymentAmounts)
    {
        // SPLIT UP ADDR/AMOUNT STRINGS AND PUT IN VECTORS

        std::vector<std::string> vecParsed1;
        std::vector<std::string> vecParsed2;
        vecParsed1 = SplitBy(strPaymentAddresses, "|");
        vecParsed2 = SplitBy(strPaymentAmounts, "|");

        // IF THESE DONT MATCH, SOMETHING IS WRONG

        if(vecParsed1.size() != vecParsed2.size()) 
        {
            strError = "Mismatched payments and amounts";
            return false;
        }

        // LOOP THROUGH THE ADDRESSES/AMOUNTS AND CREATE PAYMENTS
        /*
            ADDRESSES = [ADDR1|2|3|4|5\6]
            AMOUNTS = [AMOUNT1|2|3|4|5\6]
        */

        for(int i = 0; i < (int)vecParsed1.size(); i++)
        {
            CBitcoinAddress address(vecParsed1[i]);
            if (!address.IsValid())
            {
                strError = "Invalid Dash Address : " +  vecParsed1[i];
                return false;
            }
            int nAmount = boost::lexical_cast<int>(vecParsed2[i]);

            CGovernancePayment payment(address, nAmount);
            if(payment.IsValid())
            {
                vecPayments.push_back(payment);   
            }
        }

        return false;
    }

    /**
    * IS THIS TRIGGER ALREADY EXECUTED?
    */
    bool IsExecuted()
    {
        // 12.1 -- where should this happen?
        // return pGovObj->IsExecuted();
        return true;
    };

    /**
    * TELL THE ENGINE WE EXECUTED THIS EVENT
    */
    void SetExecuted()
    {
        // 12.1 -- todo
        //pGovObj->SetExecuted(true);
        return;
    };

    int CountPayments()
    {
        return (int)vecPayments.size();
    }

    bool GetPayment(int nPaymentIndex, CGovernancePayment& paymentOut)
    {
        if(nPaymentIndex >= (int)vecPayments.size()) 
        {
            return false;
        }

        paymentOut = vecPayments[nPaymentIndex];
        return true;
    }

    bool IsTransactionValid(const CTransaction& txNew);
};

#endif

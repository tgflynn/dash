// Copyright (c) 2014-2016 The Dash Core developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GOVERANCE_H
#define GOVERANCE_H

#include "main.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "masternode.h"
#include "governance-vote.h"
#include "masternodeman.h"
#include <boost/lexical_cast.hpp>
#include "init.h"
#include <univalue.h>
#include "utilstrencodings.h"

#include <stdio.h>
#include <string.h>

using namespace std;

#define GOVERNANCE_OBJECT_PROPOSAL          1
#define GOVERNANCE_OBJECT_TRIGGER           2

extern CCriticalSection cs_budget;

class CGovernanceManager;
class CGovernanceObject;
class CGovernanceVote;
class CNode;

static const CAmount GOVERNANCE_FEE_TX = (0.1*COIN);
static const int64_t GOVERNANCE_FEE_CONFIRMATIONS = 6;
static const int64_t GOVERNANCE_UPDATE_MIN = 60*60;

extern std::vector<CGovernanceObject> vecImmatureGovernanceObjects;
extern std::map<uint256, int64_t> mapAskedForGovernanceObject;
extern CGovernanceManager governance;

// FOR SEEN MAP ARRAYS - GOVERNANCE OBJECTS AND VOTES
#define SEEN_OBJECT_IS_VALID          0
#define SEEN_OBJECT_ERROR_INVALID     1
#define SEEN_OBJECT_ERROR_IMMATURE    2
#define SEEN_OBJECT_EXECUTED          3 //used for triggers

//Check the collateral transaction for the budget proposal/finalized budget
extern bool IsCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int& nConf, CAmount minFee);


//
// Governance Manager : Contains all proposals for the budget
//
class CGovernanceManager
{
private:

    //hold txes until they mature enough to use
    map<uint256, CTransaction> mapCollateral;
    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    int64_t nTimeLastDiff;

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    
    // keep track of the scanning errors I've seen
    map<uint256, CGovernanceObject> mapObjects;

    // todo - 12.1 - move to private for better encapsulation 
    std::map<uint256, int> mapSeenGovernanceObjects;
    std::map<uint256, int> mapSeenVotes;
    std::map<uint256, CGovernanceVote> mapOrphanVotes;

    // todo: one of these should point to the other
    //   -- must be carefully managed while adding/removing/updating
    std::map<uint256, CGovernanceVote> mapVotesByHash;
    std::map<uint256, CGovernanceVote> mapVotesByType;

    CGovernanceManager() {
        mapObjects.clear();
    }

    void ClearSeen() {
        mapSeenGovernanceObjects.clear();
        mapSeenVotes.clear();
    }

    int CountProposalInventoryItems()
    {
        return mapSeenGovernanceObjects.size() + mapSeenVotes.size();
    }

    int sizeProposals() {return (int)mapObjects.size();}

    void Sync(CNode* node, uint256 nProp);
    void SyncParentObjectByVote(CNode* pfrom, const CGovernanceVote& vote);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();

    CGovernanceObject *FindGovernanceObject(const std::string &strName);
    CGovernanceObject *FindGovernanceObject(const uint256& nHash);

    std::vector<CGovernanceVote*> GetMatchingVotes(const uint256& nParentHash);
    std::vector<CGovernanceObject*> GetAllNewerThan(int64_t nMoreThanTime);

    int CountMatchingVotes(CGovernanceObject& govobj, int nVoteSignalIn, int nVoteOutcomeIn);

    bool IsBudgetPaymentBlock(int nBlockHeight);
    bool AddGovernanceObject (CGovernanceObject& govobj);
    bool UpdateGovernanceObject(const CGovernanceVote& vote, CNode* pfrom, std::string& strError);
    bool AddOrUpdateVote(const CGovernanceVote& vote, std::string& strError);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void CleanAndRemove(bool fSignatureCheck);
    void CheckAndRemove();

    void CheckOrphanVotes();
    void Clear(){
        LOCK(cs);

        LogPrint("gobject", "Governance object manager was cleared\n");
        mapObjects.clear();
        mapSeenGovernanceObjects.clear();
        mapSeenVotes.clear();
        mapOrphanVotes.clear();
        mapVotesByType.clear();
        mapVotesByHash.clear();
    }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapSeenGovernanceObjects);
        READWRITE(mapSeenVotes);
        READWRITE(mapOrphanVotes);
        READWRITE(mapObjects);
        READWRITE(mapVotesByHash);
        READWRITE(mapVotesByType);
    }

    void UpdatedBlockTip(const CBlockIndex *pindex);
    int64_t GetLastDiffTime() {return nTimeLastDiff;}
    void UpdateLastDiffTime(int64_t nTimeIn) {nTimeLastDiff=nTimeIn;}
};

/**
* Governance Object
*
*/

class CGovernanceObject
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    CAmount nAlloted;

public:

    uint256 nHashParent; //parent object, 0 is root
    int nRevision; //object revision in the system
    std::string strName; //org name, username, prop name, etc. 
    int64_t nTime; //time this object was created
    uint256 nCollateralHash; //fee-tx
    std::string strData; // Data field - can be used for anything
    int nObjectType;

    bool fCachedLocalValidity; // is valid by blockchain 
    std::string strLocalValidityError;

    // VARIOUS FLAGS FOR OBJECT / SET VIA MASTERNODE VOTING

    bool fCachedFunding; // true == minimum network support has been reached for this object to be funded (doesn't mean it will for sure though)
    bool fCachedValid; // true == minimum network has been reached flagging this object as a valid and understood goverance object (e.g, the serialized data is correct format, etc)
    bool fCachedDelete; // true == minimum network support has been reached saying this object should be deleted from the system entirely
    bool fCachedEndorsed; // true == minimum network support has been reached flagging this object as endorsed by an elected representative body (e.g. business review board / technecial review board /etc)
    bool fDirtyCache; // object was updated and cached values should be updated soon
    bool fUnparsable; // data field was unparsible, object will be rejected

    CGovernanceObject();
    CGovernanceObject(uint256 nHashParentIn, int nRevisionIn, std::string strNameIn, int64_t nTime, uint256 nCollateralHashIn, std::string strDataIn);
    CGovernanceObject(const CGovernanceObject& other);
    void swap(CGovernanceObject& first, CGovernanceObject& second); // nothrow

    // Update local validity : store the values in memory
    bool IsValidLocally(const CBlockIndex* pindex, std::string& strError, bool fCheckCollateral=true);
    void UpdateLocalValidity(const CBlockIndex *pCurrentBlockIndex) {fCachedLocalValidity = IsValidLocally(pCurrentBlockIndex, strLocalValidityError);};
    void UpdateSentinelVariables(const CBlockIndex *pCurrentBlockIndex);

    int GetObjectType();
    int GetObjectSubtype();
    std::string GetName() {return strName; }

    // GET VOTE COUNT FOR SIGNAL

    int GetAbsoluteYesCount(int nVoteSignalIn);
    int GetAbsoluteNoCount(int nVoteSignalIn);
    int GetYesCount(int nVoteSignalIn);
    int GetNoCount(int nVoteSignalIn);
    int GetAbstainCount(int nVoteSignalIn);

    void CleanAndRemove(bool fSignatureCheck);
    void Relay();

    uint256 GetHash();

    // FUNCTIONS FOR DEALING WITH DATA STRING 

    void LoadData();
    bool SetData(std::string& strError, std::string strDataIn);
    bool GetData(UniValue& objResult);
    std::string GetDataAsHex();
    std::string GetDataAsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        // SERIALIZE DATA FOR SAVING/LOADING OR NETWORK FUNCTIONS


        READWRITE(nHashParent);
        READWRITE(nRevision);
        READWRITE(LIMITED_STRING(strName, 64));
        READWRITE(nTime);
        READWRITE(nCollateralHash);
        READWRITE(strData);

        // AFTER DESERIALIZATION OCCURS, CACHED VARIABLES MUST BE CALCULATED MANUALLY
    }
};


#endif

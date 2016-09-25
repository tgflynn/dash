// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERANCE_H
#define GOVERANCE_H

//#define ENABLE_DASH_DEBUG

#include "util.h"
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
#include "cachemap.h"

#include <stdio.h>
#include <string.h>

class CGovernanceManager;
class CGovernanceTriggerManager;
class CGovernanceObject;
class CGovernanceVote;

static const int MAX_GOVERNANCE_OBJECT_DATA_SIZE = 16 * 1024;

static const int GOVERNANCE_OBJECT_UNKNOWN = 0;
static const int GOVERNANCE_OBJECT_PROPOSAL = 1;
static const int GOVERNANCE_OBJECT_TRIGGER = 2;

static const CAmount GOVERNANCE_PROPOSAL_FEE_TX = (0.33*COIN);

static const int64_t GOVERNANCE_FEE_CONFIRMATIONS = 6;
static const int64_t GOVERNANCE_UPDATE_MIN = 60*60;

// FOR SEEN MAP ARRAYS - GOVERNANCE OBJECTS AND VOTES
static const int SEEN_OBJECT_IS_VALID = 0;
static const int SEEN_OBJECT_ERROR_INVALID = 1;
static const int SEEN_OBJECT_ERROR_IMMATURE = 2;
static const int SEEN_OBJECT_EXECUTED = 3; //used for triggers
static const int SEEN_OBJECT_UNKNOWN = 4; // the default

extern std::map<uint256, int64_t> mapAskedForGovernanceObject;
extern CGovernanceManager governance;

//
// Governance Manager : Contains all proposals for the budget
//
class CGovernanceManager : public IMasternodeIndexUpdateReceiver
{
public: // Types

    typedef std::map<uint256, CGovernanceObject> object_m_t;

    typedef object_m_t::iterator object_m_it;

    typedef object_m_t::const_iterator object_m_cit;

    typedef std::map<uint256, int> count_m_t;

    typedef count_m_t::iterator count_m_it;

    typedef count_m_t::const_iterator count_m_cit;

    typedef std::map<uint256, CGovernanceVote> vote_m_t;

    typedef vote_m_t::iterator vote_m_it;

    typedef vote_m_t::const_iterator vote_m_cit;

    typedef CacheMap<uint256, CGovernanceVote> vote_cache_t;

    typedef object_m_t::size_type size_type;

    typedef std::map<COutPoint, int> txout_m_t;

    typedef txout_m_t::iterator txout_m_it;

    typedef txout_m_t::const_iterator txout_m_cit;

private:

    static const int MAX_CACHE_SIZE = 1000;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    int64_t nTimeLastDiff;
    int nCachedBlockHeight;

    // keep track of the scanning errors
    object_m_t mapObjects;

    count_m_t mapSeenGovernanceObjects;

    vote_cache_t mapInvalidVotes;
    vote_cache_t mapOrphanVotes;

    // todo: one of these should point to the other
    //   -- must be carefully managed while adding/removing/updating
    vote_m_t mapVotesByHash;
    vote_m_t mapVotesByType;

    txout_m_t mapLastMasternodeTrigger;

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    CGovernanceManager();

    virtual ~CGovernanceManager() {}

    // Inherited from IMasternodeIndexUpdateReceiver

    /// Should lock any index dependent data structures
    void MasternodeIndexUpdateBegin();

    /// Should unlock any index dependent data structures
    void MasternodeIndexUpdateEnd();

    void ClearSeen()
    {
        LOCK(cs);
        mapSeenGovernanceObjects.clear();
    }

    int CountProposalInventoryItems()
    {
        // TODO What is this for ?
        return mapSeenGovernanceObjects.size();
        //return mapSeenGovernanceObjects.size() + mapSeenVotes.size();
    }

    void Sync(CNode* node, uint256 nProp);
    void SyncParentObjectByVote(CNode* pfrom, const CGovernanceVote& vote);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();

    CGovernanceObject *FindGovernanceObject(const uint256& nHash);

    std::vector<CGovernanceVote*> GetMatchingVotes(const uint256& nParentHash);
    std::vector<CGovernanceObject*> GetAllNewerThan(int64_t nMoreThanTime);

    int CountMatchingVotes(CGovernanceObject& govobj, vote_signal_enum_t nVoteSignalIn, vote_outcome_enum_t nVoteOutcomeIn);

    bool IsBudgetPaymentBlock(int nBlockHeight);
    bool AddGovernanceObject (CGovernanceObject& govobj);
    bool AddOrUpdateVote(const CGovernanceVote& vote, CNode* pfrom, std::string& strError);

    std::string GetRequiredPaymentsString(int nBlockHeight);
    void CleanAndRemove(bool fSignatureCheck);
    void UpdateCachesAndClean();
    void CheckAndRemove() {UpdateCachesAndClean();}

    void CheckOrphanVotes();

    void Clear()
    {
        LOCK(cs);

        LogPrint("gobject", "Governance object manager was cleared\n");
        mapObjects.clear();
        mapSeenGovernanceObjects.clear();
        mapInvalidVotes.Clear();
        mapOrphanVotes.Clear();
        mapVotesByType.clear();
        mapVotesByHash.clear();
        mapLastMasternodeTrigger.clear();
    }

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(mapSeenGovernanceObjects);
        READWRITE(mapInvalidVotes);
        READWRITE(mapOrphanVotes);
        READWRITE(mapObjects);
        READWRITE(mapVotesByHash);
        READWRITE(mapVotesByType);
        READWRITE(mapLastMasternodeTrigger);
    }

    void UpdatedBlockTip(const CBlockIndex *pindex);
    int64_t GetLastDiffTime() { return nTimeLastDiff; }
    void UpdateLastDiffTime(int64_t nTimeIn) { nTimeLastDiff = nTimeIn; }

    int GetCachedBlockHeight() { return nCachedBlockHeight; }

    // Accessors for thread-safe access to maps
    bool HaveObjectForHash(uint256 nHash);

    bool HaveVoteForHash(uint256 nHash);

    bool SerializeObjectForHash(uint256 nHash, CDataStream& ss);

    bool SerializeVoteForHash(uint256 nHash, CDataStream& ss);

    void AddSeenGovernanceObject(uint256 nHash, int status);

    void AddSeenVote(uint256 nHash, int status);

    bool MasternodeRateCheck(const CTxIn& vin);

    bool ProcessVote(const CGovernanceVote& vote, std::string& strError);

};

struct vote_instance_t {

    vote_instance_t()
        : eOutcome(VOTE_OUTCOME_NONE),
          nTime(0)
    {}

    vote_outcome_enum_t eOutcome;
    int64_t nTime;
};

struct vote_rec_t {
    vote_instance_t instanceFunding;
    vote_instance_t instanceValid;
    vote_instance_t instanceDelete;
    vote_instance_t instanceEndorsed;
};

/**
* Governance Object
*
*/

class CGovernanceObject
{
    friend class CGovernanceManager;

    friend class CGovernanceTriggerManager;

public: // Types
    typedef std::map<int, vote_rec_t> vote_m_t;

    typedef vote_m_t::iterator vote_m_it;

    typedef vote_m_t::iterator vote_m_cit;

private:
    /// critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Object typecode
    int nObjectType;

    /// parent object, 0 is root
    uint256 nHashParent;

    /// object revision in the system
    int nRevision;

    /// time this object was created
    int64_t nTime;

    /// fee-tx
    uint256 nCollateralHash;

    /// Data field - can be used for anything
    std::string strData;

    /// Masternode info for signed objects
    CTxIn vinMasternode;
    std::vector<unsigned char> vchSig;

    /// is valid by blockchain
    bool fCachedLocalValidity;
    std::string strLocalValidityError;

    // VARIOUS FLAGS FOR OBJECT / SET VIA MASTERNODE VOTING

    /// true == minimum network support has been reached for this object to be funded (doesn't mean it will for sure though)
    bool fCachedFunding;

    /// true == minimum network has been reached flagging this object as a valid and understood goverance object (e.g, the serialized data is correct format, etc)
    bool fCachedValid;

    /// true == minimum network support has been reached saying this object should be deleted from the system entirely
    bool fCachedDelete;

    /** true == minimum network support has been reached flagging this object as endorsed by an elected representative body
     * (e.g. business review board / technecial review board /etc)
     */
    bool fCachedEndorsed;

    /// object was updated and cached values should be updated soon
    bool fDirtyCache;

    /// Object is no longer of interest
    bool fExpired;

public:
    CGovernanceObject();
    CGovernanceObject(uint256 nHashParentIn, int nRevisionIn, int64_t nTime, uint256 nCollateralHashIn, std::string strDataIn);
    CGovernanceObject(const CGovernanceObject& other);
    void swap(CGovernanceObject& first, CGovernanceObject& second); // nothrow

    // Public Getter methods

    int64_t GetCreationTime() const {
        return nTime;
    }

    int GetObjectType() const {
        return nObjectType;
    }

    const uint256& GetCollateralHash() const {
        return nCollateralHash;
    }

    const CTxIn& GetMasternodeVin() const {
        return vinMasternode;
    }

    bool IsSetCachedFunding() const {
        return fCachedFunding;
    }

    bool IsSetCachedValid() const {
        return fCachedValid;
    }

    bool IsSetCachedDelete() const {
        return fCachedDelete;
    }

    bool IsSetCachedEndorsed() const {
        return fCachedEndorsed;
    }

    bool IsSetDirtyCache() const {
        return fDirtyCache;
    }

    bool IsSetExpired() const {
        return fExpired;
    }

    void InvalidateVoteCache() {
        fDirtyCache = true;
    }

    // Signature related functions

    void SetMasternodeInfo(const CTxIn& vin);
    bool Sign(CKey& keyMasternode, CPubKey& pubkeyMasternode);
    bool CheckSignature(CPubKey& pubkeyMasternode);

    // CORE OBJECT FUNCTIONS

    bool IsValidLocally(const CBlockIndex* pindex, std::string& strError, bool fCheckCollateral);

    /// Check the collateral transaction for the budget proposal/finalized budget
    bool IsCollateralValid(std::string& strError);

    void UpdateLocalValidity(const CBlockIndex *pCurrentBlockIndex);
    void UpdateSentinelVariables(const CBlockIndex *pCurrentBlockIndex);
    int GetObjectType();
    int GetObjectSubtype();

    CAmount GetMinCollateralFee();

    UniValue GetJSONObject();

    void Relay();
    uint256 GetHash();

    // GET VOTE COUNT FOR SIGNAL

    int GetAbsoluteYesCount(vote_signal_enum_t eVoteSignalIn);
    int GetAbsoluteNoCount(vote_signal_enum_t eVoteSignalIn);
    int GetYesCount(vote_signal_enum_t eVoteSignalIn);
    int GetNoCount(vote_signal_enum_t eVoteSignalIn);
    int GetAbstainCount(vote_signal_enum_t eVoteSignalIn);

    // FUNCTIONS FOR DEALING WITH DATA STRING

    std::string GetDataAsHex();
    std::string GetDataAsString();

    // SERIALIZER

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        // SERIALIZE DATA FOR SAVING/LOADING OR NETWORK FUNCTIONS

        READWRITE(nHashParent);
        READWRITE(nRevision);
        READWRITE(nTime);
        READWRITE(nCollateralHash);
        READWRITE(LIMITED_STRING(strData, MAX_GOVERNANCE_OBJECT_DATA_SIZE));
        READWRITE(nObjectType);
        READWRITE(vinMasternode);
        READWRITE(vchSig);

        // AFTER DESERIALIZATION OCCURS, CACHED VARIABLES MUST BE CALCULATED MANUALLY
    }

private:
    // FUNCTIONS FOR DEALING WITH DATA STRING

    void LoadData();
    void GetData(UniValue& objResult);

};


#endif

// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "masternode.h"

#define MASTERNODES_DUMP_SECONDS               (15*60)
#define MASTERNODES_DSEG_SECONDS               (3*60*60)

using namespace std;

class CMasternodeMan;
extern CMasternodeMan mnodeman;

/**
 * Interface (abstract base class) for receiving masternode index
 * update notifications
 */
class IMasternodeIndexUpdateReceiver
{
public:
    /// Should lock any index dependent data structures
    virtual void MasternodeIndexUpdateBegin() = 0;

    /// Should unlock any index dependent data structures
    virtual void MasternodeIndexUpdateEnd() = 0;

};

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CMasternodeMan
 */
class CMasternodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CMasternodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve masternode vin by index
    bool Get(int nIndex, CTxIn& vinMasternode) const;

    /// Get index of a masternode vin
    int GetMasternodeIndex(const CTxIn& vinMasternode) const;

    void AddMasternodeVIN(const CTxIn& vinMasternode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CMasternodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::vector<IMasternodeIndexUpdateReceiver*> receiver_v_t;

    typedef typename receiver_v_t::iterator receiver_v_it;

    typedef typename receiver_v_t::const_iterator receiver_v_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const int MASTERNODES_LAST_PAID_SCAN_BLOCKS  = 100;

    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CMasternode> vMasternodes;
    // who's asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which Masternodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

    int64_t nLastIndexRebuildTime;

    CMasternodeIndex indexMasternodes;

    CMasternodeIndex indexMasternodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Objects to notify on index update
    receiver_v_t vecMNIndexUpdateReceivers;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CMasternodeBroadcast> mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CMasternodePing> mapSeenMasternodePing;

    // keep track of dsq count to prevent masternodes from gaming darksend queue
    int64_t nDsqCount;

    // dummy script pubkey to test masternodes' vins against mempool
    CScript dummyScriptPubkey;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vMasternodes);
        READWRITE(mAskedUsForMasternodeList);
        READWRITE(mWeAskedForMasternodeList);
        READWRITE(mWeAskedForMasternodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenMasternodeBroadcast);
        READWRITE(mapSeenMasternodePing);
        READWRITE(indexMasternodes);
    }

    CMasternodeMan();

    /// Add an entry
    bool Add(CMasternode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);

    /// Check all Masternodes
    void Check();

    /// Check all Masternodes and remove inactive
    void CheckAndRemove(bool fForceExpiredRemoval = false);

    /// Clear Masternode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    /// Count Masternodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CMasternode* Find(const CScript &payee);
    CMasternode* Find(const CTxIn& vin);
    CMasternode* Find(const CPubKey& pubKeyMasternode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyMasternode, CMasternode& masternode);
    bool Get(const CTxIn& vin, CMasternode& masternode);

    /// Retrieve masternode vin by index
    bool Get(int nIndex, CTxIn& vinMasternode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexMasternodes.Get(nIndex, vinMasternode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a masternode vin
    int GetMasternodeIndex(const CTxIn& vinMasternode) {
        LOCK(cs);
        return indexMasternodes.GetMasternodeIndex(vinMasternode);
    }

    /// Get old index of a masternode vin
    int GetMasternodeIndexOld(const CTxIn& vinMasternode) {
        LOCK(cs);
        return indexMasternodesOld.GetMasternodeIndex(vinMasternode);
    }

    /// Get masternode VIN for an old index value
    bool GetMasternodeVinForIndexOld(int nMasternodeIndex, CTxIn& vinMasternodeOut) {
        LOCK(cs);
        return indexMasternodesOld.Get(nMasternodeIndex, vinMasternodeOut);
    }

    /// Get index of a masternode vin, returning rebuild flag
    int GetMasternodeIndex(const CTxIn& vinMasternode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexMasternodes.GetMasternodeIndex(vinMasternode);
    }

    void ClearOldMasternodeIndex() {
        LOCK(cs);
        indexMasternodesOld.Clear();
        fIndexRebuilt = false;
    }

    /// Register an object to receive masternode index updates
    void RegisterIndexUpdateReceiver(IMasternodeIndexUpdateReceiver* receiver);

    /// Unregister an object from receiving masternode index updates
    void UnregisterIndexUpdateReceiver(IMasternodeIndexUpdateReceiver* receiver);

    /// Find an entry in the masternode list that is next to be paid
    CMasternode* GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CMasternode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CMasternode> GetFullMasternodeVector() { Check(); return vMasternodes; }

    std::vector<pair<int, CMasternode> > GetMasternodeRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetMasternodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CMasternode* GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void InitDummyScriptPubkey();

    void ProcessMasternodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Masternodes
    int size() { return vMasternodes.size(); }

    std::string ToString() const;

    void Remove(CTxIn vin);

    int GetEstimatedMasternodes(int nBlock);

    /// Update masternode list and maps using provided CMasternodeBroadcast
    void UpdateMasternodeList(CMasternodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateMasternodeList(CMasternodeBroadcast mnb, int& nDos);

    void UpdateLastPaid(const CBlockIndex *pindex);

    void CheckAndRebuildMasternodeIndex();

};

#endif

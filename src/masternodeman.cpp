// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "darksend.h"
#include "masternode.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "util.h"
#include "addrman.h"
#include "spork.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

/** Masternode manager */
CMasternodeMan mnodeman;

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CMasternode*>& t1,
                    const std::pair<int, CMasternode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CMasternode*>& t1,
                    const std::pair<int64_t, CMasternode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CMasternodeMan::CMasternodeMan()
: cs(),
  cs_process_message(),
  vMasternodes(),
  mAskedUsForMasternodeList(),
  mWeAskedForMasternodeList(),
  mWeAskedForMasternodeListEntry(),
  mapMNIndex(),
  vecMNIndexUpdateReceivers(),
  mapSeenMasternodeBroadcast(),
  mapSeenMasternodePing(),
  nDsqCount(0),
  dummyScriptPubkey()
{}

bool CMasternodeMan::Add(CMasternode &mn)
{
    LOCK(cs);

    if (!mn.IsEnabled() && !mn.IsPreEnabled())
        return false;

    CMasternode *pmn = Find(mn.vin);
    if (pmn == NULL)
    {
        LogPrint("masternode", "CMasternodeMan: Adding new Masternode %s - %i now\n", mn.addr.ToString(), size() + 1);
        vMasternodes.push_back(mn);
        mapMNIndex[mn.vin] = int(vMasternodes.size()) - 1;
        return true;
    }

    return false;
}

void CMasternodeMan::AskForMN(CNode* pnode, CTxIn &vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMasternodeListEntry.end())
    {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrintf("CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.ToString());
    pnode->PushMessage(NetMsgType::DSEG, vin);
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH(CMasternode& mn, vMasternodes) {
        mn.Check();
    }
}

void CMasternodeMan::CheckAndRemove(bool fForceExpiredRemoval)
{
    LogPrintf("CMasternodeMan::CheckAndRemove\n");

    Check();

    LOCK(cs);

    // Remove inactive and outdated masternodes
    bool fRemoved = false;
    std::vector<CMasternode>::iterator it = vMasternodes.begin();
    while(it != vMasternodes.end()) {
        bool fRemove =  // If it's marked to be removed from the list by CMasternode::Check for whatever reason ...
                        (*it).nActiveState == CMasternode::MASTERNODE_REMOVE ||
                        // or collateral was spent ...
                        (*it).nActiveState == CMasternode::MASTERNODE_OUTPOINT_SPENT ||
                        // or we were asked to remove exired entries ...
                        (fForceExpiredRemoval && (*it).nActiveState == CMasternode::MASTERNODE_EXPIRED);

        if (fRemove) {
            LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- Removing Masternode: %s  addr=%s  %i now\n", (*it).GetStatus(), (*it).addr.ToString(), size() - 1);

            // erase all of the broadcasts we've seen from this txin, ...
            mapSeenMasternodeBroadcast.erase(CMasternodeBroadcast(*it).GetHash());
            // allow us to ask for this masternode again if we see another ping ...
            mWeAskedForMasternodeListEntry.erase((*it).vin.prevout);

            // and finally remove it from the list
            it = vMasternodes.erase(it);
            fRemoved = true;
        } else {
            ++it;
        }
    }

    if(fRemoved) {
        UpdateMNIndex();
    }

    // check who's asked for the Masternode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasternodeList.begin();
    while(it1 != mAskedUsForMasternodeList.end()){
        if((*it1).second < GetTime()) {
            mAskedUsForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Masternode list
    it1 = mWeAskedForMasternodeList.begin();
    while(it1 != mWeAskedForMasternodeList.end()){
        if((*it1).second < GetTime()){
            mWeAskedForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Masternodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
    while(it2 != mWeAskedForMasternodeListEntry.end()){
        if((*it2).second < GetTime()){
            mWeAskedForMasternodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMasternodeBroadcast
    map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while(it3 != mapSeenMasternodeBroadcast.end()){
        if((*it3).second.lastPing.sigTime < GetTime() - MASTERNODE_REMOVAL_SECONDS*2){
            LogPrint("masternode", "CMasternodeMan::CheckAndRemove - Removing expired Masternode broadcast %s\n", (*it3).second.GetHash().ToString());
            mapSeenMasternodeBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMasternodePing
    map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while(it4 != mapSeenMasternodePing.end()){
        if((*it4).second.sigTime < GetTime() - MASTERNODE_REMOVAL_SECONDS*2){
            LogPrint("masternode", "CMasternodeMan::CheckAndRemove - Removing expired Masternode ping %s\n", (*it4).second.GetHash().ToString());
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }

}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    vMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nDsqCount = 0;
}

int CMasternodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? mnpayments.GetMinMasternodePaymentsProto() : protocolVersion;

    BOOST_FOREACH(CMasternode& mn, vMasternodes) {
        mn.Check();
        if(mn.nProtocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

int CMasternodeMan::CountByIP(int nNetworkType)
{
    int nNodeCount = 0;

    BOOST_FOREACH(CMasternode& mn, vMasternodes)
        if( (nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())){
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);
            if (it != mWeAskedForMasternodeList.end())
            {
                if (GetTime() < (*it).second) {
                    LogPrintf("dseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                    return;
                }
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;
}

CMasternode *CMasternodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CMasternode& mn, vMasternodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CMasternode *CMasternodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CMasternode& mn, vMasternodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CMasternode* CMasternodeMan::Find(const CPubKey &pubKeyMasternode)
{
    LOCK(cs);

    BOOST_FOREACH(CMasternode& mn, vMasternodes)
    {
        if(mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return NULL;
}

bool CMasternodeMan::Get(const CPubKey& pubKeyMasternode, CMasternode& masternode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CMasternode* pMN = Find(pubKeyMasternode);
    if(!pMN)  {
        return false;
    }
    masternode = *pMN;
    return true;
}

bool CMasternodeMan::Get(const CTxIn& vin, CMasternode& masternode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CMasternode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    masternode = *pMN;
    return true;
}

/// Retrieve masternode by index
bool CMasternodeMan::Get(int nIndex, CMasternode& masternode)
{
    LOCK(cs);
    if((nIndex < 0) || (nIndex >= int(vMasternodes.size()))) {
        return false;
    }
    masternode = vMasternodes[nIndex];
    return true;
}

/// Get index of a masternode
int CMasternodeMan::GetMasternodeIndex(const CTxIn& vin)
{
    int nIndex = -1;

    LOCK(cs);
    index_m_it it = mapMNIndex.find(vin);
    if(it != mapMNIndex.end()) {
        nIndex = it->second;
    }

    return nIndex;
}

void CMasternodeMan::RegisterIndexUpdateReceiver(IMasternodeIndexUpdateReceiver* receiver)
{
    LOCK(cs);
    vecMNIndexUpdateReceivers.push_back(receiver);
}

void CMasternodeMan::UnregisterIndexUpdateReceiver(IMasternodeIndexUpdateReceiver* receiver)
{
    LOCK(cs);
    for(receiver_v_it it = vecMNIndexUpdateReceivers.begin(); it != vecMNIndexUpdateReceivers.end(); ++it) {
        if(*it == receiver) {
            vecMNIndexUpdateReceivers.erase(it);
            break;
        }
    }
}

// 
// Deterministically select the oldest/best masternode to pay on the network
//
CMasternode* CMasternodeMan::GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CMasternode *pBestMasternode = NULL;
    std::vector<std::pair<int, CMasternode*> > vecMasternodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH(CMasternode &mn, vMasternodes)
    {
        mn.Check();
        if(!mn.IsEnabled()) continue;

        // //check protocol version
        if(mn.nProtocolVersion < mnpayments.GetMinMasternodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(mnpayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && mn.sigTime + (nMnCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are masternodes
        if(mn.GetCollateralAge() < nMnCount) continue;

        vecMasternodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }

    nCount = (int)vecMasternodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount/3) return GetNextMasternodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them low to high
    sort(vecMasternodeLastPaid.begin(), vecMasternodeLastPaid.end(), CompareLastPaidBlock());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled()/10;
    int nCountTenth = 0;
    arith_uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int, CMasternode*)& s, vecMasternodeLastPaid){
        arith_uint256 n = UintToArith256(s.second->CalculateScore(nBlockHeight - 101));
        if(n > nHigh){
            nHigh = n;
            pBestMasternode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestMasternode;
}

CMasternode *CMasternodeMan::FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinMasternodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CMasternodeMan::FindRandomNotInVec -- %d enabled masternodes, %d masternodes aren't yet exluded\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CMasternode*> vpMasternodesShuffled;
    BOOST_FOREACH(CMasternode &mn, vMasternodes) {
        vpMasternodesShuffled.push_back(&mn);
    }

    // shuffle pointers
    std::random_shuffle(vpMasternodesShuffled.begin(), vpMasternodesShuffled.end(), GetRandInt);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CMasternode* pmn, vpMasternodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec -- found, masternode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CMasternodeMan::GetMasternodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CMasternode*> > vecMasternodeScores;

    //make sure we know about this block
    uint256 hash = uint256();
    if(!GetBlockHash(hash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CMasternode& mn, vMasternodes) {
        if(mn.nProtocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, &mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMasternode*)& scorePair, vecMasternodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<pair<int, CMasternode> > CMasternodeMan::GetMasternodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<std::pair<int64_t, CMasternode*> > vecMasternodeScores;
    std::vector<std::pair<int, CMasternode> > vecMasternodeRanks;

    //make sure we know about this block
    uint256 hash = uint256();
    if(!GetBlockHash(hash, nBlockHeight)) return vecMasternodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CMasternode& mn, vMasternodes) {

        mn.Check();

        if(mn.nProtocolVersion < minProtocol || !mn.IsEnabled()) continue;

        uint256 n = mn.CalculateScore(nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, &mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMasternode*)& s, vecMasternodeScores) {
        nRank++;
        vecMasternodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecMasternodeRanks;
}

CMasternode* CMasternodeMan::GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CMasternode*> > vecMasternodeScores;

    LOCK(cs);

    // Fill scores
    BOOST_FOREACH(CMasternode& mn, vMasternodes) {

        if(mn.nProtocolVersion < minProtocol) continue;
        if(fOnlyActive) {
            mn.Check();
            if(!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(nBlockHeight);
        int64_t n2 = UintToArith256(n).GetCompact(false);

        vecMasternodeScores.push_back(std::make_pair(n2, &mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMasternode*)& s, vecMasternodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CMasternodeMan::InitDummyScriptPubkey() {
    CKey secret;
    secret.MakeNewKey(true);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    if (pubkey.IsValid()) {
        CKeyID keyID = pubkey.GetID();
        LogPrintf("Generated dummyScriptPubkey: address %s privkey %s\n", CBitcoinAddress(keyID).ToString(), CBitcoinSecret(secret).ToString());
        dummyScriptPubkey = GetScriptForDestination(keyID);
    } else {
        LogPrintf("CMasternodeMan::InitDummyScriptPubkey - ERROR: can't assign dummyScriptPubkey\n");
    }
}

void CMasternodeMan::ProcessMasternodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fMasternode) {
            if(darkSendPool.pSubmittedToMasternode != NULL && pnode->addr == darkSendPool.pSubmittedToMasternode->addr) continue;
            LogPrintf("Closing Masternode connection %s \n", pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if(fLiteMode) return; //disable all Darksend/Masternode related functionality
    if(!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == NetMsgType::MNANNOUNCE) { //Masternode Broadcast
        CMasternodeBroadcast mnb;
        vRecv >> mnb;

        int nDos = 0;

        if (CheckMnbAndUpdateMasternodeList(mnb, nDos)) {
            // use announced Masternode as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2*60*60);
        } else {
            if(nDos > 0) Misbehaving(pfrom->GetId(), nDos);
        }

    } else if (strCommand == NetMsgType::MNPING) { //Masternode Ping

        // ignore masternode pings until masternode list is synced
        if (!masternodeSync.IsMasternodeListSynced()) return;

        CMasternodePing mnp;
        vRecv >> mnp;

        LogPrint("masternode", "mnp - Masternode ping, vin: %s\n", mnp.vin.ToString());

        if(mapSeenMasternodePing.count(mnp.GetHash())) return; //seen
        mapSeenMasternodePing.insert(make_pair(mnp.GetHash(), mnp));

        LogPrint("masternode", "mnp - Masternode ping, vin: %s new\n", mnp.vin.ToString());

        int nDos = 0;
        if(mnp.CheckAndUpdate(nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else {
            // if nothing significant failed, search existing Masternode list
            CMasternode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if(pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Masternode list or specific entry

        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masternodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("masternode", "CMasternodeMan::ProcessMessage -- DSEG -- Masternode list, masternode=%s\n", vin.prevout.ToStringShort());

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasternodeList.find(pfrom->addr);
                if (i != mAskedUsForMasternodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("CMasternodeMan::ProcessMessage -- DSEG -- peer already asked me for the list, peer=\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CMasternode& mn, vMasternodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network masternode
            if (!mn.IsEnabled()) continue;

            LogPrint("masternode", "CMasternodeMan::ProcessMessage -- DSEG -- Sending Masternode entry: masternode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
            nInvCount++;

            if (!mapSeenMasternodeBroadcast.count(hash)) {
                mapSeenMasternodeBroadcast.insert(std::make_pair(hash, mnb));
            }

            if (vin == mn.vin) {
                LogPrintf("CMasternodeMan::ProcessMessage -- DSEG -- Sent 1 Masternode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_LIST, nInvCount);
            LogPrintf("CMasternodeMan::ProcessMessage -- DSEG -- Sent %d Masternode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("masternode", "CMasternodeMan::ProcessMessage -- DSEG -- No invs sent to peer %d\n", pfrom->id);
    }
}

void CMasternodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CMasternode>::iterator it = vMasternodes.begin();
    while(it != vMasternodes.end()){
        if((*it).vin == vin){
            LogPrint("masternode", "CMasternodeMan: Removing Masternode %s - %i now\n", (*it).addr.ToString(), size() - 1);
            vMasternodes.erase(it);
            break;
        }
        ++it;
    }
}

std::string CMasternodeMan::ToString() const
{
    std::ostringstream info;

    info << "Masternodes: " << (int)vMasternodes.size() <<
            ", peers who asked us for Masternode list: " << (int)mAskedUsForMasternodeList.size() <<
            ", peers we asked for Masternode list: " << (int)mWeAskedForMasternodeList.size() <<
            ", entries in Masternode list we asked for: " << (int)mWeAskedForMasternodeListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

int CMasternodeMan::GetEstimatedMasternodes(int nBlock)
{
    /*
        Masternodes = (Coins/1000)*X on average
        
        *X = nPercentage, starting at 0.52
        nPercentage goes up 0.01 each period
        Period starts at 35040, which has exponential slowing growth

    */ 

    int nPercentage = 52; //0.52
    int nPeriod = 35040;
    int nCollateral = 1000;

    for(int i = nPeriod; i <= nBlock; i += nPeriod)
    {
        nPercentage++;
        nPeriod*=2;
    }
    return (GetTotalCoinEstimate(nBlock)/100*nPercentage/nCollateral);
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast mnb) {
    mapSeenMasternodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMasternodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    masternodeSync.AddedMasternodeList();

    LogPrintf("CMasternodeMan::UpdateMasternodeList() - addr: %s\n    vin: %s\n", mnb.addr.ToString(), mnb.vin.ToString());

    CMasternode* pmn = Find(mnb.vin);
    if(pmn == NULL)
    {
        CMasternode mn(mnb);
        Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(mnb);
    }
}

bool CMasternodeMan::CheckMnbAndUpdateMasternodeList(CMasternodeBroadcast mnb, int& nDos) {
    nDos = 0;
    LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList - Masternode broadcast, vin: %s\n", mnb.vin.ToString());

    if(mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
        masternodeSync.AddedMasternodeList();
        return true;
    }
    mapSeenMasternodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

    LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList - Masternode broadcast, vin: %s new\n", mnb.vin.ToString());

    if(!mnb.CheckAndUpdate(nDos)){
        LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList - Masternode broadcast, vin: %s CheckAndUpdate failed\n", mnb.vin.ToString());
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckDarkSendPool()
    if(mnb.CheckInputsAndAdd(nDos)) {
        masternodeSync.AddedMasternodeList();
    } else {
        LogPrintf("CMasternodeMan::CheckMnbAndUpdateMasternodeList - Rejected Masternode entry %s\n", mnb.addr.ToString());
        return false;
    }

    return true;
}

void CMasternodeMan::UpdateLastPaid(const CBlockIndex *pindex) {
    if(fLiteMode) return;

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a masternode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fMasterNode) ? mnpayments.GetStorageLimit() : MASTERNODES_LAST_PAID_SCAN_BLOCKS;

    // LogPrint("mnpayments", "CMasternodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
    //                         pindex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CMasternode& mn, vMasternodes) {
        mn.UpdateLastPaid(pindex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !masternodeSync.IsWinnersListSynced();
}

void CMasternodeMan::UpdateMNIndex()
{
    LOCK(cs);

    for(receiver_v_it it = vecMNIndexUpdateReceivers.begin(); it != vecMNIndexUpdateReceivers.end(); ++it) {
        IMasternodeIndexUpdateReceiver* receiver = *it;
        receiver->MasternodeIndexUpdateBegin();
    }

    mapMNIndex.clear();
    for(size_t i = 0; i < vMasternodes.size(); ++i) {
        mapMNIndex[vMasternodes[i].vin] = int(i);
    }

    for(receiver_v_it it = vecMNIndexUpdateReceivers.begin(); it != vecMNIndexUpdateReceivers.end(); ++it) {
        IMasternodeIndexUpdateReceiver* receiver = *it;
        receiver->MasternodeIndexUpdateEnd();
    }
}

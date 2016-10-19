// Copyright (c) 2014-2016 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "checkpoints.h"
#include "governance.h"
#include "main.h"
#include "masternode.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

class CMasternodeSync;
CMasternodeSync masternodeSync;

bool CMasternodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if(GetTime() - lastProcess > 60*60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if(fBlockchainSynced) return true;
    if(!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) return false;
    if(fCheckpointsEnabled && pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints()))
        return false;

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();

    return fBlockchainSynced;
}

void CMasternodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
}

void CMasternodeSync::Reset()
{
    nRequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastMasternodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastBudgetItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CMasternodeSync::GetAssetName()
{
    switch(nRequestedMasternodeAssets)
    {
        case(MASTERNODE_SYNC_INITIAL):      return "MASTERNODE_SYNC_INITIAL";
        case(MASTERNODE_SYNC_SPORKS):       return "MASTERNODE_SYNC_SPORKS";
        case(MASTERNODE_SYNC_LIST):         return "MASTERNODE_SYNC_LIST";
        case(MASTERNODE_SYNC_MNW):          return "MASTERNODE_SYNC_MNW";
        case(MASTERNODE_SYNC_GOVERNANCE):   return "MASTERNODE_SYNC_GOVERNANCE";
        case(MASTERNODE_SYNC_FAILED):       return "MASTERNODE_SYNC_FAILED";
        case MASTERNODE_SYNC_FINISHED:      return "MASTERNODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CMasternodeSync::SwitchToNextAsset()
{
    switch(nRequestedMasternodeAssets)
    {
        case(MASTERNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(MASTERNODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedMasternodeAssets = MASTERNODE_SYNC_SPORKS;
            break;
        case(MASTERNODE_SYNC_SPORKS):
            nTimeLastMasternodeList = GetTime();
            nRequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
            break;
        case(MASTERNODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
            break;
        case(MASTERNODE_SYNC_MNW):
            nTimeLastBudgetItem = GetTime();
            nRequestedMasternodeAssets = MASTERNODE_SYNC_GOVERNANCE;
            break;
        case(MASTERNODE_SYNC_GOVERNANCE):
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our masternode if possible
            activeMasternode.ManageState();

            TRY_LOCK(cs_vNodes, lockRecv);
            if(!lockRecv) return;

            BOOST_FOREACH(CNode* pnode, vNodes) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            }

            break;
    }
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (masternodeSync.nRequestedMasternodeAssets) {
        case MASTERNODE_SYNC_INITIAL:       return _("Synchronization pending...");
        case MASTERNODE_SYNC_SPORKS:        return _("Synchronizing sporks...");
        case MASTERNODE_SYNC_LIST:          return _("Synchronizing masternodes...");
        case MASTERNODE_SYNC_MNW:           return _("Synchronizing masternode payments...");
        case MASTERNODE_SYNC_GOVERNANCE:    return _("Synchronizing governance objects...");
        case MASTERNODE_SYNC_FAILED:        return _("Synchronization failed");
        case MASTERNODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("CMasternodeSync::ProcessMessage -- SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CMasternodeSync::ClearFulfilledRequests()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "masternode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "masternode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "governance-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CMasternodeSync::ProcessTick()
{
    static int nTick = 0;
    if(nTick++ % 6 != 0) return;
    if(!pCurrentBlockIndex) return;

    //the actual count of masternodes we have currently
    int nMnCount = mnodeman.CountMasternodes();

    LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nMnCount = %d\n", nTick, nMnCount);

    // RESET SYNCING INCASE OF FAILURE
    {
        if(IsSynced()) {
            /*
                Resync if we lose all masternodes from sleep/wake or failure to sync originally
            */
            if(nMnCount == 0) {
                Reset();
            } else {
                //if syncing is complete and we have masternodes, return
                return;
            }
        }

        //try syncing again
        if(IsFailed()) {
            if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedMasternodeAttempt + (nRequestedMasternodeAssets - 1) * 8) / (8*4);
    LogPrintf("CMasternodeSync::Process -- nTick %d nRequestedMasternodeAssets %d nRequestedMasternodeAttempt %d nSyncProgress %f\n", nTick, nRequestedMasternodeAssets, nRequestedMasternodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if(Params().NetworkIDString() != CBaseChainParams::REGTEST &&
            !IsBlockchainSynced() && nRequestedMasternodeAssets > MASTERNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    if(nRequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL ||
        (nRequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS && IsBlockchainSynced()))
    {
        SwitchToNextAsset();
    }

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if(nRequestedMasternodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if(nRequestedMasternodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if(nRequestedMasternodeAttempt < 6) {
                int nMnCount = mnodeman.CountMasternodes();
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, nMnCount); //sync payment votes
                uint256 n = uint256();
                pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, n); //sync masternode votes
            } else {
                nRequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
            }
            nRequestedMasternodeAttempt++;
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // we already fully synced from this node recently,
                // disconnect to free this connection slot for a new node
                pnode->fDisconnect = true;
                LogPrintf("CMasternodeSync::ProcessTick -- disconnecting from recently synced node %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC MASTERNODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedMasternodeAssets == MASTERNODE_SYNC_LIST) {
                // check for timeout first
                if(nTimeLastMasternodeList < GetTime() - MASTERNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CMasternodeSync::Process -- nTick %d nRequestedMasternodeAssets %d -- timeout\n", nTick, nRequestedMasternodeAssets);
                    if (nRequestedMasternodeAttempt == 0) {
                        LogPrintf("CMasternodeSync::Process -- WARNING: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without masternode list, fail here and try later
                        Fail();
                        return;
                    }
                    SwitchToNextAsset();
                    return;
                }

                // check for data
                // if we have enough masternodes in or list, switch to the next asset
                /* Note: Is this activing up? It's probably related to int CMasternodeMan::GetEstimatedMasternodes(int nBlock)
                   Surely doesn't work right for testnet currently */
                // try to fetch data from at least two peers though
                LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nMnCount %d, Estimated masternode count required: %d\n", 
                          nTick, nMnCount, mnodeman.GetEstimatedMasternodes(pCurrentBlockIndex->nHeight)*0.9);
                if(nRequestedMasternodeAttempt > 1 && nMnCount > mnodeman.GetEstimatedMasternodes(pCurrentBlockIndex->nHeight)*0.9) {
                    LogPrintf("CMasternodeSync::ProcessTick -- nTick %d nRequestedMasternodeAssets %d -- found enough data\n", nTick, nRequestedMasternodeAssets);
                    SwitchToNextAsset();
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "masternode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "masternode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinMasternodePaymentsProto()) continue;
                nRequestedMasternodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC MASTERNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nRequestedMasternodeAssets == MASTERNODE_SYNC_MNW) {
                // check for timeout first
                // This might take a lot longer than MASTERNODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(nTimeLastPaymentVote < GetTime() - MASTERNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CMasternodeSync::Process -- nTick %d nRequestedMasternodeAssets %d -- timeout\n", nTick, nRequestedMasternodeAssets);
                    if (nRequestedMasternodeAttempt == 0) {
                        LogPrintf("CMasternodeSync::Process -- WARNING: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        return;
                    }
                    SwitchToNextAsset();
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedMasternodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CMasternodeSync::Process -- nTick %d nRequestedMasternodeAssets %d -- found enough data\n", nTick, nRequestedMasternodeAssets);
                    SwitchToNextAsset();
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "masternode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "masternode-payment-sync");

                if(pnode->nVersion < mnpayments.GetMinMasternodePaymentsProto()) continue;
                nRequestedMasternodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode);

                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // GOVOBJ : SYNC GOVERNANCE ITEMS FROM OUR PEERS

            if(nRequestedMasternodeAssets == MASTERNODE_SYNC_GOVERNANCE) {
                // check for timeout first
                if(nTimeLastBudgetItem < GetTime() - MASTERNODE_SYNC_TIMEOUT_SECONDS){
                    LogPrintf("CMasternodeSync::Process -- nTick %d nRequestedMasternodeAssets %d -- timeout\n", nTick, nRequestedMasternodeAssets);
                    if(nRequestedMasternodeAttempt == 0) {
                        LogPrintf("CMasternodeSync::Process -- WARNING: failed to sync %s\n", GetAssetName());
                        // it's kind of ok to skip this for now, hopefully we'll catch up later?
                    }
                    SwitchToNextAsset();
                    return;
                }

                // check for data
                // if(nCountBudgetItemProp > 0 && nCountBudgetItemFin)
                // {
                //     if(governance.CountProposalInventoryItems() >= (nSumBudgetItemProp / nCountBudgetItemProp)*0.9)
                //     {
                //         if(governance.CountFinalizedInventoryItems() >= (nSumBudgetItemFin / nCountBudgetItemFin)*0.9)
                //         {
                //             SwitchToNextAsset();
                //             return;
                //         }
                //     }
                // }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "governance-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "governance-sync");

                if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) continue;
                nRequestedMasternodeAttempt++;

                pnode->PushMessage(NetMsgType::MNGOVERNANCESYNC, uint256()); //sync masternode votes

                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
}

void CMasternodeSync::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
}


void CMasternodeSync::AddedBudgetItem(uint256 hash)
{
    // skip this for now
    return;
}

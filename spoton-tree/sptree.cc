#include "sptree.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <unordered_map>

#include "logger.h"
#include "sptree_meta.h"

namespace spoton {

void AsyncInsert (TopLayerPmem* pmem_tree, key_t key, void* ptr) { pmem_tree->insert (key, ptr); };

void SPTree::DistroySPTree (void) {
    for (const std::string& file :
         {"/mnt/pmem/sptree_sb", "/mnt/pmem/sptree_desc", "/mnt/pmem/sptree_basemd"}) {
        bool res = ::remove (file.c_str ());
        if (res != 0) {
            perror ("can remove file\n");
        }
    }
}

SPTree* SPTree::CreateSPTree (bool isDram, bool withWriteBuffer, bool withLog) {
    // configure bottom layer leafnode as dram node or pmem node
    LeafNode64::kIsLeafNodeDram = isDram;
    if (isDram and withLog) {
        ERROR ("Cannot use wal in dram mode");
        perror ("Cannot use wal in dram mode\n");
        exit (1);
    }
    SPTree* sptree = nullptr;
    if (isDram) {
        // create a dram version of sptree
        sptree = new SPTree (isDram, withWriteBuffer, false /* dram version does not do wal*/);
        sptree->Initialize (nullptr);
    } else {
        // create the pmem version

        // 1. Initialize pmem library
        bool res = RP_init ("sptree", SPTREE_PMEM_SIZE);
        SPTreePmemRoot* sptree_pmem_root = nullptr;
        if (res) {
            INFO ("Prepare to recover");
            sptree_pmem_root = RP_get_root<SPTreePmemRoot> (0);
            int recover_res = RP_recover ();
            if (recover_res == 1) {
                INFO ("Dirty open, recover");
            } else {
                INFO ("Clean restart");
            }
        } else {
            INFO ("Clean create");
        }
        sptree_pmem_root = (SPTreePmemRoot*)RP_malloc (sizeof (SPTreePmemRoot));
        RP_set_root (sptree_pmem_root, 0);
        sptree_pmem_root->Reset ();
        sptree = new SPTree (isDram, withWriteBuffer, withLog);
        sptree->Initialize (sptree_pmem_root);
    }

    return sptree;
}

SPTree* SPTree::RecoverSPTree (bool withWriteBuffer, bool withLog) {
    // configure bottom layer leafnode as pmem
    LeafNode64::kIsLeafNodeDram = false;

    // 1. Open the pmem file
    bool res = RP_init ("sptree", SPTREE_PMEM_SIZE);
    if (res) {
        INFO ("Prepare to recover");
        RP_get_root<SPTreePmemRoot> (0);
        int recover_res = RP_recover ();
        if (recover_res == 1) {
            INFO ("Dirty open, recover");
        } else {
            INFO ("Clean restart");
        }
    } else {
        INFO ("Nothing to recover");
        printf ("There is nothing to recover.");
        exit (1);
    }

    // 2. obtain the bottom layer head and pmem btree
    SPTreePmemRoot* sptree_pmem_root = RP_get_root<SPTreePmemRoot> (0);
    SPTree* sptree = new SPTree (false, withWriteBuffer, withLog);
    sptree->Recover (sptree_pmem_root);
    return sptree;
}

void SPTree::Initialize (SPTreePmemRoot* root) {
    // pmem layers
    mBotLayer.Initialize (root);
    mTopLayerPmem.Initialize (root);
    if (!LeafNode64::kIsLeafNodeDram) {
        // pmem mode
        mTopLayerPmem.insert (kSPTreeMinKey, mBotLayer.head);
    }
    // dram layers
    mMidLayer.head->leafNode = mBotLayer.head;
    mMidLayer.head->next->leafNode = mBotLayer.head->GetNext ();
    mTopLayer.insert (kSPTreeMinKey, mMidLayer.head);
    mSPTreePmemRoot = root;
}

struct RecoverUnit {
    key_t minKey;
    key_t lkey;
    key_t hkey;
    MLNode* mnode;
    LeafNode64* leafnode;
    RecoverUnit (key_t _mk, key_t _lk, key_t _hk, MLNode* _mn, LeafNode64* _lf)
        : minKey (_mk), lkey (_lk), hkey (_hk), mnode (_mn), leafnode (_lf) {}
};
void SPTree::Recover (SPTreePmemRoot* root) {
    // obtain the bottom layer pmem head LeafNode64
    mBotLayer.Recover (root);
    // obtain the pmem btree
    mTopLayerPmem.Recover (root);

    // rebuild the top layer and middle layer

    // 1. Iterate the leaf page of pmem btree
    std::vector<RecoverUnit> units;
    mTopLayerPmem.ScanAllRecord ([&units] (key_t key, void* leafnode_ptr) {
        assert (key != 0);
        units.emplace_back (RecoverUnit (key, 0LU, 0LU, new (false) MLNode (false),
                                         reinterpret_cast<LeafNode64*> (leafnode_ptr)));
    });
    INFO ("Recover %lu leaf node from pmem btree", units.size ());

    // 2. recover top layer dram
    tbb::parallel_for (tbb::blocked_range<uint64_t> (0, units.size ()),
                       [&] (const tbb::blocked_range<uint64_t>& range) {
                           for (uint64_t i = range.begin (); i != range.end (); i++) {
                               RecoverUnit& unit = units[i];
                               // insert top layer record
                               this->mTopLayer.insert (unit.minKey, unit.mnode);
                               // recover the middle layer node, lkey, hkey
                               uint64_t* lhkeys = reinterpret_cast<uint64_t*> (unit.leafnode);
                               unit.mnode->lkey = lhkeys[0];
                               unit.mnode->hkey = lhkeys[1];
                               unit.mnode->leafNode = unit.leafnode;
                               // DEBUG ("recover lkey %lu, hkey %lu", lhkeys[0], lhkeys[1]);
                           }
                       });

    // 3. rebuild the middle layer (connect mnodes together)
    MLNode* prev = nullptr;
    MLNode* head = nullptr;
    size_t mnode_count = 0;
    for (auto& u : units) {
        u.mnode->prev = prev;
        if (!prev) {
            head = u.mnode;
        } else {
            prev->next = u.mnode;
        }
        prev = u.mnode;
        mnode_count++;
    }
    INFO ("Create %lu mnodes", mnode_count);
    mMidLayer.head = head;
    prev->next = mMidLayer.dummyTail;
    mMidLayer.dummyTail->prev = prev;
    mSPTreePmemRoot = root;
}

void SPTree::WaitAllJobs () {
    // wait all jobs in tpool to finish
    mTpool.wait_for_tasks ();
}

SPTree::SPTree (bool isDram, bool withWriteBuffer, bool withLog)
    : mBotLayer (isDram),
      mTopLayerPmem (isDram),
      mTpool (4),
      mEnableWriteBuffer (withWriteBuffer),
      mEnableLog (withLog) {}

SPTree::~SPTree () {
    if (!LeafNode64::kIsLeafNodeDram) RP_close ();
};

std::tuple<MLNode*, uint64_t> SPTree::jumpToMiddleLayer (key_t key) {
retry1:
    // 1. find the target middle layer node
    MLNode* mnode = reinterpret_cast<MLNode*> (mTopLayer.seekLE (key));
    assert (mnode != nullptr);
    mnode = mMidLayer.FindTargetMLNode (key, mnode);

retry2:
    bool needRestart = false;
    auto v = mnode->lock.readLockOrRestart (needRestart);
    if (RetryVersionLock::isObsolete (v)) {
        // this mnode has been deleted
        goto retry1;
    }
    if (needRestart) {
        // other thread is writing this mnode
        _mm_pause ();
        goto retry2;
    }
    if (key < mnode->lkey or mnode->hkey <= key) {
        // this mnode is not our target node, retry
        goto retry1;
    }
    return {mnode, v};
}

void SPTree::FlushMLNodeBuffer (MLNode* mnode, key_t key, val_t val) {
    // insert all records in the write buffer
    auto* buffer = mnode->getNodeBuffer ();
    NodeBitSet emptyBitSet = mnode->leafNode->EmptyBitSet ();
    NodeBitSet toAddedBits;
    for (int i : buffer->ValidBitSet ()) {
        auto slot = buffer->slots[i];
        int si = *emptyBitSet;
        bool res = mnode->leafNode->InsertiWithoutSetValidBitmap (slot.key, slot.val, si);
        if (res) {
            // new insert to leafnode. key is inserted in slot[si]
            toAddedBits.set (si);
            emptyBitSet++;
        } else {
            // means we overwrite an existing key and we do not go to next
            // emptyBitSet
            DEBUG ("overwrite key %lu when flush buffer", slot.key);
        }
    }
    // insert this record
    if (key != kKeyEmpty) {
        int si = *emptyBitSet;
        bool res = mnode->leafNode->InsertiWithoutSetValidBitmap (key, val, si);
        if (res) {
            // new insert to leafnode
            toAddedBits.set (si);
        }
    }

    SPTreeCLFlush ((char*)mnode->leafNode, sizeof (LeafNode64));
    mnode->leafNode->valid_bitmap |= toAddedBits.bit ();
    // batch update the bitmap
    SPTreeMemFlush (&mnode->leafNode->valid_bitmap);
    SPTreeMemFlushFence ();
    mMidLayer.SetBloomFilter (key, mnode);
}

void SPTree::SplitMLNodeAndUnlock (MLNode* mnode,
                                   const std::vector<std::pair<key_t, val_t>>& toMergedRecords) {
    // assert mnode has already been locked
retry_lock_next:
    bool needRestart = false;
    // 4a. lock next mnode when split
    mnode->next->lock.writeLockOrRestart (needRestart);
    if (needRestart) {
        goto retry_lock_next;
    }

    // 4b. split the leaf node
    MLNode* old_next_mnode = mnode->next;
    MLNode* new_mnode = new (mEnableWriteBuffer) MLNode (mEnableWriteBuffer);
    void* newLeafNodeAddr = mBotLayer.Malloc (sizeof (LeafNode64));
    LeafNode64* newLeafNode;
    key_t newlkey;
    std::tie (newLeafNode, newlkey) = mnode->leafNode->Split (
        toMergedRecords, newLeafNodeAddr, mnode->GetBloomFilter (), new_mnode->GetBloomFilter ());

    // 4c. enable bloom filters for both mnode and new_mnode
    mnode->EnableBloomFilter ();
    new_mnode->EnableBloomFilter ();

    // 4d. link mnode for newLeafNode
    new_mnode->lkey = newlkey;
    new_mnode->hkey = mnode->hkey;
    new_mnode->prev = mnode;
    new_mnode->next = mnode->next;
    new_mnode->leafNode = newLeafNode;
    mnode->next->prev = new_mnode;
    mnode->next = new_mnode;
    mnode->hkey = newlkey;

    // 4e. insert newlkey->newNode to dram toplayer
    mTopLayer.insert (newlkey, new_mnode);

    // 4f. send jobs to background thread pool to update pmem top layer
    if (mTopLayerPmem.Exist ()) {
        mTpool.submit ([this, newlkey, newLeafNode] () {
            // DEBUG ("pmem btree insert %lu", newlkey);
            this->mTopLayerPmem.insert (newlkey, newLeafNode);
        });
    }

    mnode->lock.writeUnlock ();
    old_next_mnode->lock.writeUnlock ();
}

void SPTree::FlushOrSplitAndUnlock (MLNode* mnode, key_t key, val_t val) {
    size_t count1 = mnode->recordCount ();      // record # in buffer
    size_t count2 = mnode->leafNode->Count ();  // record # in leafnode

    // 1. we can merge all the records in buffer
    if (count1 + count2 <= LeafNode64::kLeafNodeCapacity - 1) {
        // insert all records in the write buffer
        FlushMLNodeBuffer (mnode, key, val);
        // clear write buffer
        mnode->resetBuffer ();
        mnode->lock.writeUnlock ();
        return;
    }

    // 2. leafnode cannot hold all the records
    NodeBuffer* buffer = mnode->getNodeBuffer ();
    std::vector<std::pair<key_t, val_t>> toMerge;
    for (int i : buffer->ValidBitSet ()) {
        auto slot = buffer->slots[i];
        toMerge.push_back ({slot.key, slot.val});
    }
    if (key != kKeyEmpty) {
        toMerge.push_back ({key, val});
    }
    SplitMLNodeAndUnlock (mnode, toMerge);

    // 3. clear write buffer after split
    mnode->resetBuffer ();
}

void SPTree::SortLeafNode (MLNode* mnode, uint64_t& version, bool& needRestart) {
    if (mnode->leafNode->NeedSort ()) {
        mnode->lock.upgradeToWriteLockOrRestart (version, needRestart);
        if (needRestart) {
            // version has changed
            return;
        }
        if (mnode->leafNode->NeedSort ()) {
            // if no other thread has finished sorting
            mnode->leafNode->Sort ();
        }
        mnode->lock.writeUnlock ();
        // update the version
        version = mnode->lock.readLockOrRestart (needRestart);
        if (needRestart) {
            return;
        }
    }
}

uint64_t SPTree::scan (key_t startKey, int resultSize, std::vector<TID>& results) {
    assert (results.size () >= resultSize);
retry:
    // 1. find the target middle layer node, and its version snapshot
    bool needRestart = false;
    MLNode* mnode;
    uint64_t v;
    std::tie (mnode, v) = jumpToMiddleLayer (startKey);

    MLNode* cur_mnode = mnode;
    int curOff = 0;

retry_scan:
    int cur_read = 0;
    needRestart = false;
    v = cur_mnode->lock.readLockOrRestart (needRestart);
    if (needRestart) {
        goto retry_scan;
    }
    while (cur_mnode->lkey != cur_mnode->hkey && curOff + cur_read < resultSize) {
        // if node has buffer, flush it
        if (cur_mnode->type == MLNodeTypeWithBuffer) {
            if (cur_mnode->recordCount () != 0) {
                cur_mnode->lock.upgradeToWriteLockOrRestart (v, needRestart);
                if (needRestart) {
                    goto retry;
                }
                FlushOrSplitAndUnlock (cur_mnode, kKeyEmpty, 0);
                goto retry;
            }
        }
        // sort the leaf node if needed
        SortLeafNode (cur_mnode, v, needRestart);
        if (needRestart) {
            // retry scan this node
            DEBUG ("retry scan1");
            _mm_pause ();
            goto retry_scan;
        }
        // iterate sorted keys
        LeafNode64::Iterator iter;
        if (cur_mnode->lkey >= startKey) {
            // scan from the 1st key
            iter = cur_mnode->leafNode->begin ();
        } else {
            // binary search to target key
            iter = cur_mnode->leafNode->beginAt (startKey);
        }

        while (curOff + cur_read < resultSize) {
            if (iter.Valid ()) {
                if (startKey <= iter->key) {
                    results[curOff + cur_read++] = iter->val;
                }
                ++iter;
            } else {
                // go to next leaf node
                cur_mnode->lock.checkOrRestart (v, needRestart);
                if (needRestart) {
                    // retry scan this node
                    DEBUG ("retry scan2");
                    _mm_pause ();
                    goto retry_scan;
                }
                cur_mnode = cur_mnode->next;
                curOff = curOff + cur_read;
                cur_read = 0;
                break;
            }
        }
        v = cur_mnode->lock.readLockOrRestart (needRestart);
        if (needRestart) {
            // retry scan this node
            DEBUG ("retry scan3");
            _mm_pause ();
            goto retry_scan;
        }
    }
    return curOff + cur_read;
}

TID SPTree::lookup (key_t key) {
retry:
    // 1. find the target middle layer node, and its version snapshot
    bool needRestart = false;
    MLNode* mnode;
    uint64_t v;
    std::tie (mnode, v) = jumpToMiddleLayer (key);

    // 2. check the bloomfilter in middle layer
    bool maybeExist = mMidLayer.CouldExist (key, mnode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }
    if (!maybeExist) {
        mnode->lock.checkOrRestart (v, needRestart);
        if (needRestart) {
            goto retry;
        }
        return 0;
    }

    // 3. check the write buffer
    if (mnode->type == MLNodeTypeWithBuffer) {
        val_t val = mnode->lookup (key);
        if (val != 0) {
            mnode->lock.checkOrRestart (v, needRestart);
            if (needRestart) {
                goto retry;
            }
            // find in write buffer
            return val;
        }
    }

    // 4. check the leaf node
    val_t val;
    bool res = mBotLayer.Lookup (key, val, mnode->leafNode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }

    return res ? val : 0;
}

bool SPTree::insert (key_t key, TID val) {
retry:
    // 1. find the target middle layer node, and its version snapshot
    MLNode* mnode;
    uint64_t v;
    std::tie (mnode, v) = jumpToMiddleLayer (key);

    // 2. lock the mnode and try to insert
    bool needRestart = false;
    mnode->lock.upgradeToWriteLockOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }

    // 3. insert to mnode writebuffer if possible
    if (mnode->type == MLNodeTypeWithBuffer) {
        bool succ = mnode->insert (key, val);
        if (succ) {
            // insert write buffer succ, then we set bloom filter and return
            mMidLayer.SetBloomFilter (key, mnode);
            if (mEnableLog) {
                WAL* local_log = WAL::GetThreadLocalLog (mSPTreePmemRoot);
                if (!local_log) {
                    ERROR ("intert obtain nullptr log");
                    perror ("inert obtain nullptr log\n");
                    exit (1);
                }
                local_log->Append (kWALNodeInsert, key, val, mnode->headptr);
            }
            mnode->lock.writeUnlock ();
            return true;
        }
        // write buffer full, merge to pmem node
        FlushOrSplitAndUnlock (mnode, key, val);
        return true;
    }

    // 4. insert to bottom layer
    bool bottomInsertSucc = mBotLayer.Insert (key, val, mnode->leafNode);
    if (!bottomInsertSucc) {
        // fail to insert, split
        SplitMLNodeAndUnlock (mnode, {{key, val}});
        return true;
    }

    // 5. succ. update bloomfilter in Dram
    mMidLayer.SetBloomFilter (key, mnode);
    mnode->lock.writeUnlock ();
    return true;
}

bool SPTree::remove (key_t key) {
retry:
    // 1. find the target middle layer node, and its version snapshot
    MLNode* mnode;
    uint64_t v;
    std::tie (mnode, v) = jumpToMiddleLayer (key);

    // 2. check the bloomfilter in middle layer
    bool needRestart = false;
    bool maybeExist = mMidLayer.CouldExist (key, mnode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }
    if (!maybeExist) {
        // does not exist
        mnode->lock.checkOrRestart (v, needRestart);
        if (needRestart) {
            goto retry;
        }
        return false;
    }

    mnode->lock.upgradeToWriteLockOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }

    // 3. try to remove from write buffer
    if (mnode->type == MLNodeTypeWithBuffer) {
        bool removeMNodeSucc = mnode->remove (key);
        bool removeLeafNodeSucc = false;
        if (maybeExist) {
            removeLeafNodeSucc = mBotLayer.Remove (key, mnode->leafNode);
        }
        if (removeMNodeSucc) {
            if (mEnableLog) {
                WAL* local_log = WAL::GetThreadLocalLog (mSPTreePmemRoot);
                if (!local_log) {
                    ERROR ("remove obtain nullptr log");
                    perror ("remove obtain nullptr log\n");
                    exit (1);
                }
                local_log->Append (kWALNodeRemove, key, 0, mnode->headptr);
            }
            // we also need to remove it from the leafnode (the old version)
            mnode->lock.writeUnlock ();
            return true;
        } else {
            mnode->lock.writeUnlock ();
            return removeLeafNodeSucc;
        }
    }

    // 4. try to remove from bottom layer
    bool res = false;
    if (maybeExist) res = mBotLayer.Remove (key, mnode->leafNode);
    mnode->lock.writeUnlock ();
    return res;
}

std::string SPTree::ToString () {
    std::string res;
    MLNode* cur = mMidLayer.head;
    size_t total_record = 0;
    while (cur != nullptr) {
        std::string row;
        char buffer[1024];
        int outliar = 0;
        LeafNode64* leafnode = cur->leafNode;
        for (int i : leafnode->ValidBitSet ()) {
            total_record++;
            auto& slot = leafnode->slots[i];
            if (slot.key < leafnode->lkey or leafnode->hkey <= slot.key) {
                outliar++;
                sprintf (buffer, "[%lu], ", slot.key);
            } else {
                sprintf (buffer, "%lu, ", slot.key);
            }
            row += buffer;
        }
        row += "\n";
        sprintf (buffer, "=== [%lu, %lu), outlier: %d. ", leafnode->lkey, leafnode->hkey, outliar);
        res += buffer + row;
        cur = cur->next;
    }
    res += "total: " + std::to_string (total_record) + "\n";
    return res;
}

std::string SPTree::ToStats () {
    std::string toplayer_stats = mTopLayer.ToStats () + "\n";
    std::string midlayer_stats = mMidLayer.ToStats () + "\n";
    std::string botlayer_stats = mBotLayer.ToStats () + "\n";
    std::string toplayer_pmem_stats = mTopLayerPmem.ToStats ();
    return toplayer_stats + midlayer_stats + botlayer_stats + toplayer_pmem_stats;
}

}  // namespace spoton
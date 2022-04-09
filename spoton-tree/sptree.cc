#include "sptree.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

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

SPTree* SPTree::CreateSPTree (bool isDram) {
    // configure bottom layer leafnode as dram node or pmem node
    LeafNode64::kIsLeafNodeDram = isDram;

    SPTree* sptree = nullptr;
    if (isDram) {
        // create a dram version of sptree
        sptree = new SPTree (isDram);
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

        sptree = new SPTree (isDram);
        sptree->Initialize (sptree_pmem_root);
    }

    return sptree;
}

SPTree* SPTree::RecoverSPTree () {
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
    SPTree* sptree = new SPTree (false);
    sptree->Recover (sptree_pmem_root);
    return sptree;
}

void SPTree::Initialize (SPTreePmemRoot* root) {
    // pmem layers
    botLayer.Initialize (root);
    topLayerPmem.Initialize (root);
    if (!LeafNode64::kIsLeafNodeDram) {
        // pmem mode
        topLayerPmem.insert (kSPTreeMinKey, botLayer.head);
    }
    // dram layers
    midLayer.head->leafNode = botLayer.head;
    midLayer.head->next->leafNode = botLayer.head->GetNext ();
    topLayer.insert (kSPTreeMinKey, midLayer.head);
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
    botLayer.Recover (root);
    // obtain the pmem btree
    topLayerPmem.Recover (root);

    // rebuild the top layer and middle layer

    // 1. Iterate the leaf page of pmem btree
    std::vector<RecoverUnit> units;
    topLayerPmem.ScanAllRecord ([&units] (key_t key, void* leafnode_ptr) {
        DEBUG ("scan top key %lu", key);
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
                               this->topLayer.insert (unit.minKey, unit.mnode);
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
    midLayer.head = head;
    prev->next = midLayer.dummyTail;
    midLayer.dummyTail->prev = prev;
}

void SPTree::WaitAllJobs () {
    // wait all jobs in tpool to finish
    tpool.wait_for_tasks ();
}

SPTree::SPTree (bool isDram) : botLayer (isDram), topLayerPmem (isDram), tpool (4) {}

SPTree::~SPTree () {
    if (!LeafNode64::kIsLeafNodeDram) RP_close ();
};

std::tuple<MLNode*, uint64_t> SPTree::jumpToMiddleLayer (key_t key) {
retry1:
    // 1. find the target middle layer node
    MLNode* mnode = reinterpret_cast<MLNode*> (topLayer.seekLE (key));
    assert (mnode != nullptr);
    mnode = midLayer.FindTargetMLNode (key, mnode);

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

void SPTree::SplitMNodeAndUnlock (MLNode* mnode,
                                  std::vector<std::pair<key_t, val_t>>& toMergedRecords) {
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
    MLNode* new_mnode = new (EnableWriteBuffer) MLNode (EnableWriteBuffer);
    void* newLeafNodeAddr = botLayer.Malloc (sizeof (LeafNode64));
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
    topLayer.insert (newlkey, new_mnode);

    // 4f. send jobs to background thread pool to update pmem top layer
    if (topLayerPmem.Exist ()) {
        tpool.submit ([this, newlkey, newLeafNode] () {
            // DEBUG ("pmem btree insert %lu", newlkey);
            this->topLayerPmem.insert (newlkey, newLeafNode);
        });
    }

    mnode->lock.writeUnlock ();
    old_next_mnode->lock.writeUnlock ();
}

TID SPTree::lookup (key_t key) {
retry:
    // 1. find the target middle layer node, and its version snapshot
    bool needRestart = false;
    MLNode* mnode;
    uint64_t v;
    std::tie (mnode, v) = jumpToMiddleLayer (key);

    // 2. check the bloomfilter in middle layer
    bool maybeExist = midLayer.CouldExist (key, mnode);
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
    bool res = botLayer.Lookup (key, val, mnode->leafNode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }

    return res ? val : 0;
}

bool SPTree::insert (key_t key, TID val) {
    DEBUG ("new insert %lu, %lu", key, val);
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
            midLayer.SetBloomFilter (key, mnode);
            mnode->lock.writeUnlock ();
            return true;
        } else {
            // write buffer full, merge to pmem node
            size_t count1 = mnode->recordCount ();      // record # in buffer
            size_t count2 = mnode->leafNode->Count ();  // record # in leafnode

            // 3.a we can merge all the records in buffer
            if (count1 + count2 <= LeafNode64::kLeafNodeCapacity - 1) {
                // insert all records in the write buffer
                auto* buffer = mnode->getNodeBuffer ();
                for (int i : buffer->ValidBitSet ()) {
                    auto slot = buffer->slots[i];
                    DEBUG ("buffer 0x%lx flush insert %lu, %lu", buffer, slot.key, slot.val);
                    botLayer.Insert (slot.key, slot.val, mnode->leafNode);
                }
                // insert this record
                DEBUG ("buffer new insert %lu, %lu", key, val);
                bool res = botLayer.Insert (key, val, mnode->leafNode);
                if (!res) {
                    perror ("fail insertion to bottom layer");
                    ERROR ("fail insertion to bottom layer");
                    exit (1);
                }
                midLayer.SetBloomFilter (key, mnode);

                // clear write buffer
                mnode->resetBuffer ();
                mnode->lock.writeUnlock ();
                return true;
            }

            // 3.b leafnode cannot hold all the records
            NodeBuffer* buffer = mnode->getNodeBuffer ();
            std::vector<std::pair<key_t, val_t>> toMerge;
            for (int i : buffer->ValidBitSet ()) {
                auto slot = buffer->slots[i];
                DEBUG ("add toMerge %lu", slot.key);
                toMerge.push_back ({slot.key, slot.val});
            }
            DEBUG ("add toMerge %lu", key);
            toMerge.push_back ({key, val});
            SplitMNodeAndUnlock (mnode, toMerge);

            // clear write buffer after split
            mnode->resetBuffer ();
            return true;
        }
    }

    // 4. insert to bottom layer
    DEBUG ("No buffer insert %lu, %lu", key, val);
    bool bottomInsertSucc = botLayer.Insert (key, val, mnode->leafNode);
    if (!bottomInsertSucc) {
        // fail to insert, split
        std::vector<std::pair<key_t, val_t>> toMerge;
        toMerge.push_back ({key, val});
        SplitMNodeAndUnlock (mnode, toMerge);
        return true;
    }

    // 5. succ. update bloomfilter in Dram
    midLayer.SetBloomFilter (key, mnode);
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
    bool maybeExist = midLayer.CouldExist (key, mnode);
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
        if (removeMNodeSucc) {
            mnode->lock.writeUnlock ();
            return true;
        }
    }

    // 4. try to remove from bottom layer
    bool res = botLayer.Remove (key, mnode->leafNode);
    mnode->lock.writeUnlock ();
    return res;
}

std::string SPTree::ToString () {
    std::string res;
    MLNode* cur = midLayer.head;
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
    std::string toplayer_stats = topLayer.ToStats () + "\n";
    std::string midlayer_stats = midLayer.ToStats () + "\n";
    std::string botlayer_stats = botLayer.ToStats () + "\n";
    std::string toplayer_pmem_stats = topLayerPmem.ToStats ();
    return toplayer_stats + midlayer_stats + botlayer_stats + toplayer_pmem_stats;
}

}  // namespace spoton
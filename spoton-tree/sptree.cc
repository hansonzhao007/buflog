#include "sptree.h"

#include "sptree_meta.h"
namespace spoton {

void SPTree::DistroySPTree (void) {
    ::remove ("/mnt/pmem/sptree_sb");
    ::remove ("/mnt/pmem/sptree_desc");
    ::remove ("/mnt/pmem/sptree_basemd");
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
    botLayer.Initialize (root);
    topLayerPmem.Initialize (root);
    midLayer.head->leafNode = botLayer.head;
    midLayer.head->next->leafNode = botLayer.head->GetNext ();
    topLayer.insert (0, midLayer.head);
}

void SPTree::Recover (SPTreePmemRoot* root) {
    botLayer.Recover (root);
    topLayerPmem.Recover (root);
    // TODO: recover the top layer and middle layer
}

SPTree::SPTree (bool isDram) : botLayer (isDram), topLayerPmem (isDram), tpool (2) {}

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

TID SPTree::lookup (key_t key) {
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
        return 0;
    }

    // 3. check the leaf node
    val_t val;
    bool res = botLayer.Lookup (key, val, mnode->leafNode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }

    return res ? val : 0;
}

void AsyncInsert (TopLayerPmem* pmem_tree, key_t key, void* ptr) { pmem_tree->insert (key, ptr); };

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

    // 3. insert to leafnode on pmem
    bool bottomInsertRes = botLayer.Insert (key, val, mnode->leafNode);
    if (!bottomInsertRes) {
        // fail to insert, split
    retry_lock_next:
        needRestart = false;
        // 3a. lock next mnode when split
        mnode->next->lock.writeLockOrRestart (needRestart);
        if (needRestart) {
            goto retry_lock_next;
        }

        // 3b. split the leaf node
        MLNode* old_next_mnode = mnode->next;
        MLNode* new_mnode = new MLNode ();
        void* newLeafNodeAddr = botLayer.Malloc (sizeof (LeafNode64));
        LeafNode64* newLeafNode;
        key_t newlkey;
        std::tie (newLeafNode, newlkey) = mnode->leafNode->Split (
            key, val, newLeafNodeAddr, mnode->GetBloomFilter (), new_mnode->GetBloomFilter ());

        // 3c. enable bloom filters for both mnode and new_mnode
        mnode->EnableBloomFilter ();
        new_mnode->EnableBloomFilter ();

        // 3d. link mnode for newLeafNode
        new_mnode->lkey = newlkey;
        new_mnode->hkey = mnode->hkey;
        new_mnode->prev = mnode;
        new_mnode->next = mnode->next;
        new_mnode->leafNode = newLeafNode;
        mnode->next->prev = new_mnode;
        mnode->next = new_mnode;
        mnode->hkey = newlkey;

        // 3e. insert newlkey->newNode to dram toplayer
        topLayer.insert (newlkey, new_mnode);

        // 3f. send jobs to background thread pool to update pmem top layer
        if (topLayerPmem.Exist ()) {
            tpool.submit ([this, newlkey, newLeafNode] () {
                this->topLayerPmem.insert (newlkey, newLeafNode);
            });
        }

        mnode->lock.writeUnlock ();
        old_next_mnode->lock.writeUnlock ();

        return true;
    }

    // 4. succ. update bloomfilter in Dram
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
        return false;
    }

    // 3. lock the mnode and try to remove
    mnode->lock.upgradeToWriteLockOrRestart (v, needRestart);
    if (needRestart) {
        // version has changed
        goto retry;
    }
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
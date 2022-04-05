#include "sptree.h"

namespace spoton {

SPTree::SPTree (bool isDram) : botLayer (isDram) {
    // Initialize the first node in middle layer and bottom layer
    botLayer.initialize ();
    midLayer.head->leafNode = botLayer.head;
    midLayer.head->next->leafNode = botLayer.head->next;
    topLayer.insert (0, midLayer.head);
}

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
        auto [newLeafNode, newlkey] = mnode->leafNode->Split (
            key, val, newLeafNodeAddr, mnode->bloomfilter, new_mnode->bloomfilter);

        // 3b. link mnode for newLeafNode
        new_mnode->lkey = newlkey;
        new_mnode->hkey = mnode->hkey;
        new_mnode->prev = mnode;
        new_mnode->next = mnode->next;
        new_mnode->leafNode = newLeafNode;
        mnode->next->prev = new_mnode;
        mnode->next = new_mnode;
        mnode->hkey = newlkey;

        // 3b. insert newlkey->newNode to toplayer
        topLayer.insert (newlkey, new_mnode);

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

void DistroyBtree (void) {
    remove ("/mnt/pmem/sptree_sb");
    remove ("/mnt/pmem/sptree_desc");
    remove ("/mnt/pmem/sptree_basemd");
}

SPTree* CreateBtree (bool isDram) {
    printf ("Create Pmem SPTree\n");
    // Step1. Initialize pmem library
    if (!isDram) RP_init ("sptree", SPTREE_PMEM_SIZE);
    SPTree* btree_root = new SPTree (isDram);
    return btree_root;
}

}  // namespace spoton
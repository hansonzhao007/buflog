#include "sptree.h"

namespace spoton {

SPTree::SPTree () {
    // Initialize the first node in middle layer and bottom layer
    botLayer.initialize ();
    midLayer.head->leafNode = botLayer.head;
    topLayer.insert (0, midLayer.head);
}

MLNode* SPTree::jumpToMiddleLayer (key_t key) {
    MLNode* mnode = reinterpret_cast<MLNode*> (topLayer.seekLE (key));
    assert (mnode != nullptr);
    return midLayer.FindTargetMLNode (key, mnode);
}

TID SPTree::lookup (key_t& key) {
retry1:
    MLNode* mnode = jumpToMiddleLayer (key);

retry2:
    bool needRestart = false;
    auto v = mnode->lock.readLockOrRestart (needRestart);
    if (needRestart) {
        // other thread is writing this mnode
        _mm_pause ();
        goto retry2;
    }
    if (RetryVersionLock::isObsolete (v)) {
        // this mnode has been deleted
        goto retry1;
    }

    if (key < mnode->lkey or mnode->hkey <= key) {
        // this mnode is not our target node, retry
        goto retry1;
    }

    val_t val;
    botLayer.Lookup (key, val, mnode->leafNode);
    mnode->lock.checkOrRestart (v, needRestart);
    if (needRestart) {
        goto retry2;
    }

    return val;
}

bool SPTree::insert (key_t& key, TID val) {
retry1:
    // 1. find the target middle layer node
    MLNode* mnode = jumpToMiddleLayer (key);

retry2:
    bool needRestart = false;
    auto v = mnode->lock.readLockOrRestart (needRestart);
    if (needRestart) {
        // other thread is writing this mnode
        _mm_pause ();
        goto retry2;
    }
    if (RetryVersionLock::isObsolete (v)) {
        // this mnode has been deleted
        goto retry1;
    }

    // 2. lock the mnode and try to insert
    mnode->lock.upgradeToWriteLockOrRestart (v, needRestart);
    if (needRestart) {
        // fail to wirte-lock this node, retry
        goto retry2;
    }

    if (key < mnode->lkey or mnode->hkey <= key) {
        // this mnode is not our target node, retry
        mnode->lock.writeUnlock ();
        goto retry1;
    }

    // 3. insert to leafnode on pmem
    bool bottomInsertRes = botLayer.Insert (key, val, mnode->leafNode);
    if (!bottomInsertRes) {
        // fail to insert, split
    retry3:
        needRestart = false;
        // 3a. lock next mnode when split
        mnode->next->lock.writeLockOrRestart (needRestart);
        if (needRestart) {
            goto retry3;
        }

        // 3b. split the leaf node
        auto [newLeafNode, newlkey] = mnode->leafNode->Split (key, val);

        // 3b. create a mnode for newLeafNode
        MLNode* old_next_mnode = mnode->next;
        MLNode* new_mnode = new MLNode ();
        new_mnode->lkey = newlkey;
        new_mnode->hkey = newLeafNode->hkey;
        new_mnode->prev = mnode;
        new_mnode->next = mnode->next;
        mnode->next->prev = new_mnode;
        mnode->next = new_mnode;
        new_mnode->leafNode = newLeafNode;
        BloomFilterFix64::BuildBloomFilter (newLeafNode, mnode->bloomfilter);

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

}  // namespace spoton
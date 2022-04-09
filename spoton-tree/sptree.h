#ifndef SPOTON_TREE_IMPL_H
#define SPOTON_TREE_IMPL_H

#include <vector>

#include "Key.h"
#include "bottomLayer.h"
#include "middleLayer.h"
#include "thread-pool/thread_pool.hpp"
#include "topLayer.h"
#include "topLayerPmem.h"
#include "wal.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

class SPTree {
public:
    TopLayer mTopLayer;          // dram top layer
    MiddleLayer mMidLayer;       // dram middle layer
    BottomLayer mBotLayer;       // pmem bottom layer
    TopLayerPmem mTopLayerPmem;  // async update the pmem top layer
    thread_pool mTpool;

    WAL mWAL;
    bool mEnableLog;

    SPTreePmemRoot* mSPTreePmemRoot;

public:
    static void DistroySPTree ();
    static SPTree* CreateSPTree (bool isDram, bool withLog);
    // recover sptree from pmem
    static SPTree* RecoverSPTree (bool withLog);

    void WaitAllJobs ();

private:
    // You should not call the constructor directly, use CreateSPTree instead
    SPTree (bool isDram, bool withLog);
    void Initialize (SPTreePmemRoot*);
    void Recover (SPTreePmemRoot*);
    void SplitMNodeAndUnlock (MLNode* mnode, std::vector<std::pair<key_t, val_t>>& toMergedRecords);

public:
    ~SPTree ();

    bool insert (key_t key, TID val);
    bool remove (key_t key);
    TID lookup (key_t key);
    uint64_t scan (key_t startKey, int resultSize, std::vector<TID>& result);

    std::string ToString ();

    std::string ToStats ();

private:
    // locate the target middle layer node and its version, without lock
    std::tuple<MLNode*, uint64_t> jumpToMiddleLayer (key_t key);
};

};  // namespace spoton
#endif
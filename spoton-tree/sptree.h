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
private:
    TopLayer mTopLayer;          // dram top layer
    MiddleLayer mMidLayer;       // dram middle layer
    BottomLayer mBotLayer;       // pmem bottom layer
    TopLayerPmem mTopLayerPmem;  // async update the pmem top layer
    thread_pool mTpool;

    bool mEnableWriteBuffer;
    WAL mWAL;
    bool mEnableLog;

    SPTreePmemRoot* mSPTreePmemRoot;

public:
    static void DistroySPTree ();
    static SPTree* CreateSPTree (bool isDram, bool withWriteBuffer, bool withLog);
    // recover sptree from pmem
    static SPTree* RecoverSPTree (bool withWriteBuffer, bool withLog);

    SPTreePmemRoot* GetPmemRoot () { return mSPTreePmemRoot; }

public:
    ~SPTree ();

    bool insert (key_t key, TID val);
    bool remove (key_t key);
    TID lookup (key_t key);
    uint64_t scan (key_t startKey, int resultSize, std::vector<TID>& result);

    // print memory usage info
    std::string ToStats ();

    // print all slots, for debug
    std::string ToString ();

private:
    // You should not call the constructor directly, use CreateSPTree instead
    SPTree (bool isDram, bool withWriteBuffer, bool withLog);
    void Initialize (SPTreePmemRoot*);
    void Recover (SPTreePmemRoot*);

    void FlushMLNodeBuffer (MLNode* mnode, key_t key, val_t val);
    void SplitMLNodeAndUnlock (MLNode* mnode,
                               const std::vector<std::pair<key_t, val_t>>& toMergedRecords);
    void FlushOrSplitAndUnlock (MLNode* mnode, key_t key, val_t val);

    void SortLeafNode (MLNode* mnode, uint64_t& version, bool& needRestart);

    // locate the target middle layer node and its version, without lock
    std::tuple<MLNode*, uint64_t> jumpToMiddleLayer (key_t key);

    void WaitAllJobs ();
};

};  // namespace spoton
#endif
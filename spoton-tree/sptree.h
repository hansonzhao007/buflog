#ifndef SPOTON_TREE_IMPL_H
#define SPOTON_TREE_IMPL_H

#include <vector>

#include "Key.h"
#include "bottomLayer.h"
#include "middleLayer.h"
#include "thread-pool/thread_pool.hpp"
#include "topLayer.h"
#include "topLayerPmem.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

class SPTree {
public:
    TopLayer topLayer;          // dram top layer
    MiddleLayer midLayer;       // dram middle layer
    BottomLayer botLayer;       // pmem bottom layer
    TopLayerPmem topLayerPmem;  // async update the pmem top layer
    thread_pool tpool;

public:
    static constexpr size_t SPTREE_PMEM_SIZE{((128LU << 30))};

    static void DistroySPTree (void);

    static SPTree* CreateSPTree (bool isDram);

    // recover sptree from pmem
    static SPTree* RecoverSPTree ();

    void WaitAllJobs ();

private:
    // You should not call the constructor directly, use CreateSPTree instead
    SPTree (bool isDram = true);
    void Initialize (SPTreePmemRoot*);
    void Recover (SPTreePmemRoot*);

public:
    ~SPTree ();

    bool insert (key_t key, TID val);
    bool update (key_t key, TID val);
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
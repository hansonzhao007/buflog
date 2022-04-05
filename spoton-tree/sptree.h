#ifndef SPOTON_TREE_IMPL_H
#define SPOTON_TREE_IMPL_H

#include <vector>

#include "Key.h"
#include "bottomLayer.h"
#include "middleLayer.h"
#include "topLayer.h"
#include "util.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

class SPTree {
private:
    TopLayer topLayer;     // dram top layer
    MiddleLayer midLayer;  // dram middle layer
    BottomLayer botLayer;  // pmem bottom layer

public:
    SPTree (bool isDram = true);
    ~SPTree (){};

    bool insert (key_t key, TID val);
    bool update (key_t key, TID val);
    bool remove (key_t key);
    TID lookup (key_t key);
    uint64_t scan (key_t startKey, int resultSize, std::vector<TID>& result);

    std::string ToString ();

private:
    // locate the target middle layer node and its version, without lock
    std::tuple<MLNode*, uint64_t> jumpToMiddleLayer (key_t key);
};

const size_t SPTREE_PMEM_SIZE = ((100LU << 30));

void DistroyBtree (void);

SPTree* CreateBtree (bool isDram);

};  // namespace spoton
#endif
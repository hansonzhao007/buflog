#ifndef SPOTON_TREE_META_H
#define SPOTON_TREE_META_H

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {
class btree;
struct SPTreePmemRoot {
    pptr<btree> topLayerPmemBtree;
    pptr<btree> bottomLayerLeafNode64_head;
};

}  // namespace spoton

#endif
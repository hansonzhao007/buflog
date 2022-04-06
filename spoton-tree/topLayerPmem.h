#ifndef SPOTON_TREE_TOP_LAYER_PMEM_H
#define SPOTON_TREE_TOP_LAYER_PMEM_H

#include "sptree_meta.h"

namespace spoton {

class btree;

class TopLayerPmem {
public:
    btree* tree_pmem;  // pmem pointer
public:
    explicit TopLayerPmem (bool isDram) {
        if (isDram) return;
    }

    // insert the minKey->pmem_leafnode
    bool insert (key_t minKey, void* pmem_leaf);

    void* seekLE (key_t key);

    void remove (key_t key);
};
}  // namespace spoton
#endif
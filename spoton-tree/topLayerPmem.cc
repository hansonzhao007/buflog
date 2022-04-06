#include "topLayerPmem.h"

#include "btree_pmem.h"

namespace spoton {

// insert the minKey->pmem_leafnode
bool TopLayerPmem::insert (key_t minKey, void* pmem_leaf) {
    tree_pmem->btree_insert (minKey, (char*)pmem_leaf);
    return true;
}

void* TopLayerPmem::seekLE (key_t key) { return tree_pmem->btree_seekLE (key); }

void TopLayerPmem::remove (key_t key) { tree_pmem->btree_delete (key); }
}  // namespace spoton
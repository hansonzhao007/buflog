#include "topLayerPmem.h"

#include "btree_pmem.h"
#include "logger.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

void TopLayerPmem::Initialize (SPTreePmemRoot* root) {
    if (isDram) {
        if (root) {
            ERROR ("Initialize TopLayerPmem in dram mode");
            perror ("ERROR. Initialize TopLayerPmem in dram mode\n");
            exit (1);
        }
        tree_pmem = nullptr;
    } else {
        tree_pmem = (btree*)RP_malloc (sizeof (btree));
        root->topLayerPmemBtree_tree_pmem = tree_pmem;

        // Initialize members
        tree_pmem->height = 1;
        page* buf = reinterpret_cast<page*> (RP_malloc (sizeof (page)));
        tree_pmem->root = new (buf) page (0);
    }
}

void TopLayerPmem::Recover (SPTreePmemRoot* root) { tree_pmem = root->topLayerPmemBtree_tree_pmem; }

// insert the minKey->pmem_leafnode
bool TopLayerPmem::insert (key_t minKey, void* pmem_leaf) {
    tree_pmem->btree_insert (minKey, (char*)pmem_leaf);
    return true;
}

void* TopLayerPmem::seekLE (key_t key) { return tree_pmem->btree_seekLE (key); }

void TopLayerPmem::remove (key_t key) { tree_pmem->btree_delete (key); }

std::string TopLayerPmem::ToStats () {
    if (isDram) {
        return "Dram mode. No TopLayerPmem";
    }
    return tree_pmem->ToStats ();
}

}  // namespace spoton
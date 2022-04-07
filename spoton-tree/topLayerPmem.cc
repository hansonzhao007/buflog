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

void TopLayerPmem::Recover (SPTreePmemRoot* root) {
    if (isDram) {
        ERROR ("Recover TopLayerPmem in dram mode");
        perror ("ERROR. Recover TopLayerPmem in dram mode\n");
        exit (1);
    }
    if (root) {
        tree_pmem = root->topLayerPmemBtree_tree_pmem;
        tree_pmem->recoverMutex ();
    } else {
        ERROR ("Recover with nullptr root");
        perror ("Recover with nullptr root\n");
        exit (1);
    }
}

void TopLayerPmem::ScanAllRecord (RecoverCallbackFn callback) {
    if (isDram) {
        ERROR ("scan TopLayerPmem in dram mode");
        perror ("scan TopLayerPmem in dram mode\n");
        exit (1);
    }
    assert (tree_pmem);
    tree_pmem->scan_all_record (callback);
}

// insert the minKey->pmem_leafnode
bool TopLayerPmem::insert (key_t minKey, void* pmem_leaf) {
    assert (tree_pmem);
    tree_pmem->btree_insert (minKey, (char*)pmem_leaf);
    return true;
}

void* TopLayerPmem::seekLE (key_t key) {
    assert (tree_pmem);
    return tree_pmem->btree_seekLE (key);
}

void TopLayerPmem::remove (key_t key) {
    assert (tree_pmem);
    tree_pmem->btree_delete (key);
}

std::string TopLayerPmem::ToStats () {
    if (isDram) {
        return "Dram mode. No TopLayerPmem";
    }
    // tree_pmem->printAll ();
    return tree_pmem->ToStats ();
}

}  // namespace spoton
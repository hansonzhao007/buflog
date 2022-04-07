#ifndef SPOTON_TREE_TOP_LAYER_PMEM_H
#define SPOTON_TREE_TOP_LAYER_PMEM_H

#include <functional>

#include "sptree_meta.h"
namespace spoton {

using key_t = size_t;
using val_t = size_t;
using RecoverCallbackFn = std::function<void (key_t key, void* ptr)>;
class btree;

class TopLayerPmem {
public:
    btree* tree_pmem{nullptr};  // pmem pointer
    bool isDram{false};

public:
    explicit TopLayerPmem (bool is_dram) : isDram (is_dram) {}
    inline bool Exist () { return tree_pmem != nullptr; }
    void Initialize (SPTreePmemRoot*);
    void Recover (SPTreePmemRoot*);

    void ScanAllRecord (RecoverCallbackFn callback);

    // insert the minKey->pmem_leafnode
    bool insert (key_t minKey, void* pmem_leaf);
    void* seekLE (key_t key);
    void remove (key_t key);

    std::string ToStats ();
};
}  // namespace spoton
#endif
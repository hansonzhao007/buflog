#ifndef SPOTON_TREE_TOP_LAYER_H
#define SPOTON_TREE_TOP_LAYER_H

#include "ART_DRAM/Tree.h"

namespace spoton {

static void loadKey (TID tid, Key& key) {
    // here tid is the pointer to either a leaf node or a middle layer node, and the first 8 byte
    // should be the minKey
    size_t minKey = *reinterpret_cast<uint64_t*> (tid);
    key.setKeyLen (sizeof (minKey));
    reinterpret_cast<uint64_t*> (&key[0])[0] = __builtin_bswap64 (minKey);
}

class TopLayer {
public:
    ART_DRAM::Tree* tree_dram;

public:
    using LoadFn = void (*) (TID tid, Key& key);

    explicit TopLayer (LoadFn fn) { tree_dram = new ART_DRAM::Tree (fn); }

    explicit TopLayer () { tree_dram = new ART_DRAM::Tree (loadKey); }

    ~TopLayer () { delete tree_dram; }

    // insert the minKey->middle layer pointer
    bool insert (size_t minkey, void* mlptr);

    // return the leafnode pointer whose minKey <= key
    // however, it is not gurranteed that the returned node has minKey <= key,
    // that's why we have `FindTargetMLNode` in MiddleLayer class to fix the issue.
    void* seekLE (size_t key);

    void remove (size_t key, void* leafnode_ptr);
};
};  // namespace spoton

#endif
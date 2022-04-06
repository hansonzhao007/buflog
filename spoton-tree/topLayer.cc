#include "topLayer.h"

namespace spoton {

static void setKey (Key& key, size_t k) {
    key.setKeyLen (sizeof (k));
    reinterpret_cast<uint64_t*> (&key[0])[0] = __builtin_bswap64 (k);
}

// insert the minKey->middle layer pointer
bool TopLayer::insert (size_t minkey, void* mlptr) {
    auto tinfo = tree_dram->getThreadInfo ();
    Key insertKey;
    setKey (insertKey, minkey);
    tree_dram->insert (insertKey, reinterpret_cast<uint64_t> (mlptr), tinfo);
    return true;
}

// return the leafnode pointer whose minKey <= key
// however, it is not gurranteed that the returned node has minKey <= key,
// that's why we have `FindTargetMLNode` in MiddleLayer class to fix the issue.
void* TopLayer::seekLE (size_t key) {
    auto tinfo = tree_dram->getThreadInfo ();
    Key skey;
    setKey (skey, key);
    auto tid = tree_dram->seekLE (skey, tinfo);
    if (tid != 0) {
        return reinterpret_cast<void*> (tid);
    }
    return nullptr;
}

void TopLayer::remove (size_t key, void* leafnode_ptr) {
    auto tinfo = tree_dram->getThreadInfo ();
    Key rkey;
    setKey (rkey, key);
    tree_dram->remove (rkey, reinterpret_cast<uint64_t> (leafnode_ptr), tinfo);
}

}  // namespace spoton
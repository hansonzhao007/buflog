#ifndef SPOTON_TREE_META_H
#define SPOTON_TREE_META_H

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {
constexpr size_t kSPTreeMinKey = 1;
class btree;
class LeafNode64;
struct SPTreePmemRoot {
    pptr<btree> topLayerPmemBtree_tree_pmem;
    pptr<LeafNode64> bottomLayerLeafNode64_head;
    pptr<LeafNode64> bottomLayerLeafNode64_dummyTail;
};

class LeafNodeBitSet {
public:
    LeafNodeBitSet () : bits_ (0) {}

    explicit LeafNodeBitSet (uint64_t bits) : bits_ (bits) {}

    inline int validCount (void) { return __builtin_popcountll (bits_); }

    inline LeafNodeBitSet& operator++ () {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool () const { return bits_ != 0; }

    inline int operator* () const {
        // count the tailing zero bit
        return __builtin_ctzll (bits_);
    }

    inline LeafNodeBitSet begin () const { return *this; }

    inline LeafNodeBitSet end () const { return LeafNodeBitSet (0); }

    inline uint64_t bit () { return bits_; }

    inline void set (int pos) {
        assert (pos < 64);
        bits_ |= (1LU << pos);
    }

private:
    friend bool operator== (const LeafNodeBitSet& a, const LeafNodeBitSet& b) {
        return a.bits_ == b.bits_;
    }
    friend bool operator!= (const LeafNodeBitSet& a, const LeafNodeBitSet& b) {
        return a.bits_ != b.bits_;
    }
    uint64_t bits_;
};
}  // namespace spoton

#endif
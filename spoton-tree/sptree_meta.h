#ifndef SPOTON_TREE_META_H
#define SPOTON_TREE_META_H

#include <atomic>
// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

static bool EnableWriteBuffer = false;

static constexpr size_t SPTREE_LOG_SIZE = 64LU << 30;  // 64GB for log
static size_t SPTREE_LOCAL_LOG_SIZE{0};                // this is the log size for each thread
static constexpr size_t SPTREE_PMEM_SIZE{((128LU << 30)) + SPTREE_LOG_SIZE};  // 128GB for data

static inline void SPTreeMemFlush (void* addr) { asm volatile("clwb (%0)" ::"r"(addr)); }
static inline void SPTreeMemFlushFence () { asm volatile("sfence" ::: "memory"); }

static inline void SPTreeCLFlush (char* data, int len) {
    volatile char* ptr = (char*)((unsigned long)data & ~(64 - 1));
    for (; ptr < data + len; ptr += 64) {
        SPTreeMemFlush ((void*)ptr);
    }
    SPTreeMemFlushFence ();
}

constexpr size_t kSPTreeMinKey = 1;
class btree;
class LeafNode64;
class WAL;

struct SPTreePmemRoot {
    pptr<btree> topLayerPmemBtree_tree_pmem{nullptr};
    pptr<LeafNode64> bottomLayerLeafNode64_head{nullptr};
    pptr<LeafNode64> bottomLayerLeafNode64_dummyTail{nullptr};
    pptr<char> log_base_addrs[128]{nullptr};  // reserved wal space
    std::atomic<size_t> log_count{0};         // how many logs for this sptree
    void Reset () {
        topLayerPmemBtree_tree_pmem = nullptr;
        bottomLayerLeafNode64_head = nullptr;
        bottomLayerLeafNode64_dummyTail = nullptr;
        log_count = 0;
    }
};

class NodeBitSet {
public:
    NodeBitSet () : bits_ (0) {}

    explicit NodeBitSet (uint64_t bits) : bits_ (bits) {}

    inline int validCount (void) { return __builtin_popcountll (bits_); }

    inline NodeBitSet& operator++ () {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool () const { return bits_ != 0; }

    inline int operator* () const {
        // count the tailing zero bit
        return __builtin_ctzll (bits_);
    }

    inline NodeBitSet begin () const { return *this; }

    inline NodeBitSet end () const { return NodeBitSet (0); }

    inline uint64_t bit () { return bits_; }

    inline void set (int pos) {
        assert (pos < 64);
        bits_ |= (1LU << pos);
    }

private:
    friend bool operator== (const NodeBitSet& a, const NodeBitSet& b) { return a.bits_ == b.bits_; }
    friend bool operator!= (const NodeBitSet& a, const NodeBitSet& b) { return a.bits_ != b.bits_; }
    uint64_t bits_;
};
}  // namespace spoton

#endif
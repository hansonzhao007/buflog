#ifndef SPOTON_TREE_BOTTOM_LAYER_H
#define SPOTON_TREE_BOTTOM_LAYER_H

// ralloc

#include "pptr.hpp"
#include "ralloc.hpp"
#include "retryLock.h"
#include "sptree_meta.h"
namespace spoton {

class BloomFilterFix64;

using key_t = size_t;
using val_t = size_t;

struct LeafNodeSlot {
    key_t key;
    val_t val;
};

class LeafNode64;
class BottomLayer {
public:
    LeafNode64* head;
    bool isDram{false};

public:
    BottomLayer (bool isdram) : isDram (isdram) {}

    void Initialize (SPTreePmemRoot*);
    void Recover (SPTreePmemRoot*);

    bool Insert (key_t key, val_t val, LeafNode64* bnode);
    bool Lookup (key_t key, val_t& val, LeafNode64* bnode);
    bool Remove (key_t key, LeafNode64* bnode);

    void* Malloc (size_t size);

    std::string ToStats ();
};

class LeafNode64 {
public:
    key_t lkey{0};
    key_t hkey{0};
    RetryVersionLock lock;
    union {
        LeafNode64* prev_dram;
        pptr<LeafNode64> prev_pmem;
    };

    union {
        LeafNode64* next_dram;
        pptr<LeafNode64> next_pmem;
    };

    uint64_t valid_bitmap{0};  // 8B
    uint8_t tags[64];
    LeafNodeSlot slots[64];
    uint8_t seqs[64];

    static constexpr size_t kLeafNodeCapacity = 64;

public:
    // by default, create pmem leafnode
    static inline bool kIsLeafNodeDram{false};
    LeafNode64 ();

public:
    bool Insert (key_t key, val_t val);
    bool Lookup (key_t key, val_t& val);
    bool Remove (key_t key);

    struct SortByKey;
    void Sort ();

    // spilt the this node
    // return a new node and its lkey
    std::tuple<LeafNode64*, key_t> Split (std::vector<std::pair<key_t, val_t>>& toMergedRecords,
                                          void* newLeafNodeAddr, BloomFilterFix64& bleft,
                                          BloomFilterFix64& bright);

public:
    void SetPrev (LeafNode64* ptr);
    void SetNext (LeafNode64* ptr);

    LeafNode64* GetPrev ();
    LeafNode64* GetNext ();

    NodeBitSet MatchBitSet (uint8_t tag);
    inline bool Full () { return __builtin_popcountll (valid_bitmap) == 64; }
    inline size_t Count () { return __builtin_popcountll (valid_bitmap); }
    inline NodeBitSet ValidBitSet () { return NodeBitSet (valid_bitmap); }
    inline NodeBitSet EmptyBitSet () { return NodeBitSet (~valid_bitmap); }
    inline void SetValid (int pos) { valid_bitmap |= (1L << pos); }
    inline void SetErase (int pos) { valid_bitmap &= ~(1L << pos); }
    inline bool isValid (int pos) { return (valid_bitmap & (1L << pos)) != 0; }
};

};  // namespace spoton

#endif
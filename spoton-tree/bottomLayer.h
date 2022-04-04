#ifndef SPOTON_TREE_BOTTOM_LAYER_H
#define SPOTON_TREE_BOTTOM_LAYER_H

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"
#include "retryLock.h"
#include "spoton_util.h"

namespace spoton {

/**
 * @brief format:
 * | erase bitmap | valid bitmap | tags | slots |
 *
 */
using key_t = size_t;
using val_t = size_t;

class LeafNode {
public:
    key_t lkey{0};
    key_t hkey{0};

    LeafNode* prev{nullptr};
    LeafNode* next{nullptr};

    RetryVersionLock lock;

public:
    virtual bool Insert (key_t key, val_t val) = 0;
    virtual bool Lookup (key_t key, val_t& val) = 0;
    virtual bool Remove (key_t key) = 0;

public:
};

struct LeafNodeSlot {
    key_t key;
    val_t val;
};

class LeafNode32 : public LeafNode {
public:
    uint32_t valid_bitmap{0};  // 4B
    uint8_t tags[32];
    LeafNodeSlot slots[32];
    uint8_t seqs[32];

public:
    bool Insert (key_t key, val_t val) override;
    bool Lookup (key_t key, val_t& val) override;
    bool Remove (key_t key) override;

    struct SortByKey;
    void Sort ();

public:
    BitSet MatchBitSet (uint8_t tag);
    inline bool Full () { return __builtin_popcount (valid_bitmap) == 32; }
    inline BitSet ValidBitSet () { return BitSet (valid_bitmap); }
    inline BitSet EmptyBitSet () { return BitSet (~valid_bitmap); }
    inline void SetValid (int pos) { valid_bitmap |= (1 << pos); }
    inline void SetErase (int pos) { valid_bitmap &= ~(1 << pos); }
};

class LeafNode64 : public LeafNode {
public:
    uint64_t valid_bitmap{0};  // 8B
    uint8_t tags[64];
    LeafNodeSlot slots[64];
    uint8_t seqs[64];

public:
    bool Insert (key_t key, val_t val) override;
    bool Lookup (key_t key, val_t& val) override;
    bool Remove (key_t key) override;

    struct SortByKey;
    void Sort ();

    // spilt the this node
    // return a new node and its lkey
    std::tuple<LeafNode64*, key_t> Split (key_t key, val_t val);

public:
    BitSet MatchBitSet (uint8_t tag);
    inline bool Full () { return __builtin_popcountll (valid_bitmap) == 64; }
    inline BitSet ValidBitSet () { return BitSet (valid_bitmap); }
    inline BitSet EmptyBitSet () { return BitSet (~valid_bitmap); }
    inline void SetValid (int pos) { valid_bitmap |= (1L << pos); }
    inline void SetErase (int pos) { valid_bitmap &= ~(1L << pos); }
};

class BottomLayer {
public:
    LeafNode64* head;

public:
    LeafNode64* initialize ();
    bool Insert (key_t key, val_t val, LeafNode64* bnode);
    bool Lookup (key_t key, val_t& val, LeafNode64* bnode);
    bool Remove (key_t key, LeafNode64* bnode);

private:
};
};  // namespace spoton

#endif
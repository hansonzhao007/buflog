#ifndef SPOTON_TREE_BOTTOM_LAYER_H
#define SPOTON_TREE_BOTTOM_LAYER_H

#include <unordered_map>

#include "retryLock.h"
#include "sptree_meta.h"
// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

class BloomFilterFix64;

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
    union {
        LeafNode64* prev_dram;
        pptr<LeafNode64> prev_pmem;
    };

    union {
        LeafNode64* next_dram;
        pptr<LeafNode64> next_pmem;
    };
    uint64_t valid_bitmap{0};  // 8B
    size_t cur_version{1};

    uint8_t tags[64];
    LeafNodeSlot slots[64];
    uint8_t seqs[64];
    size_t sort_version{0};
    static constexpr size_t kLeafNodeCapacity = 64;

public:
    // by default, create pmem leafnode
    static inline bool kIsLeafNodeDram{false};
    LeafNode64 ();

public:
    // Insert the slot si without flushing and without setting the bitmap.
    // return true if it is inserted to si. If overwrite happens, return false;
    bool InsertiWithoutSetValidBitmap (key_t key, val_t val, int si);

    std::tuple<bool, int> Insert (key_t key, val_t val, bool wittFlush);
    bool Lookup (key_t key, val_t& val);
    bool Remove (key_t key);

    struct SortByKey;
    void Sort ();

    bool NeedSort () { return cur_version != sort_version; }

    // return the sequence for the first key >= searchKey
    int SeekGE (key_t searchKey);

    class Iterator {
        LeafNode64* node{nullptr};
        int i{0};
        int iend{0};

    public:
        Iterator () = default;
        Iterator (LeafNode64* n, int _i) : node (n), i (_i), iend (node->Count ()) {}

        inline bool Valid () { return i < iend; }
        LeafNodeSlot& operator* () const { return node->slots[node->seqs[i]]; }
        LeafNodeSlot* operator->() const { return &node->slots[node->seqs[i]]; }

        // ++a
        inline Iterator& operator++ () {
            i++;
            return *this;
        }

        inline bool operator!= (const Iterator& iter) { return this->i != iter.i; }

        Iterator begin () { return Iterator (node, 0); }

        Iterator end () { return Iterator (node, iend); }
    };

    Iterator begin () { return Iterator (this, 0); }

    Iterator beginAt (key_t searchKey) {
        int si = SeekGE (searchKey);
        return Iterator (this, si);
    }

    // spilt the this node
    // return a new node and its lkey
    std::tuple<LeafNode64*, key_t> Split (
        const std::vector<std::pair<key_t, val_t>>& toMergedRecords, void* newLeafNodeAddr,
        BloomFilterFix64& bleft, BloomFilterFix64& bright);

    // merge me with the next sibling
    bool Merge (BloomFilterFix64& b);

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
    inline void SetValid (int pos) { valid_bitmap |= (1LU << pos); }
    inline void SetErase (int pos) { valid_bitmap &= ~(1LU << pos); }
    inline bool isValid (int pos) { return (valid_bitmap & (1LU << pos)) != 0; }
};

constexpr size_t kLeafNodeSize = sizeof (LeafNode64);

};  // namespace spoton

#endif
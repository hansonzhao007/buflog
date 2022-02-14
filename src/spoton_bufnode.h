#ifndef _SPOTON_BUFNODE_H_
#define _SPOTON_BUFNODE_H_

#include <immintrin.h>
#include <inttypes.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <string>

#include "spoton_log.h"
#include "spoton_util.h"

namespace spoton {

struct kv_t {
    int64_t key;
    char* val;
};

class SortedBufNode_t {
    union NodeMeta {
        struct {
            // valid bitmap, indicate which slot can still accept new record
            uint16_t valid_;
            // delete bitmap, indicate which slot has been deleted
            uint16_t deleted_;
        };
        uint32_t data_;
    };

public:
    NodeMeta meta_;
    SpinLock lock_;
    uint8_t full_;
    uint8_t tags_[13];
    uint8_t seqs_[13];  //

    int64_t highkey_;    // 40B
    int64_t parentkey_;  // 48B
    kv_t kvs_[13];       // 13 kv slots, 12 available for new insertion
    logptr_t logptr_;    // indicate the head of linked list. which is used to recover the buffer
                         // content after crash

public:
    static constexpr uint16_t kBufNodeFull = 12;
    static constexpr uint16_t kBitMapMask = 0x1FFF;

    SortedBufNode_t ();
    SortedBufNode_t (const SortedBufNode_t&) = delete;

    inline void Lock (void) { lock_.lock (); }
    inline void Unlock (void) { lock_.unlock (); }
    inline void Reset (void) { meta_.data_ = 0; }

    // In accending order, remove the last n records.
    void MaskLastN (int n);

    // return the match slot position with `hash_val`
    inline BitSet MatchBitSet (uint8_t hash_val) {
        auto bitset = _mm_set1_epi8 (hash_val);
        auto tag_meta = _mm_loadu_si128 (reinterpret_cast<const __m128i*> (tags_));
        uint16_t mask = _mm_cmpeq_epi8_mask (bitset, tag_meta);
        return BitSet (mask & meta_.valid_ & (~meta_.deleted_) &
                       kBitMapMask);  // valid filter and detetion filter.
    }

    // return the slots that have been deleted
    inline BitSet EraseBitSet () { return BitSet (meta_.deleted_ & kBitMapMask); }

    // return the slots that have not been marked as valid
    inline BitSet BackupBitSet () { return BitSet (~meta_.valid_ & kBitMapMask); }

    // return the slots that have records and have not been deleted
    inline BitSet ValidBitSet () { return BitSet (meta_.valid_ & (~meta_.deleted_) & kBitMapMask); }

    // return how many valid record this buffer has.
    inline int ValidCount () {
        return __builtin_popcount (meta_.valid_ & (~meta_.deleted_) & kBitMapMask);
    }

    inline bool Full () { return __builtin_popcount (meta_.valid_ & kBitMapMask) == kBufNodeFull; }

    struct SortByKey;
    void Sort (void);

    /**
     * @brief insert a key-val pair to bufnode
     *
     * @param key
     * @param val
     * @return true
     * @return false
     */
    bool Insert (int64_t key, char* val);

    /**
     * @brief insert a key-val pair with key-hash
     *
     * @param key
     * @param val
     * @param hash
     * @return true
     * @return false
     */
    bool Insert (int64_t key, char* val, size_t hash);

    /**
     * @brief insert key-val to insert_pos, if old_pos has an old value, reset it
     *
     * @param key
     * @param val
     * @param insert_pos
     * @param old_pos
     * @param tag
     */
    void Insert (int64_t key, char* val, int insert_pos, int old_pos, uint8_t tag);

    bool Get (int64_t key, char*& val);
    bool Get (int64_t key, char*& val, size_t hash);

    bool Delete (int64_t key);
    bool Delete (int64_t key, size_t hash);

    void Print ();
    std::string ToString (void);
    std::string ToStringValid ();

    /**
     * @brief Sorted Iterator after Sort() is called.
     *
     */
    class IteratorSorted {
    private:
        SortedBufNode_t* node_;
        int i_;
        int i_end_;

    public:
        IteratorSorted (SortedBufNode_t* node)
            : node_ (node), i_ (0), i_end_ (node->ValidCount ()) {}

        inline bool Valid (void) { return i_ < i_end_; }
        kv_t& operator* () const { return node_->kvs_[node_->seqs_[i_]]; }
        kv_t* operator-> () const { return &node_->kvs_[node_->seqs_[i_]]; }

        // Prefix ++ overload
        inline IteratorSorted& operator++ () {
            i_++;
            return *this;
        }

        // Postfix ++ overload
        inline IteratorSorted operator++ (int) {
            auto tmp = *this;
            i_++;
            return tmp;
        }
    };
    IteratorSorted sBegin () { return IteratorSorted (this); }

    /**
     * @brief Normal iterator. Iterate valid records (not been deleted) from lowest slot to highest
     * slot.
     *
     */
    class Iterator {
    private:
        SortedBufNode_t* node_;
        BitSet bitset_;

    public:
        Iterator (SortedBufNode_t* node) : node_ (node), bitset_ (node->ValidBitSet ()) {}

        inline bool Valid (void) { return bitset_.operator bool (); }
        kv_t& operator* () const { return node_->kvs_[*bitset_]; }
        kv_t* operator-> () const { return &node_->kvs_[*bitset_]; }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            ++bitset_;
            return *this;
        }

        // Postfix ++ overload
        inline Iterator operator++ (int) {
            auto tmp = *this;
            ++bitset_;
            return tmp;
        }
    };
    Iterator Begin () { return Iterator (this); }

    friend class Iterator;
    friend class IteratorSorted;
};

static_assert (sizeof (SortedBufNode_t) == 264, "SortBufNode is not 264 byte");

}  // namespace spoton

#endif

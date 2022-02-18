#ifndef _SPOTON_HASHBUF_H_
#define _SPOTON_HASHBUF_H_

#include <cassert>
#include <vector>

#include "spoton_bufnode.h"

namespace spoton {

/**
 * @brief Write buffer for Hash Table
 *
 */

class HashBuffer {
    // static_assert (__builtin_popcount (NUM) == 1, "NUM should be power of 2");
    // static constexpr size_t kNodeNumMask = NUM - 1;
    // static constexpr size_t kNodeNum = NUM;
    // static constexpr size_t kProbeLen = NUM / 2 - 1;

public:
    std::vector<SortedBufNode_t> nodes_;
    SpinLock lock_;
    size_t local_depth;

    size_t kNodeNum;
    size_t kProbeLen;

public:
    HashBuffer () { local_depth = 0; }
    HashBuffer (size_t d, int num) : nodes_ (num) {
        local_depth = d;

        kNodeNum = num;
        kProbeLen = num / 2 - 1;
    }

    bool Put (int64_t key, char* val);
    bool Get (int64_t key, char*& val);
    bool Delete (int64_t key);

    inline void Lock () { lock_.lock (); }
    inline void Unlock () { lock_.unlock (); }

    void Reset ();

    class Iterator {
        HashBuffer* wb_;
        size_t cur_;
        SortedBufNode_t::Iterator iter;

    public:
        Iterator (HashBuffer* wb) : wb_ (wb), cur_ (0), iter (wb_->nodes_[0].Begin ()) {
            toNextValidIter ();
        }

        inline bool Valid (void) { return iter.Valid () || cur_ < wb_->kNodeNum; }
        kv_t& operator* () const { return *iter; }
        kv_t* operator-> () const { return &(*iter); }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            iter++;
            if (!iter.Valid ()) {
                toNextValidIter ();
            }
            return *this;
        }

        void toNextValidIter (void) {
            while (!iter.Valid () && cur_ < wb_->kNodeNum) {
                cur_++;
                if (cur_ == wb_->kNodeNum) {
                    break;
                }
                iter = wb_->nodes_[cur_].Begin ();
            }
        }
    };

    Iterator Begin () { return Iterator (this); }
};

bool HashBuffer::Put (int64_t key, char* val) {
    size_t hash = Hasher::hash_int (key);
    uint8_t tag = hash & 0xFF;

    int old_i = -1;
    int bucket_to_insert = -1;
    int slot_to_insert = -1;

    for (size_t p = 0; p < kProbeLen; ++p) {
        int idx = (hash + p) % kNodeNum;
        SortedBufNode_t& bucket = nodes_[idx];

        // step 1. check if this bucket already has this key
        for (int i : bucket.MatchBitSet (tag)) {
            kv_t& kv = bucket.kvs_[i];
            if (kv.key == key) {
                // this is an update request
                old_i = i;
                bucket_to_insert = idx;
                slot_to_insert = *bucket.BackupBitSet ();
                goto put_probe_end;
            }
        }

        // step 2. choose a deleted slot
        auto erase_bitset = bucket.EraseBitSet ();
        auto backup_bitset = bucket.BackupBitSet ();
        if (erase_bitset.validCount () != 0) {
            bucket_to_insert = idx;
            slot_to_insert = *erase_bitset;
        }

        // step 3. reach to the search path end
        if (!bucket.Full ()) {
            if (bucket_to_insert == -1) {
                bucket_to_insert = idx;
                slot_to_insert = *backup_bitset;
            }
            break;
        }
    }

put_probe_end:

    if (bucket_to_insert == -1) {
        // no avaliable slot
        return false;
    }

    nodes_[bucket_to_insert].Insert (key, val, slot_to_insert, old_i, tag);
    return true;
}

bool HashBuffer::Get (int64_t key, char*& val) {
    size_t hash = Hasher::hash_int (key);
    uint8_t tag = hash & 0xFF;
    for (size_t p = 0; p < kProbeLen; ++p) {
        int idx = (hash + p) % kNodeNum;
        SortedBufNode_t& bucket = nodes_[idx];

        for (int i : bucket.MatchBitSet (tag)) {
            kv_t& kv = bucket.kvs_[i];
            if (kv.key == key) {
                val = kv.val;
                return true;
            }
        }

        if (!bucket.Full ()) {
            // reach search end
            return false;
        }
    }
    return false;
}

bool HashBuffer::Delete (int64_t key) {
    size_t hash = Hasher::hash_int (key);
    size_t tag = hash & 0xFF;
    for (size_t p = 0; p < kProbeLen; ++p) {
        int idx = (hash + p) % kNodeNum;
        SortedBufNode_t& bucket = nodes_[idx];

        for (int i : bucket.MatchBitSet (tag)) {
            kv_t& kv = bucket.kvs_[i];
            if (kv.key == key) {
                // find the key, set the deleted map
                bucket.meta_.deleted_ = bucket.meta_.deleted_ | (1 << i);
                return true;
            }
        }

        if (!bucket.Full ()) {
            // reach search end
            return false;
        }
    }
    return false;
}

void HashBuffer::Reset () {
    for (size_t i = 0; i < kNodeNum; i++) {
        nodes_[i].Reset ();
    }
}

}  // namespace spoton
#endif
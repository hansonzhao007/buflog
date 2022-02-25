#pragma once

#include <immintrin.h>
#include <inttypes.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <string>

#include "spoton_util.h"

namespace spoton {

struct kv_t {
    size_t key;
    size_t val;
};

template <size_t kNumCacheLinePerBucket = 1>
class HashTable {
public:
    constexpr static size_t KeyPerBucket = kNumCacheLinePerBucket * 4;
    struct Bucket {
        kv_t slots[KeyPerBucket];

        bool insert (size_t key, size_t val) {
            for (int i = 0; i < KeyPerBucket; i++) {
                if (slots[i].key == 0) {
                    slots[i].key = key;
                    slots[i].val = val;
                    return true;
                }
            }
            return false;
        }
    };

    static_assert (sizeof (Bucket) % 64 == 0, "bucket size not number of 64 byte");

public:
    size_t bucket_count_;
    Bucket* buckets_;

    HashTable (size_t bc, char* addr) {
        bucket_count_ = bc;
        buckets_ = reinterpret_cast<Bucket*> (addr);  // assign hash table address to me
    };

    size_t getCapacity () { return bucket_count_ * KeyPerBucket; }

    // random insert
    bool insert (size_t key, size_t val) {
        size_t hash = Hasher::hash_int (key);
        size_t idx = hash % bucket_count_;

        int probe_len = 4;
        while (probe_len--) {
            bool res = buckets_[idx].insert (key, val);
            if (res) return true;
            idx = (idx + 1) % bucket_count_;
        }
        return false;
    }

    // insert to given bucket
    bool insert (size_t key, size_t val, size_t bi) {
        size_t idx = bi;
        return buckets_[idx].insert (key, val);
    }
};

}  // namespace spoton

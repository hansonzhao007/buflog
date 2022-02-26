#pragma once

#include <immintrin.h>
#include <inttypes.h>
#include <libpmem.h>

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
                    kv_t tmp{key, val};
                    pmem_memcpy (&slots[i], &tmp, sizeof (kv_t), PMEM_F_MEM_NONTEMPORAL);
                    return true;
                }
            }
            return false;
        }

        bool insert (size_t key, size_t val, size_t si) {
            kv_t tmp{key, val};
            pmem_memcpy (&slots[si], &tmp, sizeof (kv_t), PMEM_F_MEM_NONTEMPORAL);
            return true;
        }
    };

    static_assert (sizeof (Bucket) % 64 == 0, "bucket size not number of 64 byte");

public:
    size_t bucket_count_;
    Bucket* buckets_;

    HashTable (size_t bc, char* addr) {
        bucket_count_ = bc;
        buckets_ = reinterpret_cast<Bucket*> (addr);  // assign hash table address to me
        void* b_addr = buckets_;
        if (((size_t)b_addr & 0xFF) != 0) {
            // not xpline aligned
            assert (false);
        }
    };

    size_t getCapacity () { return bucket_count_ * KeyPerBucket; }

    Bucket* getBucket (size_t bi) { return &buckets_[bi]; }

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
    bool insert (Bucket& bucket, size_t bi) {
        size_t idx = bi;
        void* b_addr = &buckets_[idx];
        if (((size_t)b_addr & 0x3F) != 0) {
            // not cacheline aligned
            assert (false);
            printf ("not cachline aligned.\n");
            exit (1);
        }
        pmem_memcpy (&buckets_[idx], &bucket, sizeof (Bucket), PMEM_F_MEM_NONTEMPORAL);
        return true;
    }

    bool insert (size_t key, size_t val, size_t bi) {
        size_t idx = bi;
        return buckets_[idx].insert (key, val);
    }
};

}  // namespace spoton

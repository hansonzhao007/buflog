#include "spoton_bufnode.h"

#include <immintrin.h>
#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace spoton {

SortedBufNode_t::SortedBufNode_t () {
    memset (this, 0, 64);
    highkey_ = -1;
    parentkey_ = -1;
}

void SortedBufNode_t ::MaskLastN (int n) {
    uint16_t mask = 0;
    int count = ValidCount ();
    for (int i = count - n; i < count; ++i) {
        mask |= (1 << seqs_[i]);
    }
    meta_.valid_ &= ((~mask) & kBitMapMask);
    asm volatile("" : : : "memory"); /* Compiler fence. */
}

struct SortedBufNode_t::SortByKey {
    SortByKey (SortedBufNode_t* node) : node_ (node) {}
    bool operator() (const int& a, const int& b) { return node_->kvs_[a].key < node_->kvs_[b].key; }
    SortedBufNode_t* node_;
};

void SortedBufNode_t::Sort (void) {
    // step 1: obtain the valid pos
    std::vector<int> valid_pos;
    for (int i : ValidBitSet ()) {
        valid_pos.push_back (i);
    }

    // step 2: sort the valid_pos according to the related key
    std::sort (valid_pos.begin (), valid_pos.end (), SortByKey (this));

    // step 3: set the seqs_
    int si = 0;
    for (int i : valid_pos) {
        seqs_[si++] = i;
    }
}

bool SortedBufNode_t::Insert (int64_t key, char* val) {
    size_t hash = Hasher::hash_int (key);
    return Insert (key, val, hash);
}

bool SortedBufNode_t::Insert (int64_t key, char* val, size_t hash) {
    size_t tag = hash & 0xFF;

    int8_t old_pos = -1;
    int8_t insert_pos = -1;

    // step 1. check if this node already has this key
    for (int i : MatchBitSet (tag)) {
        kv_t kv = kvs_[i];
        if (kv.key == key) {
            // this is an update request
            old_pos = i;
            insert_pos = *BackupBitSet ();
            break;
        }
    }

    // step 2. choose a deleted slot
    if (insert_pos == -1) {
        // not an update request
        auto erase_bitset = EraseBitSet ();
        if (erase_bitset.validCount () != 0) {
            insert_pos = *erase_bitset;
        }
    }

    // step 3. choose a slot that has not been marked as valid before
    if (insert_pos == -1) {
        if (!Full ()) {
            insert_pos = *BackupBitSet ();
        }
    }

    // step 4. insert to insert_pos
    if (insert_pos >= 0) {
        Insert (key, val, insert_pos, old_pos, tag);
        return true;
    }

    // cannot insert
    return false;
}

void SortedBufNode_t::Insert (int64_t key, char* val, int insert_pos, int old_pos, uint8_t tag) {
    // set the key and value
    kvs_[insert_pos].key = key;
    kvs_[insert_pos].val = val;
    tags_[insert_pos] = tag;

    // atomicly set the meta
    NodeMeta new_meta = meta_;
    new_meta.valid_ = meta_.valid_ | (1 << insert_pos);
    if (old_pos != -1) {
        // reset old pos if this is an update request.
        new_meta.valid_ ^= (1 << old_pos);
    }
    new_meta.deleted_ = meta_.deleted_ & (~(1 << insert_pos));

    asm volatile("" : : : "memory"); /* Compiler fence. */
    meta_.data_ = new_meta.data_;
}

bool SortedBufNode_t::Get (int64_t key, char*& val) {
    size_t hash = Hasher::hash_int (key);
    return Get (key, val, hash);
}

bool SortedBufNode_t::Get (int64_t key, char*& val, size_t hash) {
    size_t tag = hash & 0xFF;

    for (int i : MatchBitSet (tag)) {
        kv_t kv = kvs_[i];
        if (kv.key == key) {
            // find the key
            val = kv.val;
            return true;
        }
    }
    return false;
}

bool SortedBufNode_t::Delete (int64_t key) {
    size_t hash = Hasher::hash_int (key);
    return Delete (key, hash);
}

bool SortedBufNode_t::Delete (int64_t key, size_t hash) {
    size_t tag = hash & 0xFF;

    for (int i : MatchBitSet (tag)) {
        kv_t kv = kvs_[i];
        if (kv.key == key) {
            // find the key, set the deleted map
            meta_.deleted_ = meta_.deleted_ | (1 << i);
            return true;
        }
    }
    return false;
}

std::string SortedBufNode_t::ToString (void) {
    char buf[1024];
    std::string str_valid = print_binary (meta_.valid_);
    std::string str_deleted = print_binary (meta_.deleted_);
    std::string str_tags;
    std::string str_seqs;
    std::string str_kvs;
    for (int i = 0; i < 14; i++) {
        str_tags += std::to_string (tags_[i]) + " ";
        str_seqs += std::to_string (seqs_[i]) + " ";
        str_kvs += std::to_string (kvs_[i].key) + " ";
    }
    sprintf (buf, "valid: 0b%s, deleted: 0b%s, tags: %s, seqs: %s, k: %s", str_valid.c_str (),
             str_deleted.c_str (), str_tags.c_str (), str_seqs.c_str (), str_kvs.c_str ());
    return buf;
}

void SortedBufNode_t::Print () {
    auto iter = Begin ();
    printf ("High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
    while (iter.Valid ()) {
        printf ("%ld, ", iter->key);
        iter++;
    }
    printf ("\n");
}

std::string SortedBufNode_t::ToStringValid () {
    char buf[1024];
    auto iter = Begin ();
    sprintf (buf, "High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
    while (iter.Valid ()) {
        sprintf (buf + strlen (buf), "%ld, ", iter->key);
        iter++;
    }
    return buf;
}

}  // namespace spoton
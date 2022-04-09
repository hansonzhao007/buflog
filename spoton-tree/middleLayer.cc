#include "middleLayer.h"

#include <emmintrin.h>
#include <immintrin.h>

#include <climits>
namespace spoton {

void BloomFilterFix64::set (key_t key) {
    char buffer[8] = {0};
    *reinterpret_cast<uint64_t*> (buffer) = key;
    set (buffer, 8);
}

void BloomFilterFix64::set (const void* addr, size_t len) {
    auto hashes = XXH3_128bits (addr, len);
    size_t hash1 = hashes.high64;
    size_t hash2 = hashes.low64;

    size_t pos[4];
    pos[0] = (hash1 >> 32) & 511;
    pos[1] = hash1 & 511;
    pos[2] = (hash2 >> 32) & 511;
    pos[3] = hash2 & 511;

    for (int i = 0; i < 4; i++) {
        setBitPos (pos[i]);
    }
}

bool BloomFilterFix64::couldExist (key_t key) {
    char buffer[8] = {0};
    *reinterpret_cast<uint64_t*> (buffer) = key;
    return couldExist (buffer, 8);
}

bool BloomFilterFix64::couldExist (const void* addr, size_t len) {
    auto hashes = XXH3_128bits (addr, len);
    size_t hash1 = hashes.high64;
    size_t hash2 = hashes.low64;

    size_t pos[4];
    pos[0] = (hash1 >> 32) & 511;
    pos[1] = hash1 & 511;
    pos[2] = (hash2 >> 32) & 511;
    pos[3] = hash2 & 511;

    for (int i = 0; i < 4; i++) {
        if (!isBitSet (pos[i])) return false;
    }
    return true;
}

NodeBitSet NodeBuffer::MatchBitSet (uint8_t tag) {
    // passing the tag to vector
    auto tag_vector = _mm_set1_epi8 (tag);
    auto tag_compare = _mm_loadu_si128 (reinterpret_cast<const __m128i*> (this->tags));
    auto bitmask = _mm_cmpeq_epi8_mask (tag_vector, tag_compare);
    return NodeBitSet (bitmask & this->valid_bitmap);
}

bool NodeBuffer::insert (key_t _key, val_t _val) {
    // DEBUG ("0x%lx  buffer insert %lu, %lu. count %lu", this, _key, _val, Count ());
    // obtain key hash
    key_t key = _key;
    val_t val = _val;
    uint64_t hash = XXH3_64bits (&key, 8);
    uint8_t tag = hash & 0xFF;
    for (int i : MatchBitSet (tag)) {
        auto& record = this->slots[i];
        if (record.key == key) {
            // update
            record.val = val;
            return true;
        }
    }
    // if reach here, we need to insert to an empty slot
    if (!Full ()) {
        for (int i : EmptyBitSet ()) {
            auto& record = this->slots[i];
            // 1. set slot
            record.key = key;
            record.val = val;
            // 2. set tag
            this->tags[i] = tag;
            // 3. validate bitmap
            SetValid (i);
            return true;
        }
    }

    return false;
}

val_t NodeBuffer::lookup (key_t _key) {
    // obtain key hash
    key_t key = _key;
    uint64_t hash = XXH3_64bits (&key, 8);
    uint8_t tag = hash & 0xFF;

    for (int i : MatchBitSet (tag)) {
        auto& record = this->slots[i];
        if (record.key == key) {
            return record.val;
        }
    }
    // fail to find
    return 0;
}

bool NodeBuffer::remove (key_t _key) {
    // obtain key hash
    key_t key = _key;
    uint64_t hash = XXH3_64bits (&key, 8);
    uint8_t tag = hash & 0xFF;

    for (int i : MatchBitSet (tag)) {
        auto& record = this->slots[i];
        if (record.key == key) {
            // find key
            SetErase (i);
            return true;
        }
    }
    return false;
}

MiddleLayer::MiddleLayer () {
    // do not use MLNode with write buffer for head and dummy tail
    head = new (false) MLNode (false);
    dummyTail = new (false) MLNode (false);
    head->EnableBloomFilter ();
    dummyTail->EnableBloomFilter ();

    head->lkey = kSPTreeMinKey;
    head->hkey = UINT64_MAX;

    head->prev = nullptr;
    head->next = dummyTail;

    dummyTail->lkey = UINT64_MAX;
    dummyTail->hkey = UINT64_MAX;
    dummyTail->prev = head;
    dummyTail->next = nullptr;
}

void MiddleLayer::SetBloomFilter (key_t key, MLNode* mnode) {
    // set bloom filter for key
    mnode->SetBloomFilter (key);
}

bool MiddleLayer::CouldExist (key_t key, MLNode* mnode) { return mnode->CouldExist (key); }

MLNode* MiddleLayer::FindTargetMLNode (key_t key, MLNode* mnode) {
    assert (mnode);
    while (mnode->lkey > key) {
        mnode = mnode->prev;
        if (mnode == nullptr) {
            ERROR ("mnode is null.");
            printf ("mnode is null.\n");
            exit (1);
        }
    }

    // now key >= mnode->lkey
    while (key >= mnode->hkey) {
        mnode = mnode->next;
        if (mnode == nullptr) {
            ERROR ("mnode is null 2.");
            printf ("mnode is null 2.\n");
            exit (1);
        }
    }

    // now we have mnode, key is in [mnode->lkey, mnode->hkey)
    return mnode;
}

std::string MiddleLayer::ToStats () {
    MLNode* cur = head;
    size_t total_size = 0;
    size_t total_count = 0;
    while (cur) {
        // DEBUG ("mnode %lu, %lu", cur->lkey, cur->hkey);
        total_size += cur->Size ();
        cur = cur->next;
        total_count++;
    }
    char buffer[128];
    sprintf (buffer, "Middle Layer size: %f MB. count: %lu", total_size / 1024.0 / 1024.0,
             total_count);
    return buffer;
}

};  // namespace spoton
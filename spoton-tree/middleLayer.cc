#include "middleLayer.h"

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

MiddleLayer::MiddleLayer () {
    head = new MLNode ();
    auto dummyTail = new MLNode ();

    head->lkey = 0;
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
    while (cur) {
        total_size += cur->Size ();
        cur = cur->next;
    }
    char buffer[128];
    sprintf (buffer, "Middle Layer size: %f MB", total_size / 1024.0 / 1024.0);
    return buffer;
}

};  // namespace spoton
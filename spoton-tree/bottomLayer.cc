
#include <immintrin.h>

#include <algorithm>
#include <climits>
#include <unordered_set>

#include "logger.h"
#include "xxhash.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

// spoton layer
#include "bottomLayer.h"
#include "middleLayer.h"

namespace spoton {

LeafNode64::LeafNode64 () {
    if (kIsLeafNodeDram) {
        // dram mode
        next_dram = nullptr;
        prev_dram = nullptr;
    } else {
        next_pmem = nullptr;
        prev_pmem = nullptr;
    }
}

void LeafNode64::SetPrev (LeafNode64* ptr) {
    if (kIsLeafNodeDram) {
        prev_dram = ptr;
    } else {
        prev_pmem = ptr;
    }
}

void LeafNode64::SetNext (LeafNode64* ptr) {
    if (kIsLeafNodeDram) {
        next_dram = ptr;
    } else {
        next_pmem = ptr;
    }
}

LeafNode64* LeafNode64::GetPrev () {
    if (kIsLeafNodeDram) {
        return prev_dram;
    }
    return prev_pmem;
}

LeafNode64* LeafNode64::GetNext () {
    if (kIsLeafNodeDram) {
        return next_dram;
    }
    return next_pmem;
}

bool LeafNode64::Insert (key_t _key, val_t _val) {
    DEBUG ("Insert %lu, %lu", _key, _val);
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

bool LeafNode64::Remove (key_t _key) {
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

bool LeafNode64::Lookup (key_t _key, val_t& val) {
    // obtain key hash
    key_t key = _key;
    uint64_t hash = XXH3_64bits (&key, 8);
    uint8_t tag = hash & 0xFF;

retry:
    bool needRestart = false;
    for (int i : MatchBitSet (tag)) {
        // retry if current node is under writing
        auto v = lock.readLockOrRestart (needRestart);
        if (needRestart) goto retry;

        auto& record = this->slots[i];
        if (record.key == key) {
            val = record.val;
            // check version after read
            lock.checkOrRestart (v, needRestart);
            if (needRestart) goto retry;
            return true;
        }
    }
    return false;
}

struct LeafNode64::SortByKey {
    SortByKey (LeafNode64* node) : node_ (node) {}
    bool operator() (const int& a, const int& b) {
        // sort by slot.key
        return node_->slots[a].key < node_->slots[b].key;
    }
    LeafNode64* node_;
};

void LeafNode64::Sort () {
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
        this->seqs[si++] = i;
    }
}

std::tuple<LeafNode64*, key_t> LeafNode64::Split (
    std::vector<std::pair<key_t, val_t>>& toMergedRecords, void* newNodeAddr,
    BloomFilterFix64& bleft, BloomFilterFix64& bright) {
    // should lock both me and my next node
    DEBUG ("Split leaf count %lu. valid: %lx", Count (), valid_bitmap);
    LeafNodeSlot copied_slots[64];
    // assert (Full ());

    // copy to dram to sort
    std::copy (std::begin (this->slots), std::end (this->slots), std::begin (copied_slots));
    // sort copied_slots by accending order
    std::sort (std::begin (copied_slots), std::end (copied_slots),
               [] (auto& a, auto& b) { return a.key < b.key; });

    key_t median = copied_slots[32].key;

    // record the valid bitmap position, which need to be cleared later
    NodeBitSet shiftedBitSet;

    // 1. create new LeafNode64 for right half keys
    LeafNode64* newNode = new (newNodeAddr) LeafNode64 ();

    // 2. copy the righ half to newNode. [median, ..] -> newNode
    for (int i = 0; i < 64; i++) {
        auto slot = this->slots[i];
        if (slot.key >= median && isValid (i)) {
            SetErase (i);
            DEBUG ("right shift insert %d: %lu, %lu", i, slot.key, slot.val);
            newNode->Insert (slot.key, slot.val);
        }
    }

    // 3. set newNode info
    newNode->lkey = median;
    newNode->hkey = this->hkey;
    newNode->SetPrev (this);
    newNode->SetNext (this->GetNext ());

    // 4. link me
    this->GetNext ()->SetPrev (newNode);
    this->SetNext (newNode);

    // store fence here
    std::atomic_thread_fence (std::memory_order_release);

    // 5. reset hkey and valid bitmap of me (remove right half from me)
    this->hkey = median;

    // 6. merge the toMergedRecords and set bloomfilter
    bleft.reset ();
    bright.reset ();
    for (auto [key, val] : toMergedRecords) {
        DEBUG ("merge %lu, %lu", key, val);
        if (key < median) {
            // to left half
            DEBUG ("merge to leaf %lu, %lu", key, val);
            this->Insert (key, val);
            bleft.set (key);
        } else {
            // to right half
            DEBUG ("merge to right %lu, %lu", key, val);
            newNode->Insert (key, val);
            bright.set (key);
        }
    }
    for (int i = 0; i < 32; i++) {
        bleft.set (copied_slots[i].key);
    }
    for (int i = 32; i < 64; i++) {
        bright.set (copied_slots[i].key);
    }

    return {newNode, median};
};

NodeBitSet LeafNode64::MatchBitSet (uint8_t tag) {
    // passing the tag to vector
    auto tag_vector = _mm512_set1_epi8 (tag);
    auto tag_compare = _mm512_loadu_si512 (reinterpret_cast<const __m512i*> (this->tags));
    auto bitmask = _mm512_cmpeq_epi8_mask (tag_vector, tag_compare);
    return NodeBitSet (bitmask & this->valid_bitmap);
}

void* BottomLayer::Malloc (size_t size) {
    if (isDram) return malloc (size);
    return RP_malloc (size);
}

void BottomLayer::Initialize (SPTreePmemRoot* root) {
    void* tmp = Malloc (sizeof (LeafNode64));
    this->head = new (tmp) LeafNode64 ();

    tmp = Malloc (sizeof (LeafNode64));
    LeafNode64* dummyTail = new (tmp) LeafNode64 ();

    head->lkey = kSPTreeMinKey;
    head->hkey = UINT64_MAX;

    head->SetPrev (nullptr);
    head->SetNext (dummyTail);

    dummyTail->lkey = UINT64_MAX;
    dummyTail->hkey = UINT64_MAX;

    dummyTail->SetPrev (head);
    dummyTail->SetNext (nullptr);

    if (root) {
        if (isDram) {
            ERROR ("passing pmem root meta in dram mode");
            exit (1);
        }
        root->bottomLayerLeafNode64_head = this->head;
        root->bottomLayerLeafNode64_dummyTail = dummyTail;
    }
    INFO ("Initial first leafnode 0x%lx, lkey: %lu, hkey: %lu", (uint64_t)this->head, head->lkey,
          head->hkey);
}

void BottomLayer::Recover (SPTreePmemRoot* root) { head = root->bottomLayerLeafNode64_head; }

bool BottomLayer::Insert (key_t key, val_t val, LeafNode64* bnode) {
    DEBUG ("Bottom insert %lu, %lu", key, val);
    return bnode->Insert (key, val);
}

bool BottomLayer::Lookup (key_t key, val_t& val, LeafNode64* bnode) {
    return bnode->Lookup (key, val);
}

bool BottomLayer::Remove (key_t key, LeafNode64* bnode) { return bnode->Remove (key); }

std::string BottomLayer::ToStats () {
    LeafNode64* cur = head;
    size_t total_size = 0;
    size_t total_count = 0;
    while (cur) {
        total_size += sizeof (LeafNode64);
        cur = cur->GetNext ();
        total_count++;
    }
    char buffer[128];
    sprintf (buffer, "Bottome Layer Size: %f MB. count: %lu", total_size / 1024.0 / 1024.0,
             total_count);
    return buffer;
}

}  // namespace spoton

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

bool LeafNode64::InsertiWithoutSetValidBitmap (key_t _key, val_t _val, int si) {
    if (isValid (si)) return false;
    // obtain key hash
    key_t key = _key;
    val_t val = _val;
    uint64_t hash = XXH3_64bits (&key, 8);
    uint8_t tag = hash & 0xFF;
    auto& slot = this->slots[si];
    slot.key = key;
    slot.val = val;
    this->tags[si] = tag;
    return true;
}

std::tuple<bool, int> LeafNode64::Insert (key_t _key, val_t _val) {
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
            SPTreeMemFlush (&this->slots[i]);
            SPTreeMemFlushFence ();
            return {true, i};
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
            SPTreeMemFlush (&this->slots[i]);
            SPTreeMemFlush (&this->tags[i]);
            SPTreeMemFlushFence ();
            // 3. validate bitmap
            SetValid (i);
            return {true, i};
        }
    }

    return {false, 0};
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
    BloomFilterFix64& bloomfilterLeft, BloomFilterFix64& bloomfilterRight) {
    // should lock both me and my next node
    bloomfilterLeft.reset ();
    bloomfilterRight.reset ();

    // 1. collect the valid kv and sort.
    std::vector<std::tuple<key_t, val_t, int>> toSplitedKVs;
    for (int i : ValidBitSet ()) {
        toSplitedKVs.push_back ({this->slots[i].key, this->slots[i].val, i});
    }
    // sort copied_slots by accending order
    std::sort (std::begin (toSplitedKVs), std::end (toSplitedKVs),
               [] (auto& a, auto& b) { return std::get<0> (a) < std::get<0> (b); });
    size_t count = Count ();
    assert (count == toSplitedKVs.size ());
    key_t median = std::get<0> (toSplitedKVs[count / 2]);

    // 2. create new LeafNode64 for right half keys and shift. [median, ..] -> newNode
    LeafNode64* newNode = new (newNodeAddr) LeafNode64 ();
    NodeBitSet rightEmptySet = newNode->EmptyBitSet ();
    NodeBitSet rightValidSet;
    NodeBitSet leftValidSet;
    for (size_t i = 0; i < count; i++) {
        auto [key, val, si] = toSplitedKVs[i];
        if (key < median) {
            bloomfilterLeft.set (key);
        } else {
            this->SetErase (si);  // clear the shifted kv from left
            int right_si = *rightEmptySet;
            rightValidSet.set (right_si);
            newNode->InsertiWithoutSetValidBitmap (key, val, right_si);
            ++rightEmptySet;
            bloomfilterRight.set (key);
        }
    }
    NodeBitSet leftEmptySet = this->EmptyBitSet ();

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

    // 6. merge the toMergedRecords
    for (auto [key, val] : toMergedRecords) {
        if (key < median) {
            // to left half
            int left_si = *leftEmptySet;
            leftValidSet.set (left_si);
            this->InsertiWithoutSetValidBitmap (key, val, left_si);
            ++leftEmptySet;
            bloomfilterLeft.set (key);
        } else {
            // to right half
            int right_si = *rightEmptySet;
            rightValidSet.set (right_si);
            newNode->InsertiWithoutSetValidBitmap (key, val, right_si);
            ++rightEmptySet;
            bloomfilterRight.set (key);
        }
    }
    SPTreeCLFlush ((char*)this, sizeof (LeafNode64));
    SPTreeCLFlush ((char*)newNode, sizeof (LeafNode64));
    // 7. batch set the valid_bitmap of left and right nodes
    this->valid_bitmap |= leftValidSet.bit ();
    newNode->valid_bitmap |= rightValidSet.bit ();
    SPTreeMemFlush (&this->valid_bitmap);
    SPTreeMemFlush (&newNode->valid_bitmap);
    SPTreeMemFlushFence ();
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
    return std::get<0> (bnode->Insert (key, val));
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
#include "bottomLayer.h"

#include <immintrin.h>

#include <algorithm>
#include <climits>

#include "xxhash.h"
namespace spoton {

bool LeafNode32::Insert (key_t key, val_t val) {
    // obtain key hash
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

bool LeafNode32::Remove (key_t key) {
    // obtain key hash
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

bool LeafNode32::Lookup (key_t key, val_t& val) {
    // obtain key hash
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

struct LeafNode32::SortByKey {
    SortByKey (LeafNode32* node) : node_ (node) {}
    bool operator() (const int& a, const int& b) {
        // sort by slot.key
        return node_->slots[a].key < node_->slots[b].key;
    }
    LeafNode32* node_;
};

void LeafNode32::Sort () {
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

BitSet LeafNode32::MatchBitSet (uint8_t tag) {
    // passing the tag to vector
    auto tag_vector = _mm256_set1_epi8 (tag);
    auto tag_compare = _mm256_loadu_si256 (reinterpret_cast<const __m256i*> (this->tags));
    auto bitmask = _mm256_cmpeq_epi8_mask (tag_vector, tag_compare);
    return BitSet (bitmask & this->valid_bitmap);
}

// -------------- LeafNode64 ----------------

bool LeafNode64::Insert (key_t key, val_t val) {
    // obtain key hash
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

bool LeafNode64::Remove (key_t key) {
    // obtain key hash
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

bool LeafNode64::Lookup (key_t key, val_t& val) {
    // obtain key hash
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

std::tuple<LeafNode64*, key_t> LeafNode64::Split (key_t key, val_t val) {
    // should lock both me and my next node
    assert (key >= this->lkey);

    LeafNodeSlot copied_slots[64];
    assert (Full ());

    // copy to dram to sort
    std::copy (std::begin (this->slots), std::end (this->slots), std::begin (copied_slots));
    // sort copied_slots by accending order
    std::sort (std::begin (copied_slots), std::end (copied_slots),
               [] (auto& a, auto& b) { return a.key < b.key; });

    key_t median = copied_slots[32].key;

    // record the valid bitmap position, which need to be cleared later
    BitSet shiftedBitSet;

    // 1. create new LeafNode64 for right half keys
    LeafNode64* newNode = new LeafNode64 ();

    // 2. copy the righ half to newNode. [median, ..] -> newNode
    for (int i = 0; i < 64; i++) {
        if (this->slots[i].key >= median) {
            shiftedBitSet.set (i);
            newNode->Insert (this->slots[i].key, this->slots[i].val);
        }
    }
    if (key >= median) {
        // to right half
        newNode->Insert (key, val);
    }

    // 3. set newNode info
    newNode->lkey = median;
    newNode->hkey = this->hkey;
    newNode->prev = this;
    newNode->next = this->next;

    // 4. link me
    this->next->prev = newNode;
    this->next = newNode;

    // store fence here
    std::atomic_thread_fence (std::memory_order_release);

    // 5. reset hkey and valid bitmap of me
    this->hkey = median;
    this->valid_bitmap &= ~(shiftedBitSet.bit ());

    // 6. insert key,val if needed
    if (key < median) {
        this->Insert (key, val);
    }

    return {newNode, median};
};

BitSet LeafNode64::MatchBitSet (uint8_t tag) {
    // passing the tag to vector
    auto tag_vector = _mm512_set1_epi8 (tag);
    auto tag_compare = _mm512_loadu_si512 (reinterpret_cast<const __m512i*> (this->tags));
    auto bitmask = _mm512_cmpeq_epi8_mask (tag_vector, tag_compare);
    return BitSet (bitmask & this->valid_bitmap);
}

LeafNode64* BottomLayer::initialize () {
    this->head = new LeafNode64 ();
    LeafNode64* dummyTail = new LeafNode64 ();

    head->lkey = 0;
    head->hkey = ULLONG_MAX;

    head->prev = nullptr;
    head->next = dummyTail;

    dummyTail->lkey = ULLONG_MAX;
    dummyTail->hkey = ULLONG_MAX;

    dummyTail->prev = head;
    dummyTail->next = nullptr;

    return head;
}

bool BottomLayer::Insert (key_t key, val_t val, LeafNode64* bnode) {
    return bnode->Insert (key, val);
}

bool BottomLayer::Lookup (key_t key, val_t& val, LeafNode64* bnode) {
    return bnode->Lookup (key, val);
}

}  // namespace spoton
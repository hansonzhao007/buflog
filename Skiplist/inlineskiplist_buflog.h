//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be found
// in the LICENSE file. See the AUTHORS file for names of contributors.
//
// InlineSkipList is derived from SkipList (skiplist.h), but it optimizes
// the memory layout by requiring that the key storage be allocated through
// the skip list instance.  For the common case of SkipList<const char*,
// Cmp> this saves 1 pointer per skip list node and gives better cache
// locality, at the expense of wasted padding from using AllocateAligned
// instead of Allocate for the keys.  The unused padding will be from
// 0 to sizeof(void*)-1 bytes, and the space savings are sizeof(void*)
// bytes, so despite the padding the space used is always less than
// SkipList<const char*, ..>.
//
// Thread safety -------------
//
// Writes via Insert require external synchronization, most likely a mutex.
// InsertConcurrently can be safely called concurrently with reads and
// with other concurrent inserts.  Reads require a guarantee that the
// InlineSkipList will not be destroyed while the read is in progress.
// Apart from that, reads progress without any internal locking or
// synchronization.
//
// Invariants:
//
// (1) Allocated nodes are never deleted until the InlineSkipList is
// destroyed.  This is trivially guaranteed by the code since we never
// delete any skip list nodes.
//
// (2) The contents of a Node except for the next/prev pointers are
// immutable after the Node has been linked into the InlineSkipList.
// Only Insert() modifies the list, and it is careful to initialize a
// node and use release-stores to publish the nodes in one or more lists.
//
// ... prev vs. next pointer ordering ...
//

#ifndef SKIPLIST_BUFLOG_H_
#define SKIPLIST_BUFLOG_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <type_traits>
#include <vector>

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"
const size_t SKIPLIST_PMEM_SIZE = ((100LU << 30));

// buflog
#include "src/buflog.h"
#include "src/logger.h"

#define DRAM_CACHE

#if defined(__GNUC__) && __GNUC__ >= 4
#define likeyly(x) (__builtin_expect ((x), 1))
#define unlikely(x) (__builtin_expect ((x), 0))
#else
#define likeyly(x) (x)
#define unlikely(x) (x)
#endif

#define PREFETCH(addr, rw, locality) __builtin_prefetch (addr, rw, locality)

const int kBufNodeLevel = 2;

#define CACHE_LINE_SIZE ((64))
inline void sfence () { asm volatile("sfence" ::: "memory"); }
inline void clflush (char* data, int len) {
    volatile char* ptr = (char*)((unsigned long)data & ~(CACHE_LINE_SIZE - 1));
    for (; ptr < data + len; ptr += CACHE_LINE_SIZE) {
        _mm_clwb ((void*)ptr);
    }
}

namespace ROCKSDB_NAMESPACE {

// A very simple random number generator.  Not especially good at
// generating truly random bits, but good enough for our needs in this
// package.
class Random {
private:
    enum : uint32_t {
        M = 2147483647L  // 2^31-1
    };
    enum : uint64_t {
        A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
    };

    uint32_t seed_;

    static uint32_t GoodSeed (uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

public:
    // This is the largest value that can be returned from Next()
    enum : uint32_t { kMaxNext = M };

    explicit Random (uint32_t s) : seed_ (GoodSeed (s)) {}

    void Reset (uint32_t s) { seed_ = GoodSeed (s); }

    uint32_t Next () {
        // We are computing
        //       seed_ = (seed_ * A) % M,    where M = 2^31-1
        //
        // seed_ must not be zero or M, or else all subsequent computed values
        // will be zero or M respectively.  For all other values, seed_ will end
        // up cycling through every number in [1,M-1]
        uint64_t product = seed_ * A;

        // Compute (product % M) using the fact that ((x << 31) % M) == x.
        seed_ = static_cast<uint32_t> ((product >> 31) + (product & M));
        // The first reduction may overflow by 1 bit, so we may need to
        // repeat.  mod == M is not possible; using > allows the faster
        // sign-bit-based test.
        if (seed_ > M) {
            seed_ -= M;
        }
        return seed_;
    }

    // Returns a uniformly distributed value in the range [0..n-1]
    // REQUIRES: n > 0
    uint32_t Uniform (int n) { return Next () % n; }

    // Randomly returns true ~"1/n" of the time, and false otherwise.
    // REQUIRES: n > 0
    bool OneIn (int n) { return Uniform (n) == 0; }

    // "Optional" one-in-n, where 0 or negative always returns false
    // (may or may not consume a random value)
    bool OneInOpt (int n) { return n > 0 && OneIn (n); }

    // Returns random bool that is true for the given percentage of
    // calls on average. Zero or less is always false and 100 or more
    // is always true (may or may not consume a random value)
    bool PercentTrue (int percentage) { return static_cast<int> (Uniform (100)) < percentage; }

    // Skewed: pick "base" uniformly from range [0,max_log] and then
    // return "base" random bits.  The effect is to pick a number in the
    // range [0,2^max_log-1] with exponential bias towards smaller numbers.
    uint32_t Skewed (int max_log) { return Uniform (1 << Uniform (max_log + 1)); }

    // Returns a random string of length "len"
    std::string RandomString (int len);

    // Generates a random string of len bytes using human-readable characters
    std::string HumanReadableString (int len);

    // Returns a Random instance for use by the current thread without
    // additional locking
    static Random* GetTLSInstance ();
};

Random* Random::GetTLSInstance () {
    static Random* tls_instance;
    static std::aligned_storage<sizeof (Random)>::type tls_instance_bytes;

    auto rv = tls_instance;
    if (unlikely (rv == nullptr)) {
        size_t seed = std::hash<std::thread::id> () (std::this_thread::get_id ());
        rv = new (&tls_instance_bytes) Random ((uint32_t)seed);
        tls_instance = rv;
    }
    return rv;
}

// !buflog: node meta
struct NodeMeta {
    uint64_t is_dram_node : 2;
    uint64_t height : 14;
    uint64_t bufnode_ptr : 48;  // for dram node, this is to store the Pmem Node pointer, for pmem
                                // node, this is used to store buffer pointer
};

struct PmemMeta {
    uint64_t pmemnode_ptr;
};

static_assert (sizeof (NodeMeta) == 8, "Meta size is not 8 byte");

template <class Comparator>
class InlineSkipList {
private:
    struct Node;
    struct BufNode;
    struct Splice;

public:
    using DecodedKey = typename std::remove_reference<Comparator>::type::DecodedType;

    static const uint16_t kMaxPossibleHeight = 32;

    static void DestroySkipList (void) {
        remove ("/mnt/pmem/skiplist_sb");
        remove ("/mnt/pmem/skiplist_desc");
        remove ("/mnt/pmem/skiplist_basemd");
    }

    static InlineSkipList* CreateSkiplist (const Comparator cmp, int32_t max_height = 12,
                                           int32_t branching_factor = 4) {
        printf ("Create Pmem Skiplist\n");
        // Step1. Initialize pmem library
        bool res = RP_init ("skiplist", SKIPLIST_PMEM_SIZE);
        InlineSkipList<Comparator>* root = nullptr;
        if (res) {
            root = RP_get_root<InlineSkipList<Comparator>> (0);
            int recover_res = RP_recover ();
            if (recover_res == 1) {
                printf ("Ralloc Dirty open, recover\n");
            } else {
                printf ("Ralloc Clean restart\n");
            }
        } else {
            printf ("Ralloc Clean create\n");
        }

        void* buffer = RP_malloc (sizeof (InlineSkipList));
        RP_set_root (buffer, 0);

        root = new (buffer) InlineSkipList (cmp, max_height, branching_factor);

        return root;
    }

    // // recover only works when we change all the pointer to pptr<T>
    // static InlineSkipList* Recover(void) {
    //   // Step1. Open the pmem file
    //   bool res = RP_init("skiplist", SKIPLIST_PMEM_SIZE);

    //   if (res) {
    //       printf("Ralloc Prepare to recover\n");
    //       InlineSkipList* root = RP_get_root<InlineSkipList<Comparator>>(0);
    //       int recover_res = RP_recover();
    //       if (recover_res == 1) {
    //           printf("Ralloc Dirty open, recover\n");
    //       } else {
    //           printf("Ralloc Clean restart\n");
    //       }
    //   } else {
    //       printf("Ralloc Nothing to recover\n");
    //       // exit(1);
    //   }

    //   InlineSkipList* root = RP_get_root<InlineSkipList<Comparator>>(0);
    //   return root;
    // }

    // Create a new InlineSkipList object that will use "cmp" for comparing
    // keys, and will allocate memory using "*allocator".  Objects allocated
    // in the allocator must remain allocated for the lifetime of the
    // skiplist object.
    explicit InlineSkipList (Comparator cmp, int32_t max_height = 12, int32_t branching_factor = 4);
    // No copying allowed
    InlineSkipList (const InlineSkipList&) = delete;
    InlineSkipList& operator= (const InlineSkipList&) = delete;

    // Allocates a key and a skip-list node, returning a pointer to the key
    // portion of the node.  This method is thread-safe if the allocator
    // is thread-safe.
    char* AllocateKey (size_t key_size);

    // Allocate a splice using allocator.
    Splice* AllocateSplice ();

    // Allocate a splice on heap.
    Splice* AllocateSpliceOnHeap ();

    // Inserts a key allocated by AllocateKey, after the actual key value
    // has been filled in.
    //
    // REQUIRES: nothing that compares equal to key is currently in the list.
    // REQUIRES: no concurrent calls to any of inserts.
    bool Insert (const char* key);

    // Inserts a key allocated by AllocateKey with a hint of last insert
    // position in the skip-list. If hint points to nullptr, a new hint will be
    // populated, which can be used in subsequent calls.
    //
    // It can be used to optimize the workload where there are multiple groups
    // of keys, and each key is likely to insert to a location close to the last
    // inserted key in the same group. One example is sequential inserts.
    //
    // REQUIRES: nothing that compares equal to key is currently in the list.
    // REQUIRES: no concurrent calls to any of inserts.
    bool InsertWithHint (const char* key, void** hint);

    // Like InsertConcurrently, but with a hint
    //
    // REQUIRES: nothing that compares equal to key is currently in the list.
    // REQUIRES: no concurrent calls that use same hint
    bool InsertWithHintConcurrently (const char* key, void** hint);

    // Like Insert, but external synchronization is not required.
    bool InsertConcurrently (const char* key);

    // Inserts a node into the skip list.  key must have been allocated by
    // AllocateKey and then filled in by the caller.  If UseCAS is true,
    // then external synchronization is not required, otherwise this method
    // may not be called concurrently with any other insertions.
    //
    // Regardless of whether UseCAS is true, the splice must be owned
    // exclusively by the current thread.  If allow_partial_splice_fix is
    // true, then the cost of insertion is amortized O(log D), where D is
    // the distance from the splice to the inserted key (measured as the
    // number of intervening nodes).  Note that this bound is very good for
    // sequential insertions!  If allow_partial_splice_fix is false then
    // the existing splice will be ignored unless the current key is being
    // inserted immediately after the splice.  allow_partial_splice_fix ==
    // false has worse running time for the non-sequential case O(log N),
    // but a better constant factor.
    template <bool UseCAS>
    bool Insert (const char* key, Splice* splice, bool allow_partial_splice_fix);

    // Returns true iff an entry that compares equal to key is in the list.
    bool Contains (const char* key) const;

    // !buflog: insert a raw key
    bool BufInsert (const char* key);

    // !buflog:
    bool BufContains (const char* key) const;

    // !buflog:
    // Out of place compact the keys in buffer in this partition.
    // Generate splice_partition.
    // Return first_key, next bufnode after the
    size_t Compact (Node* bufnode, Node* next_bufnode, Splice& splice_partition);

    // Return estimated number of entries smaller than `key`.
    uint64_t EstimateCount (const char* key) const;

    // Validate correctness of the skip-list.
    void TEST_Validate () const;

    // Iteration over the contents of a skip list
    class Iterator {
    public:
        // Initialize an iterator over the specified list.
        // The returned iterator is not valid.
        explicit Iterator (const InlineSkipList* list);

        // Change the underlying skiplist used for this iterator
        // This enables us not changing the iterator without deallocating
        // an old one and then allocating a new one
        void SetList (const InlineSkipList* list);

        // Returns true iff the iterator is positioned at a valid node.
        bool Valid () const;

        // !buflog
        // Returns the height of the node
        int Height () const;

        // !buflog
        // Returns the bufnode pointer
        inline struct BufNode* BufNode () { return node_->BufNode (); }

        // Returns the key at the current position.
        // REQUIRES: Valid()
        const char* key () const;

        // Advances to the next position.
        // REQUIRES: Valid()
        void Next ();

        // Advances to the previous position.
        // REQUIRES: Valid()
        void Prev ();

        // Advance to the first entry with a key >= target
        void Seek (const char* target);

        // Retreat to the last entry with a key <= target
        void SeekForPrev (const char* target);

        // Position at the first entry in list.
        // Final state of iterator is Valid() iff list is not empty.
        void SeekToFirst ();

        // Position at the last entry in list.
        // Final state of iterator is Valid() iff list is not empty.
        void SeekToLast ();

    private:
        const InlineSkipList* list_;
        Node* node_;
        // Intentionally copyable
    };

private:
    const uint16_t kMaxHeight_;
    const uint16_t kBranching_;
    const uint32_t kScaledInverseBranching_;

    class Allocator {
    public:
        inline char* AllocateAligned (size_t size) const {
            return static_cast<char*> (RP_malloc (size));
        }

        inline void Release (char* addr) { RP_free (addr); }
    };

    Allocator const allocator_;  // Allocator used for allocations of nodes
    // Immutable after construction
    Comparator const compare_;
    Node* head_;
    Node* const head_dram_;
    Node* const head_pmem_;

    // Modified only by Insert().  Read racily by readers, but stale
    // values are ok.
    std::atomic<int> max_height_;  // Height of the entire list

    // seq_splice_ is a Splice used for insertions in the non-concurrent
    // case.  It caches the prev and next found during the most recent
    // non-concurrent insertion.
    Splice* seq_splice_;

    inline int GetMaxHeight () const { return max_height_.load (std::memory_order_relaxed); }

    int RandomHeight ();

    Node* AllocateNode (size_t key_size, int height, bool with_buf = false);

    Node* AllocateNodeDram (size_t key_size, int height);

    bool Equal (const char* a, const char* b) const { return (compare_ (a, b) == 0); }

    bool LessThan (const char* a, const char* b) const { return (compare_ (a, b) < 0); }

    // Return true if key is greater than the data stored in "n".  Null n
    // is considered infinite.  n should not be head_.
    bool KeyIsAfterNode (const char* key, Node* n) const;
    bool KeyIsAfterNode (const DecodedKey& key, Node* n) const;

    // Returns the earliest node with a key >= key.
    // Return nullptr if there is no such node.
    Node* FindGreaterOrEqual (const char* key) const;

    // !buflog:
    Node* FindClosestBuf (const char* key, BufNode*& closest_buf) const;

    // Return the latest node with a key < key.
    // Return head_ if there is no such node.
    // Fills prev[level] with pointer to previous node at "level" for every
    // level in [0..max_height_-1], if prev is non-null.
    Node* FindLessThan (const char* key, Node** prev = nullptr) const;

    // Return the latest node with a key < key on bottom_level. Start searching
    // from root node on the level below top_level.
    // Fills prev[level] with pointer to previous node at "level" for every
    // level in [bottom_level..top_level-1], if prev is non-null.
    Node* FindLessThan (const char* key, Node** prev, Node* root, int top_level,
                        int bottom_level) const;

    // Return the last node in the list.
    // Return head_ if list is empty.
    Node* FindLast () const;

    // Traverses a single level of the list, setting *out_prev to the last
    // node before the key and *out_next to the first node after. Assumes
    // that the key is not present in the skip list. On entry, before should
    // point to a node that is before the key, and after should point to
    // a node that is after the key.  after should be nullptr if a good after
    // node isn't conveniently available.
    template <bool prefetch_before>
    void FindSpliceForLevel (const DecodedKey& key, Node* before, Node* after, int level,
                             Node** out_prev, Node** out_next);

    // !buflog: traverse and record the closest bufnode
    template <bool prefetch_before>
    void FindSpliceForLevel (const DecodedKey& key, Node* before, Node* after, int level,
                             Node** out_prev, Node** out_next, Node*& bufnode_ptr);

    // Recomputes Splice levels from highest_level (inclusive) down to
    // lowest_level (inclusive).
    void RecomputeSpliceLevels (const DecodedKey& key, Splice* splice, int recompute_level);
};

// Implementation details follow

template <class Comparator>
struct InlineSkipList<Comparator>::Splice {
    // The invariant of a Splice is that prev_[i+1].key <= prev_[i].key <
    // next_[i].key <= next_[i+1].key for all i.  That means that if a
    // key is bracketed by prev_[i] and next_[i] then it is bracketed by
    // all higher levels.  It is _not_ required that prev_[i]->Next(i) ==
    // next_[i] (it probably did at some point in the past, but intervening
    // or concurrent operations might have inserted nodes in between).
    int height_ = 0;
    Node** prev_;
    Node** next_;

    std::string ToString () {
        char buf[128];
        std::string res;
        for (int i = height_; i >= 0; i--) {
            sprintf (buf, "L%2d: key: %d\n", i, Comparator ().decode_key (prev_[i]->Key ()));
        }
    }
};

template <class Comparator>
struct InlineSkipList<Comparator>::BufNode {
public:
    buflog::BufVec buf;  // 64 byte

    BufNode () {}

    BufNode (const BufNode& p1) {
        buf.cur_ = p1.buf.cur_;
        memcpy (buf.keys_, p1.buf.keys_, buf.cur_ * sizeof (size_t));
    }
};

static constexpr int kNodeKeyPos = 2;
// The Node data type is more of a pointer into custom-managed memory than
// a traditional C++ struct.  The key is stored in the bytes immediately
// after the struct, and the next_ pointers for nodes with height > 1 are
// stored immediately _before_ the struct.  This avoids the need to include
// any pointer or sizing data, which reduces per-node memory overheads.
// |_| pmem_node_ptr (only dram node has this)
// |_| next[-3]
// |_| next[-2]
// |_| next[-1]
// |_| next[0]
// |_| NodeMeta
// |_| key
template <class Comparator>
struct InlineSkipList<Comparator>::Node {
    // Stores the height of the node in the memory location normally used for
    // next_[0].  This is used for passing data from AllocateKey to Insert.
    void StashHeight (const int height, bool is_dram = false) {
        assert (sizeof (int) <= sizeof (next_[0]));
        memcpy (static_cast<void*> (&next_[0]), &height, sizeof (int));
        NodeMeta* node_meta = reinterpret_cast<NodeMeta*> (&next_[1]);
        node_meta->height = height;
        node_meta->bufnode_ptr = 0;
        node_meta->is_dram_node = is_dram;
    }

    NodeMeta* Meta (void) { return reinterpret_cast<NodeMeta*> (&next_[1]); }

    bool isDramNode (void) { return reinterpret_cast<NodeMeta*> (&next_[1])->is_dram_node; }

    // Retrieves the value passed to StashHeight.  Undefined after a call
    // to SetNext or NoBarrier_SetNext.
    int UnstashHeight () const {
        int rv;
        memcpy (&rv, &next_[0], sizeof (int));
        return rv;
    }

    int Height () const {
        const NodeMeta* node_meta = reinterpret_cast<const NodeMeta*> (&next_[1]);
        return node_meta->height;
    }

    void SetBufNode (BufNode* bf) {
        assert (isDramNode ());
        reinterpret_cast<struct NodeMeta*> (&next_[1])->bufnode_ptr = (uint64_t)bf;
    }

    void SetPmemNode (Node* pmem_node) {
        assert (isDramNode ());
        reinterpret_cast<struct PmemMeta*> (&next_[0] - Height ())->pmemnode_ptr =
            (uint64_t)pmem_node;
    }

    Node* PmemNode (void) {
        // only dram node can call this
        assert (isDramNode ());
        return (Node*)reinterpret_cast<struct PmemMeta*> (&next_[0] - Height ())->pmemnode_ptr;
    }

    BufNode* BufNode () {
        struct NodeMeta* node_meta = reinterpret_cast<struct NodeMeta*> (&next_[1]);
        return node_meta->bufnode_ptr == 0 ? nullptr : (struct BufNode*)node_meta->bufnode_ptr;
    }

    const char* Key () const { return reinterpret_cast<const char*> (&next_[kNodeKeyPos]); }

    // Accessors/mutators for links.  Wrapped in methods so we can add
    // the appropriate barriers as necessary, and perform the necessary
    // addressing trickery for storing links below the Node in memory.
    Node* Next (int n) {
        assert (n >= 0);
        // Use an 'acquire load' so that we observe a fully initialized
        // version of the returned Node.
        return ((&next_[0] - n)->load (std::memory_order_acquire));
    }

    void SetNext (int n, Node* x) {
        assert (n >= 0);
        // Use a 'release store' so that anybody who reads through this
        // pointer observes a fully initialized version of the inserted node.
        (&next_[0] - n)->store (x, std::memory_order_release);
        FLUSH (&next_[0] - n);
        FLUSHFENCE;
    }

    void SetNextNoFlush (int n, Node* x) {
        assert (n >= 0);
        // Use a 'release store' so that anybody who reads through this
        // pointer observes a fully initialized version of the inserted node.
        (&next_[0] - n)->store (x, std::memory_order_relaxed);
    }

    bool CASNext (int n, Node* expected, Node* x) {
        assert (n >= 0);
        bool res = (&next_[0] - n)->compare_exchange_strong (expected, x);
        if (res) {
            FLUSH (&next_[0] - n);
            FLUSHFENCE;
            return true;
        }
        return false;
    }

    // No-barrier variants that can be safely used in a few locations.
    Node* NoBarrier_Next (int n) {
        assert (n >= 0);
        return (&next_[0] - n)->load (std::memory_order_relaxed);
    }

    void NoBarrier_SetNext (int n, Node* x) {
        assert (n >= 0);
        (&next_[0] - n)->store (x, std::memory_order_relaxed);
        FLUSH (&next_[0] - n);
    }

    // Insert node after prev on specific level.
    void InsertAfter (Node* prev, int level) {
        // NoBarrier_SetNext() suffices since we will add a barrier when
        // we publish a pointer to "this" in prev.
        NoBarrier_SetNext (level, prev->NoBarrier_Next (level));
        prev->SetNext (level, this);
    }

private:
    // next_[0] is the lowest level link (level 0).  Higher levels are
    // stored _earlier_, so level 1 is at next_[-1].
    std::atomic<Node*> next_[1];
};

template <class Comparator>
inline InlineSkipList<Comparator>::Iterator::Iterator (const InlineSkipList* list) {
    SetList (list);
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::SetList (const InlineSkipList* list) {
    list_ = list;
    node_ = nullptr;
}

template <class Comparator>
inline bool InlineSkipList<Comparator>::Iterator::Valid () const {
    return node_ != nullptr;
}

template <class Comparator>
inline int InlineSkipList<Comparator>::Iterator::Height () const {
    assert (Valid ());
    return node_->Height ();
}

template <class Comparator>
inline const char* InlineSkipList<Comparator>::Iterator::key () const {
    assert (Valid ());
    return node_->Key ();
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::Next () {
    assert (Valid ());
    node_ = node_->Next (0);
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::Prev () {
    // Instead of using explicit "prev" links, we just search for the
    // last node that falls before key.
    assert (Valid ());
    node_ = list_->FindLessThan (node_->Key ());
    if (node_ == list_->head_) {
        node_ = nullptr;
    }
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::Seek (const char* target) {
    node_ = list_->FindGreaterOrEqual (target);
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::SeekForPrev (const char* target) {
    Seek (target);
    if (!Valid ()) {
        SeekToLast ();
    }
    while (Valid () && list_->LessThan (target, key ())) {
        Prev ();
    }
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::SeekToFirst () {
    node_ = list_->head_->Next (0);
}

template <class Comparator>
inline void InlineSkipList<Comparator>::Iterator::SeekToLast () {
    node_ = list_->FindLast ();
    if (node_ == list_->head_) {
        node_ = nullptr;
    }
}

template <class Comparator>
int InlineSkipList<Comparator>::RandomHeight () {
    auto rnd = Random::GetTLSInstance ();

    // Increase height with probability 1 in kBranching
    int height = 1;
    while (height < kMaxHeight_ && height < kMaxPossibleHeight &&
           rnd->Next () < kScaledInverseBranching_) {
        height++;
    }
    assert (height > 0);
    assert (height <= kMaxHeight_);
    assert (height <= kMaxPossibleHeight);
    return height;
}

template <class Comparator>
bool InlineSkipList<Comparator>::KeyIsAfterNode (const char* key, Node* n) const {
    // nullptr n is considered infinite
    assert (n != head_);
    return (n != nullptr) && (compare_ (n->Key (), key) < 0);
}

template <class Comparator>
bool InlineSkipList<Comparator>::KeyIsAfterNode (const DecodedKey& key, Node* n) const {
    // nullptr n is considered infinite
    assert (n != head_);
    return (n != nullptr) && (compare_ (n->Key (), key) < 0);
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::FindGreaterOrEqual (
    const char* key) const {
    // Note: It looks like we could reduce duplication by implementing
    // this function as FindLessThan(key)->Next(0), but we wouldn't be able
    // to exit early on equality and the result wouldn't even be correct.
    // A concurrent insert might occur after FindLessThan(key) but before
    // we get a chance to call Next(0).
    Node* x = head_;
    int level = GetMaxHeight () - 1;
    Node* last_bigger = nullptr;
    const DecodedKey key_decoded = compare_.decode_key (key);
    while (true) {
        Node* next = x->Next (level);
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        // Make sure the lists are sorted
        assert (x == head_ || next == nullptr || KeyIsAfterNode (next->Key (), x));
        // Make sure we haven't overshot during our search
        assert (x == head_ || KeyIsAfterNode (key_decoded, x));
        int cmp =
            (next == nullptr || next == last_bigger) ? 1 : compare_ (next->Key (), key_decoded);
        if (cmp == 0 || (cmp > 0 && level == 0)) {
            return next;
        } else if (cmp < 0) {
            // Keep searching in this list
            x = next;
        } else {
            // Switch to next list, reuse compare_() result
            last_bigger = next;
            level--;
        }
    }
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::FindClosestBuf (
    const char* key, BufNode*& closest_buf) const {
    // Note: It looks like we could reduce duplication by implementing
    // this function as FindLessThan(key)->Next(0), but we wouldn't be able
    // to exit early on equality and the result wouldn't even be correct.
    // A concurrent insert might occur after FindLessThan(key) but before
    // we get a chance to call Next(0).
    Node* x = head_;
    closest_buf = reinterpret_cast<BufNode*> (x->BufNode ());
    int level = GetMaxHeight () - 1;
    Node* last_bigger = nullptr;
    const DecodedKey key_decoded = compare_.decode_key (key);
    Node* next;
    while (level >= kBufNodeLevel) {
        next = x->Next (level);
        DEBUG ("L %d, Node x 0x%lx: %ld, Node next 0x%lx: %ld, ", level, x,
               x == nullptr ? -1 : *(size_t*)x->Key (), next,
               next == nullptr ? -1 : *(size_t*)next->Key ());
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        // Make sure the lists are sorted
        assert (x == head_ || next == nullptr || KeyIsAfterNode (next->Key (), x));
        // Make sure we haven't overshot during our search
        assert (x == head_ || KeyIsAfterNode (key_decoded, x));
        int cmp =
            (next == nullptr || next == last_bigger) ? 1 : compare_ (next->Key (), key_decoded);
        if (cmp == 0) {
            closest_buf = reinterpret_cast<BufNode*> (next->BufNode ());
            return next;
        } else if (cmp < 0) {
            // Keep searching in this list
            x = next;
        } else {
            // Switch to next list, reuse compare_() result
            // to next level
            last_bigger = next;
            level--;
        }
    }

    closest_buf = reinterpret_cast<BufNode*> (x->BufNode ());
    return x;
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::FindLessThan (
    const char* key, Node** prev) const {
    return FindLessThan (key, prev, head_, GetMaxHeight (), 0);
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::FindLessThan (
    const char* key, Node** prev, Node* root, int top_level, int bottom_level) const {
    assert (top_level > bottom_level);
    int level = top_level - 1;
    Node* x = root;
    // KeyIsAfter(key, last_not_after) is definitely false
    Node* last_not_after = nullptr;
    const DecodedKey key_decoded = compare_.decode_key (key);
    while (true) {
        assert (x != nullptr);
        Node* next = x->Next (level);
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        assert (x == head_ || next == nullptr || KeyIsAfterNode (next->Key (), x));
        assert (x == head_ || KeyIsAfterNode (key_decoded, x));
        if (next != last_not_after && KeyIsAfterNode (key_decoded, next)) {
            // Keep searching in this list
            assert (next != nullptr);
            x = next;
        } else {
            if (prev != nullptr) {
                prev[level] = x;
            }
            if (level == bottom_level) {
                return x;
            } else {
                // Switch to next list, reuse KeyIsAfterNode() result
                last_not_after = next;
                level--;
            }
        }
    }
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::FindLast () const {
    Node* x = head_;
    int level = GetMaxHeight () - 1;
    while (true) {
        Node* next = x->Next (level);
        if (next == nullptr) {
            if (level == 0) {
                return x;
            } else {
                // Switch to next list
                level--;
            }
        } else {
            x = next;
        }
    }
}

template <class Comparator>
uint64_t InlineSkipList<Comparator>::EstimateCount (const char* key) const {
    uint64_t count = 0;

    Node* x = head_;
    int level = GetMaxHeight () - 1;
    const DecodedKey key_decoded = compare_.decode_key (key);
    while (true) {
        assert (x == head_ || compare_ (x->Key (), key_decoded) < 0);
        Node* next = x->Next (level);
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        if (next == nullptr || compare_ (next->Key (), key_decoded) >= 0) {
            if (level == 0) {
                return count;
            } else {
                // Switch to next list
                count *= kBranching_;
                level--;
            }
        } else {
            x = next;
            count++;
        }
    }
}

template <class Comparator>
InlineSkipList<Comparator>::InlineSkipList (const Comparator cmp, int32_t max_height,
                                            int32_t branching_factor)
    : kMaxHeight_ (static_cast<uint16_t> (max_height)),
      kBranching_ (static_cast<uint16_t> (branching_factor)),
      kScaledInverseBranching_ ((Random::kMaxNext + 1) / kBranching_),
      compare_ (cmp),
      head_dram_ (AllocateNodeDram (0, max_height)),
      head_pmem_ (AllocateNode (0, max_height, true)),
      max_height_ (3),
      seq_splice_ (AllocateSplice ()) {
    assert (max_height > 0 && kMaxHeight_ == static_cast<uint32_t> (max_height));
    assert (branching_factor > 1 && kBranching_ == static_cast<uint32_t> (branching_factor));
    assert (kScaledInverseBranching_ > 0);

    for (int i = 0; i < kMaxHeight_; ++i) {
        head_pmem_->SetNext (i, nullptr);
        head_dram_->SetNext (i, nullptr);
    }

    // dram head node share the same buf with pmem head node
    head_dram_->SetBufNode (head_pmem_->BufNode ());
    head_dram_->SetPmemNode (head_pmem_);

#ifdef DRAM_CACHE
    head_ = head_dram_;
#else
    head_ = head_pmem_;
#endif
}

template <class Comparator>
char* InlineSkipList<Comparator>::AllocateKey (size_t key_size) {
    return const_cast<char*> (AllocateNode (key_size, RandomHeight ())->Key ());
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::AllocateNodeDram (
    size_t key_size, int height) {
    auto prefix = sizeof (std::atomic<Node*>) * (height);

    // !bufnode: after node, we add 8-byte meta. before the next pointers, we
    // !add a pointer pointed to pmem node
    // |  2 Byte  |          6 byte         |
    // |  height  |  pointer to buffer node |
    char* raw = (char*)malloc (prefix + sizeof (Node) + sizeof (NodeMeta) + key_size);
    Node* x = reinterpret_cast<Node*> (raw + prefix);

    x->StashHeight (height, true);
    return x;
}

template <class Comparator>
typename InlineSkipList<Comparator>::Node* InlineSkipList<Comparator>::AllocateNode (
    size_t key_size, int height, bool with_buf) {
    auto prefix = sizeof (std::atomic<Node*>) * (height - 1);

    // prefix is space for the height - 1 pointers that we store before
    // the Node instance (next_[-(height - 1) .. -1]).  Node starts at
    // raw + prefix, and holds the bottom-mode (level 0) skip list pointer
    // next_[0].  key_size is the bytes for the key, which comes just after
    // the Node.

    // !bufnode: after node, we add 8-byte meta
    // |  2 Byte  |          6 byte         |
    // |  height  |  pointer to buffer node |
    char* raw = allocator_.AllocateAligned (prefix + sizeof (Node) + sizeof (NodeMeta) + key_size);
    Node* x = reinterpret_cast<Node*> (raw + prefix);

    // Once we've linked the node into the skip list we don't actually need
    // to know its height, because we can implicitly use the fact that we
    // traversed into a node at level h to known that h is a valid level
    // for that node.  We need to convey the height to the Insert step,
    // however, so that it can perform the proper links.  Since we're not
    // using the pointers at the moment, StashHeight temporarily borrow
    // storage from next_[0] for that purpose.
    x->StashHeight (height);
    if (with_buf && height >= kBufNodeLevel + 1) {
        NodeMeta* node_meta = x->Meta ();
        node_meta->bufnode_ptr = (uint64_t) new BufNode ();
    }
    FLUSH (raw);
    FLUSHFENCE;
    return x;
}

template <class Comparator>
typename InlineSkipList<Comparator>::Splice* InlineSkipList<Comparator>::AllocateSplice () {
    // size of prev_ and next_
    size_t array_size = sizeof (Node*) * (kMaxHeight_ + 1);
    char* raw = reinterpret_cast<char*> (malloc (sizeof (Splice) + array_size * 2));
    Splice* splice = reinterpret_cast<Splice*> (raw);
    splice->height_ = 0;
    splice->prev_ = reinterpret_cast<Node**> (raw + sizeof (Splice));
    splice->next_ = reinterpret_cast<Node**> (raw + sizeof (Splice) + array_size);
    return splice;
}

template <class Comparator>
typename InlineSkipList<Comparator>::Splice* InlineSkipList<Comparator>::AllocateSpliceOnHeap () {
    size_t array_size = sizeof (Node*) * (kMaxHeight_ + 1);
    char* raw = new char[sizeof (Splice) + array_size * 2];
    Splice* splice = reinterpret_cast<Splice*> (raw);
    splice->height_ = 0;
    splice->prev_ = reinterpret_cast<Node**> (raw + sizeof (Splice));
    splice->next_ = reinterpret_cast<Node**> (raw + sizeof (Splice) + array_size);
    return splice;
}

template <class Comparator>
bool InlineSkipList<Comparator>::Insert (const char* key) {
    return Insert<false> (key, seq_splice_, false);
}

template <class Comparator>
bool InlineSkipList<Comparator>::InsertConcurrently (const char* key) {
    // Node* prev[kMaxPossibleHeight];
    // Node* next[kMaxPossibleHeight];
    // Splice splice;
    // splice.prev_ = prev;
    // splice.next_ = next;
    // return Insert<true>(key, &splice, false);
    return BufInsert (key);
}

template <class Comparator>
bool InlineSkipList<Comparator>::InsertWithHint (const char* key, void** hint) {
    assert (hint != nullptr);
    Splice* splice = reinterpret_cast<Splice*> (*hint);
    if (splice == nullptr) {
        splice = AllocateSplice ();
        *hint = reinterpret_cast<void*> (splice);
    }
    return Insert<false> (key, splice, true);
}

template <class Comparator>
bool InlineSkipList<Comparator>::InsertWithHintConcurrently (const char* key, void** hint) {
    assert (hint != nullptr);
    Splice* splice = reinterpret_cast<Splice*> (*hint);
    if (splice == nullptr) {
        splice = AllocateSpliceOnHeap ();
        *hint = reinterpret_cast<void*> (splice);
    }
    return Insert<true> (key, splice, true);
}

template <class Comparator>
template <bool prefetch_before>
void InlineSkipList<Comparator>::FindSpliceForLevel (const DecodedKey& key, Node* before,
                                                     Node* after, int level, Node** out_prev,
                                                     Node** out_next, Node*& bufnode_ptr) {
    assert (level >= kBufNodeLevel);
    while (true) {
        Node* next = before->Next (level);
        DEBUG ("L %d, Node before 0x%lx: %ld, Node next 0x%lx: %ld, Node after 0x%lx: %ld, ", level,
               before, before == head_ ? -1 : *(size_t*)before->Key (), next,
               next == nullptr ? -1 : *(size_t*)next->Key (), after,
               after == nullptr ? -1 : *(size_t*)after->Key ());
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        if (prefetch_before == true) {
            if (next != nullptr && level > 0) {
                PREFETCH (next->Next (level - 1), 0, 1);
            }
        }
        assert (before == head_ || next == nullptr || KeyIsAfterNode (next->Key (), before));
        assert (before == head_ || KeyIsAfterNode (key, before));
        if (next == after || !KeyIsAfterNode (key, next)) {
            // found it. key <= next. to next level
            *out_prev = before;
            *out_next = next;
            bufnode_ptr = before;
            return;
        }
        before = next;
    }
}

template <class Comparator>
template <bool prefetch_before>
void InlineSkipList<Comparator>::FindSpliceForLevel (const DecodedKey& key, Node* before,
                                                     Node* after, int level, Node** out_prev,
                                                     Node** out_next) {
    while (true) {
        Node* next = before->Next (level);
        DEBUG ("L %d, Node before 0x%lx: %ld, Node next 0x%lx: %ld, Node after 0x%lx: %ld, ", level,
               before, before == head_ ? -1 : *(size_t*)before->Key (), next,
               next == nullptr ? -1 : *(size_t*)next->Key (), after,
               after == nullptr ? -1 : *(size_t*)after->Key ());
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        if (prefetch_before == true) {
            if (next != nullptr && level > 0) {
                PREFETCH (next->Next (level - 1), 0, 1);
            }
        }
        assert (before == head_ || next == nullptr || KeyIsAfterNode (next->Key (), before));
        assert (before == head_ || KeyIsAfterNode (key, before));
        if (next == after || !KeyIsAfterNode (key, next)) {
            // found it. key <= next
            *out_prev = before;
            *out_next = next;
            return;
        }
        before = next;
    }
}

template <class Comparator>
void InlineSkipList<Comparator>::RecomputeSpliceLevels (const DecodedKey& key, Splice* splice,
                                                        int recompute_level) {
    assert (recompute_level > 0);
    assert (recompute_level <= splice->height_);
    for (int i = recompute_level - 1; i >= 0; --i) {
        FindSpliceForLevel<true> (key, splice->prev_[i + 1], splice->next_[i + 1], i,
                                  &splice->prev_[i], &splice->next_[i]);
    }
}

template <class Comparator>
bool InlineSkipList<Comparator>::BufInsert (const char* key) {
    Node* prev[kMaxPossibleHeight];
    Node* next[kMaxPossibleHeight];
    Splice splice;
    splice.prev_ = prev;
    splice.next_ = next;

    const DecodedKey key_decoded = compare_.decode_key (key);
    DEBUG ("BufInsert key: %ld", key_decoded);

retry:
    int max_height = max_height_.load (std::memory_order_relaxed);
    splice.prev_[max_height] = head_;
    splice.next_[max_height] = nullptr;
    splice.height_ = max_height;

    // travel the skiplist
    Node* closest_bufnode = head_;
    for (int i = max_height - 1; i >= 0; --i) {
        if (i < kBufNodeLevel) {
            // if we are under kBufNodeLevel, it means we enter into a partiion
            BufNode* bufnode = closest_bufnode->BufNode ();
            bufnode->buf.Lock ();

            Node* next_bufnode = closest_bufnode->Next (kBufNodeLevel);
#ifdef DRAM_CACHE
            assert (next_bufnode == nullptr || next_bufnode->isDramNode ());
#endif
            DEBUG ("BufInsert key: %ld. Lock the bufnode 0x%lx", key_decoded, bufnode);
            // After we acquire the lock, we may just finish a compaction.
            // Need to check if this key still belongs to this partition
            if (next_bufnode && KeyIsAfterNode (key, next_bufnode)) {
                // means it belongs to next partition, retry
                bufnode->buf.Unlock ();
                DEBUG ("Retry BufInsert key: %ld", key_decoded);
                goto retry;
            }

            bool res = bufnode->buf.Insert (key_decoded);
            if (res) {
                DEBUG ("BufInsert key %d inserted", key_decoded);
                bufnode->buf.Unlock ();
                return true;
            } else {
                // bufnode is full.
                bool res = bufnode->buf.CompactInsert (key_decoded);
                if (!res) {
                    printf ("Cannot insert the last key to bufnode to compact\n");
                    exit (1);
                }

                // step 1. do compaction
                Node* partition_prev[kMaxPossibleHeight];
                Node* partition_next[kMaxPossibleHeight];
                Splice splice_partition;
                splice_partition.prev_ = partition_prev;
                splice_partition.next_ = partition_next;
                DEBUG ("bufnode 0x%lx, next bufnode before 0x%lx, is dram node: %d",
                       closest_bufnode, next_bufnode,
                       next_bufnode ? next_bufnode->isDramNode () : 0);
                size_t first_key = Compact (closest_bufnode, next_bufnode, splice_partition);
                DEBUG ("bufnode 0x%lx, next bufnode after  0x%lx, is dram node: %d",
                       closest_bufnode, next_bufnode,
                       next_bufnode ? next_bufnode->isDramNode () : 0);

                //       ...  |__|
                //       ...  |__| ----------------------------- |__|
                //       ...  |__| ------------ |__| ----------- |__|
                //       ...  |__| --- |__| --- |__| --- |__| -- |__|
                //             b1                bn               b2

                int bufnode_height = std::min (max_height, closest_bufnode->Height ());
                int next_bufnode_height =
                    next_bufnode ? next_bufnode->Height () : splice_partition.height_;
                DEBUG (
                    "skiplist height: %d, bufnode height: %d, splice_partition.height: %d, next "
                    "bufnode height: %d",
                    max_height, bufnode_height, splice_partition.height_, next_bufnode_height);

                int lower_part_height = std::min (bufnode_height, splice_partition.height_);
                int higher_part_height = std::max (bufnode_height, splice_partition.height_);
                if (next_bufnode) {
                    lower_part_height = std::min (lower_part_height, next_bufnode_height);
                    higher_part_height = std::max (higher_part_height, next_bufnode_height);
                }

                // Step 2. Link the lower part, only need to connect this new partition within two
                // bufnode
                for (int l = 0; l < lower_part_height; l++) {
#ifdef DRAM_CACHE
                    bool is_splice_partition_next_dram = splice_partition.next_[l]->isDramNode ();
                    bool is_next_bufnode_dram = next_bufnode ? next_bufnode->isDramNode () : false;
                    if (is_splice_partition_next_dram && is_next_bufnode_dram) {
                        DEBUG (
                            "Link dram - dram splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l], next_bufnode,
                            *(size_t*)splice_partition.next_[l]->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->Key () : -1));
                        splice_partition.next_[l]->NoBarrier_SetNext (
                            l, next_bufnode);  // dram -> dram
                        DEBUG (
                            "Link pmem - pmem splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l]->PmemNode (),
                            next_bufnode ? next_bufnode->PmemNode () : nullptr,
                            *(size_t*)splice_partition.next_[l]->PmemNode ()->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->PmemNode ()->Key () : -1));
                        splice_partition.next_[l]->PmemNode ()->NoBarrier_SetNext (
                            l, next_bufnode->PmemNode ());  // pmem -> pmem
                    } else if (is_splice_partition_next_dram) {
                        DEBUG (
                            "Link dram - pmem splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l], next_bufnode,
                            *(size_t*)splice_partition.next_[l]->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->Key () : -1));
                        splice_partition.next_[l]->NoBarrier_SetNext (
                            l, next_bufnode);  // dram -> pmem
                        DEBUG (
                            "Link pmem - pmem splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l]->PmemNode (), next_bufnode,
                            *(size_t*)splice_partition.next_[l]->PmemNode ()->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->Key () : -1));
                        splice_partition.next_[l]->PmemNode ()->NoBarrier_SetNext (
                            l, next_bufnode);  // pmem -> pmem
                    } else if (is_next_bufnode_dram) {
                        DEBUG (
                            "Link pmem - pmem splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l], next_bufnode->PmemNode (),
                            *(size_t*)splice_partition.next_[l]->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->PmemNode ()->Key () : -1));
                        splice_partition.next_[l]->NoBarrier_SetNext (
                            l, next_bufnode->PmemNode ());  // pmem -> pmem
                    } else {
                        DEBUG (
                            "Link pmem - pmem splice_partition.next[%d] 0x%lx -> next_bufnode "
                            "0x%lx. %ld -> %ld",
                            l, splice_partition.next_[l], next_bufnode,
                            *(size_t*)splice_partition.next_[l]->Key (),
                            (next_bufnode ? *(size_t*)next_bufnode->Key () : -1));
                        splice_partition.next_[l]->NoBarrier_SetNext (
                            l, next_bufnode);  // pmem -> pmem
                    }
#else
                    DEBUG ("Link splice_partition.next[%d] 0x%lx -> next_bufnode 0x%lx. %ld -> %ld",
                           l, splice_partition.next_[l], next_bufnode,
                           *(size_t*)splice_partition.next_[l]->Key (),
                           (next_bufnode ? *(size_t*)next_bufnode->Key () : -1));
                    splice_partition.next_[l]->NoBarrier_SetNext (l, next_bufnode);
#endif
                }

                for (int l = 0; l < lower_part_height; l++) {
#ifdef DRAM_CACHE
                    bool is_splice_partition_prev_dram = splice_partition.prev_[l]->isDramNode ();
                    if (is_splice_partition_prev_dram) {
                        DEBUG (
                            "Link dram - dram closest_bufnode 0x%lx -> splice_partition.prev_[%d] "
                            "0x%lx. %ld -> %ld",
                            closest_bufnode, l, splice_partition.prev_[l],
                            closest_bufnode != head_ ? *(size_t*)closest_bufnode->Key () : -1,
                            *(size_t*)splice_partition.prev_[l]->Key ());
                        closest_bufnode->SetNext (l, splice_partition.prev_[l]);  // dram -> dram

                        DEBUG (
                            "Link pmem - pmem closest_bufnode 0x%lx -> splice_partition.prev_[%d] "
                            "0x%lx. %ld -> %ld",
                            closest_bufnode->PmemNode (), l, splice_partition.prev_[l]->PmemNode (),
                            closest_bufnode != head_
                                ? *(size_t*)closest_bufnode->PmemNode ()->Key ()
                                : -1,
                            *(size_t*)splice_partition.prev_[l]->PmemNode ()->Key ());
                        closest_bufnode->PmemNode ()->SetNext (
                            l, splice_partition.prev_[l]->PmemNode ());  // pmem -> pmem
                    } else {
                        DEBUG (
                            "Link dram - pmem closest_bufnode 0x%lx -> splice_partition.prev_[%d] "
                            "0x%lx. %ld -> %ld",
                            closest_bufnode, l, splice_partition.prev_[l],
                            closest_bufnode != head_ ? *(size_t*)closest_bufnode->Key () : -1,
                            *(size_t*)splice_partition.prev_[l]->Key ());
                        closest_bufnode->SetNext (l, splice_partition.prev_[l]);  // dram -> pmem

                        DEBUG (
                            "Link pmem - dram closest_bufnode 0x%lx -> splice_partition.prev_[%d] "
                            "0x%lx. %ld -> %ld",
                            closest_bufnode->PmemNode (), l, splice_partition.prev_[l],
                            closest_bufnode != head_
                                ? *(size_t*)closest_bufnode->PmemNode ()->Key ()
                                : -1,
                            *(size_t*)splice_partition.prev_[l]->Key ());
                        closest_bufnode->PmemNode ()->SetNext (
                            l, splice_partition.prev_[l]);  // pmem -> pmem
                    }
#else
                    DEBUG (
                        "Link closest_bufnode 0x%lx -> splice_partition.prev_[%d] 0x%lx. %ld -> "
                        "%ld",
                        closest_bufnode, l, splice_partition.prev_[l],
                        closest_bufnode != head_ ? *(size_t*)closest_bufnode->Key () : -1,
                        *(size_t*)splice_partition.prev_[l]->Key ());
                    closest_bufnode->SetNext (l, splice_partition.prev_[l]);
#endif
                }

                // Step 3. Link the higher part, only when splice_partition.height_ is higher than
                // the two bufnode
                if (splice_partition.height_ > bufnode_height ||
                    splice_partition.height_ > next_bufnode_height) {
                    int prev_search_height = std::max (splice_partition.height_, max_height);
                    DEBUG ("key %d, prev search height: %d, splice height: %d", key_decoded,
                           prev_search_height, splice.height_);

                    assert (lower_part_height >= kBufNodeLevel + 1);

                    if (splice.height_ < prev_search_height) {
                        // the partition is higher than skiplist, recompute splice
                        splice.prev_[prev_search_height] = head_;
                        splice.next_[prev_search_height] = nullptr;
                        splice.height_ = prev_search_height;
                        for (int i = prev_search_height - 1; i >= lower_part_height; --i) {
                            DEBUG ("Recompute splice at level %d", i);
                            FindSpliceForLevel<true> (first_key, splice.prev_[i + 1],
                                                      splice.next_[i + 1], i, &splice.prev_[i],
                                                      &splice.next_[i]);
                        }
                    }

                    for (int i = lower_part_height; i < splice_partition.height_; i++) {
                        // link the new partition
                        while (true) {
#ifdef DRAM_CACHE
                            assert (splice_partition.next_[i]->isDramNode ());
                            assert (splice_partition.prev_[i]->isDramNode ());
                            assert (splice.prev_[i]->isDramNode ());
                            assert ((splice.next_[i] == nullptr) ||
                                    (splice.next_[i] && splice.next_[i]->isDramNode ()));
                            if (splice.next_[i]) {
                                splice_partition.next_[i]->NoBarrier_SetNext (
                                    i, splice.next_[i]);  // dram -> dram
                                splice_partition.next_[i]->PmemNode ()->NoBarrier_SetNext (
                                    i, splice.next_[i]->PmemNode ());  // pmem -> pmem

                                if (splice.prev_[i]->CASNext (i, splice.next_[i],
                                                              splice_partition.prev_[i]) &&
                                    splice.prev_[i]->PmemNode ()->CASNext (
                                        i, splice.next_[i]->PmemNode (),
                                        splice_partition.prev_[i]->PmemNode ())) {
                                    break;
                                }

                            } else {
                                // splice.next_[i] is nullptr
                                splice_partition.next_[i]->NoBarrier_SetNext (
                                    i, splice.next_[i]);  // dram -> nullptr
                                splice_partition.next_[i]->PmemNode ()->NoBarrier_SetNext (
                                    i, splice.next_[i]);  // pmem -> nullptr

                                if (splice.prev_[i]->CASNext (i, splice.next_[i],
                                                              splice_partition.prev_[i]) &&
                                    splice.prev_[i]->PmemNode ()->CASNext (
                                        i, splice.next_[i],
                                        splice_partition.prev_[i]->PmemNode ())) {
                                    break;
                                }
                            }
#else
                            DEBUG (
                                "Link splice_partition.next[%d] 0x%lx -> splice.next[%d] 0x%lx. "
                                "%ld -> %ld",
                                i, splice_partition.next_[i], i, splice.next_[i],
                                *(size_t*)splice_partition.next_[i]->Key (),
                                (splice.next_[i] ? *(size_t*)splice.next_[i]->Key () : -1));
                            splice_partition.next_[i]->NoBarrier_SetNext (i, splice.next_[i]);
                            if (splice.prev_[i]->CASNext (i, splice.next_[i],
                                                          splice_partition.prev_[i])) {
                                // success
                                DEBUG (
                                    "Link splice.prev[%d] 0x%lx -> splice_partition.prev[%d] "
                                    "0x%lx. %ld -> %ld",
                                    i, splice.prev_[i], i, splice_partition.prev_[i],
                                    splice.prev_[i] == head_ ? -1
                                                             : *(size_t*)splice.prev_[i]->Key (),
                                    *(size_t*)splice_partition.prev_[i]->Key ());
                                break;
                            }
#endif
                            DEBUG ("CAS retry");
                            FindSpliceForLevel<false> (first_key, splice.prev_[i], nullptr, i,
                                                       &splice.prev_[i], &splice.next_[i]);
                        }
                    }
                }

                // Step 4. update skiplist height after compaction
                int max_height = max_height_.load (std::memory_order_relaxed);
                while (splice_partition.height_ > max_height) {
                    if (max_height_.compare_exchange_weak (max_height, splice_partition.height_)) {
                        // successfully updated it
                        break;
                    }
                }

                // Step 8. Reset the bufnode
                bufnode->buf.Reset ();

                bufnode->buf.Unlock ();
                return false;
            }
        } else {
#ifdef DRAM_CACHE
            assert (closest_bufnode->isDramNode ());
#endif
            // still not sure if we enter the closest bufnode or not
            FindSpliceForLevel<true> (key_decoded, splice.prev_[i + 1], splice.next_[i + 1], i,
                                      &splice.prev_[i], &splice.next_[i], closest_bufnode);
        }
    }

    DEBUG ("BufInsert fail. Should not enter here");
    return false;
}

template <class Comparator>
template <bool UseCAS>
bool InlineSkipList<Comparator>::Insert (const char* key, Splice* splice,
                                         bool allow_partial_splice_fix) {
    Node* x = reinterpret_cast<Node*> (const_cast<char*> (key)) - kNodeKeyPos;
    const DecodedKey key_decoded = compare_.decode_key (key);
    int height = x->UnstashHeight ();
    assert (height >= 1 && height <= kMaxHeight_);

    int max_height = max_height_.load (std::memory_order_relaxed);
    while (height > max_height) {
        if (max_height_.compare_exchange_weak (max_height, height)) {
            // successfully updated it
            max_height = height;
            break;
        }
        // else retry, possibly exiting the loop because somebody else
        // increased it
    }
    assert (max_height <= kMaxPossibleHeight);

    int recompute_height = 0;
    if (splice->height_ < max_height) {
        // Either splice has never been used or max_height has grown since
        // last use.  We could potentially fix it in the latter case, but
        // that is tricky.
        splice->prev_[max_height] = head_;
        splice->next_[max_height] = nullptr;
        splice->height_ = max_height;
        recompute_height = max_height;
    } else {
        // Splice is a valid proper-height splice that brackets some
        // key, but does it bracket this one?  We need to validate it and
        // recompute a portion of the splice (levels 0..recompute_height-1)
        // that is a superset of all levels that don't bracket the new key.
        // Several choices are reasonable, because we have to balance the work
        // saved against the extra comparisons required to validate the Splice.
        //
        // One strategy is just to recompute all of orig_splice_height if the
        // bottom level isn't bracketing.  This pessimistically assumes that
        // we will either get a perfect Splice hit (increasing sequential
        // inserts) or have no locality.
        //
        // Another strategy is to walk up the Splice's levels until we find
        // a level that brackets the key.  This strategy lets the Splice
        // hint help for other cases: it turns insertion from O(log N) into
        // O(log D), where D is the number of nodes in between the key that
        // produced the Splice and the current insert (insertion is aided
        // whether the new key is before or after the splice).  If you have
        // a way of using a prefix of the key to map directly to the closest
        // Splice out of O(sqrt(N)) Splices and we make it so that splices
        // can also be used as hints during read, then we end up with Oshman's
        // and Shavit's SkipTrie, which has O(log log N) lookup and insertion
        // (compare to O(log N) for skip list).
        //
        // We control the pessimistic strategy with allow_partial_splice_fix.
        // A good strategy is probably to be pessimistic for seq_splice_,
        // optimistic if the caller actually went to the work of providing
        // a Splice.
        while (recompute_height < max_height) {
            if (splice->prev_[recompute_height]->Next (recompute_height) !=
                splice->next_[recompute_height]) {
                // splice isn't tight at this level, there must have been some inserts
                // to this
                // location that didn't update the splice.  We might only be a little
                // stale, but if
                // the splice is very stale it would be O(N) to fix it.  We haven't used
                // up any of
                // our budget of comparisons, so always move up even if we are
                // pessimistic about
                // our chances of success.
                ++recompute_height;
            } else if (splice->prev_[recompute_height] != head_ &&
                       !KeyIsAfterNode (key_decoded, splice->prev_[recompute_height])) {
                // key is from before splice
                if (allow_partial_splice_fix) {
                    // skip all levels with the same node without more comparisons
                    Node* bad = splice->prev_[recompute_height];
                    while (splice->prev_[recompute_height] == bad) {
                        ++recompute_height;
                    }
                } else {
                    // we're pessimistic, recompute everything
                    recompute_height = max_height;
                }
            } else if (KeyIsAfterNode (key_decoded, splice->next_[recompute_height])) {
                // key is from after splice
                if (allow_partial_splice_fix) {
                    Node* bad = splice->next_[recompute_height];
                    while (splice->next_[recompute_height] == bad) {
                        ++recompute_height;
                    }
                } else {
                    recompute_height = max_height;
                }
            } else {
                // this level brackets the key, we won!
                break;
            }
        }
    }
    assert (recompute_height <= max_height);
    if (recompute_height > 0) {
        RecomputeSpliceLevels (key_decoded, splice, recompute_height);
    }

    bool splice_is_valid = true;
    if (UseCAS) {
        for (int i = 0; i < height; ++i) {
            while (true) {
                // Checking for duplicate keys on the level 0 is sufficient
                if (unlikely (i == 0 && splice->next_[i] != nullptr &&
                              compare_ (x->Key (), splice->next_[i]->Key ()) >= 0)) {
                    // duplicate key
                    return false;
                }
                if (unlikely (i == 0 && splice->prev_[i] != head_ &&
                              compare_ (splice->prev_[i]->Key (), x->Key ()) >= 0)) {
                    // duplicate key
                    return false;
                }
                assert (splice->next_[i] == nullptr ||
                        compare_ (x->Key (), splice->next_[i]->Key ()) < 0);
                assert (splice->prev_[i] == head_ ||
                        compare_ (splice->prev_[i]->Key (), x->Key ()) < 0);
                x->NoBarrier_SetNext (i, splice->next_[i]);
                if (splice->prev_[i]->CASNext (i, splice->next_[i], x)) {
                    // success
                    break;
                }
                // CAS failed, we need to recompute prev and next. It is unlikely
                // to be helpful to try to use a different level as we redo the
                // search, because it should be unlikely that lots of nodes have
                // been inserted between prev[i] and next[i]. No point in using
                // next[i] as the after hint, because we know it is stale.
                FindSpliceForLevel<false> (key_decoded, splice->prev_[i], nullptr, i,
                                           &splice->prev_[i], &splice->next_[i]);

                // Since we've narrowed the bracket for level i, we might have
                // violated the Splice constraint between i and i-1.  Make sure
                // we recompute the whole thing next time.
                if (i > 0) {
                    splice_is_valid = false;
                }
            }
        }
    } else {
        // link the splice from level 0 to level height - 1
        for (int i = 0; i < height; ++i) {
            if (i >= recompute_height && splice->prev_[i]->Next (i) != splice->next_[i]) {
                FindSpliceForLevel<false> (key_decoded, splice->prev_[i], nullptr, i,
                                           &splice->prev_[i], &splice->next_[i]);
            }
            // Checking for duplicate keys on the level 0 is sufficient
            if (unlikely (i == 0 && splice->next_[i] != nullptr &&
                          compare_ (x->Key (), splice->next_[i]->Key ()) >= 0)) {
                // duplicate key
                return false;
            }
            if (unlikely (i == 0 && splice->prev_[i] != head_ &&
                          compare_ (splice->prev_[i]->Key (), x->Key ()) >= 0)) {
                // duplicate key
                return false;
            }
            assert (splice->next_[i] == nullptr ||
                    compare_ (x->Key (), splice->next_[i]->Key ()) < 0);
            assert (splice->prev_[i] == head_ ||
                    compare_ (splice->prev_[i]->Key (), x->Key ()) < 0);
            assert (splice->prev_[i]->Next (i) == splice->next_[i]);
            // link prev and next pointer
            x->NoBarrier_SetNext (i, splice->next_[i]);
            splice->prev_[i]->SetNext (i, x);
        }
    }
    if (splice_is_valid) {
        for (int i = 0; i < height; ++i) {
            splice->prev_[i] = x;
        }
        assert (splice->prev_[splice->height_] == head_);
        assert (splice->next_[splice->height_] == nullptr);
        for (int i = 0; i < splice->height_; ++i) {
            assert (splice->next_[i] == nullptr || compare_ (key, splice->next_[i]->Key ()) < 0);
            assert (splice->prev_[i] == head_ || compare_ (splice->prev_[i]->Key (), key) <= 0);
            assert (splice->prev_[i + 1] == splice->prev_[i] || splice->prev_[i + 1] == head_ ||
                    compare_ (splice->prev_[i + 1]->Key (), splice->prev_[i]->Key ()) < 0);
            assert (splice->next_[i + 1] == splice->next_[i] || splice->next_[i + 1] == nullptr ||
                    compare_ (splice->next_[i]->Key (), splice->next_[i + 1]->Key ()) < 0);
        }
    } else {
        splice->height_ = 0;
    }
    return true;
}

template <class Comparator>
bool InlineSkipList<Comparator>::Contains (const char* key) const {
    // Node* x = FindGreaterOrEqual(key);
    // if (x != nullptr && Equal(key, x->Key())) {
    //   return true;
    // } else {
    //   return false;
    // }
    return BufContains (key);
}

template <class Comparator>
bool InlineSkipList<Comparator>::BufContains (const char* key) const {
    DEBUG ("Search key: %ld", *(size_t*)key);
    BufNode* closest_buf = head_->BufNode ();
    Node* x = FindClosestBuf (key, closest_buf);
    const DecodedKey key_decoded = compare_.decode_key (key);
    if (closest_buf->buf.Contains (key_decoded)) {
        return true;
    }

    // search on level 0 ~ level 1
    Node* last_bigger = nullptr;
    int level = kBufNodeLevel;
    while (level >= 0) {
        Node* next = x->Next (level);
        DEBUG ("L %d, Node x 0x%lx: %ld, Node next 0x%lx: %ld, ", level, x,
               x == nullptr ? -1 : *(size_t*)x->Key (), next,
               next == nullptr ? -1 : *(size_t*)next->Key ());
        if (next != nullptr) {
            PREFETCH (next->Next (level), 0, 1);
        }
        // Make sure the lists are sorted
        assert (x == head_ || next == nullptr || KeyIsAfterNode (next->Key (), x));
        int cmp =
            (next == nullptr || next == last_bigger) ? 1 : compare_ (next->Key (), key_decoded);
        if (cmp == 0) {
            return true;
        } else if (cmp < 0) {
            x = next;
        } else {
            last_bigger = next;
            level--;
        }
    }

    if (x != nullptr && Equal (key, x->Key ())) {
        return true;
    } else {
        return false;
    }
}

template <class Comparator>
size_t InlineSkipList<Comparator>::Compact (Node* node /* bufnode */, Node* next_bufnode,
                                            Splice& splice_partition) {
    DEBUG ("Compact node 0x%lx: %ld", node, node == head_ ? -1 : *(size_t*)node->Key ());
    std::vector<std::pair<size_t, int>> keys;

    int total_height = 0;
    int max_height = 0;
    // collect keys in buffer
    BufNode* bufnode = node->BufNode ();
    for (int i = 0; i < buflog::BufVec::kValNum; i++) {
        DEBUG ("collect on buf node %ld", bufnode->buf.keys_[i]);
        int node_h = RandomHeight ();
        keys.push_back ({bufnode->buf.keys_[i], node_h});
        total_height += node_h;
        max_height = std::max (max_height, node_h);
    }

    // collect keys on the skiplist
    Node* cur = node->Next (0);
#ifdef DRAM_CACHE
    cur = node->PmemNode ()->Next (0);
    if (next_bufnode) {
        assert (next_bufnode->isDramNode ());
        next_bufnode = next_bufnode->PmemNode ();
    }
#endif
    int node_within_partition = 0;
    while (cur && cur != next_bufnode) {
        DEBUG ("collect on list node 0x%lx(%d): %ld. next_bufnode 0x%lx(%d): %ld", cur,
               cur->isDramNode (), cur != nullptr ? *(size_t*)cur->Key () : -1, next_bufnode,
               next_bufnode != nullptr ? next_bufnode->isDramNode () : 0,
               next_bufnode != nullptr ? *(size_t*)next_bufnode->Key () : -1);
        DecodedKey key_decoded = compare_.decode_key (cur->Key ());
        int cur_h = RandomHeight ();
        keys.push_back ({key_decoded, cur_h});
        total_height += cur_h;
        cur = cur->Next (0);
        max_height = std::max (max_height, cur_h);
        node_within_partition++;
    }
    if (max_height == 1) {
        // avoid new partition becomes lower than older one
        keys.back ().second++;
        total_height++;
        max_height = 2;
    }
    splice_partition.height_ = max_height;

    // Allocate all the space in pmem
    size_t total_size = total_height * sizeof (std::atomic<Node*>) +
                        (sizeof (NodeMeta) + sizeof (size_t)) * keys.size ();
    char* partition_addr = allocator_.AllocateAligned (total_size);

    // compact the merge keys
    std::sort (keys.begin (), keys.end ());
    size_t klen = keys.size ();
    int cur_height = 0;
    int prev_height = 0;
    char* addr = partition_addr;
    for (size_t i = 0; i < klen; i++) {
        size_t key = keys[i].first;
        int node_h = keys[i].second;

        // create new Node on existing addr
        auto prefix = sizeof (std::atomic<Node*>) * (node_h - 1);
        // char* raw = allocator_.AllocateAligned(prefix + sizeof(Node) + sizeof(NodeMeta) +
        // sizeof(size_t));
        Node* new_node = reinterpret_cast<Node*> (addr + prefix);
        memcpy (const_cast<char*> (new_node->Key ()), &key, sizeof (size_t));
        new_node->StashHeight (node_h);
        DEBUG ("Create node %lu (0x%lx), height: %d\n", key, new_node, node_h);
        if (node_h >= kBufNodeLevel + 1) {
            NodeMeta* node_meta = new_node->Meta ();
            node_meta->bufnode_ptr = (uint64_t) new BufNode ();
            DEBUG ("Create bufnode 0x%lx for %ld", node_meta->bufnode_ptr, key);
#ifdef DRAM_CACHE
            // creaet dram cache for this node
            Node* tmp = AllocateNodeDram (sizeof (size_t), node_h);
            memcpy (const_cast<char*> (tmp->Key ()), &key, sizeof (size_t));
            tmp->SetBufNode (new_node->BufNode ());
            tmp->SetPmemNode (new_node);
            DEBUG (
                "Create node dram cache (0x%lx) for %ld. bufnode: 0x%lx, pmemnode: 0x%lx, pmem "
                "bufnode: 0x%x",
                tmp, key, tmp->BufNode (), tmp->PmemNode (), tmp->PmemNode ()->BufNode ());
            new_node = tmp;
#endif
        }
        addr += prefix + sizeof (Node) + sizeof (NodeMeta) + sizeof (size_t);

        bool is_new_node_dram = new_node->isDramNode ();
        // link this node with previous node
        if (i != 0) {
            int link_height = std::min (cur_height, node_h);
            for (int l = 0; l < link_height; l++) {
#ifdef DRAM_CACHE
                bool is_next_l_dram = splice_partition.next_[l]->isDramNode ();
                if (is_next_l_dram && is_new_node_dram) {
                    // link dram part and pmem part
                    DEBUG (
                        "Connect dram - dram L%d. dram link: node 0x%lx -> node 0x%lx, pmem link: "
                        "node 0x%lx -> node 0x%lx",
                        l, splice_partition.next_[l], new_node,
                        splice_partition.next_[l]->PmemNode (), new_node->PmemNode ());
                    splice_partition.next_[l]->PmemNode ()->SetNextNoFlush (
                        l, new_node->PmemNode ());                            // pmem -> pmem
                    splice_partition.next_[l]->SetNextNoFlush (l, new_node);  // dram -> dram
                } else if (is_next_l_dram) {
                    // only next l is dram
                    DEBUG (
                        "Connect dram - pmem L%d. dram link: node 0x%lx -> node 0x%lx, pmem link: "
                        "node 0x%lx -> node 0x%lx",
                        l, splice_partition.next_[l], new_node,
                        splice_partition.next_[l]->PmemNode (), new_node);
                    splice_partition.next_[l]->PmemNode ()->SetNextNoFlush (
                        l, new_node);                                         // pmem -> pmem
                    splice_partition.next_[l]->SetNextNoFlush (l, new_node);  // dram -> pmem
                } else if (is_new_node_dram) {
                    DEBUG ("Connect pmem - dram L%d. pmem link: node 0x%lx -> node 0x%lx", l,
                           splice_partition.next_[l], new_node->PmemNode ());
                    splice_partition.next_[l]->SetNextNoFlush (
                        l, new_node->PmemNode ());  // pmem -> pmem
                } else {
                    DEBUG ("Connect pmem - pmem L%d. pmem link: node 0x%lx -> node 0x%lx", l,
                           splice_partition.next_[l], new_node);
                    splice_partition.next_[l]->SetNextNoFlush (l, new_node);  // pmem -> pmem
                }
#else
                DEBUG ("link node(0x%lx) next[%ld] -> node (0x%lx). %ld -> %ld",
                       splice_partition.next_[l], l, new_node,
                       *(size_t*)splice_partition.next_[l]->Key (), key);
                splice_partition.next_[l]->SetNextNoFlush (l, new_node);
#endif
            }
        }

        // update splice_partition.prev
        if (node_h > cur_height) {
            for (int l = cur_height; l < node_h; ++l) {
                DEBUG ("update splice_partition.prev_[%d] 0x%lx\n", l, new_node);
                splice_partition.prev_[l] = new_node;
            }
            cur_height = node_h;
        }

        // update splice_partition.next
        for (int l = 0; l < node_h; ++l) {
            DEBUG ("update splice_partition.next_[%d] 0x%lx\n", l, new_node);
            splice_partition.next_[l] = new_node;
        }

        prev_height = node_h;
    }

    clflush (addr, total_size);
    sfence ();

    return keys[0].first;
}

template <class Comparator>
void InlineSkipList<Comparator>::TEST_Validate () const {
    // Interate over all levels at the same time, and verify nodes appear in
    // the right order, and nodes appear in upper level also appear in lower
    // levels.
    Node* nodes[kMaxPossibleHeight];
    int max_height = GetMaxHeight ();
    assert (max_height > 0);
    for (int i = 0; i < max_height; i++) {
        nodes[i] = head_;
    }
    while (nodes[0] != nullptr) {
        Node* l0_next = nodes[0]->Next (0);
        if (l0_next == nullptr) {
            break;
        }
        assert (nodes[0] == head_ || compare_ (nodes[0]->Key (), l0_next->Key ()) < 0);
        nodes[0] = l0_next;

        int i = 1;
        while (i < max_height) {
            Node* next = nodes[i]->Next (i);
            if (next == nullptr) {
                break;
            }
            auto cmp = compare_ (nodes[0]->Key (), next->Key ());
            assert (cmp <= 0);
            if (cmp == 0) {
                assert (next == nodes[0]);
                nodes[i] = next;
            } else {
                break;
            }
            i++;
        }
    }
    for (int i = 1; i < max_height; i++) {
        assert (nodes[i] != nullptr && nodes[i]->Next (i) == nullptr);
    }
}

}  // namespace ROCKSDB_NAMESPACE

#endif
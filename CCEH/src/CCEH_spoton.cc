#include "CCEH_spoton.h"

#include <stdio.h>

#include <bitset>
#include <cassert>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hash.h"
#include "logger.h"
#include "spoton_log.h"
#include "util.h"

// #define CONFIG_OUT_OF_PLACE_MERGE

#define f_seed 0xc70697UL
#define s_seed 0xc70697UL
//#define f_seed 0xc70f6907UL
//#define s_seed 0xc70f6907UL

using namespace std;

void Segment::execute_path (PMEMobjpool* pop, vector<pair<size_t, size_t>>& path, Key_t& key,
                            Value_t value) {
    for (int i = path.size () - 1; i > 0; --i) {
        bucket[path[i].first] = bucket[path[i - 1].first];
        pmemobj_persist (pop, (char*)&bucket[path[i].first], sizeof (Pair));
    }
    bucket[path[0].first].value = value;
    mfence ();
    bucket[path[0].first].key = key;
    pmemobj_persist (pop, (char*)&bucket[path[0].first], sizeof (Pair));
}

void Segment::execute_path (vector<pair<size_t, size_t>>& path, Pair _bucket) {
    int i = 0;
    int j = (i + 1) % 2;

    Pair temp[2];
    temp[0] = _bucket;
    for (auto p : path) {
        temp[j] = bucket[p.first];
        bucket[p.first] = temp[i];
        i = (i + 1) % 2;
        j = (i + 1) % 2;
    }
}

vector<pair<size_t, size_t>> Segment::find_path (size_t target, size_t pattern) {
    vector<pair<size_t, size_t>> path;
    path.reserve (kCuckooThreshold);
    path.emplace_back (target, bucket[target].key);

    auto cur = target;
    size_t i = 0;

    do {
        Key_t* key = &bucket[cur].key;
        auto f_hash = hash_funcs[0](key, sizeof (Key_t), f_seed);
        auto s_hash = hash_funcs[2](key, sizeof (Key_t), s_seed);

        if ((f_hash >> (8 * sizeof (f_hash) - local_depth)) != pattern || *key == INVALID) {
            break;
        }

        for (size_t j = 0; j < kNumPairPerCacheLine * kNumCacheLine; ++j) {
            auto f_idx = (((f_hash & kMask) * kNumPairPerCacheLine) + j) % kNumSlot;
            auto s_idx = (((s_hash & kMask) * kNumPairPerCacheLine) + j) % kNumSlot;

            if (f_idx == cur) {
                path.emplace_back (s_idx, bucket[s_idx].key);
                cur = s_idx;
                break;
            } else if (s_idx == cur) {
                path.emplace_back (f_idx, bucket[f_idx].key);
                cur = f_idx;
                break;
            }
        }
        ++i;
    } while (i < kCuckooThreshold);

    if (i == kCuckooThreshold) {
        path.resize (0);
    }

    return (path);
}

bool Segment::Insert4split (Key_t& key, Value_t value, size_t loc) {
    for (size_t i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
        auto slot = (loc + i) % kNumSlot;
        if (bucket[slot].key == INVALID) {
            bucket[slot].key = key;
            bucket[slot].value = value;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief return the new segment that is created by spliting
 *
 * @param iter
 * @return Segment*
 */
Segment* Segment::SplitDram (WriteBuffer::Iterator& iter) {
    Segment* split = new Segment ();
    // splits[0].initSegment(local_depth+1); //   old segment
    split->initSegment (local_depth + 1);  // split segment

    auto pattern = ((size_t)1 << (sizeof (Key_t) * 8 - local_depth - 1));

    for (size_t i = 0; i < kNumSlot; ++i) {
        auto f_hash = h (&bucket[i].key, sizeof (Key_t));
        if (f_hash & pattern) {
            if (!split->Insert4split (bucket[i].key, bucket[i].value,
                                      (f_hash & kMask) * kNumPairPerCacheLine)) {
                auto s_hash = hash_funcs[2](&bucket[i].key, sizeof (Key_t), s_seed);
                if (!split->Insert4split (bucket[i].key, bucket[i].value,
                                          (s_hash & kMask) * kNumPairPerCacheLine)) {
                    INFO ("S hash 1 insert split segment fail");
                }
            }
            // invalidate the migrated key
            bucket[i].key = INVALID;
        }
    }

    while (iter.Valid ()) {
        auto& kv = *iter;
        Key_t key = kv.key;
        Value_t val = kv.val;
        auto f_hash = h (&key, sizeof (Key_t));
        if (f_hash & pattern) {
            // insert to split segment
            if (!split->Insert4split (key, val, (f_hash & kMask) * kNumPairPerCacheLine)) {
                auto s_hash = hash_funcs[2](&key, sizeof (Key_t), s_seed);
                if (!split->Insert4split (key, val, (s_hash & kMask) * kNumPairPerCacheLine)) {
                    INFO ("S hash iter 1 insert fail.");
                }
            }
        } else {
            // insert to this original segment
            if (!Insert4split (key, val, (f_hash & kMask) * kNumPairPerCacheLine)) {
                auto s_hash = hash_funcs[2](&key, sizeof (Key_t), s_seed);
                if (!Insert4split (key, val, (s_hash & kMask) * kNumPairPerCacheLine)) {
                    INFO ("S hash iter 0 insert fail");
                }
            }
        }
        ++iter;
    }

    return split;
}

TOID (struct Segment) * Segment::Split (PMEMobjpool* pop) {
    TOID (struct Segment)* split = new TOID (struct Segment)[2];
    POBJ_ALLOC (pop, &split[0], struct Segment, sizeof (struct Segment), NULL, NULL);
    POBJ_ALLOC (pop, &split[1], struct Segment, sizeof (struct Segment), NULL, NULL);
    D_RW (split[0])->initSegment (local_depth + 1);
    D_RW (split[1])->initSegment (local_depth + 1);

    auto pattern = ((size_t)1 << (sizeof (Key_t) * 8 - local_depth - 1));
    for (size_t i = 0; i < kNumSlot; ++i) {
        auto f_hash = h (&bucket[i].key, sizeof (Key_t));
        if (f_hash & pattern) {
            D_RW (split[1])->Insert4split (bucket[i].key, bucket[i].value,
                                           (f_hash & kMask) * kNumPairPerCacheLine);
        } else {
            D_RW (split[0])->Insert4split (bucket[i].key, bucket[i].value,
                                           (f_hash & kMask) * kNumPairPerCacheLine);
        }
    }

    pmemobj_persist (pop, (char*)D_RO (split[0]), sizeof (struct Segment));
    pmemobj_persist (pop, (char*)D_RO (split[1]), sizeof (struct Segment));

    return split;
}

void CCEH::initCCEH (PMEMobjpool* pop) {
    crashed = true;
#ifdef CONFIG_DRAM_INNER
    dir_dram = new Directory ();
#else
    POBJ_ALLOC (pop, &dir_pmem, struct Directory, sizeof (struct Directory), NULL, NULL);
#endif
    dir ()->initDirectory ();
    POBJ_ALLOC (pop, &dir ()->segment, TOID (struct Segment),
                sizeof (TOID (struct Segment)) * dir ()->capacity, NULL, NULL);

    dir ()->bufnodes =
        reinterpret_cast<WriteBuffer**> (malloc (sizeof (WriteBuffer*) * dir ()->capacity));

    for (size_t i = 0; i < dir ()->capacity; ++i) {
        POBJ_ALLOC (pop, &D_RO (dir ()->segment)[i], struct Segment, sizeof (struct Segment), NULL,
                    NULL);
        D_RW (D_RW (dir ()->segment)[i])->initSegment ();
        dir ()->bufnodes[i] = new WriteBuffer ();
    }
}

void CCEH::initCCEH (PMEMobjpool* pop, size_t initCap) {
    crashed = true;
#ifdef CONFIG_DRAM_INNER
    dir_dram = new Directory ();
#else
    POBJ_ALLOC (pop, &dir_pmem, struct Directory, sizeof (struct Directory), NULL, NULL);
#endif
    dir ()->initDirectory (static_cast<size_t> (log2 (initCap)));
    POBJ_ALLOC (pop, &dir ()->segment, TOID (struct Segment),
                sizeof (TOID (struct Segment)) * dir ()->capacity, NULL, NULL);

    dir ()->bufnodes =
        reinterpret_cast<WriteBuffer**> (malloc (sizeof (WriteBuffer*) * dir ()->capacity));

    for (size_t i = 0; i < dir ()->capacity; ++i) {
        POBJ_ALLOC (pop, &D_RO (dir ()->segment)[i], struct Segment, sizeof (struct Segment), NULL,
                    NULL);
        D_RW (D_RW (dir ()->segment)[i])->initSegment (static_cast<size_t> (log2 (initCap)));
        dir ()->bufnodes[i] =
            new WriteBuffer (static_cast<size_t> (log2 (initCap)), kWriteBufferSize / 256);
    }
}

void CCEH::Insert (PMEMobjpool* pop, Key_t& key, Value_t value) {
    auto f_hash = hash_funcs[0](&key, sizeof (Key_t), f_seed);

retry:
    auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    auto target = D_RO (dir ()->segment)[x];

    auto* bufnode = dir ()->bufnodes[x];
    bufnode->Lock ();

    if (D_RO (target)->local_depth != bufnode->local_depth) {
        bufnode->Unlock ();
        std::this_thread::yield ();
        goto retry;
    }

    bool res = bufnode->Put (key, (char*)value);
    if (res) {
#ifdef CONFIG_LOG
        auto* log_ptr = spoton::Log_t::GetThreadLocalLog ();
        log_ptr->Append (spoton::kLogNodeValid, key, (size_t)value, spoton::logptr_nullptr);
#endif
        // successfully insert to bufnode
        bufnode->Unlock ();
    } else {
        // bufnode is full. merge to CCEH
#ifndef CONFIG_OUT_OF_PLACE_MERGE
        auto iter = bufnode->Begin ();
        while (iter.Valid ()) {
            auto& kv = *iter;
            Key_t ikey = kv.key;
            Value_t ival = kv.val;
            insert (pop, ikey, ival, false);
            ++iter;
        }
        bufnode->Reset ();
        bufnode->Unlock ();
        goto retry;
#else
        mergeBufAndSplitWhenNeeded (pop, bufnode, target, f_hash);
        bufnode->Unlock ();
        goto retry;
#endif
    }
}

void CCEH::insert (PMEMobjpool* pop, Key_t& key, Value_t value, bool with_lock) {
    auto f_hash = hash_funcs[0](&key, sizeof (Key_t), f_seed);
    auto f_idx = (f_hash & kMask) * kNumPairPerCacheLine;

RETRY:
    auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    auto target = D_RO (dir ()->segment)[x];

    if (!D_RO (target)) {
        std::this_thread::yield ();
        goto RETRY;
    }

    if (with_lock) {
        /* acquire segment exclusive lock */
        if (!D_RW (target)->lock ()) {
            std::this_thread::yield ();
            goto RETRY;
        }
    }

    auto target_check = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    if (D_RO (target) != D_RO (D_RO (dir ()->segment)[target_check])) {
        if (with_lock) D_RW (target)->unlock ();
        std::this_thread::yield ();
        goto RETRY;
    }

    auto pattern = (f_hash >> (8 * sizeof (f_hash) - D_RO (target)->local_depth));
    for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
        auto loc = (f_idx + i) % Segment::kNumSlot;
        auto _key = D_RO (target)->bucket[loc].key;
        /* validity check for entry keys */
        if ((((hash_funcs[0](&D_RO (target)->bucket[loc].key, sizeof (Key_t), f_seed) >>
               (8 * sizeof (f_hash) - D_RO (target)->local_depth)) != pattern) ||
             (D_RO (target)->bucket[loc].key == INVALID)) &&
            (D_RO (target)->bucket[loc].key != SENTINEL)) {
            if (CAS (&D_RW (target)->bucket[loc].key, &_key, SENTINEL)) {
                D_RW (target)->bucket[loc].value = value;
                mfence ();
                D_RW (target)->bucket[loc].key = key;
                pmemobj_persist (pop, (char*)&D_RO (target)->bucket[loc], sizeof (Pair));
                /* release segment exclusive lock */
                if (with_lock) D_RW (target)->unlock ();
                return;
            }
        }
    }

    auto s_hash = hash_funcs[2](&key, sizeof (Key_t), s_seed);
    auto s_idx = (s_hash & kMask) * kNumPairPerCacheLine;

    for (unsigned i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
        auto loc = (s_idx + i) % Segment::kNumSlot;
        auto _key = D_RO (target)->bucket[loc].key;
        if ((((hash_funcs[0](&D_RO (target)->bucket[loc].key, sizeof (Key_t), f_seed) >>
               (8 * sizeof (s_hash) - D_RO (target)->local_depth)) != pattern) ||
             (D_RO (target)->bucket[loc].key == INVALID)) &&
            (D_RO (target)->bucket[loc].key != SENTINEL)) {
            if (CAS (&D_RW (target)->bucket[loc].key, &_key, SENTINEL)) {
                D_RW (target)->bucket[loc].value = value;
                mfence ();
                D_RW (target)->bucket[loc].key = key;
                pmemobj_persist (pop, (char*)&D_RO (target)->bucket[loc], sizeof (Pair));
                if (with_lock) D_RW (target)->unlock ();
                return;
            }
        }
    }

    auto target_local_depth = D_RO (target)->local_depth;
    // COLLISION !!
    /* need to split segment but release the exclusive lock first to avoid deadlock */
    if (with_lock) D_RW (target)->unlock ();

    if (with_lock && !D_RW (target)->suspend ()) {
        std::this_thread::yield ();
        goto RETRY;
    }

    /* need to check whether the target segment has been split */
    if (target_local_depth != D_RO (D_RO (dir ()->segment)[x])->local_depth) {
        D_RW (target)->sema = 0;
        std::this_thread::yield ();
        goto RETRY;
    }

    TOID (struct Segment)* s = D_RW (target)->Split (pop);

    // create new Writebuffer for split_segment_bufnode
    WriteBuffer* split_segment_bufnode =
        new WriteBuffer (D_RW (s[1])->local_depth, kWriteBufferSize / 256);
    // update new_segment_bufnode local depth
    WriteBuffer* segment_bufnode = dir ()->bufnodes[x];
    segment_bufnode->local_depth = D_RW (s[0])->local_depth;

DIR_RETRY:
    /* need to double the directory */
    if (D_RO (target)->local_depth == dir ()->depth) {
        if (!dir ()->suspend ()) {
            std::this_thread::yield ();
            goto DIR_RETRY;
        }
        printf ("Double dir\n");

        x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
        // auto dir_old = dir;
        TOID_ARRAY (TOID (struct Segment)) d = dir ()->segment;
#ifdef CONFIG_DRAM_INNER
        Directory* _dir = new Directory ();
#else
        TOID (struct Directory) _dir_pmem;
        POBJ_ALLOC (pop, &_dir_pmem, struct Directory, sizeof (struct Directory), NULL, NULL);
        Directory* _dir = D_RW (_dir_pmem);
#endif
        POBJ_ALLOC (pop, &_dir->segment, TOID (struct Segment),
                    sizeof (TOID (struct Segment)) * dir ()->capacity * 2, NULL, NULL);
        _dir->initDirectory (dir ()->depth + 1);
        _dir->bufnodes =
            reinterpret_cast<WriteBuffer**> (malloc (sizeof (WriteBuffer*) * dir ()->capacity * 2));

        for (size_t i = 0; i < dir ()->capacity; ++i) {
            if (i == x) {
                D_RW (_dir->segment)[2 * i] = s[0];
                D_RW (_dir->segment)[2 * i + 1] = s[1];
                _dir->bufnodes[2 * i] = segment_bufnode;
                _dir->bufnodes[2 * i + 1] = split_segment_bufnode;
            } else {
                D_RW (_dir->segment)[2 * i] = D_RO (d)[i];
                D_RW (_dir->segment)[2 * i + 1] = D_RO (d)[i];
                _dir->bufnodes[2 * i] = dir ()->bufnodes[i];
                _dir->bufnodes[2 * i + 1] = dir ()->bufnodes[i];
            }
        }

        pmemobj_persist (pop, (char*)&D_RO (_dir->segment)[0],
                         sizeof (TOID (struct Segment)) * _dir->capacity);
        pmemobj_persist (pop, (char*)&_dir, sizeof (struct Directory));
        SetDir (_dir);
#ifdef CONFIG_DRAM_INNER
#else
        pmemobj_persist (pop, (char*)&dir_pmem, sizeof (TOID (struct Directory)));
#endif

        /* TBD */
        // POBJ_FREE(&dir_old);

    } else {  // normal split
        while (!dir ()->lock ()) {
            asm("nop");
        }
        x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
        if (dir ()->depth == D_RO (target)->local_depth + 1) {
            if (x % 2 == 0) {
                D_RW (dir ()->segment)[x + 1] = s[1];
                mfence ();
                D_RW (dir ()->segment)[x] = s[0];
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x],
                                 sizeof (TOID (struct Segment)) * 2);

                dir ()->bufnodes[x] = segment_bufnode;
                dir ()->bufnodes[x + 1] = split_segment_bufnode;
            } else {
                D_RW (dir ()->segment)[x] = s[1];
                mfence ();
                D_RW (dir ()->segment)[x - 1] = s[0];
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x - 1],
                                 sizeof (TOID (struct Segment)) * 2);

                dir ()->bufnodes[x] = split_segment_bufnode;
                dir ()->bufnodes[x - 1] = segment_bufnode;
            }
            dir ()->unlock ();
        } else {
            int stride = pow (2, dir ()->depth - target_local_depth);
            auto loc = x - (x % stride);
            for (int i = 0; i < stride / 2; ++i) {
                D_RW (dir ()->segment)[loc + stride / 2 + i] = s[1];
                dir ()->bufnodes[loc + stride / 2 + i] = split_segment_bufnode;
            }
            for (int i = 0; i < stride / 2; ++i) {
                D_RW (dir ()->segment)[loc + i] = s[0];
                dir ()->bufnodes[loc + i] = segment_bufnode;
            }

            pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[loc],
                             sizeof (TOID (struct Segment)) * stride);
            dir ()->unlock ();
        }
    }
    std::this_thread::yield ();
    goto RETRY;
}

void CCEH::mergeBufAndSplitWhenNeeded (PMEMobjpool* pop, WriteBuffer* bufnode, Segment_toid& target,
                                       size_t f_hash) {
    // bufnode has already been locked
    // out of place merge

    // step 1. copy old segment from pmem to dram
    Segment old_segment_dram;
    memcpy (&old_segment_dram, D_RW (target), sizeof (Segment));

    // step 2. migrate bufnode's kv to new_segment_dram
    auto iter = bufnode->Begin ();
    int inserted = 0;
    while (iter.Valid ()) {
        inserted++;
        auto& kv = *iter;
        Key_t key = kv.key;
        Value_t val = kv.val;
        auto f_hash = h (&key, sizeof (Key_t));
        if (!old_segment_dram.Insert4split (key, val, (f_hash & kMask) * kNumPairPerCacheLine)) {
            // insert fail, the new segment is full. we need to split it
            // INFO("Insert %d record, prepare to split", inserted);
            break;
        }
        ++iter;
    }

    if (iter.Valid ()) {  // we have to split new_segment_dram

        auto target_local_depth = D_RO (target)->local_depth;
        // INFO("Split segment %lu. depth %lu, ", x, target_local_depth);

        // step 1. Split the dram segment
        Segment* split = old_segment_dram.SplitDram (iter);
        Segment* split_segment_dram = split;
        Segment* new_segment_dram = &old_segment_dram;
        new_segment_dram->local_depth = split_segment_dram->local_depth;

        // step 2. Copy dram version to pmem
        TOID (struct Segment) split_segment;
        POBJ_ALLOC (pop, &split_segment, struct Segment, sizeof (struct Segment), NULL, NULL);
        // persist the split segment
        pmemobj_memcpy (pop, D_RW (split_segment), split_segment_dram, sizeof (struct Segment),
                        PMEMOBJ_F_MEM_NONTEMPORAL);
        TOID (struct Segment) new_segment;
        POBJ_ALLOC (pop, &new_segment, struct Segment, sizeof (struct Segment), NULL, NULL);
        // persist the new segment after split
        pmemobj_memcpy (pop, D_RW (new_segment), new_segment_dram, sizeof (struct Segment),
                        PMEMOBJ_F_MEM_NONTEMPORAL);
        pmemobj_drain (pop);

        // create new Writebuffer for split_segment_bufnode
        WriteBuffer* split_segment_bufnode =
            new WriteBuffer (split_segment_dram->local_depth, kWriteBufferSize / 256);
        // update new_segment_bufnode local depth
        auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
        WriteBuffer* new_segment_bufnode = dir ()->bufnodes[x];
        new_segment_bufnode->local_depth = new_segment_dram->local_depth;

        // step 3. Set the directory
    MERGE_SPLIT_RETRY:
        if (D_RO (target)->local_depth == dir ()->depth) {  // need double the directory
            if (!dir ()->suspend ()) {
                // INFO("Double directory conflicts");
                // other thread is doubling the directory
                std::this_thread::yield ();
                goto MERGE_SPLIT_RETRY;
            }
            printf ("Double Directory\n");
            INFO ("Double Directory Begin\n");

            // begin doubling
            TOID_ARRAY (TOID (struct Segment)) d = dir ()->segment;
#ifdef CONFIG_DRAM_INNER
            Directory* _dir = new Directory ();
#else
            TOID (struct Directory) _dir_pmem;
            POBJ_ALLOC (pop, &_dir_pmem, struct Directory, sizeof (struct Directory), NULL, NULL);
            Directory* _dir = D_RW (_dir_pmem);
#endif
            POBJ_ALLOC (pop, &_dir->segment, TOID (struct Segment),
                        sizeof (TOID (struct Segment)) * dir ()->capacity * 2, NULL, NULL);
            _dir->initDirectory (dir ()->depth + 1);

            _dir->bufnodes = reinterpret_cast<WriteBuffer**> (
                malloc (sizeof (WriteBuffer*) * dir ()->capacity * 2));

            auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
            for (size_t i = 0; i < dir ()->capacity; ++i) {
                if (i == x) {
                    D_RW (_dir->segment)[2 * i] = new_segment;
                    D_RW (_dir->segment)[2 * i + 1] = split_segment;

                    _dir->bufnodes[2 * i] = new_segment_bufnode;
                    _dir->bufnodes[2 * i + 1] = split_segment_bufnode;

                } else {
                    D_RW (_dir->segment)[2 * i] = D_RO (d)[i];
                    D_RW (_dir->segment)[2 * i + 1] = D_RO (d)[i];

                    _dir->bufnodes[2 * i] = dir ()->bufnodes[i];
                    _dir->bufnodes[2 * i + 1] = dir ()->bufnodes[i];

                    DEBUG (
                        "Double directory segment %lu: 0x%lx. move segment 0x%lx to dir %lu and "
                        "dir %lu",
                        x, D_RW (target), D_RW (D_RW (d)[i]), 2 * i, 2 * i + 1);
                }
            }

            pmemobj_flush (pop, (char*)&D_RO (_dir->segment)[0],
                           sizeof (TOID (struct Segment)) * _dir->capacity);
            pmemobj_flush (pop, (char*)&_dir, sizeof (struct Directory));
            pmemobj_drain (pop);
            SetDir (_dir);
#ifdef CONFIG_DRAM_INNER
#else
            pmemobj_persist (pop, (char*)&dir_pmem, sizeof (TOID (struct Directory)));
#endif
        } else {  // normal split
            while (!dir ()->lock ()) {
                asm("nop");
            }
            // Here other thread may change the dir by doubling
            // We need to recalculate x
            auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
            if (dir ()->depth == D_RO (target)->local_depth + 1) {
                if (x % 2 == 0) {
                    D_RW (dir ()->segment)[x + 1] = split_segment;
                    pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x + 1],
                                     sizeof (TOID (struct Segment)));

                    mfence ();
                    D_RW (dir ()->segment)[x] = new_segment;
                    pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x],
                                     sizeof (TOID (struct Segment)) * 2);

                    dir ()->bufnodes[x] = new_segment_bufnode;
                    dir ()->bufnodes[x + 1] = split_segment_bufnode;

                } else {
                    D_RW (dir ()->segment)[x] = split_segment;
                    pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x],
                                     sizeof (TOID (struct Segment)));

                    mfence ();

                    D_RW (dir ()->segment)[x - 1] = new_segment;
                    pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x - 1],
                                     sizeof (TOID (struct Segment)) * 2);

                    dir ()->bufnodes[x] = split_segment_bufnode;
                    dir ()->bufnodes[x - 1] = new_segment_bufnode;
                }
            } else {
                // there are multiple dir entries pointing to split segment
                int stride = pow (2, dir ()->depth - target_local_depth);
                // INFO("Split stride %d ", stride);
                auto loc = x - (x % stride);
                for (int i = 0; i < stride / 2; ++i) {
                    D_RW (dir ()->segment)[loc + stride / 2 + i] = split_segment;
                    dir ()->bufnodes[loc + stride / 2 + i] = split_segment_bufnode;
                }
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[loc + stride / 2],
                                 sizeof (TOID (struct Segment)) * stride / 2);

                for (int i = 0; i < stride / 2; ++i) {
                    D_RW (dir ()->segment)[loc + i] = new_segment;
                    dir ()->bufnodes[loc + i] = new_segment_bufnode;
                }
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[loc],
                                 sizeof (TOID (struct Segment)) * stride);
            }

            dir ()->unlock ();
        }

    } else {  // all records in bufnode has been merge to new_segment_dram

        TOID (struct Segment) new_segment;
        POBJ_ALLOC (pop, &new_segment, struct Segment, sizeof (struct Segment), NULL, NULL);
        // persist the new segment
        pmemobj_memcpy (pop, D_RW (new_segment), &old_segment_dram, sizeof (Segment),
                        PMEMOBJ_F_MEM_NONTEMPORAL);
        // wait for directory lock. Replace old segment with new segment
        while (!dir ()->lock ()) {
            asm("nop");
        }

        auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));

        if (dir ()->depth == D_RO (target)->local_depth) {
            // only need to update one dir entry
            // INFO("Update one directory %lu", x);
            D_RW (dir ()->segment)[x] = new_segment;
            pmemobj_persist (pop, (char*)&D_RW (dir ()->segment)[x],
                             sizeof (TOID (struct Segment)));
        } else if (dir ()->depth == D_RO (target)->local_depth + 1) {
            if (x % 2 == 0) {
                // INFO("Merge. Update two directory %lu, %lu. Segment %lu depth: %u, dir depth:
                // %lu", x, x+1, x, D_RO(target)->local_depth, D_RO(dir)->depth);
                D_RW (dir ()->segment)[x + 1] = new_segment;
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x + 1],
                                 sizeof (TOID (struct Segment)));
                mfence ();
                D_RW (dir ()->segment)[x] = new_segment;
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x],
                                 sizeof (TOID (struct Segment)) * 2);
            } else {
                // INFO("Merge. Update two directory %lu, %lu. Segment %lu depth: %u, dir depth:
                // %lu", x, x-1, x, D_RO(target)->local_depth, D_RO(dir)->depth);
                D_RW (dir ()->segment)[x] = new_segment;
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x],
                                 sizeof (TOID (struct Segment)));
                mfence ();
                D_RW (dir ()->segment)[x - 1] = new_segment;
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[x - 1],
                                 sizeof (TOID (struct Segment)) * 2);
            }
        } else {
            // there are multiple dir entries pointing to this segment
            // INFO("Update multiple directory");
            int stride = pow (2, dir ()->depth - D_RO (target)->local_depth);
            auto loc = x - (x % stride);
            for (int i = 0; i < stride / 2; ++i) {
                auto new_x = loc + stride / 2 + i;
                // INFO("Update multiple directory %lu", new_x);
                D_RW (dir ()->segment)[new_x] = new_segment;
            }
            pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[loc + stride / 2],
                             sizeof (TOID (struct Segment)) * stride / 2);
        }

        dir ()->unlock ();
    }

    bufnode->Reset ();
}

bool CCEH::Delete (Key_t& key) { return false; }

Value_t CCEH::Get (Key_t& key) {
    auto f_hash = hash_funcs[0](&key, sizeof (Key_t), f_seed);
RETRY:
    while (dir ()->sema < 0) {
        asm("nop");
    }
    auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    auto target = D_RO (dir ()->segment)[x];
    if (!D_RO (target)) {
        std::this_thread::yield ();
        goto RETRY;
    }

    char* val;
    auto res = dir ()->bufnodes[x]->Get (key, val);
    if (res) {
        return Value_t (val);
    } else {
        return get (key);
    }
}

Value_t CCEH::get (Key_t& key) {
    auto f_hash = hash_funcs[0](&key, sizeof (Key_t), f_seed);
    auto f_idx = (f_hash & kMask) * kNumPairPerCacheLine;

RETRY:
    while (dir ()->sema < 0) {
        asm("nop");
    }

    auto x = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    auto target = D_RO (dir ()->segment)[x];

    if (!D_RO (target)) {
        std::this_thread::yield ();
        goto RETRY;
    }

    auto target_check = (f_hash >> (8 * sizeof (f_hash) - dir ()->depth));
    if (D_RO (target) != D_RO (D_RO (dir ()->segment)[target_check])) {
        D_RW (target)->unlock ();
        std::this_thread::yield ();
        goto RETRY;
    }

    for (size_t i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
        auto loc = (f_idx + i) % Segment::kNumSlot;
        if (D_RO (target)->bucket[loc].key == key) {
            Value_t v = D_RO (target)->bucket[loc].value;
            return v;
        }
    }

    auto s_hash = hash_funcs[2](&key, sizeof (Key_t), s_seed);
    auto s_idx = (s_hash & kMask) * kNumPairPerCacheLine;
    for (size_t i = 0; i < kNumPairPerCacheLine * kNumCacheLine; ++i) {
        auto loc = (s_idx + i) % Segment::kNumSlot;
        if (D_RO (target)->bucket[loc].key == key) {
            Value_t v = D_RO (target)->bucket[loc].value;
            return v;
        }
    }

    DEBUG ("Not find key: %lu", key);
    return NONE;
}

void CCEH::Recovery (PMEMobjpool* pop) {
    size_t i = 0;
    while (i < dir ()->capacity) {
        size_t depth_cur = D_RO (D_RO (dir ()->segment)[i])->local_depth;
        size_t stride = pow (2, dir ()->depth - depth_cur);
        size_t buddy = i + stride;
        if (buddy == dir ()->capacity) break;
        for (size_t j = buddy - 1; i < j; j--) {
            if (D_RO (D_RO (dir ()->segment)[j])->local_depth != depth_cur) {
                D_RW (dir ()->segment)[j] = D_RO (dir ()->segment)[i];
                pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[i],
                                 sizeof (TOID (struct Segment)));
            }
        }
        i += stride;
    }
    pmemobj_persist (pop, (char*)&D_RO (dir ()->segment)[0],
                     sizeof (TOID (struct Segment)) * dir ()->capacity);
}

double CCEH::Utilization (void) {
    size_t sum = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < dir ()->capacity; ++cnt) {
        auto target = D_RO (dir ()->segment)[i];
        int stride = pow (2, dir ()->depth - D_RO (target)->local_depth);
        auto pattern = (i >> (dir ()->depth - D_RO (target)->local_depth));
        for (unsigned j = 0; j < Segment::kNumSlot; ++j) {
            auto f_hash = h (&D_RO (target)->bucket[j].key, sizeof (Key_t));
            if (((f_hash >> (8 * sizeof (f_hash) - D_RO (target)->local_depth)) == pattern) &&
                (D_RO (target)->bucket[j].key != INVALID)) {
                sum++;
            }
        }
        i += stride;
    }
    return ((double)sum) / ((double)cnt * Segment::kNumSlot) * 100.0;
}

size_t CCEH::Capacity (void) {
    size_t cnt = 0;
    for (size_t i = 0; i < dir ()->capacity; cnt++) {
        auto target = D_RO (dir ()->segment)[i];
        int stride = pow (2, dir ()->depth - D_RO (target)->local_depth);
        i += stride;
    }

    return cnt * Segment::kNumSlot;
}

// for debugging
Value_t CCEH::FindAnyway (Key_t& key) {
    for (size_t i = 0; i < dir ()->capacity; ++i) {
        for (size_t j = 0; j < Segment::kNumSlot; ++j) {
            if (D_RO (D_RO (dir ()->segment)[i])->bucket[j].key == key) {
                cout << "segment(" << i << ")" << endl;
                cout << "global_depth(" << dir ()->depth << "), local_depth("
                     << D_RO (D_RO (dir ()->segment)[i])->local_depth << ")" << endl;
                cout << "pattern: "
                     << bitset<sizeof (int64_t)> (
                            i >> (dir ()->depth - D_RO (D_RO (dir ()->segment)[i])->local_depth))
                     << endl;
                cout << "Key MSB: "
                     << bitset<sizeof (int64_t)> (
                            h (&key, sizeof (key)) >>
                            (8 * sizeof (key) - D_RO (D_RO (dir ()->segment)[i])->local_depth))
                     << endl;
                return D_RO (D_RO (dir ()->segment)[i])->bucket[j].value;
            }
        }
    }
    return NONE;
}

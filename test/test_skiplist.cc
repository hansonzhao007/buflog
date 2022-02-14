//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <cstring>
#include <set>
#include <unordered_set>

#include "Skiplist/inlineskiplist_spoton.h"
#include "Skiplist/port_posix.h"
#include "gtest/gtest.h"

namespace ROCKSDB_NAMESPACE {

// Our test skip list stores 8-byte unsigned integers
typedef uint64_t Key;

static const char* Encode (const uint64_t* key) { return reinterpret_cast<const char*> (key); }

static Key Decode (const char* key) {
    Key rv;
    memcpy (&rv, key, sizeof (Key));
    return rv;
}

struct TestComparator {
    typedef Key DecodedType;

    static DecodedType decode_key (const char* b) { return Decode (b); }

    int operator() (const char* a, const char* b) const {
        if (Decode (a) < Decode (b)) {
            return -1;
        } else if (Decode (a) > Decode (b)) {
            return +1;
        } else {
            return 0;
        }
    }

    int operator() (const char* a, const DecodedType b) const {
        if (Decode (a) < b) {
            return -1;
        } else if (Decode (a) > b) {
            return +1;
        } else {
            return 0;
        }
    }
};

typedef InlineSkipList<TestComparator> TestInlineSkipList;

class InlineSkipTest : public testing::Test {
public:
    void Insert (TestInlineSkipList* list, Key key) {
        char* buf = list->AllocateKey (sizeof (Key));
        memcpy (buf, &key, sizeof (Key));
        list->Insert (buf);
        keys_.insert (key);
    }

    bool InsertWithHint (TestInlineSkipList* list, Key key, void** hint) {
        char* buf = list->AllocateKey (sizeof (Key));
        memcpy (buf, &key, sizeof (Key));
        bool res = list->InsertWithHint (buf, hint);
        keys_.insert (key);
        return res;
    }

    void Validate (TestInlineSkipList* list) {
        // Check keys exist.
        for (Key key : keys_) {
            ASSERT_TRUE (list->Contains (Encode (&key)));
        }
        // Iterate over the list, make sure keys appears in order and no extra
        // keys exist.
        TestInlineSkipList::Iterator iter (list);
        ASSERT_FALSE (iter.Valid ());
        Key zero = 0;
        iter.Seek (Encode (&zero));
        for (Key key : keys_) {
            ASSERT_TRUE (iter.Valid ());
            ASSERT_EQ (key, Decode (iter.key ()));
            iter.Next ();
        }
        ASSERT_FALSE (iter.Valid ());
        // Validate the list is well-formed.
        list->TEST_Validate ();
    }

private:
    std::set<Key> keys_;
};

// TEST_F(InlineSkipTest, Empty) {
//   TestComparator cmp;
//   InlineSkipList<TestComparator>* list(InlineSkipList<TestComparator>::CreateSkiplist(cmp));
//   Key key = 10;
//   ASSERT_TRUE(!list->Contains(Encode(&key)));

//   InlineSkipList<TestComparator>::Iterator iter(list);
//   ASSERT_TRUE(!iter.Valid());
//   iter.SeekToFirst();
//   ASSERT_TRUE(!iter.Valid());
//   key = 100;
//   iter.Seek(Encode(&key));
//   ASSERT_TRUE(!iter.Valid());
//   iter.SeekForPrev(Encode(&key));
//   ASSERT_TRUE(!iter.Valid());
//   iter.SeekToLast();
//   ASSERT_TRUE(!iter.Valid());
// }

// TEST_F(InlineSkipTest, Insert) {
//   const int N = 20;
//   const int R = 5000;
//   Random rnd(1000);
//   std::set<Key> keys;
//   TestComparator cmp;
//   InlineSkipList<TestComparator>* list(InlineSkipList<TestComparator>::CreateSkiplist(cmp));
//   for (int i = 0; i < N; i+=2) {
//     Key key = i;
//     if (keys.insert(key).second) {
//       char* buf = list->AllocateKey(sizeof(Key));
//       memcpy(buf, &key, sizeof(Key));
//       list->Insert(buf);
//     }
//   }

//   {
//     char* buf = list->AllocateKey(sizeof(Key));
//     Key key = 33;
//     memcpy(buf, &key, sizeof(Key));
//     bool res = list->Insert(buf);
//     printf("Insert 33 %s\n", res ? "succ" : "fail");
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     iter.SeekToFirst();
//     while (iter.Valid()) {
//       printf("%lu(h:%d, buf:%lx), ", Decode(iter.key()), iter.Height(), iter.BufNode());
//       iter.Next();
//     }
//     printf("\n");
//   }

//   {
//     char* buf = list->AllocateKey(sizeof(Key));
//     Key key = 33;
//     memcpy(buf, &key, sizeof(Key));
//     bool res = list->Insert(buf);
//     printf("Insert 33 %s\n", res ? "succ" : "fail");
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     iter.SeekToFirst();
//     while (iter.Valid()) {
//       printf("%lu, ", Decode(iter.key()));
//       iter.Next();
//     }
//     printf("\n");
//   }

// }

// TEST_F(InlineSkipTest, BufInsertBasic) {
//   const int N = 8;
//   const int R = 5000;
//   Random rnd(1000);
//   TestComparator cmp;
//   InlineSkipList<TestComparator>* list(InlineSkipList<TestComparator>::CreateSkiplist(cmp));
//   for (int i = 1; i <= N; i++) {
//     size_t key = i;
//     list->BufInsert((char*)&key);
//   }

//   for (int i = 1; i <= N; i++) {
//     size_t key = i;
//     EXPECT_TRUE(list->BufContains((char*)&key));
//   }

//   size_t key = 9;
//   EXPECT_TRUE(list->BufInsert((char*)&key));
//   EXPECT_TRUE(list->BufContains((char*)&key));

//   {
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     iter.SeekToFirst();
//     while (iter.Valid()) {
//       printf("%lu(h:%d, buf:%lx), ", Decode(iter.key()), iter.Height(), iter.BufNode());
//       iter.Next();
//     }
//     printf("\n");
//   }
// }

TEST_F (InlineSkipTest, BufInsert) {
    const int N = 1000;
    Random rnd (1000);
    TestComparator cmp;
    InlineSkipList<TestComparator>* list (InlineSkipList<TestComparator>::CreateSkiplist (cmp));
    for (int i = 0; i < N; i++) {
        size_t key = i;
        list->BufInsert ((char*)&key);
    }

    for (int i = 0; i < N; i++) {
        size_t key = i;
        EXPECT_TRUE (list->BufContains ((char*)&key));
    }
}

TEST_F (InlineSkipTest, BufInsert2) {
    const int N = 1000;
    Random rnd (1000);
    TestComparator cmp;
    std::unordered_set<int> mset;
    InlineSkipList<TestComparator>* list (InlineSkipList<TestComparator>::CreateSkiplist (cmp));
    for (int i = 0; i < N; i++) {
        size_t key = random () % 100000;
        while (mset.count (key) != 0) {
            key = random () % 100000;
        }
        list->BufInsert ((char*)&key);
        mset.insert (key);
    }
}

// TEST_F(InlineSkipTest, InsertAndLookup) {
//   const int N = 2000;
//   const int R = 5000;
//   Random rnd(1000);
//   std::set<Key> keys;
//   TestComparator cmp;
//   InlineSkipList<TestComparator>* list(InlineSkipList<TestComparator>::CreateSkiplist(cmp));
//   for (int i = 0; i < N; i++) {
//     Key key = rnd.Next() % R;
//     if (keys.insert(key).second) {
//       char* buf = list->AllocateKey(sizeof(Key));
//       memcpy(buf, &key, sizeof(Key));
//       list->Insert(buf);
//     }
//   }

//   for (Key i = 0; i < R; i++) {
//     if (list->Contains(Encode(&i))) {
//       ASSERT_EQ(keys.count(i), 1U);
//     } else {
//       ASSERT_EQ(keys.count(i), 0U);
//     }
//   }

//   // Simple iterator tests
//   {
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     ASSERT_TRUE(!iter.Valid());

//     uint64_t zero = 0;
//     iter.Seek(Encode(&zero));
//     ASSERT_TRUE(iter.Valid());
//     ASSERT_EQ(*(keys.begin()), Decode(iter.key()));

//     uint64_t max_key = R - 1;
//     iter.SeekForPrev(Encode(&max_key));
//     ASSERT_TRUE(iter.Valid());
//     ASSERT_EQ(*(keys.rbegin()), Decode(iter.key()));

//     iter.SeekToFirst();
//     ASSERT_TRUE(iter.Valid());
//     ASSERT_EQ(*(keys.begin()), Decode(iter.key()));

//     iter.SeekToLast();
//     ASSERT_TRUE(iter.Valid());
//     ASSERT_EQ(*(keys.rbegin()), Decode(iter.key()));
//   }

//   // Forward iteration test
//   for (Key i = 0; i < R; i++) {
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     iter.Seek(Encode(&i));

//     // Compare against model iterator
//     std::set<Key>::iterator model_iter = keys.lower_bound(i);
//     for (int j = 0; j < 3; j++) {
//       if (model_iter == keys.end()) {
//         ASSERT_TRUE(!iter.Valid());
//         break;
//       } else {
//         ASSERT_TRUE(iter.Valid());
//         ASSERT_EQ(*model_iter, Decode(iter.key()));
//         ++model_iter;
//         iter.Next();
//       }
//     }
//   }

//   // Backward iteration test
//   for (Key i = 0; i < R; i++) {
//     InlineSkipList<TestComparator>::Iterator iter(list);
//     iter.SeekForPrev(Encode(&i));

//     // Compare against model iterator
//     std::set<Key>::iterator model_iter = keys.upper_bound(i);
//     for (int j = 0; j < 3; j++) {
//       if (model_iter == keys.begin()) {
//         ASSERT_TRUE(!iter.Valid());
//         break;
//       } else {
//         ASSERT_TRUE(iter.Valid());
//         ASSERT_EQ(*--model_iter, Decode(iter.key()));
//         iter.Prev();
//       }
//     }
//   }
// }

// TEST_F(InlineSkipTest, InsertWithHint_Sequential) {
//   const int N = 100000;
//   TestComparator cmp;
//   TestInlineSkipList* list(TestInlineSkipList::CreateSkiplist(cmp));
//   void* hint = nullptr;
//   for (int i = 0; i < N; i++) {
//     Key key = i;
//     InsertWithHint(list, key, &hint);
//   }
//   Validate(list);
// }

// TEST_F(InlineSkipTest, InsertWithHint_MultipleHints) {
//   const int N = 100000;
//   const int S = 100;
//   Random rnd(534);
//   TestComparator cmp;
//   TestInlineSkipList* list(TestInlineSkipList::CreateSkiplist(cmp));
//   void* hints[S];
//   Key last_key[S];
//   for (int i = 0; i < S; i++) {
//     hints[i] = nullptr;
//     last_key[i] = 0;
//   }
//   for (int i = 0; i < N; i++) {
//     Key s = rnd.Uniform(S);
//     Key key = (s << 32) + (++last_key[s]);
//     InsertWithHint(list, key, &hints[s]);
//   }
//   Validate(list);
// }

// TEST_F(InlineSkipTest, InsertWithHint_MultipleHintsRandom) {
//   const int N = 100000;
//   const int S = 100;
//   Random rnd(534);
//   TestComparator cmp;
//   TestInlineSkipList* list(TestInlineSkipList::CreateSkiplist(cmp));
//   void* hints[S];
//   for (int i = 0; i < S; i++) {
//     hints[i] = nullptr;
//   }
//   for (int i = 0; i < N; i++) {
//     Key s = rnd.Uniform(S);
//     Key key = (s << 32) + rnd.Next();
//     InsertWithHint(list, key, &hints[s]);
//   }
//   Validate(list);
// }

// TEST_F(InlineSkipTest, InsertWithHint_CompatibleWithInsertWithoutHint) {
//   const int N = 100000;
//   const int S1 = 100;
//   const int S2 = 100;
//   Random rnd(534);
//   TestComparator cmp;
//   TestInlineSkipList* list(TestInlineSkipList::CreateSkiplist(cmp));
//   std::unordered_set<Key> used;
//   Key with_hint[S1];
//   Key without_hint[S2];

//   void* hints[S1];
//   for (int i = 0; i < S1; i++) {
//     hints[i] = nullptr;
//     while (true) {
//       Key s = rnd.Next();
//       if (used.insert(s).second) {
//         with_hint[i] = s;
//         break;
//       }
//     }
//   }
//   for (int i = 0; i < S2; i++) {
//     while (true) {
//       Key s = rnd.Next();
//       if (used.insert(s).second) {
//         without_hint[i] = s;
//         break;
//       }
//     }
//   }
//   for (int i = 0; i < N; i++) {
//     Key s = rnd.Uniform(S1 + S2);
//     if (s < S1) {
//       Key key = (with_hint[s] << 32) + rnd.Next();
//       InsertWithHint(list, key, &hints[s]);
//     } else {
//       Key key = (without_hint[s - S1] << 32) + rnd.Next();
//       Insert(list, key);
//     }
//   }
//   Validate(list);
// }

#ifndef ROCKSDB_VALGRIND_RUN
// We want to make sure that with a single writer and multiple
// concurrent readers (with no synchronization other than when a
// reader's iterator is created), the reader always observes all the
// data that was present in the skip list when the iterator was
// constructor.  Because insertions are happening concurrently, we may
// also observe new values that were inserted since the iterator was
// constructed, but we should never miss any values that were present
// at iterator construction time.
//
// We generate multi-part keys:
//     <key,gen,hash>
// where:
//     key is in range [0..K-1]
//     gen is a generation number for key
//     hash is hash(key,gen)
//
// The insertion code picks a random key, sets gen to be 1 + the last
// generation number inserted for that key, and sets hash to Hash(key,gen).
//
// At the beginning of a read, we snapshot the last inserted
// generation number for each key.  We then iterate, including random
// calls to Next() and Seek().  For every key we encounter, we
// check that it is either expected given the initial snapshot or has
// been concurrently added since the iterator started.
class ConcurrentTest {
public:
    static const uint32_t K = 8;

private:
    static uint64_t key (Key key) { return (key >> 40); }
    static uint64_t gen (Key key) { return (key >> 8) & 0xffffffffu; }
    static uint64_t hash (Key key) { return key & 0xff; }

    static inline uint32_t DecodeFixed32 (const char* ptr) {
        if (true) {
            // Load the raw bytes
            uint32_t result;
            memcpy (&result, ptr, sizeof (result));  // gcc optimizes this to a plain load
            return result;
        } else {
            return ((static_cast<uint32_t> (static_cast<unsigned char> (ptr[0]))) |
                    (static_cast<uint32_t> (static_cast<unsigned char> (ptr[1])) << 8) |
                    (static_cast<uint32_t> (static_cast<unsigned char> (ptr[2])) << 16) |
                    (static_cast<uint32_t> (static_cast<unsigned char> (ptr[3])) << 24));
        }
    }

    static uint32_t Hash (const char* data, size_t n, uint32_t seed) {
        // MurmurHash1 - fast but mediocre quality
        // https://github.com/aappleby/smhasher/wiki/MurmurHash1
        //
        const uint32_t m = 0xc6a4a793;
        const uint32_t r = 24;
        const char* limit = data + n;
        uint32_t h = static_cast<uint32_t> (seed ^ (n * m));

        // Pick up four bytes at a time
        while (data + 4 <= limit) {
            uint32_t w = DecodeFixed32 (data);
            data += 4;
            h += w;
            h *= m;
            h ^= (h >> 16);
        }

        // Pick up remaining bytes
        switch (limit - data) {
            // Note: The original hash implementation used data[i] << shift, which
            // promotes the char to int and then performs the shift. If the char is
            // negative, the shift is undefined behavior in C++. The hash algorithm is
            // part of the format definition, so we cannot change it; to obtain the same
            // behavior in a legal way we just cast to uint32_t, which will do
            // sign-extension. To guarantee compatibility with architectures where chars
            // are unsigned we first cast the char to int8_t.
            case 3:
                h += static_cast<uint32_t> (static_cast<int8_t> (data[2])) << 16;
            case 2:
                h += static_cast<uint32_t> (static_cast<int8_t> (data[1])) << 8;
            case 1:
                h += static_cast<uint32_t> (static_cast<int8_t> (data[0]));
                h *= m;
                h ^= (h >> r);
                break;
        }
        return h;
    }

    static uint64_t HashNumbers (uint64_t k, uint64_t g) {
        uint64_t data[2] = {k, g};
        return Hash (reinterpret_cast<char*> (data), sizeof (data), 0);
    }

    static Key MakeKey (uint64_t k, uint64_t g) {
        assert (sizeof (Key) == sizeof (uint64_t));
        assert (k <= K);  // We sometimes pass K to seek to the end of the skiplist
        assert (g <= 0xffffffffu);
        return ((k << 40) | (g << 8) | (HashNumbers (k, g) & 0xff));
    }

    static bool IsValidKey (Key k) { return hash (k) == (HashNumbers (key (k), gen (k)) & 0xff); }

    static Key RandomTarget (Random* rnd) {
        switch (rnd->Next () % 10) {
            case 0:
                // Seek to beginning
                return MakeKey (0, 0);
            case 1:
                // Seek to end
                return MakeKey (K, 0);
            default:
                // Seek to middle
                return MakeKey (rnd->Next () % K, 0);
        }
    }

    // Per-key generation
    struct State {
        std::atomic<int> generation[K];
        void Set (int k, int v) { generation[k].store (v, std::memory_order_release); }
        int Get (int k) { return generation[k].load (std::memory_order_acquire); }

        State () {
            for (unsigned int k = 0; k < K; k++) {
                Set (k, 0);
            }
        }
    };

    // Current state of the test
    State current_;

    // InlineSkipList is not protected by mu_.  We just use a single writer
    // thread to modify it.
    InlineSkipList<TestComparator>* list_;

public:
    ConcurrentTest ()
        : list_ (InlineSkipList<TestComparator>::CreateSkiplist (TestComparator ())) {}

    // REQUIRES: No concurrent calls to WriteStep or ConcurrentWriteStep
    void WriteStep (Random* rnd) {
        const uint32_t k = rnd->Next () % K;
        const int g = current_.Get (k) + 1;
        const Key new_key = MakeKey (k, g);
        char* buf = list_->AllocateKey (sizeof (Key));
        memcpy (buf, &new_key, sizeof (Key));
        list_->Insert (buf);
        current_.Set (k, g);
    }

    // REQUIRES: No concurrent calls for the same k
    void ConcurrentWriteStep (uint32_t k, bool use_hint = false) {
        const int g = current_.Get (k) + 1;
        const Key new_key = MakeKey (k, g);
        char* buf = list_->AllocateKey (sizeof (Key));
        memcpy (buf, &new_key, sizeof (Key));
        if (use_hint) {
            void* hint = nullptr;
            list_->InsertWithHintConcurrently (buf, &hint);
            delete[] reinterpret_cast<char*> (hint);
        } else {
            list_->InsertConcurrently (buf);
        }
        ASSERT_EQ (g, current_.Get (k) + 1);
        current_.Set (k, g);
    }

    void ReadStep (Random* rnd) {
        // Remember the initial committed state of the skiplist.
        State initial_state;
        for (unsigned int k = 0; k < K; k++) {
            initial_state.Set (k, current_.Get (k));
        }

        Key pos = RandomTarget (rnd);
        InlineSkipList<TestComparator>::Iterator iter (list_);
        iter.Seek (Encode (&pos));
        while (true) {
            Key current;
            if (!iter.Valid ()) {
                current = MakeKey (K, 0);
            } else {
                current = Decode (iter.key ());
                ASSERT_TRUE (IsValidKey (current)) << current;
            }
            ASSERT_LE (pos, current) << "should not go backwards";

            // Verify that everything in [pos,current) was not present in
            // initial_state.
            while (pos < current) {
                ASSERT_LT (key (pos), K) << pos;

                // Note that generation 0 is never inserted, so it is ok if
                // <*,0,*> is missing.
                ASSERT_TRUE ((gen (pos) == 0U) ||
                             (gen (pos) > static_cast<uint64_t> (
                                              initial_state.Get (static_cast<int> (key (pos))))))
                    << "key: " << key (pos) << "; gen: " << gen (pos)
                    << "; initgen: " << initial_state.Get (static_cast<int> (key (pos)));

                // Advance to next key in the valid key space
                if (key (pos) < key (current)) {
                    pos = MakeKey (key (pos) + 1, 0);
                } else {
                    pos = MakeKey (key (pos), gen (pos) + 1);
                }
            }

            if (!iter.Valid ()) {
                break;
            }

            if (rnd->Next () % 2) {
                iter.Next ();
                pos = MakeKey (key (pos), gen (pos) + 1);
            } else {
                Key new_target = RandomTarget (rnd);
                if (new_target > pos) {
                    pos = new_target;
                    iter.Seek (Encode (&new_target));
                }
            }
        }
    }
};
const uint32_t ConcurrentTest::K;

// Simple test that does single-threaded testing of the ConcurrentTest
// scaffolding.
// TEST_F(InlineSkipTest, ConcurrentReadWithoutThreads) {
//   ConcurrentTest test;
//   Random rnd(random());
//   for (int i = 0; i < 10000; i++) {
//     test.ReadStep(&rnd);
//     test.WriteStep(&rnd);
//   }
// }

// TEST_F(InlineSkipTest, ConcurrentInsertWithoutThreads) {
//   ConcurrentTest test;
//   Random rnd(random());
//   for (int i = 0; i < 10000; i++) {
//     test.ReadStep(&rnd);
//     uint32_t base = rnd.Next();
//     for (int j = 0; j < 4; ++j) {
//       test.ConcurrentWriteStep((base + j) % ConcurrentTest::K);
//     }
//   }
// }

#endif  // ROCKSDB_VALGRIND_RUN
}  // namespace ROCKSDB_NAMESPACE

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}

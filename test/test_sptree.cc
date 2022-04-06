#include <chrono>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "spoton-tree/sptree.h"
#include "spoton_util.h"

using namespace std;
using namespace spoton;

class SPTreeTest : public testing::Test {
public:
    SPTreeTest () { tree_ = spoton::SPTree::CreateSPTree (true); }
    ~SPTreeTest () { delete tree_; }
    SPTree* tree_;

    void Load (size_t num) {
        for (size_t i = 1; i <= num; i++) {
            tree_->insert (i, i);
        }
    }

    void LoadRandom (size_t num) {
        keys.clear ();
        std::unordered_set<uint64_t> uniquekeys;
        for (size_t i = 0; i < num; i++) {
            size_t key = random ();
            while (uniquekeys.count (key) != 0) {
                key = random ();
            }
            tree_->insert (key, key);
            keys.push_back (key);
            uniquekeys.insert (key);
        }
    }
    std::vector<size_t> keys;
};

TEST_F (SPTreeTest, Basic64) {
    size_t num = 64;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeTest, Basic65) {
    size_t num = 65;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeTest, Basic) {
    size_t num = 100000;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeTest, RandomInsert) {
    size_t num = 100000;
    LoadRandom (num);

    for (size_t i = 0; i < num; i++) {
        TID tid = tree_->lookup (keys[i]);
        ASSERT_EQ (tid, keys[i]);
    }
}

TEST_F (SPTreeTest, RandomInsertAndRemove) {
    size_t num = 100000;
    LoadRandom (num);

    for (size_t i = 0; i < num; i++) {
        TID tid = tree_->lookup (keys[i]);
        ASSERT_EQ (tid, keys[i]);
    }

    for (size_t i = 0; i < num; i++) {
        bool res = tree_->remove (keys[i]);
        if (res == false) {
            printf ("------ remove %lu fail\n", keys[i]);
        }
        ASSERT_TRUE (res);
    }

    for (size_t i = 0; i < num; i++) {
        TID tid = tree_->lookup (keys[i]);
        if (tid != 0) {
            tree_->lookup (keys[i]);
        }
    }
}

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
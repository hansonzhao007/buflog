#include <chrono>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "spoton-tree/sptree.h"
#include "test_util.h"
using namespace std;
using namespace spoton;

class SPTreeDramTest : public testing::Test {
public:
    SPTreeDramTest () { tree_ = spoton::SPTree::CreateSPTree (true, false, false); }
    ~SPTreeDramTest () { delete tree_; }
    SPTree* tree_;

    void Load (size_t num) {
        for (size_t i = 1; i <= num; i++) {
            tree_->insert (i, i);
        }
    }

    void LoadRandom (size_t num) {
        keys.clear ();
        std::unordered_set<uint64_t> uniquekeys;
        while (uniquekeys.size () != num) {
            size_t key = u64Rand (1LU, kRandNumMax) & 0xFFFFFFFF;
            while (uniquekeys.count (key) != 0) {
                key = u64Rand (1LU, kRandNumMax) & 0xFFFFFFFF;
            }
            DEBUG ("insert %lu", key);
            tree_->insert (key, key);
            keys.push_back (key);
            uniquekeys.insert (key);
        }
    }
    std::vector<size_t> keys;
};

class SPTreePmemTest : public testing::Test {
public:
    SPTreePmemTest () {}
    ~SPTreePmemTest () {}

    void Create () { tree_ = spoton::SPTree::CreateSPTree (false, false, false); }

    void Destroy () {
        if (tree_) delete tree_;
        spoton::SPTree::DistroySPTree ();
    }
    SPTree* tree_{nullptr};

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

TEST_F (SPTreeDramTest, Basic64) {
    size_t num = 64;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeDramTest, Basic65) {
    size_t num = 65;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeDramTest, Basic) {
    size_t num = 100000;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

TEST_F (SPTreeDramTest, RandomInsert) {
    DEBUG ("RandomInsert");
    size_t num = 100000;
    LoadRandom (num);

    for (size_t i = 0; i < num; i++) {
        TID tid = tree_->lookup (keys[i]);
        if (tid != keys[i]) {
            printf ("not equal\n");
            tid = tree_->lookup (keys[i]);
        }
    }
}

TEST_F (SPTreeDramTest, RandomInsertAndRemove) {
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

TEST_F (SPTreeDramTest, Scan) {
    DEBUG ("RandomInsert");
    size_t num = 1000;
    Load (num);

    std::vector<uint64_t> vals;
    vals.resize (100);

    size_t found = 0;
    found = tree_->scan (1, 50, vals);
    for (int i = 0; i < 50; i++) {
        ASSERT_EQ (i + 1, vals[i]);
    }
    ASSERT_EQ (found, 50);

    found = tree_->scan (961, 50, vals);
    ASSERT_EQ (found, 40);
}

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
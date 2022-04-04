#include <chrono>
#include <iostream>

#include "gtest/gtest.h"
#include "spoton-tree/sptree.h"
#include "spoton_util.h"

using namespace std;
using namespace spoton;

class SPTreeTest : public testing::Test {
public:
    SPTreeTest () { tree_ = new SPTree{}; }
    SPTree* tree_;

    void Load (size_t num) {
        for (size_t i = 1; i <= num; i++) {
            tree_->insert (i, i);
        }
    }
};

TEST_F (SPTreeTest, Basic) {
    size_t num = 1000;
    Load (num);

    for (size_t i = 1; i <= num; i++) {
        TID tid = tree_->lookup (i);
        ASSERT_EQ (tid, i);
    }
}

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
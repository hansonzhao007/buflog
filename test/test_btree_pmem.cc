#include "gtest/gtest.h"
#include "spoton-tree/btree_pmem.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

using namespace spoton;

TEST (FASTFAIR, Page) {
    btree::DistroyBtree ();
    btree::CreateBtree ();
    void* addr = RP_malloc (sizeof (page));
    page* mpage = new (addr) page (0);
    int count;
    for (size_t i = 1; i < 101; i += 10) {
        mpage->insert_key (i, (char*)i, &count, false);
    }

    for (size_t i = 0; i < 101; i += 10) {
        auto res = mpage->seekLE (i);
        printf ("seek %lu, %lu\n", i, res);
    }
}

TEST (FASTFAIR, Seek) {
    btree::DistroyBtree ();
    btree* tree_ = btree::CreateBtree ();
    size_t num = 1'000'000;
    // insert the even number
    for (size_t i = 0; i < num; i += 2) {
        tree_->btree_insert (i, (char*)i);
    }

    // seek the odd number
    for (size_t i = 1; i < num + 1; i += 2) {
        auto res = tree_->btree_seekLE (i);
        ASSERT_EQ ((size_t)res, i - 1);
    }
}

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
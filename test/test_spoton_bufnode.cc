#include "spoton_bufnode.h"

#include "gtest/gtest.h"

using namespace spoton;

TEST (SortedBufNode_t, BasicOperations) {
    SortedBufNode_t buf_node;

    char val[128] = "value 123";
    for (int i = SortedBufNode_t::kBufNodeFull; i > 0; i--) {
        // insert th key in reverse order
        bool res = buf_node.Insert (i, val);
        EXPECT_TRUE (res);
    }
    // Insert to a full bufnode should fail
    bool res = buf_node.Insert (1000, val);
    EXPECT_FALSE (res);
    EXPECT_EQ (buf_node.meta_.valid_, 0xFFF);

    // Sort the buf node
    buf_node.Sort ();
    for (int i = 0; i < SortedBufNode_t::kBufNodeFull; i++) {
        // after sorting, the sequence should be in reverse order
        EXPECT_EQ (buf_node.seqs_[i], SortedBufNode_t::kBufNodeFull - 1 - i);
    }
    EXPECT_EQ (SortedBufNode_t::kBufNodeFull, buf_node.ValidCount ());

    // Get the record
    char* tmp = nullptr;
    for (int i = SortedBufNode_t::kBufNodeFull; i > 0; i--) {
        bool res = buf_node.Get (i, tmp);
        EXPECT_TRUE (res);
    }

    // Overwrite existing key-val
    for (int i = SortedBufNode_t::kBufNodeFull; i > 0; i--) {
        bool res = buf_node.Insert (i, val);
        int zero_bit_pos = SortedBufNode_t::kBufNodeFull - i;
        // the only one backup bit (which is 0), is going to left shift in a circualr manner
        EXPECT_EQ (buf_node.meta_.valid_, 0x1FFF & ~(1 << zero_bit_pos));
        EXPECT_TRUE (res);
    }

    tmp = nullptr;
    for (int i = SortedBufNode_t::kBufNodeFull; i > 0; i--) {
        bool res = buf_node.Get (i, tmp);
        EXPECT_TRUE (res);
    }

    EXPECT_EQ (SortedBufNode_t::kBufNodeFull, buf_node.ValidCount ());

    EXPECT_TRUE (buf_node.Delete (10));
    EXPECT_FALSE (buf_node.Get (10, tmp));
    EXPECT_FALSE (buf_node.Delete (10));
    EXPECT_FALSE (buf_node.Get (10, tmp));

    EXPECT_TRUE (buf_node.Delete (8));
    EXPECT_FALSE (buf_node.Get (8, tmp));
    EXPECT_FALSE (buf_node.Delete (8));
    EXPECT_FALSE (buf_node.Get (8, tmp));

    EXPECT_TRUE (buf_node.Delete (4));
    EXPECT_FALSE (buf_node.Delete (4));

    EXPECT_EQ (9, buf_node.ValidCount ());

    buf_node.Insert (10, val);
    buf_node.Sort ();

    auto siter = buf_node.sBegin ();
    for (int i : {1, 2, 3, 5, 6, 7, 9, 10, 11, 12}) {
        EXPECT_EQ (i, siter->key);
        siter++;
    }
}

TEST (SortedBufNode_t, MaskLastN) {
    SortedBufNode_t buf_node;

    char val[128] = "value 123";
    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Insert (i, val);
        EXPECT_TRUE (res);
    }

    buf_node.Sort ();
    buf_node.MaskLastN (5);

    auto iter = buf_node.sBegin ();
    for (int i : {1, 2, 3, 4, 5, 6, 7}) {
        EXPECT_EQ (i, iter->key);
        iter++;
    }
}

TEST (SortedBufNode_t, Iterator) {
    SortedBufNode_t buf_node;

    char val[128] = "value 123";
    for (int i : {1, 2, 3, 6, 5, 4, 7, 9, 8, 11, 10, 12}) {
        bool res = buf_node.Insert (i, val);
        EXPECT_TRUE (res);
    }

    auto iter = buf_node.Begin ();
    for (int i : {1, 2, 3, 6, 5, 4, 7, 9, 8, 11, 10, 12}) {
        EXPECT_EQ (i, iter->key);
        iter++;
    }

    // delete
    for (int delete_key : {1, 3, 4, 7, 10}) {
        bool res = buf_node.Delete (delete_key);
        EXPECT_TRUE (res);
    }

    // delete again
    for (int delete_key : {1, 3, 4, 7, 10}) {
        bool res = buf_node.Delete (delete_key);
        EXPECT_FALSE (res);
    }

    auto iter1 = buf_node.Begin ();
    for (int i : {2, 6, 5, 9, 8, 11, 12}) {
        EXPECT_EQ (iter1->key, i);
        iter1++;
    }
}

TEST (SortedBufNode_t, IteratorSorted) {
    SortedBufNode_t buf_node;

    char val[128] = "value 123";
    for (int i : {1, 2, 3, 6, 5, 4, 7, 9, 8, 11, 10, 12}) {
        bool res = buf_node.Insert (i, val);
        EXPECT_TRUE (res);
    }

    buf_node.Sort ();
    auto iter = buf_node.sBegin ();
    for (int i : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
        EXPECT_EQ (i, iter->key);
        iter++;
    }

    // delete
    for (int delete_key : {1, 3, 4, 7, 10}) {
        bool res = buf_node.Delete (delete_key);
        EXPECT_TRUE (res);
    }

    buf_node.Sort ();
    auto iter1 = buf_node.sBegin ();
    for (int i : {2, 5, 6, 8, 9, 11, 12}) {
        EXPECT_EQ (iter1->key, i);
        iter1++;
    }

    EXPECT_FALSE (iter.Valid ());
}

int main (int argc, char* argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}

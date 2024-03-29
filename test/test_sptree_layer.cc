#include <chrono>
#include <iostream>

#include "gtest/gtest.h"
#include "spoton-tree/bottomLayer.h"
#include "spoton-tree/middleLayer.h"
#include "spoton-tree/topLayer.h"

using namespace std;
using namespace spoton;

static void testLoadKey (TID tid, Key& key) {
    key.setKeyLen (sizeof (tid));
    reinterpret_cast<uint64_t*> (&key[0])[0] = __builtin_bswap64 (tid);
}

class TopLayerTest : public testing::Test {
public:
    TopLayerTest () { tree_ = new TopLayer{testLoadKey}; }
    TopLayer* tree_;

    void Load (size_t num) {
        for (size_t i = 1; i <= num; i++) {
            tree_->insert (i, (void*)i);
        }
    }

    void LoadInterval (size_t num) {
        for (size_t i = 3; i <= num; i += 10) {
            tree_->insert (i, (void*)i);
        }
    }
};

TEST (BitSet, Basic) {
    NodeBitSet bitset{0x5555555555555555};
    int count = 0;
    for (auto i : bitset) {
        ASSERT_EQ (i, count);
        count += 2;
    }
}

TEST_F (TopLayerTest, SeekSame) {
    size_t num = 1000;
    Load (num);
    int neq = 0;
    for (size_t i = 1; i <= num; i++) {
        auto res = tree_->seekLE (i);
        if ((uint64_t)res != i) {
            neq++;
        }
    }

    ASSERT_EQ (neq, 0);

    auto res = tree_->seekLE (1000);
    ASSERT_EQ (uint64_t (res), 1000LU);
};

TEST_F (TopLayerTest, Seek) {
    size_t num = 1000;
    LoadInterval (num);
    int seeklarger = 0;
    int total = 0;
    for (size_t i = 2; i <= num; i += 10) {
        total++;
        auto res = tree_->seekLE (i);
        if (uint64_t (res) > i) {
            seeklarger++;
            // printf ("seek %d, return %d\n", i, res);
        }
    }
    ASSERT_EQ (seeklarger, 4);
    // printf ("seek find larger: %d in %d\n", seeklarger, total);
};

TEST (MiddleLayer, Basic) {
    MLNode mnode (false);
    MiddleLayer mlayer;
    for (int i = 0; i < 64; i++) {
        mlayer.SetBloomFilter (i, &mnode);
    }

    for (int i = 0; i < 64; i++) {
        bool res = mlayer.CouldExist (i, &mnode);
        ASSERT_TRUE (res);
    }
}

TEST (MiddleLayer, NodeBuffer) {
    NodeBuffer node;
    for (int i = 1; i <= 14; i++) {
        bool res = node.insert (i, i);
        ASSERT_TRUE (res);
    }

    bool res = node.insert (15, 15);
    ASSERT_FALSE (res);

    for (uint64_t i = 1; i <= 14; i++) {
        uint64_t res = node.lookup (i);
        ASSERT_EQ (res, i);
    }

    for (uint64_t i = 15; i <= 30; i++) {
        uint64_t res = node.lookup (i);
        ASSERT_EQ (res, 0);
    }

    for (uint64_t i = 1; i <= 14; i++) {
        bool res = node.remove (i);
        ASSERT_EQ (res, true);
    }

    for (uint64_t i = 1; i <= 30; i++) {
        uint64_t res = node.lookup (i);
        ASSERT_EQ (res, 0);
    }
}

TEST (MiddleLayer, MLNodeAllocation) {
    // without buffer MLNode
    MLNode* node0 = new (false) MLNode (false);
    ASSERT_EQ (node0->type, MLNodeTypeNoBuffer);

    // with buffer MLNode
    MLNode* node1 = new (true) MLNode (true);
    ASSERT_EQ (node1->type, MLNodeTypeWithBuffer);

    delete node0;
    delete node1;
}

TEST (BottomLayer, Basic) {
    LeafNode64::kIsLeafNodeDram = true;
    LeafNode64 leafnode;
    for (size_t i = 0; i < 64; i++) {
        auto [res, si] = leafnode.Insert (i, i, false);
        ASSERT_TRUE (res);
    }

    ASSERT_TRUE (leafnode.Full ());
};

TEST (BottomLayer, UnSort) {
    LeafNode64::kIsLeafNodeDram = true;
    LeafNode64 leafnode;

    for (size_t i = 0; i < 64; i++) {
        auto [res, si] = leafnode.Insert (1000 - i, i, false);
        ASSERT_TRUE (res);
    }

    ASSERT_TRUE (leafnode.Full ());

    size_t prev = 10000;
    for (int i = 0; i < 64; i++) {
        auto& slot = leafnode.slots[i];
        ASSERT_FALSE (prev < slot.key);
        prev = slot.key;
    }
};

TEST (BottomLayer, Sort) {
    LeafNode64::kIsLeafNodeDram = true;
    LeafNode64 leafnode;
    for (size_t i = 64; i > 0; i--) {
        auto [res, si] = leafnode.Insert (i, i, false);
        ASSERT_TRUE (res);
    }

    ASSERT_TRUE (leafnode.Full ());

    leafnode.Sort ();
    for (int i = 1; i <= 64; i++) {
        int si = leafnode.SeekGE (i);
        ASSERT_EQ (si, i - 1);
    }

    size_t prev = 0;
    for (int i = 0; i < 64; i++) {
        auto& slot = leafnode.slots[leafnode.seqs[i]];
        ASSERT_TRUE (prev < slot.key);
        prev = slot.key;
    }

    {
        auto iter = leafnode.begin ();
        int i = 1;
        while (iter.Valid ()) {
            ASSERT_EQ (iter->val, i++);
            ++iter;
        }
    }

    {
        auto iter = leafnode.begin ();
        int i = 1;
        for (auto& slot : iter) {
            // printf ("i: %d, val: %lu\n", i, slot.val);
            ASSERT_EQ (slot.val, i++);
        }
    }

    {
        auto iter = leafnode.beginAt (65);
        ASSERT_FALSE (iter.Valid ());
    }
};

TEST (BottomLayer, Split) {
    LeafNode64::kIsLeafNodeDram = true;
    LeafNode64 leafnode;
    LeafNode64 dummy;
    leafnode.SetNext (&dummy);
    dummy.SetPrev (&leafnode);

    for (size_t i = 0; i < 64; i++) {
        auto [res, si] = leafnode.Insert (i, i, false);
        ASSERT_TRUE (res);
    }

    ASSERT_TRUE (leafnode.Full ());

    void* addr = malloc (sizeof (LeafNode64));
    BloomFilterFix64 left, right;
    auto [new_leaf_node, new_lkey] = leafnode.Split ({{64, 64}}, addr, left, right);

    for (int i = 0; i < 32; i++) {
        val_t val;
        bool res = leafnode.Lookup (i, val);
        ASSERT_TRUE (res);
    }

    for (int i = 32; i <= 64; i++) {
        val_t val;
        bool res = new_leaf_node->Lookup (i, val);
        ASSERT_TRUE (res);
    }

    for (size_t i = 0; i < 32; i++) {
        bool res = left.couldExist (i);
        ASSERT_TRUE (res);
    }
    for (size_t i = 32; i <= 64; i++) {
        bool res = left.couldExist (i);
        ASSERT_FALSE (res);
    }

    for (size_t i = 0; i < 32; i++) {
        bool res = right.couldExist (i);
        ASSERT_FALSE (res);
    }

    for (size_t i = 32; i <= 64; i++) {
        bool res = right.couldExist (i);
        ASSERT_TRUE (res);
    }
};

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
#include "src/spoton_hashbuf.h"

#include <unordered_set>

#include "gtest/gtest.h"

using namespace spoton;

TEST (HashBuffer, BasicOperations) {
    HashBuffer<4> buf_node;
    char val[128] = "value 123";

    // load until overflow
    int inserted;
    for (inserted = 0;; inserted++) {
        bool res = buf_node.Put (inserted, val);
        if (res == false) break;
    }

    // get
    char* tmp = val;
    for (int n = 0; n < inserted; n++) {
        bool res = buf_node.Get (n, tmp);
        EXPECT_TRUE (res);
    }
    auto ret = buf_node.Get (inserted, tmp);
    EXPECT_FALSE (ret);

    // delete
    for (int n = 0; n < inserted; n++) {
        bool res = buf_node.Delete (n);
        EXPECT_TRUE (res);
    }
    ret = buf_node.Delete (inserted);
    EXPECT_FALSE (ret);
}

TEST (HashBuffer, Iterator) {
    HashBuffer<8> buf_node;

    char val[128] = "value 123";

    // load until overflow
    int inserted;
    for (inserted = 0;; inserted++) {
        bool res = buf_node.Put (inserted, val);
        if (res == false) break;
    }
    bool ret = buf_node.Put (inserted, val);
    EXPECT_FALSE (ret);

    // generate delete key
    std::unordered_set<int> deletes;
    for (int loop = std::min (40, inserted); loop > 0; loop--) {
        int n = random () % inserted;
        deletes.insert (n);
    }
    // delete keys
    for (int i : deletes) {
        bool res = buf_node.Delete (i);
        EXPECT_TRUE (res);
    }
    for (int i : deletes) {
        bool res = buf_node.Delete (i);
        EXPECT_FALSE (res);
    }

    // obtain the remaining keys
    std::unordered_set<int> remains;
    for (int i = 0; i < inserted; i++) {
        if (deletes.count (i)) continue;
        remains.insert (i);
    }

    auto iter = buf_node.Begin ();
    EXPECT_TRUE (iter.Valid ());

    while (iter.Valid ()) {
        bool res = remains.count (iter->key);
        EXPECT_TRUE (res);
        ++iter;
    }

    EXPECT_TRUE ((size_t)inserted == remains.size () + deletes.size ());
}

int main (int argc, char* argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}

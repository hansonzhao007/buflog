#include "src/spoton_skipbuf.h"

#include "gtest/gtest.h"

using namespace spoton;

TEST (SkipBuf, BasicOperations) {
    HashBuffer buf_node (4);
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

int main (int argc, char* argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}

#include "src/buflog.h"
#include "gtest/gtest.h"

using namespace buflog;

TEST(DataLog, DramCreate) {
    linkedredolog::DataLog log;
    EXPECT_FALSE(log.Create(nullptr, 123));
    EXPECT_TRUE(log.Create(nullptr, 1024));
}

TEST(DataLog, DramIterator) {
    linkedredolog::DataLog log;
    char* addr = (char*)malloc(1024*1024);

    log.Create(addr, 1024 * 1024);

    size_t next = 0;
    for (int i = 100; i < 110; i++) {
        std::string tmp = "This is slice" + std::to_string(i);
        next = log.Append(buflog::kDataLogNodeValid, tmp, next, false);
    }

    auto iter = log.Begin();
    EXPECT_TRUE(iter->Valid());

    int i = 0;
    while (iter.Valid()) {
        printf("entry %d: %s\n", i++, iter->ToString().c_str());
        iter++;
    }

    auto iterr = log.rBegin();
    while (iterr.Valid()) {
        printf("entry %d: %s\n", i++, iterr->ToString().c_str());
        iterr--;
    }

    auto iterl = log.lBegin();
    while (iterl.Valid()) {
        printf("entry %d: %s\n", i++, iterl->ToString().c_str());
        iterl++;
    }


}

TEST(BufVec, Operation) {
    BufVec node;

    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(node.Insert(random() % 100));
    }

    EXPECT_FALSE(node.Insert(8));
    EXPECT_TRUE(node.CompactInsert(8));
    node.Sort();

    auto iter = node.Begin();
    while (iter.Valid()) {
        printf("%lu, ", *iter);
        ++iter;
    }

    printf("\n");
}

TEST(SortedBufNode, PutGet) {
    SortedBufNode buf_node;


    char val[128] = "value 123";
    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Put(i, val);
        EXPECT_TRUE(res);
    }
    printf("%s\n", buf_node.ToString().c_str());
    bool res = buf_node.Put(14, val);
    EXPECT_FALSE(res);
    buf_node.Sort();
    printf("%s\n", buf_node.ToString().c_str());
    EXPECT_EQ(12, buf_node.ValidCount());

    char* tmp = nullptr;
    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Get(i, tmp);
        EXPECT_TRUE(res);
    }

    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Put(i, val);
        EXPECT_TRUE(res);
    }

    tmp = nullptr;
    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Get(i, tmp);
        EXPECT_TRUE(res);
    }

    EXPECT_EQ(12, buf_node.ValidCount());
    printf("%s\n", buf_node.ToString().c_str());

    EXPECT_TRUE(buf_node.Delete(10));
    EXPECT_FALSE(buf_node.Get(10, tmp));
    EXPECT_FALSE(buf_node.Delete(10));
    EXPECT_FALSE(buf_node.Get(10, tmp));

    EXPECT_TRUE(buf_node.Delete(8));
    EXPECT_FALSE(buf_node.Get(8, tmp));
    EXPECT_FALSE(buf_node.Delete(8));
    EXPECT_FALSE(buf_node.Get(8, tmp));

    EXPECT_TRUE(buf_node.Delete(4));
    EXPECT_FALSE(buf_node.Delete(4));
    
    EXPECT_EQ(9, buf_node.ValidCount());
    printf("%s\n", buf_node.ToString().c_str());

    buf_node.Sort();
    printf("%s\n", buf_node.ToString().c_str());

    buf_node.Put(10, val);
    buf_node.Sort();
    printf("%s\n", buf_node.ToString().c_str());
    
    auto iter = buf_node.sBegin();
    while (iter.Valid()) {
        printf("%u, ", iter->key);
        iter++;
    }
    printf("\n");
}

TEST(SortedBufNode, MaskLastN) {
    SortedBufNode buf_node;

    char val[128] = "value 123";
    for (int i = 12; i > 0; i--) {
        bool res = buf_node.Put(i, val);
        EXPECT_TRUE(res);
    }
    
    buf_node.Sort();

    printf("%s\n", buf_node.ToString().c_str());
    printf("bufnode before mask: %s\n", buf_node.ToStringValid().c_str());

    buf_node.MaskLastN(5);

    printf("bufnode after  mask: %s\n", buf_node.ToStringValid().c_str());
    printf("%s\n", buf_node.ToString().c_str());
}

TEST(WriteBuffer, Iterator) {
    WriteBuffer<8> buf_node;

    char val[128] = "value 123";
    for (int i = 40; i > 0; i--) {
        bool res = buf_node.Put(i, val);
        EXPECT_TRUE(res);
    }


    auto iter = buf_node.Begin();
    EXPECT_TRUE(iter.Valid());

    int i = 0;
    while (iter.Valid()) {        
        printf("entry %2d: key: %d, val: %s\n", i++, iter->key, iter->val);
        // printf("%s\n", iter.iter.node_->ToString().c_str());
        ++iter;
    }

}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

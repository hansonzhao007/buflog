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
    for (int i = 0; i < 10; i++) {
        std::string tmp = "This is data" + std::to_string(i); 
        linkedredolog::DataLogNodeBuilder node(tmp.data(), tmp.size(), 0);
        log.Append(node, false);
    }

    for (int i = 100; i < 110; i++) {
        std::string tmp = "This is slice" + std::to_string(i); 
        uint8_t checksum = Hasher::hash(tmp.data(), tmp.size()) & 0xFF;
        log.Append(tmp, 0, checksum, false);
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

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

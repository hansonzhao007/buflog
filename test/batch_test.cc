#include "src/batch.h"

#include "gtest/gtest.h"

using namespace xsbatch;

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

    auto iter = log.Begin();
    EXPECT_TRUE(iter->Valid());
    
    auto node_meta = *iter;

    int i = 0;
    while (iter.Valid()) {
        auto node_meta = *iter;
        printf("entry %d: %s\n", i++, iter->ToString().c_str());
        iter++;
    }

    auto iterr = log.rBegin();
    while (iterr.Valid()) {
        auto node_meta = *iterr;
        printf("entry %d: %s\n", i++, iterr->ToString().c_str());
        iterr--;
    }
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

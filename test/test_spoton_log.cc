#include "gtest/gtest.h"
#include "spoton_log.h"

using namespace spoton;

TEST (DataLog, DramIterator) {
    Log_t log;
    char* addr = (char*)malloc (1024 * 1024);

    log.Create (0, addr, 1024 * 1024);

    logptr_t ptr = logptr_nullptr;
    printf ("null logptr_t logid %u, off: %lu. data: 0x%lx\n", ptr.getLogID (), ptr.getOffset (),
            ptr.getData ());
    for (int i = 100; i < 110; i++) {
        std::string tmp = "This is data" + std::to_string (i);
        ptr = log.Append (kLogNodeValid, tmp, ptr);
        printf ("Append %s to logid: %u, off: %lu. data: %lu\n", tmp.c_str (), ptr.getLogID (),
                ptr.getOffset (), ptr.getData ());
    }

    {
        auto iter = log.begin ();
        EXPECT_TRUE (iter->Valid ());

        int i = 0;
        // scan log from left to right
        while (iter.Valid ()) {
            printf ("iter entry %d: %s\n", i++, iter->ToString ().c_str ());
            ++iter;
        }

        // scan log from right to left
        auto riter = log.rbegin ();
        while (riter.Valid ()) {
            printf ("riter entry %d: %s\n", --i, riter->ToString ().c_str ());
            riter--;
        }
    }

    {
        // test logptr_t Next()
        LogNode_t* log_node = &(*log.begin ());
        LogNode_t* prev_log_node;
        while (log_node->Valid ()) {
            printf ("log node %s\n", log_node->ToString ().c_str ());
            prev_log_node = log_node;
            log_node = log_node->Next ();
        }

        // test logptr_t ListNext
        int i = 10;
        while (prev_log_node && prev_log_node->Valid () && i--) {
            printf ("ListNext node %s\n", prev_log_node->ToString ().c_str ());
            prev_log_node = prev_log_node->ListNext ();
        }
    }
}

TEST (DataLog, DramIterator2) {
    Log_t log;
    char* addr = (char*)malloc (1024 * 1024);

    log.Create (0, addr, 1024 * 1024);

    logptr_t ptr = logptr_nullptr;
    printf ("null logptr_t logid %u, off: %lu. data: 0x%lx\n", ptr.getLogID (), ptr.getOffset (),
            ptr.getData ());
    for (int i = 100; i < 110; i++) {
        ptr = log.Append (kLogNodeValid, i, i, ptr);
        printf ("Append %d to logid: %u, off: %lu. data: %lu\n", i, ptr.getLogID (),
                ptr.getOffset (), ptr.getData ());
    }

    {
        auto iter = log.begin ();
        EXPECT_TRUE (iter->Valid ());

        int i = 0;
        // scan log from left to right
        while (iter.Valid ()) {
            printf ("iter entry %d: %s\n", i++, iter->ToString ().c_str ());
            ++iter;
        }

        // scan log from right to left
        auto riter = log.rbegin ();
        while (riter.Valid ()) {
            printf ("riter entry %d: %s\n", --i, riter->ToString ().c_str ());
            riter--;
        }
    }

    {
        // test logptr_t Next()
        LogNode_t* log_node = &(*log.begin ());
        LogNode_t* prev_log_node;
        while (log_node->Valid ()) {
            printf ("log node %s\n", log_node->ToString ().c_str ());
            prev_log_node = log_node;
            log_node = log_node->Next ();
        }

        // test logptr_t ListNext
        int i = 10;
        while (prev_log_node && prev_log_node->Valid () && i--) {
            printf ("ListNext node %s\n", prev_log_node->ToString ().c_str ());
            prev_log_node = prev_log_node->ListNext ();
        }
    }
}

int main (int argc, char* argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}

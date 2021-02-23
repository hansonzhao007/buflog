#include <atomic>
#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>

// ralloc
#include "ralloc.hpp"
#include "pptr.hpp"
#include "libpmem.h"

#include "test_util.h"

#include "gflags/gflags.h"
using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

DEFINE_string(op, "write", "write, read, delete");

struct Node {
    int32_t key;
    int32_t val;
    pptr<Node> next;
    pptr<char> str;
};

void SerilizeStr(char* addr, const std::string& str) {
    // printf("serialize %s\n", str.c_str());
    *addr = str.size();
    addr += 1;
    memcpy(addr, str.data(), str.size());
    FLUSH(addr);
    FLUSHFENCE;
}

const std::string ParseStr(char* addr) {
    std::string res;
    int len = *addr;
    res.assign(addr + 1, len);
    // printf("Decode len: %d. %s\n", len, res.c_str());
    return res;
}

int main(int argc, char *argv[])
{
    ParseCommandLineFlags(&argc, &argv, true);

    printf("Size of Node with pptr pointer: %lu\n", sizeof(Node));

    size_t LOG_SIZE = 2LU << 30;
    bool res = RP_init("hanson", LOG_SIZE);

    if (res) {
        printf("Rmapping, prepare to recover\n");
        RP_get_root<Node>(0);
        int recover_res = RP_recover();
        if (recover_res == 1) {
            printf("Dirty open, recover\n");
        } else {
            printf("Clean restart.\n");
        }
    } else {
        printf("Clean create\n");
        {
            util::IPMWatcher watcher("ralloc");
            size_t tmp_size = 512 << 20;
            void* tmp = RP_malloc(tmp_size);
            pmem_memset_persist(tmp, 0, tmp_size);
        }
    }

    

    if (FLAGS_op == "write") {        
        void* buf = RP_malloc(sizeof(Node));
        Node* head = static_cast<Node*>(buf);
        head->key = 666;
        head->val = 6969;
        char* tmp = (char*) RP_malloc(5);
        SerilizeStr(tmp, "head");
        head->str = tmp;
        FLUSH(head);
        FLUSHFENCE;
        RP_set_root(buf, 0);

        // add 10 nodes
        Node* cur = head;
        for (int i = 0; i < 10; i++) {
            Node* node = (Node*)RP_malloc(sizeof(Node));
            printf("RP malloced size: %lu\n", RP_malloc_size(node));
            node->key = 100 + i;
            node->val = 1000 + i;
            node->next = nullptr;
            char* tmp = (char*) RP_malloc(6);
            SerilizeStr(tmp, "node" + std::to_string(i)); 
            node->str = tmp;
            FLUSH(node);
            FLUSHFENCE;              
            cur->next = node;       
            printf("Write node%d. Dram addr: 0x%lx. Pmem off: 0x%lx, key: %d, val: %d\n", i, node, cur->next.off, node->key, node->val);
            cur = node;
            FLUSH(node);
            FLUSHFENCE;
        }
    } else if (FLAGS_op == "read") {
        Node* head = RP_get_root<Node>(0);
        Node* prev = head;
        Node* cur  = head->next;
        
        while (cur != nullptr) {
            char *str = cur->str;
            auto res = ParseStr(str);
            printf("Read %s. Dram addr: 0x%lx. Pmem off: 0x%lx, key: %d, val: %d.\n", res.c_str(), cur, prev->next.off, cur->key, cur->val);
            prev = cur;
            cur = cur->next;
        }
    }
    
    RP_close();
    return 0;
}

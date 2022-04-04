#ifndef SPOTON_TREE_MIDDLE_LAYER_H
#define SPOTON_TREE_MIDDLE_LAYER_H

#include <stddef.h>

#include <atomic>
#include <memory>

#include "Key.h"
#include "bottomLayer.h"
#include "logger.h"
#include "retryLock.h"
#include "xxhash.h"

namespace spoton {

using key_t = size_t;
using val_t = size_t;

enum MLNodeType : char { MLNodeTypeNoBuffer = 0, MLNodeTypeWithBuffer, MLNodeTypeEnd };

class BloomFilterFix64 {
    uint16_t bits[32];  // 512 bits

public:
    static void BuildBloomFilter (LeafNode64* leafnode, BloomFilterFix64& bloomfilter);

    void reset () { memset (this->bits, 0, 64); }

    void set (key_t key);

    void set (const void* addr, size_t len);

    bool couldExist (key_t key);

    bool couldExist (const void* addr, size_t len);

    std::string ToStringBinary () {
        std::string res;
        for (int i = 0; i < 32; i++) {
            uint16_t data = *reinterpret_cast<uint16_t*> (&bits[i]);
            res += (print_binary (data));
        }
        return res;
    }

private:
    inline void setBitPos (size_t pos) {
        assert (pos < 512);
        bits[pos >> 4] |= (1 << (pos & 0xF));
    }

    inline bool isBitSet (size_t pos) {
        assert (pos < 512);
        return (bits[pos >> 4] & (1 << (pos & 0xF))) != 0;
    }

    std::string print_binary (uint16_t bitmap) {
        char buffer[1024];
        static std::string bit_rep[16] = {"0000", "0001", "0010", "0011", "0100", "0101",
                                          "0110", "0111", "1000", "1001", "1010", "1011",
                                          "1100", "1101", "1110", "1111"};
        sprintf (buffer, "%s%s%s%s", bit_rep[(bitmap >> 12) & 0x0F].c_str (),
                 bit_rep[(bitmap >> 8) & 0x0F].c_str (), bit_rep[(bitmap >> 4) & 0x0F].c_str (),
                 bit_rep[(bitmap >> 0) & 0x0F].c_str ());
        return buffer;
    }
};

/**
 * @brief MLNode is an in dram middle layer node
 *
 */
class __attribute__ ((packed)) MLNode {
public:
    size_t lkey{0};
    size_t hkey{0};

    MLNode* prev{nullptr};
    MLNode* next{nullptr};

    RetryVersionLock lock;

    char* headptr{nullptr};

    BloomFilterFix64 bloomfilter;

    LeafNode64* leafNode{nullptr};
    MLNodeType type{MLNodeTypeNoBuffer};  // 1B

    char writeBuffer[0];
};

class MiddleLayer {
public:
    MLNode* head;

public:
    MiddleLayer ();

    // key may be out of the range of [mnode->lkey, mnode->hkey], fix it
    MLNode* FindTargetMLNode (key_t key, MLNode* mnode);

    void SetBloomFilter (key_t key, MLNode* mnode);

    bool CouldExist (key_t key, MLNode* mnode);
};

};  // namespace spoton
#endif

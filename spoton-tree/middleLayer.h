#ifndef SPOTON_TREE_MIDDLE_LAYER_H
#define SPOTON_TREE_MIDDLE_LAYER_H

#include <stddef.h>

#include <atomic>

#include "Key.h"
#include "bottomLayer.h"
#include "retryLock.h"
#include "xxhash.h"

namespace spoton {

using key_t = size_t;
using val_t = size_t;

enum MLNodeType : char { MLNodeTypeNoBuffer = 0, MLNodeTypeWithBuffer, MLNodeTypeEnd };

class BloomFilter {
public:
    virtual void set (const void* addr, size_t len) = 0;
    virtual bool couldExist (const void* addr, size_t len) = 0;
    virtual ~BloomFilter () {}

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

class BloomFilterFix32 : public BloomFilter {
public:
    void set (const void* addr, size_t len) override {
        size_t hash1 = XXH3_64bits (addr, len);
        size_t hash2 = XXH64 (addr, len, UINT64_C (0xde5fb9d2630458e9));

        size_t pos[4];
        pos[0] = (hash1 >> 32) & 255;
        pos[1] = hash1 & 255;
        pos[2] = (hash2 >> 32) & 255;
        pos[3] = hash2 & 255;

        for (int i = 0; i < 4; i++) {
            setBitPos (pos[i]);
        }
    }

    bool couldExist (const void* addr, size_t len) override {
        size_t hash1 = XXH3_64bits (addr, len);
        size_t hash2 = XXH64 (addr, len, UINT64_C (0xde5fb9d2630458e9));

        size_t pos[4];
        pos[0] = (hash1 >> 32) & 255;
        pos[1] = hash1 & 255;
        pos[2] = (hash2 >> 32) & 255;
        pos[3] = hash2 & 255;

        for (int i = 0; i < 4; i++) {
            if (!isBitSet (pos[i])) return false;
        }
        return true;
    }

    std::string ToStringBinary () {
        std::string res;
        for (int i = 0; i < 16; i++) {
            uint16_t data = *reinterpret_cast<uint16_t*> (&bits[i * 2]);
            res += (print_binary (data));
        }
        return res;
    }

private:
    inline void setBitPos (size_t pos) {
        assert (pos < 256);
        bits[pos >> 3] |= (1 << (pos & 0x7));
    }

    inline bool isBitSet (size_t pos) {
        assert (pos < 256);
        return (bits[pos >> 3] & (1 << (pos & 0x7))) != 0;
    }

    uint8_t bits[32];  // 256 bits
};

class BloomFilterFix64 : public BloomFilter {
public:
    static void BuildBloomFilter (LeafNode64* leafnode, BloomFilterFix64& bloomfilter);

    void reset () { memset (this->bits, 0, 64); }

    void set (const void* addr, size_t len) override {
        size_t hash1 = XXH3_64bits (addr, len);
        size_t hash2 = XXH64 (addr, len, UINT64_C (0xde5fb9d2630458e9));

        size_t pos[4];
        pos[0] = (hash1 >> 32) & 511;
        pos[1] = hash1 & 511;
        pos[2] = (hash2 >> 32) & 511;
        pos[3] = hash2 & 511;

        for (int i = 0; i < 4; i++) {
            setBitPos (pos[i]);
        }
    }

    bool couldExist (const void* addr, size_t len) override {
        size_t hash1 = XXH3_64bits (addr, len);
        size_t hash2 = XXH64 (addr, len, UINT64_C (0xde5fb9d2630458e9));

        size_t pos[4];
        pos[0] = (hash1 >> 32) & 511;
        pos[1] = hash1 & 511;
        pos[2] = (hash2 >> 32) & 511;
        pos[3] = hash2 & 511;

        for (int i = 0; i < 4; i++) {
            if (!isBitSet (pos[i])) return false;
        }
        return true;
    }

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

    uint16_t bits[32];  // 512 bits
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

    bool Insert (key_t key, val_t val);
    bool Remove (key_t key);
};

class MiddleLayer {
public:
    MLNode* head;

public:
    MiddleLayer ();

    // key may be out of the range of [mnode->lkey, mnode->hkey], fix it
    MLNode* FindTargetMLNode (key_t key, MLNode* mnode);

    inline void SetBloomFilter (key_t key, MLNode* mnode) { mnode->bloomfilter.set (&key, 8); }
    inline bool CouldExist (key_t key, MLNode* mnode) {
        return mnode->bloomfilter.couldExist (&key, 8);
    }
};

};  // namespace spoton
#endif

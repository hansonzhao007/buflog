#ifndef SPOTON_TREE_MIDDLE_LAYER_H
#define SPOTON_TREE_MIDDLE_LAYER_H

#include <stddef.h>

#include <atomic>
#include <memory>

#include "Key.h"
#include "bottomLayer.h"
#include "logger.h"
#include "retryLock.h"
#include "wal.h"
#include "xxhash.h"
namespace spoton {

using key_t = size_t;
using val_t = size_t;

enum MLNodeType : char { MLNodeTypeNoBuffer = 0, MLNodeTypeWithBuffer, MLNodeTypeEnd };

class BloomFilterFix64 {
    uint16_t bits[32];  // 512 bits

public:
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

constexpr size_t kBloomFilterSize = sizeof (BloomFilterFix64);
struct NodeBufferSlot {
    key_t key;
    val_t val;
};
class NodeBuffer {
public:
    uint16_t valid_bitmap{0};  // 2B
                               // .. ->8B
    uint8_t tags[16];          // 16B
    NodeBufferSlot slots[14];  // 14 * 16B =  224B

    bool insert (key_t key, val_t val);
    val_t lookup (key_t key);
    bool remove (key_t key);

    inline size_t Count () { return __builtin_popcount (valid_bitmap); }

public:
    NodeBitSet MatchBitSet (uint8_t tag);
    inline bool Full () { return __builtin_popcount (valid_bitmap) == 14; }
    inline NodeBitSet ValidBitSet () { return NodeBitSet (valid_bitmap); }
    inline NodeBitSet EmptyBitSet () { return NodeBitSet (~valid_bitmap); }
    inline void SetValid (int pos) {
        assert (pos < 14);
        valid_bitmap |= (1 << pos);
    }
    inline void SetErase (int pos) {
        assert (pos < 14);
        valid_bitmap &= ~(1 << pos);
    }
    inline void Reset () {
        valid_bitmap = 0;
        // DEBUG ("buffer 0x%lx reset, count: %lu, valid: %lx", this, Count (), valid_bitmap);
    }
};

constexpr size_t kNodeBufferSize = sizeof (NodeBuffer);
static_assert (kNodeBufferSize == 248, "");
/**
 * @brief MLNode is an in dram middle layer node
 *
 */
class MLNode {
public:
    size_t lkey{0};
    size_t hkey{0};

    MLNode* prev{nullptr};
    MLNode* next{nullptr};

    RetryVersionLock lock;

    WALNode* headptr{nullptr};

private:
    BloomFilterFix64 bloomfilter;

public:
    LeafNode64* leafNode{nullptr};  // 8B

    MLNodeType type{MLNodeTypeNoBuffer};  //
    bool isDisabled{true};                //
    uint8_t none_[6];                     // fill to 8B. (alignment)

    char writeBuffer[0];

    explicit MLNode (bool withBuffer)
        : lkey (0),
          hkey (0),
          prev (nullptr),
          next (nullptr),
          headptr (nullptr),
          leafNode (nullptr),
          isDisabled (true) {
        if (withBuffer) {
            type = MLNodeTypeWithBuffer;
            getNodeBuffer ()->Reset ();
        } else {
            type = MLNodeTypeNoBuffer;
        }
        // DEBUG ("create MLNode 0x%lx, buffer addr: 0x%lx", this, writeBuffer);
    }

    static void* operator new (size_t sz, bool withBuffer) {
        if (!withBuffer) {
            return malloc (sz);
        } else {
            return malloc (sz + sizeof (NodeBuffer));
        }
    }

    size_t Size () {
        if (type == MLNodeTypeNoBuffer)
            return sizeof (MLNode);
        else if (type == MLNodeTypeWithBuffer)
            return sizeof (MLNode) + sizeof (NodeBuffer);
        ERROR ("MLNode invalie type");
        perror ("MLNode invalie type");
        exit (1);
    }

public:
    inline void EnableBloomFilter () { isDisabled = false; }

    void SetBloomFilter (key_t key) {
        if (isDisabled) return;
        bloomfilter.set (key);
    }
    bool CouldExist (key_t key) {
        // If a bloom filter has not been enable, we should not use it. After recover, all the
        // bloomfilter is disabled. A leaf node split will automatically enable the bloom filter
        if (isDisabled) return true;
        return bloomfilter.couldExist (key);
    }

    BloomFilterFix64& GetBloomFilter () { return bloomfilter; }

public:
    // only for MLNode with NodeBuffer, i.e., type == MLNodeTypeWithBuffer
    inline NodeBuffer* getNodeBuffer () { return reinterpret_cast<NodeBuffer*> (writeBuffer); }

    bool insert (key_t key, val_t val) {
        if (type == MLNodeTypeNoBuffer) {
            ERROR ("insert to MLNode without NodeBuffer");
            printf ("insert to MLNode without NodeBuffer");
            exit (1);
        }
        return getNodeBuffer ()->insert (key, val);
    }

    val_t lookup (key_t key) {
        if (type == MLNodeTypeNoBuffer) {
            ERROR ("lookup to MLNode without NodeBuffer");
            printf ("lookup to MLNode without NodeBuffer");
            exit (1);
        }
        return getNodeBuffer ()->lookup (key);
    }

    bool remove (key_t key) {
        if (type == MLNodeTypeNoBuffer) {
            ERROR ("remove to MLNode without NodeBuffer");
            printf ("rempve to MLNode without NodeBuffer");
            exit (1);
        }
        return getNodeBuffer ()->remove (key);
    }

    size_t recordCount () { return getNodeBuffer ()->Count (); }

    inline void resetBuffer () { getNodeBuffer ()->Reset (); }
};

class MiddleLayer {
public:
    MLNode* head;
    MLNode* dummyTail;

public:
    MiddleLayer ();

    // key may be out of the range of [mnode->lkey, mnode->hkey], fix it
    MLNode* FindTargetMLNode (key_t key, MLNode* mnode);

    void SetBloomFilter (key_t key, MLNode* mnode);
    bool CouldExist (key_t key, MLNode* mnode);

    std::string ToStats ();
};

};  // namespace spoton
#endif

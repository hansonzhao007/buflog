#ifndef CCEH_SPOTON_H_
#define CCEH_SPOTON_H_

#include <libpmemobj.h>
#include <pthread.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "spoton_hashbuf.h"
#include "util.h"

#define TOID_ARRAY(x) TOID (x)

typedef size_t Key_t;
typedef const char* Value_t;

const Key_t SENTINEL = -2;
const Key_t INVALID = -1;
const Value_t NONE = 0x0;

struct Pair {
    Key_t key;
    Value_t value;
};

class CCEH;
struct Directory;
struct Segment;
POBJ_LAYOUT_BEGIN (HashTable);
POBJ_LAYOUT_ROOT (HashTable, CCEH);
POBJ_LAYOUT_TOID (HashTable, struct Directory);
POBJ_LAYOUT_ROOT (HashTable, struct Segment);
POBJ_LAYOUT_TOID (HashTable, TOID (struct Segment));
POBJ_LAYOUT_END (HashTable);

constexpr size_t kSegmentBits = 8;
constexpr size_t kMask = (1 << kSegmentBits) - 1;
constexpr size_t kShift = kSegmentBits;
constexpr size_t kSegmentSize = (1 << kSegmentBits) * 16 * 4;
constexpr size_t kWriteBufferSize = kSegmentSize / 2;
constexpr size_t kNumPairPerCacheLine = 4;
constexpr size_t kNumCacheLine = 8;
constexpr size_t kCuckooThreshold = 16;

using WriteBuffer = spoton::HashBuffer;
// constexpr size_t kCuckooThreshold = 32;

struct Segment {
    static const size_t kNumSlot = kSegmentSize / sizeof (Pair);

    Segment (void) {}
    ~Segment (void) {}

    void initSegment (void) {
        for (size_t i = 0; i < kNumSlot; ++i) {
            bucket[i].key = INVALID;
        }
        local_depth = 0;
        sema = 0;
    }

    void initSegment (size_t depth) {
        for (size_t i = 0; i < kNumSlot; ++i) {
            bucket[i].key = INVALID;
        }
        local_depth = depth;
        sema = 0;
    }

    bool suspend (void) {
        int64_t val;
        do {
            val = sema;
            if (val < 0) return false;
        } while (!CAS (&sema, &val, -1));

        int64_t wait = 0 - val - 1;
        while (val && sema != wait) {
            asm("nop");
        }
        return true;
    }

    bool lock (void) {
        int64_t val = sema;
        while (val > -1) {
            if (CAS (&sema, &val, val + 1)) return true;
            val = sema;
        }
        return false;
    }

    void unlock (void) {
        int64_t val = sema;
        while (!CAS (&sema, &val, val - 1)) {
            val = sema;
        }
    }

    int Insert (PMEMobjpool*, Key_t&, Value_t, size_t, size_t);
    bool Insert4split (Key_t&, Value_t, size_t);
    TOID (struct Segment) * Split (PMEMobjpool*);
    struct Segment* SplitDram (WriteBuffer::Iterator& iter);
    std::vector<std::pair<size_t, size_t>> find_path (size_t, size_t);
    void execute_path (PMEMobjpool*, std::vector<std::pair<size_t, size_t>>&, Key_t&, Value_t);
    void execute_path (std::vector<std::pair<size_t, size_t>>&, Pair);
    size_t numElement (void);

    Pair bucket[kNumSlot];
    int64_t sema = 0;
    size_t local_depth;
};

struct Directory {
    static const size_t kDefaultDepth = 10;

    TOID_ARRAY (TOID (struct Segment)) segment;
    WriteBuffer** bufnodes;

    int64_t sema = 0;
    size_t capacity;
    size_t depth;

    bool suspend (void) {
        int64_t val;
        do {
            val = sema;
            if (val < 0) return false;
        } while (!CAS (&sema, &val, -1));

        int64_t wait = 0 - val - 1;
        while (val && sema != wait) {
            asm("nop");
        }
        return true;
    }

    bool lock (void) {
        int64_t val = sema;
        while (val > -1) {
            if (CAS (&sema, &val, val + 1)) return true;
            val = sema;
        }
        return false;
    }

    void unlock (void) {
        int64_t val = sema;
        while (!CAS (&sema, &val, val - 1)) {
            val = sema;
        }
    }

    Directory (void) {}
    ~Directory (void) {}

    void initDirectory (void) {
        depth = kDefaultDepth;
        capacity = pow (2, depth);
        sema = 0;
        // INFO ("Directory capacity: %lu. depth %lu\n", capacity, depth);
        printf ("Directory capacity: %lu. depth %lu\n", capacity, depth);
    }

    void initDirectory (size_t _depth) {
        depth = _depth;
        capacity = pow (2, _depth);
        // INFO ("Directory capacity: %lu. depth %lu\n", capacity, depth);
        printf ("Directory capacity: %lu. depth %lu\n", capacity, depth);
        sema = 0;
    }
};

class CCEH {
public:
    CCEH (void) {}
    ~CCEH (void) {}
    void initCCEH (PMEMobjpool*);
    void initCCEH (PMEMobjpool*, size_t);

    void Insert (PMEMobjpool*, Key_t&, Value_t);
    void insert (PMEMobjpool*, Key_t&, Value_t, bool with_lock);
    void mergeBufAndSplitWhenNeeded (PMEMobjpool*, WriteBuffer* bufnode, Segment_toid& target,
                                     size_t x);
    bool InsertOnly (PMEMobjpool*, Key_t&, Value_t);
    bool Delete (Key_t&);
    Value_t Get (Key_t&);
    Value_t get (Key_t&);
    Value_t FindAnyway (Key_t&);

    double Utilization (void);
    size_t Capacity (void);
    void Recovery (PMEMobjpool*);

    bool crashed = true;

    Directory* dir () {
#ifdef CONFIG_DRAM_INNER
        return dir_dram;
#else
        return D_RW (dir_pmem);
#endif
    }

    void SetDir (Directory* d) {
#ifdef CONFIG_DRAM_INNER
        dir_dram = d;
#else
        dir_pmem = pmemobj_oid (d);
#endif
    }

private:
    union {
        TOID (struct Directory) dir_pmem;
        Directory* dir_dram;
    };
};

#endif

#ifndef _BUFLOG_H_
#define _BUFLOG_H_

#include <immintrin.h>
#include <inttypes.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "buflog.hasher.h"
#include "buflog.util.h"
#include "logger.h"
#include "slice.h"
#include "turbo_hash.h"

#define BUFLOG_FLUSH(addr) asm volatile("clwb (%0)" ::"r"(addr))
#define BUFLOG_FLUSHFENCE asm volatile("sfence" ::: "memory")

inline void BUFLOG_CLFLUSH (char* data, int len) {
    volatile char* ptr = (char*)((unsigned long)data & ~(64 - 1));
    for (; ptr < data + len; ptr += 64) {
        _mm_clwb ((void*)ptr);
    }
}

inline void BUFLOG_COMPILER_FENCE () {
    std::atomic_thread_fence (std::memory_order_release);
    // asm volatile("" : : : "memory"); /* Compiler fence. */
}

namespace buflog {

/** A redo log that connect log entry with linked list.
 *  Note: After we link the log entries with linked list, we
 *        can replay from the log tail to log head (new -> old).
 *        Meanwhile, we only have to recover the head node of each linked-list,
 *        so the recover speed should be fast.
 */

enum DataLogNodeMetaType : unsigned char {
    kDataLogNodeValid = 0x10,  // a valid data log node
    kDataLogNodeLinkEnd =
        0x11,  // reach to a end of the linked list because the data in buffer has been merged
    kDataLogNodeCheckpoint =
        0x12,  // the checkpoint node. recover should stop reverse scan if this node appears.
    kDataLogNodeFail
};

namespace linkedredolog {

/**
 *  Format:
 *  | len | .. data ... |  DataLogNodeMeta |
 *                      |
 *            where the next_ point to
 *  `len` : 1 byte. the size of data
 *  `data`: any bytes
 *      any length of record
 *  `DataLogNodeMeta`: 8 bytes
 *      type_: 1 byte
 *      data_size_: 1 byte, the size of data
 *      next_; 6 byte
 */
class DataLogNodeMeta {
public:
    inline DataLogNodeMeta* Next (void) { return reinterpret_cast<DataLogNodeMeta*> (_.next_); }

    inline util::Slice Data (void) {
        return util::Slice ((char*)(this) - _.data_size_, _.data_size_);
    }

    inline bool Valid (void) {
        return _.type_ < kDataLogNodeFail &&  // type is valid
               _.type_ >= kDataLogNodeValid;
    }

    std::string ToString (void) {
        char buffer[1024];
        sprintf (buffer, "type: %02d, data size: %u - %s. next off: %lu", _.type_, _.data_size_,
                 Data ().ToString ().c_str (), _.next_);
        return buffer;
    }

    struct {
        uint64_t type_ : 8;
        uint64_t data_size_ : 8;
        uint64_t next_ : 48;
    } _;
};

static_assert (sizeof (DataLogNodeMeta) == 8, "DataLogNodeMeta is not 8 byte ");

struct DataLogMeta {
    uint64_t size_;         // lenght of this log file
    uint64_t commit_tail_;  // current tail
    char _none[240];

    std::string ToString () {
        char buf[128];
        sprintf (buf, "log size: %ld, cur tail: %ld", size_, commit_tail_);
        return buf;
    }
};

static_assert (sizeof (DataLogMeta) == 256, "size of LogMeta is not 256 byte");

/**
 *  |  DataLogMeta  | Log data ....
 *  |   256 byte    |          ...
 */
class DataLog {
public:
    DataLog ()
        : log_size_ (0), log_size_mask_ (0), log_start_addr_ (nullptr), log_cur_tail_ (256) {}

    // create a new log with size
    bool Create (char* addr, size_t size) {
        if (!isPowerOfTwo (size)) {
            // perror ("Data log is not power of 2.");
            return false;
        }
        if (addr == nullptr) {
            return true;
        }
        log_size_ = size;
        log_size_mask_ = size - 1;
        log_start_addr_ = addr;
        log_cur_tail_ = 256;

        DataLogMeta* meta = reinterpret_cast<DataLogMeta*> (addr);
        meta->size_ = size;
        BUFLOG_FLUSH (addr);
        BUFLOG_FLUSHFENCE;
        return true;
    }

    void Open (char* addr) {
        DataLogMeta* meta = reinterpret_cast<DataLogMeta*> (addr);
        log_size_ = meta->size_;
        log_size_mask_ = log_size_ - 1;
        log_start_addr_ = addr;
        log_cur_tail_ = meta->commit_tail_;
        printf ("Data log: %s\n", meta->ToString ().c_str ());

        // auto iter = Begin();

        // int i = 0;
        // while (iter.Valid()) {
        //     printf("entry %d: %s\n", i++, iter->ToString().c_str());
        //     iter++;
        // }
    }

    void CommitTail (bool is_pmem) {
        DataLogMeta* meta = reinterpret_cast<DataLogMeta*> (log_start_addr_);
        meta->commit_tail_ = log_cur_tail_.load ();

        if (is_pmem) {
            BUFLOG_FLUSH (meta);
            BUFLOG_FLUSHFENCE;
        }
    }

    inline uint64_t LogTail (void) { return log_cur_tail_; }

    inline size_t Append (DataLogNodeMetaType type, util::Slice entry, uint64_t next,
                          bool is_pmem) {
        int total_size = 1 + entry.size () + sizeof (DataLogNodeMeta);
        size_t off = log_cur_tail_.fetch_add (total_size, std::memory_order_relaxed);
        char* addr = log_start_addr_ + off;

        int len = entry.size ();

        //  | len | .. data ... |  DataLogNodeMeta |
        //  |  1B        len             8B
        // addr
        *(uint8_t*)(addr) = len;

        // copy the slice entry
        memcpy (addr + 1, entry.data (), len);

        DataLogNodeMeta* meta = reinterpret_cast<DataLogNodeMeta*> (addr + 1 + len);
        meta->_.type_ = type;
        meta->_.data_size_ = len;
        meta->_.next_ = next;

        if (is_pmem) {
            BUFLOG_CLFLUSH (addr, total_size);
        }

        // return start offset of DataLogNodeMeta
        return off + 1 + len;
    }

    inline size_t Append (DataLogNodeMetaType type, char* target, size_t len, uint64_t next,
                          bool is_pmem) {
        int total_size = 1 + len + sizeof (DataLogNodeMeta);
        size_t off = log_cur_tail_.fetch_add (total_size, std::memory_order_relaxed);
        char* addr = log_start_addr_ + off;

        //  | len | .. data ... |  DataLogNodeMeta |
        //  |  1B        len             8B
        // addr
        *(uint8_t*)(addr) = len;

        // copy the slice entry
        memcpy (addr + 1, target, len);

        DataLogNodeMeta* meta = reinterpret_cast<DataLogNodeMeta*> (addr + 1 + len);
        meta->_.type_ = type;
        meta->_.data_size_ = len;
        meta->_.next_ = next;

        if (is_pmem) {
            BUFLOG_CLFLUSH (addr, total_size);
        }

        // return start offset of DataLogNodeMeta
        return off + 1 + len;
    }

    // append fixed 16 byte kv pair
    inline size_t Append (DataLogNodeMetaType type, size_t key, size_t val, uint64_t next,
                          bool is_pmem) {
        int total_size = 1 + 16 + sizeof (DataLogNodeMeta);
        size_t off = log_cur_tail_.fetch_add (total_size, std::memory_order_relaxed);
        char* addr = log_start_addr_ + off;

        //  | len | .. data ... |  DataLogNodeMeta |
        //  |  1B        len             8B
        // addr
        *(uint8_t*)(addr) = 16;

        *(uint64_t*)(addr + 1) = key;
        *(uint64_t*)(addr + 1 + 8) = val;

        DataLogNodeMeta* meta = reinterpret_cast<DataLogNodeMeta*> (addr + 1 + 16);
        meta->_.type_ = type;
        meta->_.data_size_ = 16;
        meta->_.next_ = next;

        if (is_pmem) {
            BUFLOG_CLFLUSH (addr, total_size);
        }

        // return start offset of DataLogNodeMeta
        return off + 1 + 16;
    }

    /**
     *  An iterator that scan the log from left to right
     */
    class SeqIterator {
    public:
        SeqIterator (uint64_t front_offset, char* log_start_addr)
            : front_off_ (front_offset), log_addr_ (log_start_addr) {}

        // Check if the DataLogNode pointed by myself is valid or not
        inline bool Valid (void) const { return (*this)->Valid (); }

        inline DataLogNodeMeta operator* () const { return *this->operator-> (); }

        inline DataLogNodeMeta* operator-> (void) const {
            uint8_t data_size = *reinterpret_cast<uint8_t*> (log_addr_ + front_off_);
            return reinterpret_cast<DataLogNodeMeta*> (log_addr_ + front_off_ + 1 + data_size);
        }

        // Prefix ++ overload
        inline SeqIterator& operator++ () {
            front_off_ += size ();
            return *this;
        }

        // Postfix ++ overload
        inline SeqIterator operator++ (int) {
            auto tmp = *this;
            front_off_ += size ();
            return tmp;
        }

    private:
        // size of the entire LogDataNode
        inline size_t size (void) {
            uint8_t data_size = *reinterpret_cast<uint8_t*> (log_addr_ + front_off_);
            return 1 + data_size + sizeof (DataLogNodeMeta);
        }

        size_t front_off_;
        char* log_addr_;
    };  // end of class SeqIterator

    class ReverseIterator {
    public:
        ReverseIterator (uint64_t end_off, char* log_start_addr)
            : end_off_ (end_off), log_addr_ (log_start_addr) {}

        // Check if the DataLogNode pointed by myself is valid or not
        inline bool Valid (void) const {
            return end_off_ > 256;  // should be larger than the DataLogMeta
        }

        inline DataLogNodeMeta operator* () const { return *this->operator-> (); }

        inline DataLogNodeMeta* operator-> (void) const {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*> (
                log_addr_ + end_off_ - sizeof (DataLogNodeMeta));
            return node_meta;
        }

        // Prefix -- overload
        inline ReverseIterator& operator-- () {
            end_off_ -= NodeSize ();
            return *this;
        }

        // Postfix -- overload
        inline ReverseIterator operator-- (int) {
            auto tmp = *this;
            end_off_ -= NodeSize ();
            return tmp;
        }

    private:
        // size of the entire LogDataNode of data log node
        inline size_t NodeSize (void) {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*> (
                log_addr_ + end_off_ - sizeof (DataLogNodeMeta));
            return 1 + node_meta->_.data_size_ + sizeof (DataLogNodeMeta);
        }
        int64_t end_off_;
        char* log_addr_;
    };

    class LinkIterator {
    public:
        LinkIterator (uint64_t next, char* log_start_addr)
            : next_off_ (next), log_addr_ (log_start_addr) {}

        // Check if the DataLogNode pointed by myself is valid or not
        inline bool Valid (void) const {
            return next_off_ > 256 &&  // should be larger than the DataLogMeta
                   reinterpret_cast<DataLogNodeMeta*> (log_addr_ + next_off_)->_.type_ !=
                       kDataLogNodeLinkEnd;
        }

        inline DataLogNodeMeta operator* () const { return *this->operator-> (); }

        inline DataLogNodeMeta* operator-> (void) const {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*> (log_addr_ + next_off_);
            return node_meta;
        }

        // Prefix -- overload
        inline LinkIterator& operator++ () {
            next_off_ = reinterpret_cast<DataLogNodeMeta*> (log_addr_ + next_off_)->_.next_;
            return *this;
        }

        // Postfix -- overload
        inline LinkIterator operator++ (int) {
            auto tmp = *this;
            next_off_ = reinterpret_cast<DataLogNodeMeta*> (log_addr_ + next_off_)->_.next_;
            return tmp;
        }

    private:
        // size of the entire LogDataNode of data log node
        inline size_t NodeSize (void) {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*> (log_addr_ + next_off_);
            return 1 + node_meta->_.data_size_ + sizeof (DataLogNodeMeta);
        }
        int64_t next_off_;
        char* log_addr_;
    };

    SeqIterator Begin (void) { return SeqIterator (256, log_start_addr_); }

    ReverseIterator rBegin (void) { return ReverseIterator (log_cur_tail_, log_start_addr_); }

    LinkIterator lBegin () {
        return LinkIterator (log_cur_tail_ - sizeof (DataLogNodeMeta), log_start_addr_);
    }

private:
    size_t log_size_;
    size_t log_size_mask_;
    char* log_start_addr_;
    std::atomic<size_t> log_cur_tail_;
};

}  // end of namespace linkedredolog

// https://rigtorp.se/spinlock/
class AtomicSpinLock {
public:
    std::atomic<bool> lock_ = {0};

    void inline lock () noexcept {
        for (;;) {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange (true, std::memory_order_acquire)) {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load (std::memory_order_relaxed)) {
                // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                // hyper-threads
                __builtin_ia32_pause ();
            }
        }
    }

    bool inline is_locked (void) noexcept { return lock_.load (std::memory_order_relaxed); }

    bool inline try_lock () noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load (std::memory_order_relaxed) &&
               !lock_.exchange (true, std::memory_order_acquire);
    }

    void inline unlock () noexcept { lock_.store (false, std::memory_order_release); }
};  // end of class AtomicSpinLock

struct KV {
    int64_t key;
    char* val;
};

class SortedBufNode {
public:
    static constexpr uint16_t kBufNodeFull = 12;
    static constexpr uint16_t kBitMapMask = 0x1FFF;

    SortedBufNode () {
        memset (this, 0, 64);
        highkey_ = -1;
        parentkey_ = -1;
    }

    inline void Lock (void) { lock_.lock (); }

    inline void Unlock (void) { lock_.unlock (); }

    inline void Reset (void) { meta_.data_ = 0; }

    inline void MaskLastN (int n) {
        uint16_t mask = 0;
        int count = ValidCount ();
        for (int i = count - n; i < count; ++i) {
            mask |= (1 << seqs_[i]);
        }
        meta_.valid_ &= ((~mask) & kBitMapMask);
        BUFLOG_COMPILER_FENCE ();
    }

    inline void Invalid (uint16_t valid_mask) { meta_.valid_ &= (~valid_mask); }

    inline BitSet MatchBitSet (uint8_t hash) {
        auto bitset = _mm_set1_epi8 (hash);
        auto tag_meta = _mm_loadu_si128 (reinterpret_cast<const __m128i*> (tags_));
        uint16_t mask = _mm_cmpeq_epi8_mask (bitset, tag_meta);
        return BitSet (mask & meta_.valid_ & (~meta_.deleted_) &
                       kBitMapMask);  // valid filter and detetion filter.
    }

    inline BitSet EmptyBitSet () {
        return BitSet (((~meta_.valid_) | meta_.deleted_) & kBitMapMask);
    }

    inline BitSet ValidBitSet () { return BitSet (meta_.valid_ & (~meta_.deleted_) & kBitMapMask); }

    inline int ValidCount () {
        return __builtin_popcount (meta_.valid_ & (~meta_.deleted_) & kBitMapMask);
    }

    inline bool Full () { return __builtin_popcount (meta_.valid_ & kBitMapMask) == kBufNodeFull; }

    struct SortByKey {
        SortByKey (SortedBufNode* node) : node_ (node) {}
        bool operator() (const int& a, const int& b) {
            return node_->kvs_[a].key < node_->kvs_[b].key;
        }
        SortedBufNode* node_;
    };

    void Sort (void) {
        // step 1: obtain the valid pos
        std::vector<int> valid_pos;
        for (int i : ValidBitSet ()) {
            valid_pos.push_back (i);
        }

        // step 2: sort the valid_pos according to the related key
        std::sort (valid_pos.begin (), valid_pos.end (), SortByKey (this));

        // step 3: set the seqs_
        int si = 0;
        for (int i : valid_pos) {
            seqs_[si++] = i;
        }
    }

    inline bool Put (int64_t key, char* val) {
        size_t hash = Hasher::hash_int (key);
        return Put (key, val, hash);
    }

    inline bool Put (int64_t key, char* val, size_t hash) {
        size_t tag = hash & 0xFF;

        // step 1. check if node already has this key
        int old_pos = -1;
        for (int i : MatchBitSet (tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // update request
                old_pos = i;
                break;
            }
        }

        int valid_count = ValidCount ();
        if (old_pos == -1) {
            // new insertion
            if (valid_count == kBufNodeFull) {
                // full of records
                return false;
            }
        }

        // can insert
        int insert_pos = *EmptyBitSet ();  // obtain empty slot

        // set the key and value
        kvs_[insert_pos].key = key;
        kvs_[insert_pos].val = val;
        tags_[insert_pos] = tag;

        // atomicly set the meta
        Meta new_meta = meta_;
        new_meta.valid_ = meta_.valid_ | (1 << insert_pos);
        if (old_pos != -1) {
            // reset old pos if this is an update request.
            new_meta.valid_ ^= (1 << old_pos);
        }
        new_meta.deleted_ = meta_.deleted_ & (~(1 << insert_pos));

        BUFLOG_COMPILER_FENCE ();
        meta_.data_ = new_meta.data_;
        return true;
    }

    inline void insert (int64_t key, char* val, int i, int oi, uint8_t tag) {
        kvs_[i].key = key;
        kvs_[i].val = val;
        tags_[i] = tag;

        // atomicly set the meta
        Meta new_meta = meta_;
        new_meta.valid_ = meta_.valid_ | (1 << i);
        if (oi != -1) {
            // reset old pos if this is an update request.
            new_meta.valid_ ^= (1 << oi);
        }
        new_meta.deleted_ = meta_.deleted_ & (~(1 << i));

        BUFLOG_COMPILER_FENCE ();
        meta_.data_ = new_meta.data_;
    }

    inline bool Get (int64_t key, char*& val) {
        size_t hash = Hasher::hash_int (key);
        return Get (key, val, hash);
    }

    inline bool Get (int64_t key, char*& val, size_t hash) {
        size_t tag = hash & 0xFF;

        for (int i : MatchBitSet (tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // find the key
                val = kv.val;
                return true;
            }
        }
        return false;
    }

    inline bool Delete (int64_t key) {
        size_t hash = Hasher::hash_int (key);
        return Delete (key, hash);
    }

    inline bool Delete (int64_t key, size_t hash) {
        size_t tag = hash & 0xFF;

        for (int i : MatchBitSet (tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // find the key, set the deleted map
                meta_.deleted_ = meta_.deleted_ | (1 << i);
                return true;
            }
        }
        return false;
    }

    std::string ToString (void) {
        char buf[1024];
        std::string str_valid = print_binary (meta_.valid_);
        std::string str_deleted = print_binary (meta_.deleted_);
        std::string str_tags;
        std::string str_seqs;
        std::string str_kvs;
        for (int i = 0; i < 14; i++) {
            str_tags += std::to_string (tags_[i]) + " ";
            str_seqs += std::to_string (seqs_[i]) + " ";
            str_kvs += std::to_string (kvs_[i].key) + " ";
        }
        sprintf (buf, "valid: 0b%s, deleted: 0b%s, tags: %s, seqs: %s, k: %s", str_valid.c_str (),
                 str_deleted.c_str (), str_tags.c_str (), str_seqs.c_str (), str_kvs.c_str ());
        return buf;
    }

    void Print () {
        auto iter = Begin ();
        printf ("High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
        while (iter.Valid ()) {
            printf ("%ld, ", iter->key);
            iter++;
        }
        printf ("\n");
    }

    std::string ToStringValid () {
        char buf[1024];
        auto iter = Begin ();
        sprintf (buf, "High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
        while (iter.Valid ()) {
            sprintf (buf + strlen (buf), "%ld, ", iter->key);
            iter++;
        }
        return buf;
    }

    class IteratorSorted {
    public:
        IteratorSorted (SortedBufNode* node) : node_ (node), i_ (0), i_end_ (node->ValidCount ()) {}

        inline bool Valid (void) { return i_ < i_end_; }

        KV& operator* () const { return node_->kvs_[node_->seqs_[i_]]; }

        KV* operator-> () const { return &node_->kvs_[node_->seqs_[i_]]; }

        // Prefix ++ overload
        inline IteratorSorted& operator++ () {
            i_++;
            return *this;
        }

        // Postfix ++ overload
        inline IteratorSorted operator++ (int) {
            auto tmp = *this;
            i_++;
            return tmp;
        }

    private:
        SortedBufNode* node_;
        int i_;
        int i_end_;
    };

    IteratorSorted sBegin () { return IteratorSorted (this); }

    class Iterator {
    public:
        Iterator (SortedBufNode* node) : node_ (node), bitset_ (node->meta_.valid_) {}

        inline bool Valid (void) { return bitset_.operator bool (); }

        KV& operator* () const { return node_->kvs_[*bitset_]; }

        KV* operator-> () const { return &node_->kvs_[*bitset_]; }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            ++bitset_;
            return *this;
        }

        // Postfix ++ overload
        inline Iterator operator++ (int) {
            auto tmp = *this;
            ++bitset_;
            return tmp;
        }
        // private:
        SortedBufNode* node_;
        BitSet bitset_;
    };

    Iterator Begin () { return Iterator (this); }

    friend class Iterator;
    friend class IteratorSorted;

public:
    union Meta {
        struct {
            uint16_t valid_;
            uint16_t deleted_;
        };
        uint32_t data_;
    } meta_;  // 4 bytes
    AtomicSpinLock lock_;
    uint8_t full_;
    char tags_[13];
    signed char seqs_[13];  // 32 bytes
    int64_t highkey_;       // 40 bytes
    int64_t parentkey_;     // 48 bytes
    KV kvs_[13];            // 256 bytes
    uint64_t next_;
};

static_assert (sizeof (SortedBufNode) == 264, "SortBufNode is not 264 byte");

struct BufVec {
    static constexpr int kValNum = 8;

    BufVec () {
        memset (keys_, 0, sizeof (size_t) * kValNum);
        cur_ = 0;
    }

    void Reset () {
        memset (keys_, 0, sizeof (size_t) * kValNum);
        cur_ = 0;
    }

    void Sort () { std::sort (keys_, keys_ + cur_); }

    bool Contains (size_t key) {
        auto compare_key_vec = _mm512_set1_epi64 (key);
        auto stored_key_vec = _mm512_loadu_epi64 (reinterpret_cast<const __m512i*> (keys_));
        uint8_t mask = _mm512_cmpeq_epi64_mask (compare_key_vec, stored_key_vec);
        if (mask != 0) return true;
        return false;
    }

    bool Insert (size_t k) {
        if (cur_ >= kValNum - 1) {
            return false;
        } else {
            keys_[cur_++] = k;
            return true;
        }
    }

    bool CompactInsert (size_t k) {
        if (cur_ != kValNum - 1) {
            return false;
        }
        keys_[cur_++] = k;
        return true;
    }

    inline void Lock () { lock_.lock (); }

    inline void Unlock () { lock_.unlock (); }

    int Count () const { return cur_; }

    class Iterator {
    public:
        Iterator (const size_t* _keys, int i) : keys_ (_keys), cur_ (i) {}

        inline bool Valid () { return cur_ < kValNum; }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            cur_++;
            return *this;
        }

        size_t operator* () const { return keys_[cur_]; }

        const size_t* keys_;
        int cur_;
    };

    Iterator Begin () { return Iterator (keys_, 0); }

    size_t keys_[kValNum];
    int cur_;
    AtomicSpinLock lock_;
};

// A group of SortedBufNode
template <size_t NUM>
class WriteBuffer {
public:
    static_assert (__builtin_popcount (NUM) == 1, "NUM should be power of 2");
    static constexpr size_t kNodeNumMask = NUM - 1;
    static constexpr size_t kNodeNum = NUM;
    static constexpr size_t kProbeLen = NUM / 2 - 1;

    WriteBuffer () { local_depth = 0; }

    WriteBuffer (size_t d) { local_depth = d; }

    inline bool Put (int64_t key, char* val) {
        size_t hash = Hasher::hash_int (key);
        uint8_t tag = hash & 0xFF;
        int old_i = -1;
        int bucket_to_insert = -1;
        int slot_to_insert = -1;
        for (size_t p = 0; p < kProbeLen; ++p) {
            int idx = (hash + p) & kNodeNumMask;
            SortedBufNode& bucket = nodes_[idx];
            auto empty_bitset = bucket.EmptyBitSet ();

            for (int i : bucket.MatchBitSet (tag)) {
                KV& kv = bucket.kvs_[i];
                if (kv.key == key) {
                    // update
                    old_i = i;
                    bucket_to_insert = idx;
                    slot_to_insert = *empty_bitset;
                    goto put_probe_end;
                }
            }

            if (empty_bitset.validCount () > 1) {
                bucket_to_insert = idx;
                slot_to_insert = *empty_bitset;
            }

            if (!bucket.Full ()) {
                // reach search end
                bucket_to_insert = idx;
                slot_to_insert = *empty_bitset;
                break;
            }
        }
    put_probe_end:
        if (bucket_to_insert == -1) {
            // no avaliable slot
            return false;
        }

        nodes_[bucket_to_insert].insert (key, val, slot_to_insert, old_i, tag);
        return true;
    }

    inline bool Get (int64_t key, char*& val) {
        size_t hash = Hasher::hash_int (key);
        uint8_t tag = hash & 0xFF;
        for (size_t p = 0; p < kProbeLen; ++p) {
            int idx = (hash + p) & kNodeNumMask;
            SortedBufNode& bucket = nodes_[idx];

            for (int i : bucket.MatchBitSet (tag)) {
                KV& kv = bucket.kvs_[i];
                if (kv.key == key) {
                    val = kv.val;
                    return true;
                }
            }

            if (!bucket.Full ()) {
                // reach search end
                return false;
            }
        }
        return false;
    }

    inline bool Delete (int64_t key) {
        size_t hash = Hasher::hash_int (key);
        size_t tag = hash & 0xFF;
        for (int p = 0; p < kProbeLen; ++p) {
            int idx = (hash + p) & kNodeNumMask;
            SortedBufNode& bucket = nodes_[idx];

            for (int i : bucket.MatchBitSet (tag)) {
                KV& kv = bucket.kvs_[i];
                if (kv.key == key) {
                    // find the key, set the deleted map
                    bucket.meta_.deleted_ = bucket.meta_.deleted_ | (1 << i);
                    return true;
                }
            }

            if (!bucket.Full ()) {
                // reach search end
                return false;
            }
        }
        return false;
    }

    inline void Lock () { lock_.lock (); }

    inline void Unlock () { lock_.unlock (); }

    inline void Reset () {
        for (size_t i = 0; i < kNodeNum; i++) {
            nodes_[i].Reset ();
        }
    }
    class Iterator {
    public:
        Iterator (WriteBuffer<kNodeNum>* wb) : wb_ (wb), cur_ (0), iter (wb_->nodes_[0].Begin ()) {
            toNextValidIter ();
        }

        inline bool Valid (void) { return iter.Valid () || cur_ < kNodeNum; }

        KV& operator* () const { return *iter; }

        KV* operator-> () const { return &(*iter); }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            iter++;
            if (!iter.Valid ()) {
                toNextValidIter ();
            }
            return *this;
        }

        void toNextValidIter (void) {
            while (!iter.Valid () && cur_ < kNodeNum) {
                cur_++;
                if (cur_ == kNodeNum) {
                    break;
                }
                iter = wb_->nodes_[cur_].Begin ();
            }
        }
        WriteBuffer<kNodeNum>* wb_;
        size_t cur_;
        SortedBufNode::Iterator iter;
    };

    Iterator Begin () { return Iterator (this); }

    SortedBufNode nodes_[NUM];
    AtomicSpinLock lock_;
    size_t local_depth;
};

};  // end of namespace buflog

#endif
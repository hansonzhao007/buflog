#ifndef _BUFLOG_H_
#define _BUFLOG_H_

#include "turbo_hash.h"
#include "slice.h"

#include <immintrin.h>
#include <inttypes.h>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>

#include <atomic>

#include <libpmem.h>

#include "logger.h"

#define BUFLOG_FLUSH(addr) asm volatile ("clwb (%0)" :: "r"(addr))
#define BUFLOG_FLUSHFENCE asm volatile ("sfence" ::: "memory")


inline void BUFLOG_COMPILER_FENCE() {
    std::atomic_thread_fence(std::memory_order_release);
    // asm volatile("" : : : "memory"); /* Compiler fence. */
}


namespace buflog {

inline std::string print_binary(uint16_t bitmap) {
    char buffer[1024];
    static std::string bit_rep[16] = {
        "0000", "0001", "0010", "0011",
        "0100", "0101", "0110", "0111",
        "1000", "1001", "1010", "1011",
        "1100", "1101", "1110", "1111"
    };
    sprintf(buffer, "%s%s%s%s", 
        bit_rep[(bitmap >> 12) & 0x0F].c_str(),
        bit_rep[(bitmap >>  8) & 0x0F].c_str(),
        bit_rep[(bitmap >>  4) & 0x0F].c_str(),
        bit_rep[(bitmap >>  0) & 0x0F].c_str()
    );
    return buffer;
}
    
inline bool isPowerOfTwo(size_t n) {
    return (n != 0 && (__builtin_popcount(n >> 32) + __builtin_popcount(n & 0xFFFFFFFF)) == 1 );
}


// Usage example:
// BitSet bitset(0x05); 
// for (int i : bitset) {
//     printf("i: %d\n", i);
// }
// this will print out 0, 2
class BitSet {
public:
    BitSet():
        bits_(0) {}

    explicit BitSet(uint32_t bits): bits_(bits) {}

    BitSet(const BitSet& b) {
        bits_ = b.bits_;
    }
    
    inline BitSet& operator++() {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool() const { return bits_ != 0; }

    inline int operator*() const { 
        // count the tailing zero bit
        return __builtin_ctz(bits_); 
    }

    inline BitSet begin() const { return *this; }

    inline BitSet end() const { return BitSet(0); }

    inline uint32_t bit() {
        return bits_;
    }
private:
    friend bool operator==(const BitSet& a, const BitSet& b) {
        return a.bits_ == b.bits_;
    }
    friend bool operator!=(const BitSet& a, const BitSet& b) {
        return a.bits_ != b.bits_;
    }
    uint32_t bits_;
};

/** Hasher
 *  @note: provide hash function for string
*/
class Hasher {
public:

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using uint128_t = unsigned __int128;
#pragma GCC diagnostic pop
#endif

    static inline uint64_t umul128(uint64_t a, uint64_t b, uint64_t* high) noexcept {
        auto result = static_cast<uint128_t>(a) * static_cast<uint128_t>(b);
        *high = static_cast<uint64_t>(result >> 64U);
        return static_cast<uint64_t>(result);
    }

    static inline size_t hash ( const void * key, int len)
    {
        return ((MurmurHash64A(key, len)));
    }

    static inline size_t hash_int(uint64_t obj) noexcept {
        // 167079903232 masksum, 120428523 ops best: 0xde5fb9d2630458e9
        static constexpr uint64_t k = UINT64_C(0xde5fb9d2630458e9);
        uint64_t h;
        uint64_t l = umul128(obj, k, &h);
        return h + l;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    static inline uint64_t MurmurHash64A ( const void * key, int len)
    {
        const uint64_t m = UINT64_C(0xc6a4a7935bd1e995);
        const uint64_t seed = UINT64_C(0xe17a1465);
        const int r = 47;

        uint64_t h = seed ^ (len * m);

        const uint64_t * data = (const uint64_t *)key;
        const uint64_t * end = data + (len/8);

        while(data != end)
        {
            uint64_t k = *data++;

            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;
        }

        const unsigned char * data2 = (const unsigned char*)data;

        switch(len & 7)
        {
        case 7: h ^= ((uint64_t)data2[6]) << 48;
        case 6: h ^= ((uint64_t)data2[5]) << 40;
        case 5: h ^= ((uint64_t)data2[4]) << 32;
        case 4: h ^= ((uint64_t)data2[3]) << 24;
        case 3: h ^= ((uint64_t)data2[2]) << 16;
        case 2: h ^= ((uint64_t)data2[1]) << 8; 
        case 1: h ^= ((uint64_t)data2[0]);
            h *= m;
        };

        h ^= h >> r;
        h *= m;
        h ^= h >> r;

        return h;
    }
#pragma GCC diagnostic pop

}; // end fo class Hasher

/** A redo log that connect log entry with linked list.
 *  Note: After we link the log entries with linked list, we 
 *        can replay from the log tail to log head (new -> old).
 *        Meanwhile, we only have to recover the head node of each linked-list,
 *        so the recover speed should be fast.
*/
namespace linkedredolog {

const size_t kDataLogNodeSizeLimit = 65536;

enum DataLogNodeMetaType: unsigned char {
    kDataLogNodeValid = 0x10,        // a valid data log node
    kDataLogNodeLinkEnd = 0x11,      // reach to a end of the linked list because the data in buffer has been merged
    kDataLogNodeCheckpoint = 0x12,   // the checkpoint node. recover should stop reverse scan if this node appears.
    kDataLogNodeFail
};

using DataLogNodePtr  = uint64_t;
using log_data_size   = uint32_t;
/** 
 *  Format:
 *  | size | .. data ... |  DataLogNodeMeta |
 *         |              
 *        Data()
 * 
 *  `size`: 4 bytes
 *      size of the `data`
 *  `data`: any bytes
 *      any length of record
 *  `DataLogNodeMeta`: 16 bytes 
 *      type_:
 *      none_:
 *      data_size_: equals to `size`
 *      next_;
*/
class DataLogNodeMeta {
public:
    inline DataLogNodeMeta* Next(void) {
        return reinterpret_cast<DataLogNodeMeta*>(next_);
    }

    inline util::Slice Data(void) {
        return util::Slice((char*)(this) - data_size_, data_size_);
    }

    inline bool Valid(void) {
        return type_ < kDataLogNodeFail &&              // type is valid
               type_ >= kDataLogNodeValid &&
               data_size_ <= kDataLogNodeSizeLimit &&   // size is valid
               (Hasher::hash(Data().data(), Data().size()) & 0xFF) == checksum_; // checksum is valid
    }

    std::string ToString(void) {
        char buffer[1024];
        sprintf(buffer, "type: %02d, checksum: 0x%x, data size: %u - %s. next off: %lu", 
            type_,            
            checksum_,
            data_size_,
            Data().ToString().c_str(),
            next_);
        return buffer;
    }

    DataLogNodeMetaType type_;
    uint8_t             checksum_;
    log_data_size       data_size_;
    DataLogNodePtr      next_;
};

class DataLogNodeBuilder {
public:
    explicit DataLogNodeBuilder(const char* data_addr, log_data_size size, DataLogNodePtr next) {
        size_ = sizeof(log_data_size) + size + sizeof(DataLogNodeMeta);
        buf_  = (char*)malloc(size_);
        *(log_data_size*)(buf_) = size;
        memcpy(buf_ + sizeof(log_data_size), data_addr, size);
        DataLogNodeMeta* meta = reinterpret_cast<DataLogNodeMeta*>(buf_ + sizeof(log_data_size) + size);
        meta->type_ = kDataLogNodeValid;
        meta->data_size_ = size;
        meta->next_ = next;
        meta->checksum_ = Hasher::hash(data_addr, size) & 0xFF;
    }

    ~DataLogNodeBuilder(){
        free(buf_);
    }

    char*   buf_;
    size_t  size_;
};

struct DataLogMeta {
    uint64_t size_;         // lenght of this log file
    uint64_t commit_tail_;  // current tail
    char    _none[240];
};

static_assert(sizeof(DataLogMeta) == 256, "size of LogMeta is not 256 byte");

/**
 *  |  DataLogMeta  | Log data ....
 *  |   256 byte    |          ...
*/
class DataLog {
public:
    DataLog():
        log_size_(0),
        log_size_mask_(0),
        log_start_addr_(nullptr),
        log_cur_tail_(256){}
    
    // create a new log with size
    bool Create(char* addr, size_t size) {
        if (!isPowerOfTwo(size)) {
            perror("Data log is not power of 2.");
            return false;
        }
        log_size_       = size;
        log_size_mask_  = size - 1;
        log_start_addr_ = addr;
        log_cur_tail_   = 256;
        return true;
    }

    void Open(char* addr) {
        DataLogMeta* meta = reinterpret_cast<DataLogMeta*>(addr);
        log_size_ = meta->size_;
        log_size_mask_ = log_size_ - 1;
        log_start_addr_ = addr;
        log_cur_tail_ = meta->commit_tail_;
    }
    
    void CommitTail(bool is_pmem) {
        DataLogMeta* meta = reinterpret_cast<DataLogMeta*>(log_start_addr_);
        meta->commit_tail_ = log_cur_tail_;

        if (is_pmem) {
            BUFLOG_FLUSH(meta);
            BUFLOG_FLUSHFENCE;
        }
    }

    inline uint64_t Append(DataLogNodeBuilder& entry, bool is_pmem) {
        size_t off = log_cur_tail_.fetch_add(entry.size_, std::memory_order_relaxed);
        if (is_pmem) {
            pmem_memcpy_nodrain(log_start_addr_ + off, entry.buf_, entry.size_);
        } else {
            memcpy(log_start_addr_ + off, entry.buf_, entry.size_);
        }
        return off;
    };

    inline uint64_t LogTail(void) {
        return log_cur_tail_;
    }

    inline char* Append(util::Slice entry, DataLogNodePtr next, uint8_t checksum, bool is_pmem) {
        int total_size = sizeof(log_data_size) + entry.size() + sizeof(DataLogNodeMeta);
        size_t off = log_cur_tail_.fetch_add(total_size, std::memory_order_relaxed);
        char* addr = log_start_addr_ + off;
        // | size | .. data ... |  DataLogNodeMeta |
        *(log_data_size*)(addr) = entry.size();
        if (is_pmem) {
            pmem_memcpy_nodrain(addr + sizeof(log_data_size), entry.data(), entry.size());
        } else {
            memcpy(addr + sizeof(log_data_size), entry.data(), entry.size());
        }
        
        DataLogNodeMeta meta;
        meta.type_ = kDataLogNodeValid;
        meta.data_size_ = entry.size();
        meta.next_ = next;
        meta.checksum_ = checksum;
        if (is_pmem) {
            pmem_memcpy_nodrain(addr + sizeof(log_data_size) + entry.size(), &meta, sizeof(DataLogNodeMeta));
        } else {
            memcpy(addr + sizeof(log_data_size) + entry.size(), &meta, sizeof(DataLogNodeMeta));
        }

        if (is_pmem) {
            pmem_flush(addr + off, total_size);
        }
        
        return log_start_addr_ + off;
    }


    /**
     *  An iterator that scan the log from left to right
    */
    class SeqIterator {
    public:
        SeqIterator(uint64_t front_offset, char* log_start_addr):
            front_off_(front_offset),
            log_addr_(log_start_addr) {
        }

        // Check if the DataLogNode pointed by myself is valid or not
        inline bool Valid(void) const {
            return (*this)->Valid();
        }

        inline DataLogNodeMeta operator*() const{ 
            return *this->operator->();
        }

        inline DataLogNodeMeta* operator->(void) const {
            log_data_size data_size = *reinterpret_cast<log_data_size*>(log_addr_ + front_off_);
            return reinterpret_cast<DataLogNodeMeta*>(log_addr_ + front_off_ + sizeof(log_data_size) + data_size);
        }
        
        // Prefix ++ overload 
        inline SeqIterator& operator++() 
        { 
            front_off_ += size();
            return *this; 
        }

        // Postfix ++ overload 
        inline SeqIterator operator++(int) 
        { 
            auto tmp = *this;
            front_off_ += size();
            return tmp; 
        }
        
    private:
        // size of the entire LogDataNode
        inline size_t size(void) {
            log_data_size data_size = *reinterpret_cast<log_data_size*>(log_addr_ + front_off_);
            return sizeof(log_data_size) + data_size + sizeof(DataLogNodeMeta);
        }

        size_t front_off_;
        char*  log_addr_;
    }; // end of class SeqIterator

    class ReverseIterator {
    public:
        ReverseIterator(uint64_t end_off, char* log_start_addr):
            end_off_(end_off),
            log_addr_(log_start_addr) {
        }

        // Check if the DataLogNode pointed by myself is valid or not
        inline bool Valid(void) const {
            return end_off_ > 256; // should be larger than the DataLogMeta
        }

        inline DataLogNodeMeta operator*() const{ 
            return *this->operator->();
        }

        inline DataLogNodeMeta* operator->(void) const {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*>(log_addr_ + end_off_ - sizeof(DataLogNodeMeta));
            return node_meta;
        }

        // Prefix -- overload 
        inline ReverseIterator& operator--() 
        { 
            end_off_ -= NodeSize();
            return *this;
        }

        // Postfix -- overload 
        inline ReverseIterator operator--(int) 
        { 
            auto tmp = *this;
            end_off_ -= NodeSize();
            return tmp; 
        }
        
    private:
        // size of the entire LogDataNode of data log node 
        inline size_t NodeSize(void) {
            DataLogNodeMeta* node_meta = reinterpret_cast<DataLogNodeMeta*>(log_addr_ + end_off_ - sizeof(DataLogNodeMeta));
            return sizeof(log_data_size) + node_meta->data_size_ + sizeof(DataLogNodeMeta);
        }
        int64_t end_off_;
        char*   log_addr_;
    };

    SeqIterator Begin(void) {
        return SeqIterator(256, log_start_addr_);
    }

    ReverseIterator rBegin(void) {
        return ReverseIterator(log_cur_tail_, log_start_addr_);
    }

private:
    size_t               log_size_;
    size_t               log_size_mask_;
    char*                log_start_addr_;
    std::atomic<size_t>  log_cur_tail_;
};

} // end of namespace linkedredolog


// https://rigtorp.se/spinlock/
class AtomicSpinLock {
public:
    std::atomic<bool> lock_ = {0};

    void inline lock() noexcept {
        for (;;) {
        // Optimistically assume the lock is free on the first try
        if (!lock_.exchange(true, std::memory_order_acquire)) {
            return;
        }
        // Wait for lock to be released without generating cache misses
        while (lock_.load(std::memory_order_relaxed)) {
            // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
            // hyper-threads
            __builtin_ia32_pause();
        }
        }
    }

    bool inline is_locked(void) noexcept {
        return lock_.load(std::memory_order_relaxed);
    }

    bool inline try_lock() noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load(std::memory_order_relaxed) &&
            !lock_.exchange(true, std::memory_order_acquire);
    }

    void inline unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
}; // end of class AtomicSpinLock

struct KV {
    int64_t  key;
    char*    val;
};

class SortedBufNode {
public:
    static constexpr uint16_t kBufNodeFull = 12;
    static constexpr uint16_t kBitMapMask = 0x1FFF;

    SortedBufNode () {
        memset(this, 0, 64);
        highkey_ = -1;
        parentkey_ = -1;
    }
    
    inline void Lock(void) {
        lock_.lock();
    }

    inline void Unlock(void) {
        lock_.unlock();
    }

    inline void Reset(void) {
        meta_.data_ = 0;
    }
    
    inline void MaskLastN(int n) {
        uint16_t mask = 0;
        int count = ValidCount();
        for (int i = count - n; i < count; ++i) {
            mask |= (1 << seqs_[i]);
        }
        meta_.valid_ &= ((~mask) & kBitMapMask);    
        BUFLOG_COMPILER_FENCE();
    }

    inline void Invalid(uint16_t valid_mask) {
        meta_.valid_ &= (~valid_mask);
    }

    inline BitSet MatchBitSet(uint8_t hash) {
        auto bitset = _mm_set1_epi8(hash);
        auto tag_meta = _mm_loadu_si128(reinterpret_cast<const __m128i*>(tags_));
        uint16_t mask = _mm_cmpeq_epi8_mask(bitset, tag_meta);
        return BitSet(mask & meta_.valid_ & (~meta_.deleted_) & kBitMapMask);  // valid filter and detetion filter.
    }

    inline BitSet EmptyBitSet() {
        return BitSet( ((~meta_.valid_) | meta_.deleted_) & kBitMapMask);
    }

    inline BitSet ValidBitSet() {
        return BitSet(meta_.valid_ & (~meta_.deleted_) & kBitMapMask);
    }

    inline int ValidCount() {
        return __builtin_popcount(meta_.valid_ & (~meta_.deleted_) & kBitMapMask);
    }

    struct SortByKey {
        SortByKey(SortedBufNode* node): node_(node) {
        }
        bool operator()(const int& a, const int& b) {
            return node_->kvs_[a].key < node_->kvs_[b].key;
        }
        SortedBufNode* node_;
    };

    void Sort(void) {
        // step 1: obtain the valid pos
        std::vector<int> valid_pos;
        for (int i : ValidBitSet()) {
            valid_pos.push_back(i);
        }  

        // step 2: sort the valid_pos according to the related key
        std::sort(valid_pos.begin(), valid_pos.end(), SortByKey(this));

        // step 3: set the seqs_
        int si = 0;
        for (int i : valid_pos) {
            seqs_[si++] = i;
        }
    }

    inline bool Put(int64_t key, char* val) {
        size_t hash = Hasher::hash_int(key);
        return Put(key, val, hash);
    }

    inline bool Put(int64_t key, char* val, size_t hash) {        
        size_t tag  = hash & 0xFF;

        // step 1. check if node already has this key
        int old_pos = -1;
        for (int i : MatchBitSet(tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // update request
                old_pos = i;
            } 
        }

        int valid_count = ValidCount();
        if (old_pos == -1) {
            // new insertion
            if (valid_count == kBufNodeFull) {
                // full of records
                return false;
            } 
        }

        // can insert
        int insert_pos = *EmptyBitSet(); // obtain empty slot

        // set the key and value
        kvs_[insert_pos].key = key;
        kvs_[insert_pos].val = val;
        tags_[insert_pos]    = tag;

        // atomicly set the meta
        Meta new_meta = meta_;
        new_meta.valid_ = meta_.valid_ | (1 << insert_pos);
        if (old_pos != -1) {
            // reset old pos if this is an update request.
            new_meta.valid_ ^= (1 << old_pos);
        }
        new_meta.deleted_ = meta_.deleted_ & (~(1 << insert_pos));

        BUFLOG_COMPILER_FENCE();
        meta_.data_ = new_meta.data_;
        return true;
    }

    inline bool Get(int64_t key, char*& val) {
        size_t hash = Hasher::hash_int(key);
        return Get(key, val, hash);
    }

    inline bool Get(int64_t key, char*& val, size_t hash) {        
        size_t tag  = hash & 0xFF;

        for (int i : MatchBitSet(tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // find the key
                val = kv.val;
                return true;
            } 
        }
        return false;
    }

    inline bool Delete(int64_t key) {
        size_t hash = Hasher::hash_int(key);
        return Delete(key, hash);
    }

    inline bool Delete(int64_t key, size_t hash) {        
        size_t tag  = hash & 0xFF;

        for (int i : MatchBitSet(tag)) {
            KV kv = kvs_[i];
            if (kv.key == key) {
                // find the key, set the deleted map
                meta_.deleted_ = meta_.deleted_ | (1 << i);
                return true;
            } 
        }
        return false;
    }

    std::string ToString(void) {
        char buf[1024];
        std::string str_valid = print_binary(meta_.valid_);
        std::string str_deleted = print_binary(meta_.deleted_);
        std::string str_tags;
        std::string str_seqs;
        std::string str_kvs;
        for (int i = 0; i < 14; i++) {
            str_tags += std::to_string(tags_[i]) + " ";
            str_seqs += std::to_string(seqs_[i]) + " ";
            str_kvs  += std::to_string(kvs_[i].key) + " ";
        }
        sprintf(buf, "valid: 0b%s, deleted: 0b%s, tags: %s, seqs: %s, k: %s", 
                str_valid.c_str(),
                str_deleted.c_str(),
                str_tags.c_str(),
                str_seqs.c_str(),
                str_kvs.c_str());
        return buf;
    }

    void Print() {
        auto iter = Begin();
        printf("High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
        while (iter.Valid()) {
            printf("%u, ", iter->key);
            iter++;
        }
        printf("\n");
    }

    std::string ToStringValid() {
        char buf[1024];
        auto iter = Begin();
        sprintf(buf, "High key: %ld. Parent: %ld, Valid key: ", highkey_, parentkey_);
        while (iter.Valid()) {
            sprintf(buf + strlen(buf),"%u, ", iter->key);
            iter++;
        }
        return buf;
    }
    
    class IteratorSorted {
    public:
        IteratorSorted(SortedBufNode* node) :
            node_(node),
            i_(0),
            i_end_(node->ValidCount()) {
        }

        inline bool Valid(void) {
            return i_ < i_end_;
        }

        KV& operator*() const {
            return node_->kvs_[node_->seqs_[i_]];
        }

        KV* operator->() const {
            return &node_->kvs_[node_->seqs_[i_]];
        }

        // Prefix ++ overload 
        inline IteratorSorted& operator++() { 
            i_++;
            return *this; 
        }

        // Postfix ++ overload 
        inline IteratorSorted operator++(int) { 
            auto tmp = *this;
            i_++;
            return tmp; 
        }
    private:
        SortedBufNode* node_;
        int i_;
        int i_end_;
    };

    IteratorSorted sBegin() {
        return IteratorSorted(this);
    }

    class Iterator {
    public:
        Iterator(SortedBufNode* node) :
            node_(node),
            bitset_(node->meta_.valid_) {
        }

        inline bool Valid(void) {
            return bitset_.operator bool();
        }

        KV& operator*() const {
            return node_->kvs_[*bitset_];
        }

        KV* operator->() const {
            return &node_->kvs_[*bitset_];
        }

        // Prefix ++ overload 
        inline Iterator& operator++() { 
            ++bitset_;
            return *this; 
        }

        // Postfix ++ overload 
        inline Iterator operator++(int) { 
            auto tmp = *this;
            ++bitset_;
            return tmp; 
        }
    // private:
        SortedBufNode* node_;
        BitSet bitset_;
    };
    
    Iterator Begin() {
        return Iterator(this);
    }

    friend class Iterator;
    friend class IteratorSorted;

public:
    union Meta{
        struct {
            uint16_t    valid_;
            uint16_t    deleted_;
        };
        uint32_t data_;
    } meta_;                 // 4 bytes
    AtomicSpinLock  lock_; 
    uint8_t         full_;       
    char            tags_[13];   
    signed char     seqs_[13];   // 32 bytes
    int64_t         highkey_;    // 40 bytes
    int64_t         parentkey_;  // 48 bytes
    KV              kvs_[13];    // 256 bytes
};

static_assert(sizeof(SortedBufNode) == 256, "SortBufNode is not 256 byte");

// A group of SortedBufNode
template<size_t NUM>
class WriteBuffer {
public:
    static_assert(__builtin_popcount(NUM) == 1, "NUM should be power of 2");
    static constexpr size_t kNodeNumMask = NUM - 1;
    static constexpr size_t kNodeNum = NUM;
    static constexpr size_t kProbeLen = NUM / 2;
    
    WriteBuffer() {
        local_depth = 0;
    }

    WriteBuffer(size_t d) {
        local_depth = d;
    }
    
    inline bool Put(int64_t key, char* val) {
        size_t hash = Hasher::hash_int(key);
        for (int i = 0; i < kProbeLen; ++i) {
            int idx = (hash + i) & kNodeNumMask;
            bool res = nodes_[idx].Put(key, val, hash);
            if (res) return true;
        }
        return false;
    }

    inline bool Get(int64_t key, char*& val) {
        size_t hash = Hasher::hash_int(key);
        for (int i = 0; i < kProbeLen; ++i) {
            int idx = (hash + i) & kNodeNumMask;
            bool res = nodes_[idx].Get(key, val, hash);
            if (res) return true;
        }
        return false;
    }

    inline bool Delete(int64_t key) {
        size_t hash = Hasher::hash_int(key);
        return nodes_[hash & kNodeNumMask].Delete(key, hash);
    }

    inline void Lock() {
        lock_.lock();
    }

    inline void Unlock() {
        lock_.unlock();
    }

    inline void Reset() {
        for (int i = 0; i < kNodeNum; i++) {
            nodes_[i].Reset();
        }
    }
    class Iterator {
    public:
        Iterator(WriteBuffer<kNodeNum>* wb):
            wb_(wb),
            cur_(0),
            iter(wb_->nodes_[0].Begin()) {
            toNextValidIter();
        }

        inline bool Valid(void) {            
            return iter.Valid() || cur_ < kNodeNum;
        }

        KV& operator*() const {
            return *iter;
        }

        KV* operator->() const {
            return &(*iter);
        }
        
        // Prefix ++ overload 
        inline Iterator& operator++() {
            iter++;
            if (!iter.Valid()) {
                toNextValidIter();  
            }          
            return *this; 
        }

        void toNextValidIter(void) {
            while (!iter.Valid() && cur_ < kNodeNum) {
                cur_++;
                if (cur_ == kNodeNum) {
                    break;
                }
                iter = wb_->nodes_[cur_].Begin();                
            }
        }
        WriteBuffer<kNodeNum>* wb_;
        SortedBufNode::Iterator iter;
        int cur_;
    };


    Iterator Begin() {
        return Iterator(this);
    }
    
    SortedBufNode nodes_[NUM];
    AtomicSpinLock lock_;
    size_t local_depth;
};

class BufNode128 {
public:


private:


};

}; // end of namespace buflog

#endif
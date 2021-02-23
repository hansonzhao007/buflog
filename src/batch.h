#ifndef _XSBATCH_H_
#define _XSBATCH_H_

#include <inttypes.h>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>

#include <atomic>

#include <libpmem.h>

#define XSBATCH_FLUSH(addr) asm volatile ("clwb (%0)" :: "r"(addr))
#define XSBATCH_FLUSHFENCE asm volatile ("sfence" ::: "memory")

namespace xsbatch {
    
inline bool isPowerOfTwo(size_t n) {
    return (n != 0 && (__builtin_popcount(n >> 32) + __builtin_popcount(n & 0xFFFFFFFF)) == 1 );
}

/** Slice
 *  @note: Derived from LevelDB. the data is stored in the *data_
*/
class Slice {
public:
    using type = Slice;
    // operator <
    bool operator < (const Slice& b) const {
        return compare(b) < 0 ;
    }

    bool operator > (const Slice& b) const {
        return compare(b) > 0 ;
    }

    // explicit conversion
    inline operator std::string() const {
        return std::string(data_, size_);
    }

    // Create an empty slice.
    Slice() : data_(""), size_(0) { }

    // Create a slice that refers to d[0,n-1].
    Slice(const char* d, size_t n) : data_(d), size_(n) { }

    // Create a slice that refers to the contents of "s"
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) { }

    // Create a slice that refers to s[0,strlen(s)-1]
    Slice(const char* s) : 
        data_(s), 
        size_((s == nullptr) ? 0 : strlen(s)) {
    }

    // Return a pointer to the beginning of the referenced data
    inline const char* data() const { return data_; }

    // Return the length (in bytes) of the referenced data
    inline size_t size() const { return size_; }

    // Return true iff the length of the referenced data is zero
    inline bool empty() const { return size_ == 0; }

    // Return the ith byte in the referenced data.
    // REQUIRES: n < size()
    inline char operator[](size_t n) const {
        assert(n < size());
        return data_[n];
    }

    // Change this slice to refer to an empty array
    inline void clear() { data_ = ""; size_ = 0; }

    inline std::string ToString() const {
        std::string res;
        res.assign(data_, size_);
        return res;
    }

    // Three-way comparison.  Returns value:
    //   <  0 iff "*this" <  "b",
    //   == 0 iff "*this" == "b",
    //   >  0 iff "*this" >  "b"
    inline int compare(const Slice& b) const {
        assert(data_ != nullptr && b.data_ != nullptr);
        const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
        int r = memcmp(data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_) r = -1;
            else if (size_ > b.size_) r = +1;
        }
        return r;
    }

    friend std::ostream& operator<<(std::ostream& os, const Slice& str) {
        os <<  str.ToString();
        return os;
    }

    const char* data_;
    size_t size_;
}; // end of class Slice

inline bool operator==(const Slice& x, const Slice& y) {
    return ((x.size() == y.size()) &&
            (memcmp(x.data(), y.data(), x.size()) == 0));
}

inline bool operator!=(const Slice& x, const Slice& y) {
    return !(x == y);
}

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
using log_data_size = uint32_t;
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

    inline Slice Data(void) {
        return Slice((char*)(this) - data_size_, data_size_);
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
            XSBATCH_FLUSH(meta);
            XSBATCH_FLUSHFENCE;
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
            DataLogNodeMeta* pre_node_meta = reinterpret_cast<DataLogNodeMeta*>(log_addr_ + end_off_ - sizeof(DataLogNodeMeta));
            return pre_node_meta;
        }

        // Prefix -- overload 
        inline ReverseIterator& operator--() 
        { 
            end_off_ -= preNodeSize();
            return *this;
        }

        // Postfix -- overload 
        inline ReverseIterator operator--(int) 
        { 
            auto tmp = *this;
            end_off_ -= preNodeSize();
            return tmp; 
        }
        
    private:
        // size of the entire LogDataNode of previous node
        inline size_t preNodeSize(void) {
            DataLogNodeMeta* pre_node_meta = reinterpret_cast<DataLogNodeMeta*>(log_addr_ + end_off_ - sizeof(DataLogNodeMeta));
            return sizeof(log_data_size) + pre_node_meta->data_size_ + sizeof(DataLogNodeMeta);
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

class BufferNode {
public:

private:

}; // end of class buffer node

}; // end of namespace xsbatch

#endif
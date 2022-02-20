#ifndef _SPOTON_LOG_H_
#define _SPOTON_LOG_H_

#include <immintrin.h>
#include <inttypes.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include "spoton_util.h"

namespace spoton {

class logptr_t {
private:
    static inline uint64_t kEmptyLogPtr{0xFFFF'0000'0000'0000};

    static inline std::array<char*, 64> kLogBaseAddress{};

    // MSB
    // * +----------------------------------+
    // * | logid (16 bit) | offset (48 bit) |
    // * +----------------------------------+
    union {
        // LSB first for little endian
        struct {
            uint64_t offset : 48;
            uint64_t logid : 16;
        };
        uint64_t data;
    };

public:
    logptr_t () : data (kEmptyLogPtr) {}
    logptr_t (uint16_t lid, uint64_t off) : offset (off), logid (lid){};

    static void RegistertBaseAddress (uint16_t logid, char* addr) { kLogBaseAddress[logid] = addr; }

    inline uint64_t getOffset () { return offset; }
    inline uint16_t getLogID () { return logid; }

    inline uint64_t getData () { return data; }

    inline char* vaddr () {
        if (data != kEmptyLogPtr) {
            return kLogBaseAddress[logid] + offset;
        }
        return nullptr;
    }

    bool operator== (const logptr_t& p) { return this->data == p.data; }
    inline explicit operator bool () const { return this->data != kEmptyLogPtr; }
};

const logptr_t logptr_nullptr{};
/**
 * @brief A redo log that connect log entry with linked list.
 *  Note: After we link the log entries with linked list, we
 *        can replay from the log tail to log head (new -> old).
 *        Meanwhile, we only have to recover the head node of each linked-list,
 *        so the recover speed should be fast.
 */
enum LogNodeType_t : unsigned char {
    kLogNodeValid = 0x10,  // a valid data log node
    kLogNodeLinkEnd =
        0x11,  // reach to a end of the linked list because the data in buffer has been merged
    kLogNodeCheckpoint =
        0x12,  // the checkpoint node. recover should stop reverse scan if this node appears.
    kLogNodeFail
};

/**
 * @brief Log Node Format
 * +------------------ NodeMeta (16B) ----------------+-....-+-----------+
 * | type (1B) | size (1B) | checksum (6B) | ptr (8B) | data | size (1B) |
 * +--------------------------------------------------+-....-+-----------+
 *  NodeMeta:
 *      type:
 *      size: data size
 *      ptr; points to previous LogNode_t address
 *      checkum:
 *  data:
 */
class LogNode_t {
private:
    uint64_t type_ : 8;
    uint64_t size_ : 8;
    uint64_t checksum_ : 48;
    logptr_t ptr_;
    char data_[0];

public:
    /**
     * @brief return the next LogNode on the linked list. The next node is at eralier offset on the
     * log
     *
     * @return LogNode_t*
     */
    inline LogNode_t* ListNext (void) { return reinterpret_cast<LogNode_t*> (ptr_.vaddr ()); }

    /**
     * @brief return next node on the Log
     *
     * @return LogNode_t*
     */
    inline LogNode_t* Next (void) {
        return reinterpret_cast<LogNode_t*> (reinterpret_cast<char*> (this) + sizeof (LogNode_t) +
                                             size_ + 1);
    }

    /**
     * @brief Set the Data object with varialble size
     *
     * @param type
     * @param data
     * @param len
     * @param ptr
     */
    void SetData (LogNodeType_t type, const char* data, size_t len, logptr_t ptr);

    /**
     * @brief Set the Data object with fixed key-val size
     *
     * @param type
     * @param key
     * @param val
     * @param ptr
     */
    void SetData (LogNodeType_t type, uint64_t key, uint64_t val, logptr_t ptr);

    /**
     * @brief Get the LogNode type
     *
     * @return LogNodeType_t
     */
    LogNodeType_t GetType () { return static_cast<LogNodeType_t> (type_); }

    inline char* GetData (void) { return data_; }
    inline size_t GetDataSize (void) { return size_; }

    inline Slice GetDataSlice (void) { return Slice (data_, size_); }

    /**
     * @brief Get the size of this lognode (including metadata and data)
     *
     * @return size_t
     */
    inline size_t GetSize (void) { return sizeof (LogNode_t) + size_ + 1; }

    inline bool Valid (void) {
        return this->type_ < kLogNodeFail &&  // type is valid
               this->type_ >= kLogNodeValid;
    }

    std::string ToString (void) {
        char buffer[1024];
        sprintf (buffer,
                 "type: %02d, data size: %u - %s. ptr: 0x%lx, logid: %u, off: %lu, checksum: %lx",
                 type_, size_, GetDataSlice ().ToString ().c_str (), ptr_.vaddr (),
                 ptr_.getLogID (), ptr_.getOffset (), checksum_);
        return buffer;
    }
};

static_assert (sizeof (LogNode_t) == 16, "size of LogNode_t is not 16 byte");

/**
 * @brief
 *
 */
class Log_t {
public:
    uint16_t log_id_;  // log id (unique)
    size_t log_size_;  // reserved log size
    size_t log_size_mask_;
    char* log_start_addr_;          // start address of the log
    std::atomic<size_t> log_tail_;  // current tail of the log

    // each thread has its own log
    static thread_local Log_t* thread_local_log;

    // monotonic increased log id
    static std::atomic<uint16_t> global_log_id;

public:
    /**
     * @brief Register thread local log. Caller should use provide the pmem base addr
     *
     * @param addr Pmem base address provided by caller
     * @param sz size of the log
     */
    static void RegisterThreadLocalLog (char* addr, size_t sz);

    /**
     * @brief Reopen the pmem log
     *
     * @param logid
     * @param addr
     */
    static void OpenThreadLocalLog (uint16_t logid, char* addr);

    /**
     * @brief Get the Thread Local Log object
     *
     * @return Log_t*
     */
    static Log_t* GetThreadLocalLog () { return thread_local_log; }

public:
    Log_t ();

    // * +----------------------+-----------+
    // * | Pmem Log Meta (256B) | LogNode ...
    // * +----------------------+-----------+
    struct PmemLogMeta_t {
        uint64_t size_;  // lenght of this log file
        uint64_t tail_;  // current tail
        char _none[240];

        std::string ToString () {
            char buf[128];
            sprintf (buf, "log size: %ld, cur tail: %ld", size_, tail_);
            return buf;
        }
    };

    static constexpr size_t kLogMetaOffset{sizeof (PmemLogMeta_t)};

    inline uint64_t GetLogTail (void) { return log_tail_; }

    /**
     * @brief After allocate a pmem log space, initialize the Log passing address and size
     *
     * @param addr
     * @param size
     * @return true
     * @return false
     */
    bool Create (uint16_t logid, char* addr, size_t size);

    /**
     * @brief Reopen an existing log for recovery
     *
     * @param addr
     */
    void Open (uint16_t logid, char* addr);

    /**
     * @brief Flush current tail to Pmem log metadata
     */
    void CommitTail ();

    /**
     * @brief Append a Slice record
     *
     * @param type
     * @param entry
     * @param ptr pointer of previous log node, which is belonged to the same buffer
     * @return
     */
    logptr_t Append (LogNodeType_t type, Slice entry, logptr_t ptr);

    /**
     * @brief Append a char string
     *
     * @param type
     * @param target
     * @param len
     * @param ptr
     * @return
     */
    logptr_t Append (LogNodeType_t type, char* target, size_t len, logptr_t ptr);

    /**
     * @brief Append a fixed key-value pair
     *
     * @param type
     * @param key
     * @param val
     * @param ptr
     * @return size_t
     */
    logptr_t Append (LogNodeType_t type, size_t key, size_t val, logptr_t ptr);

    /**
     * @brief An iterator of the log nodes on the log
     *
     */
    class Iterator {
    private:
        size_t log_offset_;
        char* log_base_addr_;

    public:
        Iterator (uint64_t offset, char* addr, bool is_reverse = false) {
            assert (offset >= kLogMetaOffset);
            if (!is_reverse) {
                log_offset_ = offset;
            } else {
                uint8_t data_size = *reinterpret_cast<uint8_t*> (addr + offset - 1);
                log_offset_ = offset - sizeof (LogNode_t) - data_size - 1;
            }
            log_base_addr_ = addr;
        }

        inline LogNode_t* operator-> (void) const {
            return reinterpret_cast<LogNode_t*> (log_base_addr_ + log_offset_);
        }
        inline LogNode_t& operator* () const { return *this->operator-> (); }

        // Check if the LogNode pointed by myself is valid or not
        inline bool Valid (void) const {
            return log_offset_ >= kLogMetaOffset && (*this)->Valid ();
        }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            log_offset_ += (*this)->GetSize ();
            return *this;
        }

        // Postfix ++ overload
        inline Iterator operator++ (int) {
            auto tmp = *this;
            log_offset_ += (*this)->GetSize ();
            return tmp;
        }

        // Prefix ++ overload
        inline Iterator& operator-- () {
            log_offset_ -= (*this)->GetSize ();
            return *this;
        }

        // Postfix -- overload
        inline Iterator operator-- (int) {
            auto tmp = *this;
            log_offset_ -= (*this)->GetSize ();
            return tmp;
        }

    };  // end of class Iterator

    Iterator begin () { return Iterator (kLogMetaOffset, log_start_addr_); }

    Iterator rbegin () { return Iterator (log_tail_.load (), log_start_addr_, true); }
};

}  // namespace spoton

#endif
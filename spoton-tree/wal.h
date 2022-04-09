#ifndef SPOTON_TREE_WAL_H_
#define SPOTON_TREE_WAL_H_

#include <immintrin.h>
#include <inttypes.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sptree_meta.h"

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"

namespace spoton {

/**
 * @brief A redo log that connect log entry with linked list.
 *  Note: After we link the log entries with linked list, we
 *        can replay from the log tail to log head (new -> old).
 *        Meanwhile, we only have to recover the head node of each linked-list,
 *        so the recover speed should be fast.
 */
enum WALNodeType : unsigned char {
    kWALNodeValid = 0x10,  // a valid data log node
    kWALNodeLinkEnd =
        0x11,  // reach to a end of the linked list because the data in buffer has been merged
    kWALNodeCheckpoint =
        0x12,  // the checkpoint node. recover should stop reverse scan if this node appears.
    kWALNodeEnd
};

/**
 * @brief WAL Node Format
 * +------------------ NodeMeta (15B) ----------------+-....-+-----------+
 * | ptr (8B) | type (1B) | size (1B) | checksum (5B) | data | size (1B) |
 * +--------------------------------------------------+-....-+-----------+
 *  NodeMeta:
 *      next; points to next WALNode on the linked list
 *      type:
 *      size: data size
 *      checkum:
 *  data:
 */
class __attribute__ ((packed)) WALNode {
private:
    pptr<WALNode> next_;      // 8B
    uint64_t type_ : 8;       // 1B
    uint64_t size_ : 8;       // 1B
    uint64_t checksum_ : 40;  // 5B
    char data_[0];            // ... size_ then + 1B size

public:
    // return next node on linked list
    inline WALNode* NextOnList (void) { return next_; }

    // following the scan order (left->right) on WAL, return next node
    inline WALNode* NextSeq (void) {
        return reinterpret_cast<WALNode*> (reinterpret_cast<char*> (this) + sizeof (WALNode) +
                                           size_ + 1 /* size byte at end of data */);
    }

    void SetData (WALNodeType type, const char* data, size_t len, WALNode* ptr);
    void SetData (WALNodeType type, uint64_t key, uint64_t val, WALNode* ptr);

    WALNodeType GetType () { return static_cast<WALNodeType> (type_); }
    inline char* GetData (void) { return data_; }
    inline size_t GetDataSize (void) { return size_; }

    inline std::string GetDataSlice (void) { return std::string (data_, size_); }

    // Get the size of this node (including metadata and data)
    inline size_t Size (void) { return sizeof (WALNode) + size_ + 1; }

    inline bool Valid (void) {
        return this->type_ < kWALNodeEnd &&  // type is valid
               this->type_ >= kWALNodeValid;
    }

    std::string ToString (void) {
        char buffer[1024];
        WALNode* next = next_;
        sprintf (buffer, "type: %02d, data size: %u - %s. ptr: 0x%lx, checksum: %lx", type_, size_,
                 GetDataSlice ().c_str (), next, checksum_);
        return buffer;
    }
};

static_assert (sizeof (WALNode) == 15, "size of WALNode is not 15 byte");

/**
 * @brief
 *
 */
class WAL {
public:
    uint16_t log_id_{0};                            // log id (unique)
    size_t log_size_{0};                            // reserved log size
    char* log_start_addr_{nullptr};                 // start address of the log
    std::atomic<size_t> log_tail_{kLogMetaOffset};  // current tail of the log

    // each thread has its own log
    static inline thread_local WAL* thread_local_log_{nullptr};

    static inline std::vector<WAL*> log_ptrs_{128, nullptr};

    // monotonic increased log id
    static inline std::atomic<uint16_t> global_log_id_{0};

public:
    /**
     * @brief Get the Thread Local Log object
     *
     * @return WAL*
     */
    static WAL* GetThreadLocalLog (SPTreePmemRoot*);

    // Create thread_num logs. only when this function is called, can we use wal.
    static void CreateLogsForThread (SPTreePmemRoot*, size_t thread_num);

private:
    // Recover a log for current thread
    void Recover (SPTreePmemRoot*);

public:
    // * +----------------------+-----------+
    // * | Pmem Log Meta (256B) | WALNode ...
    // * +----------------------+-----------+
    struct WALMeta {
        uint64_t size_;  // lenght of this log file
        uint64_t tail_;  // current tail
        char _none[240];

        std::string ToString () {
            char buf[128];
            sprintf (buf, "log size: %ld, cur tail: %ld", size_, tail_);
            return buf;
        }

        void Reset (uint64_t sz) {
            size_ = sz;
            tail_ = sizeof (WALMeta);
        }
    };

    static constexpr size_t kLogMetaOffset{sizeof (WALMeta)};

    inline uint64_t GetLogTail (void) { return log_tail_; }

    /**
     * @brief Flush current tail to Pmem log metadata
     */
    void CommitTail ();

    /**
     * @brief Append a record
     *
     * @param type
     * @param target
     * @param len
     * @param ptr pointer to next WALNode on the linked list
     * @return this node's address
     */
    WALNode* Append (WALNodeType type, char* target, size_t len, WALNode* ptr);

    /**
     * @brief Append a fixed key-value pair
     *
     * @param type
     * @param key
     * @param val
     * @param ptr
     * @return size_t
     */
    WALNode* Append (WALNodeType type, size_t key, size_t val, WALNode* ptr);

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
                log_offset_ = offset - sizeof (WALNode) - data_size - 1;
            }
            log_base_addr_ = addr;
        }

        inline WALNode* operator-> (void) const {
            return reinterpret_cast<WALNode*> (log_base_addr_ + log_offset_);
        }
        inline WALNode& operator* () const { return *this->operator-> (); }

        // Check if the LogNode pointed by myself is valid or not
        inline bool Valid (void) const {
            return log_offset_ >= kLogMetaOffset && (*this)->Valid ();
        }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            log_offset_ += (*this)->Size ();
            return *this;
        }

        // Postfix ++ overload
        inline Iterator operator++ (int) {
            auto tmp = *this;
            log_offset_ += (*this)->Size ();
            return tmp;
        }

        // Prefix ++ overload
        inline Iterator& operator-- () {
            log_offset_ -= (*this)->Size ();
            return *this;
        }

        // Postfix -- overload
        inline Iterator operator-- (int) {
            auto tmp = *this;
            log_offset_ -= (*this)->Size ();
            return tmp;
        }

    };  // end of class Iterator

    Iterator begin () { return Iterator (kLogMetaOffset, log_start_addr_); }

    Iterator rbegin () { return Iterator (log_tail_.load (), log_start_addr_, true); }
};

}  // namespace spoton

#endif
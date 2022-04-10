#include "wal.h"

#include "logger.h"
#include "sptree_meta.h"
#include "xxhash.h"

namespace spoton {

void WALNode::SetData (WALNodeType type, const char* data, size_t len, WALNode* next) {
    this->type_ = type;
    this->size_ = len;
    this->next_ = next;
    memcpy (this->data_, data, len);

    // calculate the checksum
    this->checksum_ = XXH3_64bits (data, len);

    // set size after data
    *reinterpret_cast<uint8_t*> (&this->data_[len]) = len;
}

void WALNode::SetData (WALNodeType type, uint64_t key, uint64_t val, WALNode* next) {
    char buf[128];
    WALNode* tmp = reinterpret_cast<WALNode*> (buf);

    tmp->type_ = type;
    tmp->size_ = 16;
    tmp->next_ = next;
    *reinterpret_cast<uint64_t*> (tmp->data_) = key;
    *reinterpret_cast<uint64_t*> (tmp->data_ + 8) = val;
    // calculate the checksum
    tmp->checksum_ = XXH3_64bits (this->data_, 16);
    // set size after data
    *reinterpret_cast<uint8_t*> (&tmp->data_[16]) = 16;

    // copy the data together to me
    memcpy (this, buf, 33);
}

void WAL::CreateLogsForThread (SPTreePmemRoot* root, size_t thread_num) {
    SPTREE_LOCAL_LOG_SIZE = SPTREE_LOG_SIZE / thread_num;
    root->log_count = thread_num;
    for (size_t i = 0; i < thread_num; i++) {
        // create log space on pmem
        root->log_base_addrs[i] = (char*)RP_malloc (SPTREE_LOCAL_LOG_SIZE);
        void* tmp = root->log_base_addrs[i];
        // initialize the meta
        WALMeta* log_meta = reinterpret_cast<WALMeta*> (tmp);
        log_meta->Reset (SPTREE_LOCAL_LOG_SIZE);
        INFO ("Create log for thread %lu. addr: 0x%lx, size: %lu", i, (size_t)tmp,
              SPTREE_LOCAL_LOG_SIZE);
    }
    root->log_count = thread_num;
}

WAL* WAL::GetThreadLocalLog (SPTreePmemRoot* root) {
    if (thread_local_log_ == nullptr && root->log_count != 0) {
        // If I do not have my local log
        WAL* log = new WAL ();
        log->Recover (root);
    }
    return thread_local_log_;
}

void WAL::Recover (SPTreePmemRoot* root) {
    if (!root) {
        ERROR ("recover log with SPTreePmemRoot nullptr");
        exit (1);
    }
    if (SPTREE_LOCAL_LOG_SIZE == 0) {
        ERROR ("local log size not initialze.");
        perror ("local log size not initialze.\n");
        exit (1);
    }

    this->log_id_ = global_log_id_.fetch_add (1);
    void* tmp = root->log_base_addrs[log_id_];
    WALMeta* log_meta = reinterpret_cast<WALMeta*> (tmp);
    this->log_start_addr_ = (char*)tmp;
    this->log_size_ = log_meta->size_;
    this->log_tail_.store (log_meta->tail_);
    log_ptrs_[log_id_] = this;
    thread_local_log_ = this;
    INFO ("Recover a WAL log. addr: 0x%lx, size: %lu, tail: %lu", (size_t)tmp, this->log_size_,
          this->log_tail_.load ());
    assert (thread_local_log_);
}

void WAL::CommitTail () {
    auto* meta = reinterpret_cast<WALMeta*> (log_start_addr_);
    meta->tail_ = log_tail_.load ();

    SPTreeMemFlush (meta);
    SPTreeMemFlushFence ();
}

WALNode* WAL::Append (WALNodeType type, char* target, size_t len, WALNode* ptr) {
    size_t record_size = len;
    size_t total_size = record_size + sizeof (WALNode) + 1;
    size_t cur_off = log_tail_.fetch_add (total_size, std::memory_order_relaxed);
    char* cur_addr = log_start_addr_ + cur_off;

    auto* node = reinterpret_cast<WALNode*> (cur_addr);
    node->SetData (type, target, record_size, ptr);

    // flush the log node
    SPTreeCLFlush (cur_addr, total_size);

    return node;
}

WALNode* WAL::Append (WALNodeType type, size_t key, size_t val, WALNode* ptr) {
    size_t record_size = 16;
    size_t total_size = record_size + sizeof (WALNode) + 1;
    size_t cur_off = log_tail_.fetch_add (total_size, std::memory_order_relaxed);
    char* cur_addr = log_start_addr_ + cur_off;

    auto* node = reinterpret_cast<WALNode*> (cur_addr);
    node->SetData (type, key, val, ptr);

    // flush the log node (sync flushing each log record really compromises performance)
    SPTreeCLFlush (cur_addr, total_size);

    return node;
}
}  // namespace spoton

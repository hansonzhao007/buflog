#include "spoton_log.h"

#include "logger.h"

namespace spoton {

thread_local Log_t* Log_t::thread_local_log{nullptr};
std::atomic<uint16_t> Log_t::global_log_id{0};

void Log_t::RegisterThreadLocalLog (char* addr, size_t sz) {
    // obtain unique log id
    uint16_t cur_log_id = global_log_id.fetch_add (1);

    thread_local_log = new Log_t ();
    bool res = thread_local_log->Create (cur_log_id, addr, sz);
    if (!res) {
        perror ("Register log fail");
        exit (1);
    }

    DEBUG ("Register local log 0x%lx. logid: %2u, size: %lu", thread_local_log, cur_log_id, sz);
}

void Log_t::OpenThreadLocalLog (uint16_t logid, char* addr) {
    // reset the global logid
    global_log_id = std::max (global_log_id.load (), logid);

    thread_local_log = new Log_t ();
    thread_local_log->Open (logid, addr);

    DEBUG ("Open local log 0x%lx. logid: %2u, size: %lu", thread_local_log, logid,
           thread_local_log->log_size_);
}

Log_t::Log_t ()
    : log_size_ (0), log_size_mask_ (0), log_start_addr_ (nullptr), log_tail_ (kLogMetaOffset) {}

bool Log_t::Create (uint16_t logid, char* addr, size_t size) {
    // if (!isPowerOfTwo (size)) {
    //     perror ("Data log is not power of 2.");
    //     return false;
    // }
    if (addr == nullptr) {
        return false;
    }

    // register the [logid -> pmem base address] mapping
    logptr_t::RegistertBaseAddress (logid, addr);

    log_id_ = logid;
    log_size_ = size;
    log_size_mask_ = size - 1;
    log_start_addr_ = addr;
    log_tail_ = kLogMetaOffset;  // initial offset start from kLogMetaOffset

    auto* meta = reinterpret_cast<PmemLogMeta_t*> (addr);
    meta->size_ = size;
    meta->tail_ = kLogMetaOffset;
    SPOTON_FLUSH (addr);
    SPOTON_FLUSHFENCE;
    return true;
}

void Log_t::Open (uint16_t logid, char* addr) {
    logptr_t::RegistertBaseAddress (logid, addr);

    auto* meta = reinterpret_cast<PmemLogMeta_t*> (addr);
    log_size_ = meta->size_;
    log_size_mask_ = log_size_ - 1;
    log_start_addr_ = addr;
    log_tail_ = meta->tail_;
    DEBUG ("Recover log tail %lu", meta->tail_);
}

void Log_t::CommitTail () {
    auto* meta = reinterpret_cast<PmemLogMeta_t*> (log_start_addr_);
    meta->tail_ = log_tail_.load ();

    SPOTON_FLUSH (meta);
    SPOTON_FLUSHFENCE;
}

void LogNode_t::SetData (LogNodeType_t type, const char* data, size_t len, logptr_t ptr) {
    this->type_ = type;
    this->size_ = len;
    this->ptr_ = ptr;
    memcpy (this->data_, data, len);

    // calculate the checksum
    this->checksum_ = Hasher::hash_string (data, len);

    // set size after data
    *reinterpret_cast<uint8_t*> (&this->data_[len]) = len;
}

void LogNode_t::SetData (LogNodeType_t type, uint64_t key, uint64_t val, logptr_t ptr) {
    char buf[128];
    LogNode_t* tmp = reinterpret_cast<LogNode_t*> (buf);

    tmp->type_ = type;
    tmp->size_ = 16;
    tmp->ptr_ = ptr;
    *reinterpret_cast<uint64_t*> (tmp->data_) = key;
    *reinterpret_cast<uint64_t*> (tmp->data_ + 8) = val;
    // calculate the checksum
    tmp->checksum_ = Hasher::hash_string (this->data_, 16);
    // set size after data
    *reinterpret_cast<uint8_t*> (&tmp->data_[16]) = 16;

    // copy the data together to me
    memcpy (this, buf, 33);
}

logptr_t Log_t::Append (LogNodeType_t type, Slice record, logptr_t ptr) {
    size_t record_size = record.size ();
    size_t total_size = record_size + sizeof (LogNode_t) + 1;
    size_t cur_off = log_tail_.fetch_add (total_size, std::memory_order_relaxed);
    char* cur_addr = log_start_addr_ + cur_off;

    auto* log_node = reinterpret_cast<LogNode_t*> (cur_addr);
    log_node->SetData (type, record.data (), record_size, ptr);

    // flush the log node
    SPOTON_CLFLUSH (cur_addr, total_size);

    return logptr_t{log_id_, cur_off};
}

logptr_t Log_t::Append (LogNodeType_t type, char* target, size_t len, logptr_t ptr) {
    return Append (type, Slice (target, len), ptr);
}

logptr_t Log_t::Append (LogNodeType_t type, size_t key, size_t val, logptr_t ptr) {
    size_t record_size = 16;
    size_t total_size = record_size + sizeof (LogNode_t) + 1;
    size_t cur_off = log_tail_.fetch_add (total_size, std::memory_order_relaxed);
    char* cur_addr = log_start_addr_ + cur_off;

    auto* log_node = reinterpret_cast<LogNode_t*> (cur_addr);
    log_node->SetData (type, key, val, ptr);

    // flush the log node (sync flushing each log record really compromises performance)
    SPOTON_CLFLUSH (cur_addr, total_size);

    return logptr_t{log_id_, cur_off};
}
}  // namespace spoton

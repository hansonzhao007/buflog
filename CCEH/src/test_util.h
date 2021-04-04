#pragma once

#include <string>
#include <assert.h>
#include <cstring>

#include <vector>
#include <algorithm>
#include <fstream>
#include <random>


std::string Execute(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Returns the number of micro-seconds since some fixed point in time. Only
// useful for computing deltas of time.
inline uint64_t NowMicros() {
static constexpr uint64_t kUsecondsPerSecond = 1000000;
struct ::timeval tv;
::gettimeofday(&tv, nullptr);
return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
}
// Returns the number of nano-seconds since some fixed point in time. Only
// useful for computing deltas of time in one run.
// Default implementation simply relies on NowMicros.
// In platform-specific implementations, NowNanos() should return time points
// that are MONOTONIC.
inline uint64_t NowNanos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000L + ts.tv_nsec;
}

struct IPMInfo{
    std::string dimm_name;
    // Number of read and write operations performed from/to the physical media. 
    // Each operation transacts a 64byte operation. These operations includes 
    // commands transacted for maintenance as well as the commands transacted 
    // by the CPU.
    uint64_t    read_64B_ops_received  = 0;
    uint64_t    write_64B_ops_received = 0;

    // Number of read and write operations received from the CPU (memory controller)
    // unit: 64 byte
    uint64_t    ddrt_read_ops   = 0;      
    uint64_t    ddrt_write_ops  = 0;

    // actual byte write/read to DIMM media
    // bytes_read (derived)   : number of bytes transacted by the read operations
    // bytes_written (derived): number of bytes transacted by the write operations
    // Note: The total number of bytes transacted in any sample is computed as 
    // bytes_read (derived) + 2 * bytes_written (derived).
    // 
    // Formula:
    // bytes_read   : (read_64B_ops_received - write_64B_ops_received) * 64
    // bytes_written: write_64B_ops_received * 64
    uint64_t    bytes_read      = 0;
    uint64_t    bytes_written   = 0;

    // Number of bytes received from the CPU (memory controller)
    // ddrt_read_bytes  : (ddrt_read_ops * 64)
    // ddrt_write_bytes : (ddrt_write_ops * 64)
    uint64_t    ddrt_read_bytes   = 0;      
    uint64_t    ddrt_write_bytes  = 0;

    // DIMM ; read_64B_ops_received  ; write_64B_ops_received ; ddrt_read_ops  ; ddrt_write_ops;
    // DIMM0; 1292533678412          ; 643726680616           ; 508664958085   ; 560639638344;
    void Parse(const std::string& result) {
        // printf("Parse: %s\n", result.c_str());
        std::vector<std::string> infos = split(result, ";");
        dimm_name = infos[0];
        read_64B_ops_received  = stol(infos[1]);
        write_64B_ops_received = stol(infos[2]);
        ddrt_read_ops          = stol(infos[3]);
        ddrt_write_ops         = stol(infos[4]);
        ddrt_read_bytes        = ddrt_read_ops * 64;
        ddrt_write_bytes       = ddrt_write_ops * 64; 
        bytes_read             = (read_64B_ops_received - write_64B_ops_received) * 64;
        bytes_written          = write_64B_ops_received * 64;
        // printf("%s", ToString().c_str());
    }

    std::string ToString() {
        std::string res;
        char buffer[1024];
        sprintf(buffer, "\033[34m%s | Read from IMC | Write from IMC | Read DIMM | Write DIMM |\n", dimm_name.c_str());
        res += buffer;
        sprintf(buffer, "  MB  | %13.0f | %14.0f | %9.0f | %10.0f |", 
            ddrt_read_bytes/1024.0/1024.0,
            ddrt_write_bytes/1024.0/1024.0,
            read_64B_ops_received*64/1024.0/1024.0,
            write_64B_ops_received*64/1024.0/1024.0);
        res += buffer;
        res += "\033[0m\n";
        return res;
    }

    std::vector<std::string> split(std::string str, std::string token){
        std::vector<std::string>result;
        while(str.size()){
            size_t index = str.find(token);
            if(index != std::string::npos){
                result.push_back(str.substr(0,index));
                str = str.substr(index+token.size());
                if(str.size()==0)result.push_back(str);
            }else{
                result.push_back(str);
                str = "";
            }
        }
        return result;
    }
};

class IPMMetric {
public:
    IPMMetric(){}
    IPMMetric(const IPMInfo& before, const IPMInfo& after) {
        metric_.bytes_read = after.bytes_read - before.bytes_read;
        metric_.bytes_written = after.bytes_written - before.bytes_written;
        metric_.ddrt_read_bytes = after.ddrt_read_bytes - before.ddrt_read_bytes;
        metric_.ddrt_write_bytes = after.ddrt_write_bytes - before.ddrt_write_bytes;
    }

    void Merge(const IPMMetric& metric) {
        metric_.bytes_read += metric.metric_.bytes_read;
        metric_.bytes_written += metric.metric_.bytes_written;
        metric_.ddrt_read_bytes += metric.metric_.ddrt_read_bytes;
        metric_.ddrt_write_bytes += metric.metric_.ddrt_write_bytes;
    }

    uint64_t GetByteReadToDIMM() {
        return metric_.bytes_read;
    }

    uint64_t GetByteWriteToDIMM() {
        return metric_.bytes_written;
    }

    uint64_t GetByteReadFromIMC() {
        return metric_.ddrt_read_bytes;
    }

    uint64_t GetByteWriteFromIMC() {
        return metric_.ddrt_write_bytes;
    }

    
private:
    IPMInfo metric_;
};

class IPMWatcher {
public:
    
    IPMWatcher(const std::string& name): file_name_("ipm_" + name + ".txt") {
        printf("\033[32mStart IPMWatcher for %s\n\033[0m", name.c_str());
        metrics_before_ = Profiler();
        start_time_ = NowMicros();
    }

    ~IPMWatcher() {
        if (!finished_) {
            Report();
        }
    }

    void Report() {
        finished_ = true;
        duration_ = (NowMicros() - start_time_) / 1000000.0;
        metrics_after_ = Profiler();
        IPMMetric metric_merge;
        for (size_t i = 0; i < metrics_before_.size(); ++i) {
            auto& info_before = metrics_before_[i];
            auto& info_after  = metrics_after_[i];
            IPMMetric metric(info_before, info_after);
            metric_merge.Merge(metric);
            std::string res;
            char buffer[1024];
            sprintf(buffer, "\033[34m%s | Read from IMC | Write from IMC |  Read DIMM  |  Write DIMM  |   RA   |   WA   |\n", info_before.dimm_name.c_str());
            res += buffer;
            // double duration = (end_time_ - start_time_) / 1000000.0;
            // printf("duration: %f s", duration);
            // double read_throughput = metric.GetByteReadToDIMM() / 1024.0 / 1024.0 / duration;
            // double write_throughput = metric.GetByteWriteToDIMM() / 1024.0 / 1024.0 / duration;
            sprintf(buffer, "  MB  | %13.2f | %14.2f | %11.2f | %12.2f | %6.2f | %6.2f |", // Read: %6.2f MB/s, Write: %6.2f MB/s", 
                    metric.GetByteReadFromIMC()/1024.0/1024.0,
                    metric.GetByteWriteFromIMC() /1024.0/1024.0,
                    metric.GetByteReadToDIMM() /1024.0/1024.0,
                    metric.GetByteWriteToDIMM() /1024.0/1024.0,
                    (double) metric.GetByteReadToDIMM() / metric.GetByteReadFromIMC(),
                    (double) metric.GetByteWriteToDIMM() / metric.GetByteWriteFromIMC()
                    // write_throughput
                    );
            res += buffer;
            res += "\033[0m\n";
            printf("%s", res.c_str());
        }           
        dimm_read_  = metric_merge.GetByteReadToDIMM() / 1024.0/1024.0/ (duration_);
        dimm_write_ = metric_merge.GetByteWriteToDIMM() / 1024.0/1024.0/ (duration_);
        app_read_   = metric_merge.GetByteReadFromIMC() / 1024.0/1024.0/ (duration_);
        app_write_  = metric_merge.GetByteWriteFromIMC() / 1024.0/1024.0/ (duration_);
        printf("\033[34m*SUM* | DIMM-R: %7.1f MB/s. User-R: %7.1f MB/s   | DIMM-W: %7.1f MB/s, User-W: %7.1f MB/s. Time: %6.2fs.\033[0m\n", 
            dimm_read_,            
            app_read_,
            dimm_write_,
            app_write_,
            duration_);  
        printf("\033[32mDestroy IPMWatcher.\n\033[0m\n");
        fflush(nullptr);
    }

    std::vector<IPMInfo> Profiler() const {
        std::vector<IPMInfo> infos;
        Execute("/opt/intel/ipmwatch/bin64/ipmwatch -l >" + file_name_);
        std::string results = Execute("grep -w \'DIMM.\' " + file_name_);
        std::stringstream ss(results);
        while (!ss.eof()) {
            std::string res;
            ss >> res;
            if (res.empty()) break;
            IPMInfo tmp;
            tmp.Parse(res);
            infos.push_back(tmp);
        }
        return infos;
    }

    const std::string file_name_;
    std::vector<IPMInfo> metrics_before_;
    std::vector<IPMInfo> metrics_after_;
    double dimm_read_   = 0;
    double dimm_write_  = 0;
    double app_read_    = 0;
    double app_write_   = 0;
    double start_time_  = 0;
    double duration_    = 0;
    bool   finished_    = false;
};

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

auto rng = std::default_random_engine {};

class RandomKeyTrace {
public:
    RandomKeyTrace(size_t count, int seed = random()) {
        count_ = count;
        keys_.resize(count);
        for (size_t i = 0; i < count; i++) {
            keys_[i] = i;
        }
        Randomize();
    }

    ~RandomKeyTrace() {
    }

    void Randomize(void) {
        printf("Randomize the trace...\r");
        fflush(nullptr);
        std::shuffle(std::begin(keys_), std::end(keys_), rng);
    }
    
    class RangeIterator {
    public:
        RangeIterator(std::vector<size_t>* pkey_vec, size_t start, size_t end):
            pkey_vec_(pkey_vec),                        
            end_index_(end), 
            cur_index_(start) { }

        inline bool Valid() {
            return (cur_index_ < end_index_);
        }

        inline size_t Next() {
            return (*pkey_vec_)[cur_index_++];
        }

        std::vector<size_t>* pkey_vec_;
        size_t end_index_;
        size_t cur_index_;
    };

    class Iterator {
    public:
        Iterator(std::vector<size_t>* pkey_vec, size_t start_index, size_t range):
            pkey_vec_(pkey_vec),
            range_(range),                        
            end_index_(start_index % range_), 
            cur_index_(start_index % range_),
            begin_(true)  
        {
            
        }

        Iterator(){}

        inline bool Valid() {
            return (begin_ || cur_index_ != end_index_);
        }

        inline size_t Next() {
            begin_ = false;
            size_t index = cur_index_;
            cur_index_++;
            if (cur_index_ >= range_) {
                cur_index_ = 0;
            }
            return (*pkey_vec_)[index];
        }

        std::string Info() {
            char buffer[128];
            sprintf(buffer, "valid: %s, cur i: %lu, end_i: %lu, range: %lu", Valid() ? "true" : "false", cur_index_, end_index_, range_);
            return buffer;
        }

        std::vector<size_t>* pkey_vec_;
        size_t range_;
        size_t end_index_;
        size_t cur_index_;
        bool   begin_;
    };

    Iterator trace_at(size_t start_index, size_t range) {
        return Iterator(&keys_, start_index, range);
    }

    RangeIterator Begin(void) {
        return RangeIterator(&keys_, 0, keys_.size());
    }

    RangeIterator iterate_between(size_t start, size_t end) {
        return RangeIterator(&keys_, start, end);
    }

    size_t count_;
    std::vector<size_t> keys_;
};



enum YCSBOpType {kYCSB_Write, kYCSB_Read, kYCSB_Query, kYCSB_ReadModifyWrite};

inline uint32_t wyhash32() {
    static thread_local uint32_t wyhash32_x = random();
    wyhash32_x += 0x60bee2bee120fc15;
    uint64_t tmp;
    tmp = (uint64_t) wyhash32_x * 0xa3b195354a39b70d;
    uint32_t m1 = (tmp >> 32) ^ tmp;
    tmp = (uint64_t)m1 * 0x1b03738712fad5c9;
    uint32_t m2 = (tmp >> 32) ^ tmp;
    return m2;
}

class YCSBGenerator {
public:
    // Generate 
    YCSBGenerator() {
    }

    inline YCSBOpType NextA() {
        // ycsba: 50% reads, 50% writes
        uint32_t rnd_num = wyhash32();

        if ((rnd_num & 0x1) == 0) {
            return kYCSB_Read;
        } else {
            return kYCSB_Write;
        }
    }

    inline YCSBOpType NextB() {
        // ycsbb: 95% reads, 5% writes
        // 51/1024 = 0.0498
        uint32_t rnd_num = wyhash32();

        if ((rnd_num & 1023) < 51) {
            return kYCSB_Write;
        } else {
            return kYCSB_Read;
        }
    }

    inline YCSBOpType NextC() {
        return kYCSB_Read;
    }

    inline YCSBOpType NextD() {
        // ycsbd: read latest inserted records
        return kYCSB_Read;
    }

    inline YCSBOpType NextF() {
        // ycsba: 50% reads, 50% writes
        uint32_t rnd_num = wyhash32();

        if ((rnd_num & 0x1) == 0) {
            return kYCSB_Read;
        } else {
            return kYCSB_ReadModifyWrite;
        }
    }
};
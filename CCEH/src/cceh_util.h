#pragma once

#include <assert.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <vector>

std::string Execute (const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype (&pclose)> pipe (popen (cmd.c_str (), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error ("popen() failed!");
    }
    while (fgets (buffer.data (), buffer.size (), pipe.get ()) != nullptr) {
        result += buffer.data ();
    }
    return result;
}

// Returns the number of micro-seconds since some fixed point in time. Only
// useful for computing deltas of time.
inline uint64_t NowMicros () {
    static constexpr uint64_t kUsecondsPerSecond = 1000000;
    struct ::timeval tv;
    ::gettimeofday (&tv, nullptr);
    return static_cast<uint64_t> (tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
}
// Returns the number of nano-seconds since some fixed point in time. Only
// useful for computing deltas of time in one run.
// Default implementation simply relies on NowMicros.
// In platform-specific implementations, NowNanos() should return time points
// that are MONOTONIC.
inline uint64_t NowNanos () {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t> (ts.tv_sec) * 1000000000L + ts.tv_nsec;
}

enum {
    DimmID,
    MediaReads,
    MediaWrites,
    ReadRequests,
    WriteRequests,
    TotalMediaReads,
    TotalMediaWrites,
    TotalReadRequests,
    TotalWriteRequests
};

class DimmAttribute128b {
public:
    uint64_t l_u64b, h_u64b;
};

class DimmDataContainer {
public:
    std::string dimm_id_;
    DimmAttribute128b stat_[8];
};

class PMMData {
    const std::string filename;

public:
    explicit PMMData (const std::string& file) : filename (file) {}

    DimmDataContainer PMM_[6];
    std::vector<DimmDataContainer> pmm_dimms_;
    void get_pmm_data () {
        Execute ("ipmctl show -performance > " + filename);
        std::ifstream ipmctl_stat;
        ipmctl_stat.open (filename);  // open the input file

        std::vector<std::string> reg_init_set = {R"(DimmID=0x([0-9a-f]*))",
                                                 R"(^MediaReads=0x([0-9a-f]*))",
                                                 R"(^MediaWrites=0x([0-9a-f]*))",
                                                 R"(^ReadRequests=0x([0-9a-f]*))",
                                                 R"(^WriteRequests=0x([0-9a-f]*))",
                                                 R"(^TotalMediaReads=0x([0-9a-f]*))",
                                                 R"(^TotalMediaWrites=0x([0-9a-f]*))",
                                                 R"(^TotalReadRequests=0x([0-9a-f]*))",
                                                 R"(^TotalWriteRequests=0x([0-9a-f]*))"};
        std::vector<std::regex> reg_set;
        std::regex stat_bit_convert_reg (R"(^([0-9a-f]{16})([0-9a-f]{16}))");
        for (auto i : reg_init_set) {
            reg_set.push_back (std::regex (i));
        }

        std::string str_line;
        std::smatch matched_data;
        std::smatch matched_num;
        int index = 0;
        while (ipmctl_stat >> str_line) {
            for (size_t i = 0; i < reg_set.size (); i++) {
                if (std::regex_search (str_line, matched_data, reg_set.at (i))) {
                    index = i;
                    break;
                }
            }
            if (index == DimmID) {
                pmm_dimms_.push_back (DimmDataContainer ());
                pmm_dimms_.back ().dimm_id_ = matched_data[1];
            } else {
                std::string str128b = matched_data[1];
                if (std::regex_search (str128b, matched_num, stat_bit_convert_reg)) {
                    pmm_dimms_.back ().stat_[index - 1].h_u64b =
                        std::stoull (matched_num[1], nullptr, 16);
                    pmm_dimms_.back ().stat_[index - 1].l_u64b =
                        std::stoull (matched_num[2], nullptr, 16);
                } else {
                    perror ("parse dimm stat");
                    exit (EXIT_FAILURE);
                }
            }
        }
    }
};

class IPMWatcher {
public:
    std::string file_name_;

    PMMData *start, *end;
    float *outer_imc_read_addr = nullptr, *outer_imc_write_addr = nullptr,
          *outer_media_read_addr = nullptr, *outer_media_write_addr = nullptr;
    std::chrono::_V2::system_clock::time_point start_timer, end_timer;

    IPMWatcher (const std::string& name) : file_name_ ("ipm_" + name + ".txt") {
        printf ("\033[32mStart IPMWatcher for %s\n\033[0m", name.c_str ());
        start = new PMMData (file_name_);
        end = new PMMData (file_name_);
        start_timer = std::chrono::system_clock::now ();
        start->get_pmm_data ();
    }

    ~IPMWatcher () { Report (); }

    void Report () {
        end->get_pmm_data ();
        end_timer = std::chrono::system_clock::now ();
        int dimm_num = end->pmm_dimms_.size ();
        float media_read_size_MB = 0, imc_read_size_MB = 0, imc_write_size_MB = 0,
              media_write_size_MB = 0;

        setlocale (LC_NUMERIC, "");
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds> (end_timer - start_timer);

        std::vector<float> TotalMediaReads_ (dimm_num), TotalMediaWrites_ (dimm_num),
            TotalReadRequests_ (dimm_num), TotalWriteRequests_ (dimm_num);
        for (int i = 0; i < dimm_num; i++) {
            for (int j = 0; j < 8; j++)
                assert (start->pmm_dimms_.at (i).stat_[j].h_u64b ==
                        end->pmm_dimms_.at (i).stat_[j].h_u64b);
            TotalMediaReads_.at (i) = (end->pmm_dimms_.at (i).stat_[TotalMediaReads - 1].l_u64b -
                                       start->pmm_dimms_.at (i).stat_[TotalMediaReads - 1].l_u64b) /
                                      16384.0;
            TotalMediaWrites_.at (i) =
                (end->pmm_dimms_.at (i).stat_[TotalMediaWrites - 1].l_u64b -
                 start->pmm_dimms_.at (i).stat_[TotalMediaWrites - 1].l_u64b) /
                16384.0;
            TotalReadRequests_.at (i) =
                (end->pmm_dimms_.at (i).stat_[TotalReadRequests - 1].l_u64b -
                 start->pmm_dimms_.at (i).stat_[TotalReadRequests - 1].l_u64b) /
                16384.0;
            TotalWriteRequests_.at (i) =
                (end->pmm_dimms_.at (i).stat_[TotalWriteRequests - 1].l_u64b -
                 start->pmm_dimms_.at (i).stat_[TotalWriteRequests - 1].l_u64b) /
                16384.0;

            TotalMediaReads_.at (i) = TotalMediaReads_.at (i) - TotalMediaWrites_.at (i);

            imc_read_size_MB += TotalReadRequests_.at (i);
            media_read_size_MB += TotalMediaReads_.at (i);
            imc_write_size_MB += TotalWriteRequests_.at (i);
            media_write_size_MB += TotalMediaWrites_.at (i);
        }
        if (outer_imc_read_addr) *outer_imc_read_addr = imc_read_size_MB;
        if (outer_imc_write_addr) *outer_imc_write_addr = imc_write_size_MB;
        if (outer_media_read_addr) *outer_media_read_addr = media_read_size_MB;
        if (outer_media_write_addr) *outer_media_write_addr = media_write_size_MB;
        for (int i = 0; i < dimm_num; i++) {
            printf (
                "DIMM%d | Read from IMC | Write from IMC |  Read DIMM  |  Write "
                "DIMM  |   "
                "RA   |   WA   |\n",
                i);
            printf ("  MB  | %13.2f | %14.2f | %11.2f | %12.2f | %6.2f | %6.2f |\n",
                    TotalReadRequests_.at (i), TotalWriteRequests_.at (i), TotalMediaReads_.at (i),
                    TotalMediaWrites_.at (i), (TotalMediaReads_.at (i) / TotalReadRequests_.at (i)),
                    (TotalMediaWrites_.at (i) / TotalWriteRequests_.at (i)));
        }

        double seconds = duration.count () / 1000.0;
        printf (
            "*SUM* | DIMM-R: %7.1f MB/s. User-R: %7.1f MB/s   | DIMM-W: %7.1f "
            "MB/s, "
            "User-W: %7.1f MB/s. Time: %6.2fs\n",
            media_read_size_MB / seconds, imc_read_size_MB / seconds, media_write_size_MB / seconds,
            imc_write_size_MB / seconds, seconds);

        delete start;
        delete end;

        printf ("\033[32mDestroy IPMWatcher.\n\033[0m\n");
    }
};

/** Slice
 *  @note: Derived from LevelDB. the data is stored in the *data_
 */
class Slice {
public:
    using type = Slice;
    // operator <
    bool operator< (const Slice& b) const { return compare (b) < 0; }

    bool operator> (const Slice& b) const { return compare (b) > 0; }

    // explicit conversion
    inline operator std::string () const { return std::string (data_, size_); }

    // Create an empty slice.
    Slice () : data_ (""), size_ (0) {}

    // Create a slice that refers to d[0,n-1].
    Slice (const char* d, size_t n) : data_ (d), size_ (n) {}

    // Create a slice that refers to the contents of "s"
    Slice (const std::string& s) : data_ (s.data ()), size_ (s.size ()) {}

    // Create a slice that refers to s[0,strlen(s)-1]
    Slice (const char* s) : data_ (s), size_ ((s == nullptr) ? 0 : strlen (s)) {}

    // Return a pointer to the beginning of the referenced data
    inline const char* data () const { return data_; }

    // Return the length (in bytes) of the referenced data
    inline size_t size () const { return size_; }

    // Return true iff the length of the referenced data is zero
    inline bool empty () const { return size_ == 0; }

    // Return the ith byte in the referenced data.
    // REQUIRES: n < size()
    inline char operator[] (size_t n) const {
        assert (n < size ());
        return data_[n];
    }

    // Change this slice to refer to an empty array
    inline void clear () {
        data_ = "";
        size_ = 0;
    }

    inline std::string ToString () const {
        std::string res;
        res.assign (data_, size_);
        return res;
    }

    // Three-way comparison.  Returns value:
    //   <  0 iff "*this" <  "b",
    //   == 0 iff "*this" == "b",
    //   >  0 iff "*this" >  "b"
    inline int compare (const Slice& b) const {
        assert (data_ != nullptr && b.data_ != nullptr);
        const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
        int r = memcmp (data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_)
                r = -1;
            else if (size_ > b.size_)
                r = +1;
        }
        return r;
    }

    friend std::ostream& operator<< (std::ostream& os, const Slice& str) {
        os << str.ToString ();
        return os;
    }

    const char* data_;
    size_t size_;
};  // end of class Slice

inline bool operator== (const Slice& x, const Slice& y) {
    return ((x.size () == y.size ()) && (memcmp (x.data (), y.data (), x.size ()) == 0));
}

inline bool operator!= (const Slice& x, const Slice& y) { return !(x == y); }
class RandomKeyTrace {
public:
    RandomKeyTrace (size_t count) {
        count_ = count;
        keys_.resize (count);

        printf ("generate %lu keys\n", count);
        auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, count),
                           [&](const tbb::blocked_range<uint64_t>& range) {
                               for (uint64_t i = range.begin (); i != range.end (); i++) {
                                   //    uint64_t num = u64Rand (1LU, kRandNumMax);
                                   keys_[i] = i;
                               }
                           });
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("generate duration %f s.\n", duration.count () / 1000000.0);

        Randomize ();
    }

    ~RandomKeyTrace () {}

    void Randomize (void) {
        // printf ("randomize %lu keys\n", keys_.size ());
        auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, keys_.size ()),
                           [&](const tbb::blocked_range<uint64_t>& range) {
                               auto rng = std::default_random_engine{};
                               std::shuffle (keys_.begin () + range.begin (),
                                             keys_.begin () + range.end (), rng);
                           });
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        // printf ("randomize duration %f s.\n", duration.count () / 1000000.0);
    }

    class RangeIterator {
    public:
        RangeIterator (std::vector<size_t>* pkey_vec, size_t start, size_t end)
            : pkey_vec_ (pkey_vec), end_index_ (end), cur_index_ (start) {}

        inline bool Valid () { return (cur_index_ < end_index_); }

        inline size_t operator* () { return (*pkey_vec_)[cur_index_]; }

        inline size_t& Next () { return (*pkey_vec_)[cur_index_++]; }

        std::vector<size_t>* pkey_vec_;
        size_t end_index_;
        size_t cur_index_;
    };

    class Iterator {
    public:
        Iterator (std::vector<size_t>* pkey_vec, size_t start_index, size_t range)
            : pkey_vec_ (pkey_vec),
              range_ (range),
              end_index_ (start_index % range_),
              cur_index_ (start_index % range_),
              begin_ (true) {}

        Iterator () {}

        inline bool Valid () { return (begin_ || cur_index_ != end_index_); }

        inline size_t& Next () {
            begin_ = false;
            size_t index = cur_index_;
            cur_index_++;
            if (cur_index_ >= range_) {
                cur_index_ = 0;
            }
            return (*pkey_vec_)[index];
        }

        inline size_t operator* () { return (*pkey_vec_)[cur_index_]; }

        std::string Info () {
            char buffer[128];
            sprintf (buffer, "valid: %s, cur i: %lu, end_i: %lu, range: %lu",
                     Valid () ? "true" : "false", cur_index_, end_index_, range_);
            return buffer;
        }

        std::vector<size_t>* pkey_vec_;
        size_t range_;
        size_t end_index_;
        size_t cur_index_;
        bool begin_;
    };

    Iterator trace_at (size_t start_index, size_t range) {
        return Iterator (&keys_, start_index, range);
    }

    RangeIterator Begin (void) { return RangeIterator (&keys_, 0, keys_.size ()); }

    RangeIterator iterate_between (size_t start, size_t end) {
        return RangeIterator (&keys_, start, end);
    }

    size_t count_;
    std::vector<size_t> keys_;
};

enum YCSBOpType { kYCSB_Write, kYCSB_Read, kYCSB_Query, kYCSB_ReadModifyWrite };

inline uint32_t wyhash32 () {
    static thread_local uint32_t wyhash32_x = random ();
    wyhash32_x += 0x60bee2bee120fc15;
    uint64_t tmp;
    tmp = (uint64_t)wyhash32_x * 0xa3b195354a39b70d;
    uint32_t m1 = (tmp >> 32) ^ tmp;
    tmp = (uint64_t)m1 * 0x1b03738712fad5c9;
    uint32_t m2 = (tmp >> 32) ^ tmp;
    return m2;
}

class YCSBGenerator {
public:
    // Generate
    YCSBGenerator () {}

    inline YCSBOpType NextA () {
        // ycsba: 50% reads, 50% writes
        uint32_t rnd_num = wyhash32 ();

        if ((rnd_num & 0x1) == 0) {
            return kYCSB_Read;
        } else {
            return kYCSB_Write;
        }
    }

    inline YCSBOpType NextB () {
        // ycsbb: 95% reads, 5% writes
        // 51/1024 = 0.0498
        uint32_t rnd_num = wyhash32 ();

        if ((rnd_num & 1023) < 51) {
            return kYCSB_Write;
        } else {
            return kYCSB_Read;
        }
    }

    inline YCSBOpType NextC () { return kYCSB_Read; }

    inline YCSBOpType NextD () {
        // ycsbd: read latest inserted records
        return kYCSB_Read;
    }

    inline YCSBOpType NextF () {
        // ycsba: 50% reads, 50% writes
        uint32_t rnd_num = wyhash32 ();

        if ((rnd_num & 0x1) == 0) {
            return kYCSB_Read;
        } else {
            return kYCSB_ReadModifyWrite;
        }
    }
};
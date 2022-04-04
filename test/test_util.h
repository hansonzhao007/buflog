#pragma once

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "slice.h"

#define KEY_LEN ((15))

auto rng = std::default_random_engine{};

static constexpr uint64_t kRandNumMax = (1LU << 60);
static constexpr uint64_t kRandNumMaxMask = kRandNumMax - 1;

static uint64_t u64Rand (const uint64_t& min, const uint64_t& max) {
    static thread_local std::mt19937 generator (std::random_device{}());
    std::uniform_int_distribution<uint64_t> distribution (min, max);
    return distribution (generator);
}

namespace util {

// A very simple random number generator.  Not especially good at
// generating truly random bits, but good enough for our needs in this
// package.
class Random {
private:
    uint32_t seed_;

public:
    explicit Random (uint32_t s) : seed_ (s & 0x7fffffffu) {
        // Avoid bad seeds.
        if (seed_ == 0 || seed_ == 2147483647L) {
            seed_ = 1;
        }
    }
    uint32_t Next () {
        static const uint32_t M = 2147483647L;  // 2^31-1
        static const uint64_t A = 16807;        // bits 14, 8, 7, 5, 2, 1, 0
        // We are computing
        //       seed_ = (seed_ * A) % M,    where M = 2^31-1
        //
        // seed_ must not be zero or M, or else all subsequent computed values
        // will be zero or M respectively.  For all other values, seed_ will end
        // up cycling through every number in [1,M-1]
        uint64_t product = seed_ * A;

        // Compute (product % M) using the fact that ((x << 31) % M) == x.
        seed_ = static_cast<uint32_t> ((product >> 31) + (product & M));
        // The first reduction may overflow by 1 bit, so we may need to
        // repeat.  mod == M is not possible; using > allows the faster
        // sign-bit-based test.
        if (seed_ > M) {
            seed_ -= M;
        }
        return seed_;
    }
    // Returns a uniformly distributed value in the range [0..n-1]
    // REQUIRES: n > 0
    uint32_t Uniform (int n) { return Next () % n; }

    // Randomly returns true ~"1/n" of the time, and false otherwise.
    // REQUIRES: n > 0
    bool OneIn (int n) { return (Next () % n) == 0; }

    // Skewed: pick "base" uniformly from range [0,max_log] and then
    // return "base" random bits.  The effect is to pick a number in the
    // range [0,2^max_log-1] with exponential bias towards smaller numbers.
    uint32_t Skewed (int max_log) { return Uniform (1 << Uniform (max_log + 1)); }
};

Slice RandomString (Random* rnd, int len, std::string* dst) {
    dst->resize (len);
    for (int i = 0; i < len; i++) {
        (*dst)[i] = static_cast<char> (' ' + rnd->Uniform (95));  // ' ' .. '~'
    }
    return Slice (*dst);
}

// Helper for quickly generating random data.
class RandomGenerator {
private:
    std::string data_;
    int pos_;

public:
    RandomGenerator () {
        // We use a limited amount of data over and over again and ensure
        // that it is larger than the compression window (32KB), and also
        // large enough to serve all typical value sizes we want to write.
        Random rnd (301);
        std::string piece;
        while (data_.size () < 1048576) {
            // Add a short fragment that is as compressible as specified
            // by FLAGS_compression_ratio.
            RandomString (&rnd, 100, &piece);
            data_.append (piece);
        }
        pos_ = 0;
    }

    Slice Generate (size_t len) {
        if (pos_ + len > data_.size ()) {
            pos_ = 0;
            assert (len < data_.size ());
        }
        pos_ += len;
        return Slice (data_.data () + pos_ - len, len);
    }
};

inline std::string Execute (const std::string& cmd) {
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
        util::Execute ("ipmctl show -performance > " + filename);
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

        printf (
            "--------------------------------------------------------------------------------------"
            "\n");
        printf (
            "DIMM  | Read from IMC | Write from IMC |   Read DIMM |   Write DIMM |     RA |     WA "
            "|\n");
        for (int i = 0; i < dimm_num; i++) {
            printf ("  %-2d  | %13.2f | %14.2f | %11.2f | %12.2f | %6.2f | %6.2f |\n", i,
                    TotalReadRequests_.at (i), TotalWriteRequests_.at (i), TotalMediaReads_.at (i),
                    TotalMediaWrites_.at (i), (TotalMediaReads_.at (i) / TotalReadRequests_.at (i)),
                    (TotalMediaWrites_.at (i) / TotalWriteRequests_.at (i)));
        }

        double seconds = duration.count () / 1000.0;
        printf (
            "--------------------------------------------------------------------------------------"
            "\n");
        printf (
            " SUM:\n"
            " DIMM-R: %7.1f MB/s, %7.1f MB\n"
            " User-R: %7.1f MB/s, %7.1f MB\n"
            " DIMM-W: %7.1f MB/s, %7.1f MB\n"
            " User-W: %7.1f MB/s, %7.1f MB\n"
            "   Time: %7.1f s\n",
            media_read_size_MB / seconds, media_read_size_MB, imc_read_size_MB / seconds,
            imc_read_size_MB, media_write_size_MB / seconds, media_write_size_MB,
            imc_write_size_MB / seconds, imc_write_size_MB, seconds);

        delete start;
        delete end;

        printf ("\033[32mDestroy IPMWatcher.\n\033[0m\n");
    }
};
class RandomKeyTrace {
public:
    RandomKeyTrace (size_t count) {
        count_ = count;
        keys_.resize (count);

        printf ("generate %lu keys\n", count);
        auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, count),
                           [&] (const tbb::blocked_range<uint64_t>& range) {
                               for (uint64_t i = range.begin (); i != range.end (); i++) {
                                   uint64_t num = u64Rand (1LU, kRandNumMax);
                                   keys_[i] = num;
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
        // auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, keys_.size ()),
                           [&] (const tbb::blocked_range<uint64_t>& range) {
                               auto rng = std::default_random_engine{};
                               std::shuffle (keys_.begin () + range.begin (),
                                             keys_.begin () + range.end (), rng);
                           });
        // auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
        //     std::chrono::system_clock::now () - starttime);
        // printf ("randomize duration %f s.\n", duration.count () / 1000000.0);
    }

    class RangeIterator {
    public:
        RangeIterator (std::vector<size_t>* pkey_vec, size_t start, size_t end)
            : pkey_vec_ (pkey_vec), end_index_ (end), cur_index_ (start) {}

        inline bool Valid () { return (cur_index_ < end_index_); }

        inline size_t operator* () { return (*pkey_vec_)[cur_index_]; }

        inline size_t& Next () { return (*pkey_vec_)[cur_index_++]; }
        inline size_t* NextRef () { return &(*pkey_vec_)[cur_index_++]; }

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

// class RandomKeyTraceString {
// public:
//     RandomKeyTraceString (size_t count) {
//         count_ = count;
//         keys_.resize (count);
//         for (size_t i = 0; i < count; i++) {
//             char buf[128];
//             sprintf (buf, "%0.*lu", KEY_LEN, i);
//             keys_[i] = std::string (buf, KEY_LEN);
//         }
//         Randomize ();
//         keys_non_ = keys_;
//         for (size_t i = 0; i < count; i++) {
//             keys_non_[i][0] = 'a';
//         }
//     }

//     ~RandomKeyTraceString () {}

//     void Randomize (void) {
//         printf ("Randomize the trace...\r");
//         fflush (nullptr);
//         std::shuffle (std::begin (keys_), std::end (keys_), rng);
//     }

//     class RangeIterator {
//     public:
//         RangeIterator (std::vector<std::string>* pkey_vec, size_t start, size_t end)
//             : pkey_vec_ (pkey_vec), end_index_ (end), cur_index_ (start) {}

//         inline bool Valid () { return (cur_index_ < end_index_); }

//         inline std::string& Next () { return (*pkey_vec_)[cur_index_++]; }

//         std::vector<std::string>* pkey_vec_;
//         size_t end_index_;
//         size_t cur_index_;
//     };

//     class Iterator {
//     public:
//         Iterator (std::vector<std::string>* pkey_vec, size_t start_index, size_t range)
//             : pkey_vec_ (pkey_vec),
//               range_ (range),
//               end_index_ (start_index % range_),
//               cur_index_ (start_index % range_),
//               begin_ (true) {}

//         Iterator () {}

//         inline bool Valid () { return (begin_ || cur_index_ != end_index_); }

//         inline std::string& Next () {
//             begin_ = false;
//             size_t index = cur_index_;
//             cur_index_++;
//             if (cur_index_ >= range_) {
//                 cur_index_ = 0;
//             }
//             return (*pkey_vec_)[index];
//         }

//         std::string Info () {
//             char buffer[128];
//             sprintf (buffer, "valid: %s, cur i: %lu, end_i: %lu, range: %lu",
//                      Valid () ? "true" : "false", cur_index_, end_index_, range_);
//             return buffer;
//         }

//         std::vector<std::string>* pkey_vec_;
//         size_t range_;
//         size_t end_index_;
//         size_t cur_index_;
//         bool begin_;
//     };

//     Iterator trace_at (size_t start_index, size_t range) {
//         return Iterator (&keys_, start_index, range);
//     }

//     Iterator nontrace_at (size_t start_index, size_t range) {
//         return Iterator (&keys_non_, start_index, range);
//     }

//     RangeIterator Begin (void) { return RangeIterator (&keys_, 0, keys_.size ()); }

//     RangeIterator iterate_between (size_t start, size_t end) {
//         return RangeIterator (&keys_, start, end);
//     }

//     size_t count_;
//     std::vector<std::string> keys_;
//     std::vector<std::string> keys_non_;
// };

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

}  // namespace util
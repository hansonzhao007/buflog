#ifndef _SPOTON_UTIL_H_
#define _SPOTON_UTIL_H_

#include <inttypes.h>

#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>

#include "xxhash.h"

#define SPOTON_FLUSH(addr) asm volatile("clwb (%0)" ::"r"(addr))
#define SPOTON_FLUSHFENCE asm volatile("sfence" ::: "memory")

inline void SPOTON_CLFLUSH (char* data, int len) {
    volatile char* ptr = (char*)((unsigned long)data & ~(64 - 1));
    for (; ptr < data + len; ptr += 64) {
        _mm_clwb ((void*)ptr);
    }
    _mm_sfence ();
}

namespace spoton {

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

    static inline uint64_t umul128 (uint64_t a, uint64_t b, uint64_t* high) noexcept {
        auto result = static_cast<uint128_t> (a) * static_cast<uint128_t> (b);
        *high = static_cast<uint64_t> (result >> 64U);
        return static_cast<uint64_t> (result);
    }

    static inline size_t hash (const void* key, int len) { return ((MurmurHash64A (key, len))); }

    static inline size_t hash_int (uint64_t obj) noexcept {
        // 167079903232 masksum, 120428523 ops best: 0xde5fb9d2630458e9
        static constexpr uint64_t k = UINT64_C (0xde5fb9d2630458e9);
        uint64_t h;
        uint64_t l = umul128 (obj, k, &h);
        return h + l;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    static inline uint64_t MurmurHash64A (const void* key, int len) {
        const uint64_t m = UINT64_C (0xc6a4a7935bd1e995);
        const uint64_t seed = UINT64_C (0xe17a1465);
        const int r = 47;

        uint64_t h = seed ^ (len * m);

        const uint64_t* data = (const uint64_t*)key;
        const uint64_t* end = data + (len / 8);

        while (data != end) {
            uint64_t k = *data++;

            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;
        }

        const unsigned char* data2 = (const unsigned char*)data;

        switch (len & 7) {
            case 7:
                h ^= ((uint64_t)data2[6]) << 48;
            case 6:
                h ^= ((uint64_t)data2[5]) << 40;
            case 5:
                h ^= ((uint64_t)data2[4]) << 32;
            case 4:
                h ^= ((uint64_t)data2[3]) << 24;
            case 3:
                h ^= ((uint64_t)data2[2]) << 16;
            case 2:
                h ^= ((uint64_t)data2[1]) << 8;
            case 1:
                h ^= ((uint64_t)data2[0]);
                h *= m;
        };

        h ^= h >> r;
        h *= m;
        h ^= h >> r;

        return h;
    }
#pragma GCC diagnostic pop

    static inline XXH32_hash_t hash_string (const char* string, XXH32_hash_t seed) {
        // NULL pointers are only valid if the length is zero
        size_t length = (string == NULL) ? 0 : strlen (string);
        return XXH32 (string, length, seed);
    }
};  // end fo class Hasher

// https://rigtorp.se/spinlock/
class SpinLock {
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

static_assert (sizeof (SpinLock) == 1, "AtomicSpinLock size is not 1 byte");

inline std::string print_binary (uint16_t bitmap) {
    char buffer[1024];
    static std::string bit_rep[16] = {"0000", "0001", "0010", "0011", "0100", "0101",
                                      "0110", "0111", "1000", "1001", "1010", "1011",
                                      "1100", "1101", "1110", "1111"};
    sprintf (buffer, "%s%s%s%s", bit_rep[(bitmap >> 12) & 0x0F].c_str (),
             bit_rep[(bitmap >> 8) & 0x0F].c_str (), bit_rep[(bitmap >> 4) & 0x0F].c_str (),
             bit_rep[(bitmap >> 0) & 0x0F].c_str ());
    return buffer;
}

inline bool isPowerOfTwo (size_t n) {
    return (n != 0 && (__builtin_popcount (n >> 32) + __builtin_popcount (n & 0xFFFFFFFF)) == 1);
}

// Usage example:
// BitSet bitset(0x05);
// for (int i : bitset) {
//     printf("i: %d\n", i);
// }
// this will print out 0, 2
class BitSet {
public:
    BitSet () : bits_ (0) {}

    explicit BitSet (uint64_t bits) : bits_ (bits) {}

    inline int validCount (void) { return __builtin_popcountll (bits_); }

    inline BitSet& operator++ () {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool () const { return bits_ != 0; }

    inline int operator* () const {
        // count the tailing zero bit
        return __builtin_ctzll (bits_);
    }

    inline BitSet begin () const { return *this; }

    inline BitSet end () const { return BitSet (0); }

    inline uint64_t bit () { return bits_; }

    inline void set (int pos) {
        assert (pos < 64);
        bits_ |= (1LU << pos);
    }

private:
    friend bool operator== (const BitSet& a, const BitSet& b) { return a.bits_ == b.bits_; }
    friend bool operator!= (const BitSet& a, const BitSet& b) { return a.bits_ != b.bits_; }
    uint64_t bits_;
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

}  // namespace spoton

#endif
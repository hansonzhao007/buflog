#pragma once

#include <inttypes.h>

#include <string>

namespace buflog {

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

    explicit BitSet (uint32_t bits) : bits_ (bits) {}

    // BitSet (const BitSet& b) { bits_ = b.bits_; }

    inline int validCount (void) { return __builtin_popcount (bits_); }

    inline BitSet& operator++ () {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool () const { return bits_ != 0; }

    inline int operator* () const {
        // count the tailing zero bit
        return __builtin_ctz (bits_);
    }

    inline BitSet begin () const { return *this; }

    inline BitSet end () const { return BitSet (0); }

    inline uint32_t bit () { return bits_; }

private:
    friend bool operator== (const BitSet& a, const BitSet& b) { return a.bits_ == b.bits_; }
    friend bool operator!= (const BitSet& a, const BitSet& b) { return a.bits_ != b.bits_; }
    uint32_t bits_;
};

}  // namespace buflog
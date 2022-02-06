#pragma once

#include <inttypes.h>

namespace buflog {

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

};  // end fo class Hasher

};  // namespace buflog

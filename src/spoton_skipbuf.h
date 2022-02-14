#ifndef _SPOTON_SKIPBUF_H_
#define _SPOTON_SKIPBUF_H_

#include "spoton_util.h"

namespace spoton {

struct SkipBuf {
    static constexpr int kValNum = 8;

    size_t keys_[kValNum];
    int count_;
    SpinLock lock_;

    SkipBuf () {
        memset (keys_, 0, sizeof (size_t) * kValNum);
        count_ = 0;
    }

    void Reset () {
        memset (keys_, 0, sizeof (size_t) * kValNum);
        count_ = 0;
    }

    void Sort () { std::sort (keys_, keys_ + count_); }

    bool Contains (size_t key) {
        auto compare_key_vec = _mm512_set1_epi64 (key);
        auto stored_key_vec = _mm512_loadu_epi64 (reinterpret_cast<const __m512i*> (keys_));
        uint8_t mask = _mm512_cmpeq_epi64_mask (compare_key_vec, stored_key_vec);
        if (mask != 0) return true;
        return false;
    }

    bool Insert (size_t k) {
        if (count_ >= kValNum - 1) {
            return false;
        } else {
            keys_[count_++] = k;
            return true;
        }
    }

    bool CompactInsert (size_t k) {
        if (count_ != kValNum - 1) {
            return false;
        }
        keys_[count_++] = k;
        return true;
    }

    inline void Lock () { lock_.lock (); }

    inline void Unlock () { lock_.unlock (); }

    int Count () const { return count_; }

    class Iterator {
    public:
        Iterator (const size_t* _keys, int i) : keys_ (_keys), count_ (i) {}

        inline bool Valid () { return count_ < kValNum; }

        // Prefix ++ overload
        inline Iterator& operator++ () {
            count_++;
            return *this;
        }

        size_t operator* () const { return keys_[count_]; }

        const size_t* keys_;
        int count_;
    };

    Iterator Begin () { return Iterator (keys_, 0); }
};
}  // namespace spoton

#endif
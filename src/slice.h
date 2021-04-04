#pragma once

#include <assert.h>

namespace util {
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

}
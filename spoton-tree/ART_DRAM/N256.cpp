#include <assert.h>

#include <algorithm>

#include "N.h"

namespace ART_DRAM {

template <typename Fn>
void N256::DeepVisit (Stat& stat, Fn&& callback) {
    for (unsigned i = 0; i < 256; i++) {
        if (this->children[i] != nullptr) {
            auto node = this->children[i];
            N::DeepVisit (node, stat, callback);
            callback (node);
        }
    }
}

std::tuple<N*, uint8_t> N256::seekSmaller (uint8_t key) const {
    // TODO: replace this with O(1) algorithm
    for (int i = key; i >= 0; i--) {
        if (children[i] != nullptr) {
            return {children[i], i};
        }
    }
    return {nullptr, 0};
}

// find node's first child that have leaf node >= key
// return [child, child_key]
std::tuple<N*, uint8_t> N256::seek (uint8_t key) const {
    // TODO: replace this with O(1) algorithm
    for (unsigned i = key; i < 256; i++) {
        if (children[i] != nullptr) {
            return {children[i], i};
        }
    }
    return {nullptr, 0};
}

bool N256::isFull () const { return false; }

bool N256::isUnderfull () const { return count == 37; }

void N256::deleteChildren () {
    for (uint64_t i = 0; i < 256; ++i) {
        if (children[i] != nullptr) {
            N::deleteChildren (children[i]);
            N::deleteNode (children[i]);
        }
    }
}

void N256::insert (uint8_t key, N* val) {
    children[key] = val;
    count++;
}

template <class NODE>
void N256::copyTo (NODE* n) const {
    for (int i = 0; i < 256; ++i) {
        if (children[i] != nullptr) {
            n->insert (i, children[i]);
        }
    }
}

bool N256::change (uint8_t key, N* n) {
    children[key] = n;
    return true;
}

N* N256::getChild (const uint8_t k) const { return children[k]; }

void N256::remove (uint8_t k) {
    children[k] = nullptr;
    count--;
}

N* N256::getAnyChild () const {
    N* anyChild = nullptr;
    for (uint64_t i = 0; i < 256; ++i) {
        if (children[i] != nullptr) {
            if (N::isLeaf (children[i])) {
                return children[i];
            } else {
                anyChild = children[i];
            }
        }
    }
    return anyChild;
}

uint64_t N256::getChildren (uint8_t start, uint8_t end, std::tuple<uint8_t, N*>*& children,
                            uint32_t& childrenCount) const {
restart:
    bool needRestart = false;
    uint64_t v;
    v = readLockOrRestart (needRestart);
    if (needRestart) goto restart;
    childrenCount = 0;
    for (unsigned i = start; i <= end; i++) {
        if (this->children[i] != nullptr) {
            children[childrenCount] = std::make_tuple (i, this->children[i]);
            childrenCount++;
        }
    }
    readUnlockOrRestart (v, needRestart);
    if (needRestart) goto restart;
    return v;
}
}  // namespace ART_DRAM
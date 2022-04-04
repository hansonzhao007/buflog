#ifndef SPOTON_TREE_IMPL_H
#define SPOTON_TREE_IMPL_H

#include <vector>

#include "Key.h"
#include "bottomLayer.h"
#include "middleLayer.h"
#include "topLayer.h"
#include "util.h"

namespace spoton {

class SPTree {
private:
    TopLayer topLayer;
    MiddleLayer midLayer;
    BottomLayer botLayer;

public:
    SPTree ();
    ~SPTree (){};

    bool insert (key_t key, TID val);
    bool update (key_t key, TID val);
    bool remove (key_t key);
    TID lookup (key_t key);
    uint64_t scan (key_t startKey, int resultSize, std::vector<TID>& result);

    std::string ToString ();
    // pmem part
private:
    // locate the target middle layer node and its version, without lock
    std::tuple<MLNode*, uint64_t> jumpToMiddleLayer (key_t key);
};

};  // namespace spoton
#endif
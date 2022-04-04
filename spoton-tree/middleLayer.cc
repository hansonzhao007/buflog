#include "middleLayer.h"

#include <climits>
namespace spoton {

void BloomFilterFix64::BuildBloomFilter (LeafNode64* leafnode, BloomFilterFix64& bloomfilter) {
    bloomfilter.reset ();
    for (int i : leafnode->ValidBitSet ()) {
        bloomfilter.set (&leafnode->slots[i].key, 8);
    }
}

bool MLNode::Insert (key_t key, val_t val) {
    // set the bloomfilter
    bloomfilter.set (&key, 8);
    // return false if cannot insert or update
    return leafNode->Insert (key, val);
}

bool MLNode::Remove (key_t key) {
    // return false if cannot insert or update
    return leafNode->Remove (key);
}

MiddleLayer::MiddleLayer () {
    head = new MLNode ();
    auto dummyTail = new MLNode ();

    head->lkey = 0;
    head->hkey = ULLONG_MAX;

    head->prev = nullptr;
    head->next = dummyTail;

    dummyTail->lkey = ULLONG_MAX;
    dummyTail->hkey = ULLONG_MAX;
    dummyTail->prev = head;
    dummyTail->next = nullptr;
}

MLNode* MiddleLayer::FindTargetMLNode (key_t key, MLNode* mnode) {
    assert (mnode);
    while (mnode->lkey > key) {
        mnode = mnode->prev;
        if (mnode == nullptr) {
            ERROR ("mnode is null.");
            printf ("mnode is null.\n");
            exit (1);
        }
    }

    // now key >= mnode->lkey
    while (key >= mnode->hkey) {
        mnode = mnode->next;
        if (mnode == nullptr) {
            ERROR ("mnode is null 2.");
            printf ("mnode is null 2.\n");
            exit (1);
        }
    }

    // now we have mnode, key is in [mnode->lkey, mnode->hkey)
    return mnode;
}

};  // namespace spoton
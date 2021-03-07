/*
   Copyright (c) 2018, UNIST. All rights reserved.  The license is a free
   non-exclusive, non-transferable license to reproduce, use, modify and display
   the source code version of the Software, with or without modifications solely
   for non-commercial research, educational or evaluation purposes. The license
   does not entitle Licensee to technical support, telephone assistance,
   enhancements or updates to the Software. All rights, title to and ownership
   interest in the Software, including all intellectual property rights therein
   shall remain in UNIST.

   Please use at your own risk.
*/
#ifndef __BTREE_PMEM_BUFLOG_H
#define __BTREE_PMEM_BUFLOG_H

#include <cassert>
#include <climits>
#include <fstream>
#include <future>
#include <iostream>
#include <math.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>

// ralloc
#include "ralloc.hpp"
#include "pptr.hpp"
const size_t FASTFAIR_PMEM_SIZE = ((16LU << 30));
const size_t FASTFAIR_PMEM_DATALOG_SIZE = ((8LU << 30));

// buflog
#include "src/buflog.h"

#define PAGESIZE 512
#define PAGESIZE_BUFNODE 256

#define CACHE_LINE_SIZE 64

#define IS_FORWARD(c) ((c & 1) == 0)

using entry_key_t = int64_t;

pthread_mutex_t print_mtx;

using namespace std;

inline void mfence() { asm volatile("sfence" ::: "memory"); }

inline void clflush(char *data, int len) {
  volatile char *ptr = (char *)((unsigned long)data & ~(CACHE_LINE_SIZE - 1));
  for (; ptr < data + len; ptr += CACHE_LINE_SIZE) {
    _mm_clwb((void*)ptr);
  }
  mfence();
}

class page;

class btree {
private:
  int height;
  pptr<page> root;
  pptr<char> datalog_addr_;
  
  // btree();

public:
  using HashTable = robin_hood::unordered_map<size_t, char*>;
  inline static buflog::linkedredolog::DataLog datalog_;
  inline static HashTable bufnode_table_;    // dram table to record bufnode

  ~btree() {
    RP_close();
  }
  friend btree* CreateBtree(void);
  friend btree* RecoverBtree(void);
  void setNewRoot(page *);
  void getNumberOfNodes();
  void btree_insert(entry_key_t, char *);
  void btree_insert_internal(char *, entry_key_t, char *, uint32_t);
  void btree_delete(entry_key_t);
  void btree_delete_internal(entry_key_t, char *, uint32_t, entry_key_t *,
                             bool *, page **);
  char *btree_search(entry_key_t);
  page *btree_search_internal(entry_key_t, int& pi); // search key's leafnode
  void btree_search_range(entry_key_t, entry_key_t, unsigned long *);
  void printAll();

  friend class page;
};

static_assert(sizeof(btree) == 24, "btree size is not 24 byte");

class header {
public:
  pptr<page> leftmost_ptr;// 8 bytes
  pptr<page> sibling_ptr; // 8 bytes
  struct {
    uint32_t level:30;
    uint32_t immutable:2;
  };                      // 4 bytes
  
  uint8_t switch_counter; // 1 bytes
  uint8_t is_deleted;     // 1 bytes
  int16_t last_index;     // 2 bytes

  std::mutex *mtx;        // 8 bytes

  friend class page;
  friend class btree;

public:
  header() {
    mtx = new std::mutex();
    immutable = 0;
    leftmost_ptr = nullptr;
    sibling_ptr = nullptr;
    switch_counter = 0;
    last_index = -1;
    is_deleted = false;
  }

  // TODO: fix the dram lock. Need to new mutex for each page after reboot.
  ~header() { delete mtx; }
};

static_assert(sizeof(header) == 32, "page header is not 32 byte");

class entry {
public:
  entry_key_t key; // 8 bytes
  pptr<char>  ptr; // 8 bytes

public:
  entry() {
    key = LONG_MAX;
    ptr = nullptr;
  }

  friend class page;
  friend class btree;
};

const int cardinality = (PAGESIZE - sizeof(header)) / sizeof(entry);
const int count_in_line = CACHE_LINE_SIZE / sizeof(entry);

class page {
public:
  header hdr;                 // header in persistent memory, 16 bytes
  entry records[cardinality]; // slots in persistent memory, 16 bytes * n

public:
  friend class btree;

  page(uint32_t level = 0) {
    hdr.level = level;
    records[0].ptr = nullptr;
  }

  // this is called when tree grows
  page(page *left, entry_key_t key, page *right, uint32_t level = 0) {
    hdr.leftmost_ptr = left;
    hdr.level = level;
    records[0].key = key;
    records[0].ptr = (char *)right;
    records[1].ptr = nullptr;

    hdr.last_index = 0;

    clflush((char *)this, sizeof(page));
  }

  inline int count() {
    uint8_t previous_switch_counter;
    int count = 0;
    do {
      previous_switch_counter = hdr.switch_counter;
      count = hdr.last_index + 1;

      while (count >= 0 && records[count].ptr != nullptr) {
        if (IS_FORWARD(previous_switch_counter))
          ++count;
        else
          --count;
      }

      if (count < 0) {
        count = 0;
        while (records[count].ptr != nullptr) {
          ++count;
        }
      }

    } while (previous_switch_counter != hdr.switch_counter);

    return count;
  }

  inline bool remove_key(entry_key_t key) {
    // Set the switch_counter
    if (IS_FORWARD(hdr.switch_counter))
      ++hdr.switch_counter;

    bool shift = false;
    int i;
    for (i = 0; records[i].ptr != nullptr; ++i) {
      if (!shift && records[i].key == key) {
        page* lptr = hdr.leftmost_ptr;
        char* rptr = records[i - 1].ptr;
        records[i].ptr =
            (i == 0) ? (char *)lptr : rptr;
        shift = true;
      }

      if (shift) {
        records[i].key = records[i + 1].key;
        records[i].ptr = records[i + 1].ptr;

        // flush
        uint64_t records_ptr = (uint64_t)(&records[i]);
        int remainder = records_ptr % CACHE_LINE_SIZE;
        bool do_flush =
            (remainder == 0) ||
            ((((int)(remainder + sizeof(entry)) / CACHE_LINE_SIZE) == 1) &&
             ((remainder + sizeof(entry)) % CACHE_LINE_SIZE) != 0);
        if (do_flush) {
          clflush((char *)records_ptr, CACHE_LINE_SIZE);
        }
      }
    }

    if (shift) {
      --hdr.last_index;
    }
    return shift;
  }

  bool remove(btree *bt, entry_key_t key, bool only_rebalance = false,
              bool with_lock = true) {
    hdr.mtx->lock();

    bool ret = remove_key(key);

    hdr.mtx->unlock();

    return ret;
  }

  /*
   * Although we implemented the rebalancing of B+-Tree, it is currently blocked
   * for the performance. Please refer to the follow. Chi, P., Lee, W. C., &
   * Xie, Y. (2014, August). Making B+-tree efficient in PCM-based main memory.
   * In Proceedings of the 2014 international symposium on Low power electronics
   * and design (pp. 69-74). ACM.
   */
  bool remove_rebalancing(btree *bt, entry_key_t key,
                          bool only_rebalance = false, bool with_lock = true) {
    if (with_lock) {
      hdr.mtx->lock();
    }
    if (hdr.is_deleted) {
      if (with_lock) {
        hdr.mtx->unlock();
      }
      return false;
    }

    if (!only_rebalance) {
       int num_entries_before = count();

      // This node is root
      if (this == bt->root) {
        if (hdr.level > 0) {
          if (num_entries_before == 1 && hdr.sibling_ptr == nullptr) {
            bt->root = hdr.leftmost_ptr;
            clflush((char *)&(bt->root), sizeof(char *));

            hdr.is_deleted = 1;
          }
        }

        // Remove the key from this node
        bool ret = remove_key(key);

        if (with_lock) {
          hdr.mtx->unlock();
        }
        return true;
      }

      bool should_rebalance = true;
      // check the node utilization
      if (num_entries_before - 1 >= (int)((cardinality - 1) * 0.5)) {
        should_rebalance = false;
      }

      // Remove the key from this node
      bool ret = remove_key(key);

      if (!should_rebalance) {
        if (with_lock) {
          hdr.mtx->unlock();
        }
        return (hdr.leftmost_ptr == nullptr) ? ret : true;
      }
    }

    // Remove a key from the parent node
    entry_key_t deleted_key_from_parent = 0;
    bool is_leftmost_node = false;
    page *left_sibling;
    bt->btree_delete_internal(key, (char *)this, hdr.level + 1,
                              &deleted_key_from_parent, &is_leftmost_node,
                              &left_sibling);

    if (is_leftmost_node) {
      if (with_lock) {
        hdr.mtx->unlock();
      }

      if (!with_lock) {
        hdr.sibling_ptr->hdr.mtx->lock();
      }
      hdr.sibling_ptr->remove(bt, hdr.sibling_ptr->records[0].key, true,
                              with_lock);
      if (!with_lock) {
        hdr.sibling_ptr->hdr.mtx->unlock();
      }
      return true;
    }

    if (with_lock) {
      left_sibling->hdr.mtx->lock();
    }

    while (left_sibling->hdr.sibling_ptr != this) {
      if (with_lock) {
        page *t = left_sibling->hdr.sibling_ptr;
        left_sibling->hdr.mtx->unlock();
        left_sibling = t;
        left_sibling->hdr.mtx->lock();
      } else
        left_sibling = left_sibling->hdr.sibling_ptr;
    }

     int num_entries = count();
     int left_num_entries = left_sibling->count();

    // Merge or Redistribution
    int total_num_entries = num_entries + left_num_entries;
    if (hdr.leftmost_ptr != nullptr)
      ++total_num_entries;

    entry_key_t parent_key;

    if (total_num_entries > cardinality - 1) { // Redistribution
       int m = (int)ceil(total_num_entries / 2);

      if (num_entries < left_num_entries) { // left -> right
        if (hdr.leftmost_ptr == nullptr) {
          for (int i = left_num_entries - 1; i >= m; i--) {
            insert_key(left_sibling->records[i].key,
                       left_sibling->records[i].ptr, &num_entries);
          }

          left_sibling->records[m].ptr = nullptr;
          clflush((char *)&(left_sibling->records[m].ptr), sizeof(char *));

          left_sibling->hdr.last_index = m - 1;
          clflush((char *)&(left_sibling->hdr.last_index), sizeof(int16_t));

          parent_key = records[0].key;
        } else {
          page* lptr = hdr.leftmost_ptr;
          insert_key(deleted_key_from_parent, (char *)lptr,
                     &num_entries);

          for (int i = left_num_entries - 1; i > m; i--) {
            insert_key(left_sibling->records[i].key,
                       left_sibling->records[i].ptr, &num_entries);
          }

          parent_key = left_sibling->records[m].key;

          char* rptr = left_sibling->records[m].ptr;
          hdr.leftmost_ptr = (page *)rptr;
          clflush((char *)&(hdr.leftmost_ptr), sizeof(page *));

          left_sibling->records[m].ptr = nullptr;
          clflush((char *)&(left_sibling->records[m].ptr), sizeof(char *));

          left_sibling->hdr.last_index = m - 1;
          clflush((char *)&(left_sibling->hdr.last_index), sizeof(int16_t));
        }

        if (left_sibling == ((page *)bt->root)) {
          page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
          page* root = new (buf) page(left_sibling, parent_key, this, hdr.level + 1);
          page *new_root = root;
          bt->setNewRoot(new_root);
        } else {
          bt->btree_insert_internal((char *)left_sibling, parent_key,
                                    (char *)this, hdr.level + 1);
        }
      } else { // from leftmost case
        hdr.is_deleted = 1;
        clflush((char *)&(hdr.is_deleted), sizeof(uint8_t));
        page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
        page* root = new (buf) page(hdr.level);
        page *new_sibling = root;
        new_sibling->hdr.mtx->lock();
        new_sibling->hdr.sibling_ptr = hdr.sibling_ptr;

        int num_dist_entries = num_entries - m;
        int new_sibling_cnt = 0;

        if (hdr.leftmost_ptr == nullptr) {
          for (int i = 0; i < num_dist_entries; i++) {
            left_sibling->insert_key(records[i].key, records[i].ptr,
                                     &left_num_entries);
          }

          for (int i = num_dist_entries; records[i].ptr != nullptr; i++) {
            new_sibling->insert_key(records[i].key, records[i].ptr,
                                    &new_sibling_cnt, false);
          }

          clflush((char *)(new_sibling), sizeof(page));

          left_sibling->hdr.sibling_ptr = new_sibling;
          clflush((char *)&(left_sibling->hdr.sibling_ptr), sizeof(page *));

          parent_key = new_sibling->records[0].key;
        } else {
          page* lptr = hdr.leftmost_ptr;
          left_sibling->insert_key(deleted_key_from_parent,
                                   (char *)lptr, &left_num_entries);

          for (int i = 0; i < num_dist_entries - 1; i++) {
            left_sibling->insert_key(records[i].key, records[i].ptr,
                                     &left_num_entries);
          }

          parent_key = records[num_dist_entries - 1].key;

          char* rptr = records[num_dist_entries - 1].ptr;
          new_sibling->hdr.leftmost_ptr = (page *) rptr;
          for (int i = num_dist_entries; records[i].ptr != nullptr; i++) {
            new_sibling->insert_key(records[i].key, records[i].ptr,
                                    &new_sibling_cnt, false);
          }
          clflush((char *)(new_sibling), sizeof(page));

          left_sibling->hdr.sibling_ptr = new_sibling;
          clflush((char *)&(left_sibling->hdr.sibling_ptr), sizeof(page *));
        }

        if (left_sibling == ((page *)bt->root)) {
          page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
          page* root = new (buf) page(left_sibling, parent_key, new_sibling, hdr.level + 1);
          page *new_root = root;              
          bt->setNewRoot(new_root);
        } else {
          bt->btree_insert_internal((char *)left_sibling, parent_key,
                                    (char *)new_sibling, hdr.level + 1);
        }

        new_sibling->hdr.mtx->unlock();
      }
    } else {
      hdr.is_deleted = 1;
      clflush((char *)&(hdr.is_deleted), sizeof(uint8_t));

      if (hdr.leftmost_ptr != nullptr) {
        page* lptr = hdr.leftmost_ptr;
        left_sibling->insert_key(deleted_key_from_parent,
                                 (char *)lptr, &left_num_entries);
      }

      for (int i = 0; records[i].ptr != nullptr; ++i) {
        left_sibling->insert_key(records[i].key, records[i].ptr,
                                 &left_num_entries);
      }

      left_sibling->hdr.sibling_ptr = hdr.sibling_ptr;
      clflush((char *)&(left_sibling->hdr.sibling_ptr), sizeof(page *));
    }

    if (with_lock) {
      left_sibling->hdr.mtx->unlock();
      hdr.mtx->unlock();
    }

    return true;
  }

  /**
   * @brief Create a new page using existing page and full bufnode.
   *        Out-of-place merge.
   * !note: make sure the bufnode has been sorted before using.
   * @param p 
   * @param bufnode 
   * @return page* 
   */
  inline page* merge_page_buffer(page* old_leafnode, int num_entreis, buflog::SortedBufNode* bufnode) {
    // step 1. Create new leafnode page
    page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
    page* new_leafnode = new (buf) page(old_leafnode->hdr.level);

    // step 2. Merge the kv in old_leafnode and bufnode into new_leafnode
    auto iter = bufnode->sBegin();
    int old_i = 0, new_i = 0;
    while (old_i < num_entreis && iter.Valid()) {
      auto& buf_kv = *iter;
      auto& leaf_kv = old_leafnode->records[old_i];
      if (buf_kv.key < leaf_kv.key) {
        new_leafnode->records[new_i].key = buf_kv.key;
        new_leafnode->records[new_i].ptr = buf_kv.val;
        iter++;
      } else {
        new_leafnode->records[new_i].key = leaf_kv.key;
        new_leafnode->records[new_i].ptr = leaf_kv.ptr;
        old_i++;
      }
      new_i++;
    }
    // merge the remaining 
    if (old_i < num_entreis) {
      while (old_i < num_entreis) {
        auto& leaf_kv = old_leafnode->records[old_i];
        new_leafnode->records[new_i].key = leaf_kv.key;
        new_leafnode->records[new_i].ptr = leaf_kv.ptr;
        old_i++;
        new_i++;
      }
    } else {
      while (iter.Valid()) {
        auto& buf_kv = *iter;
        new_leafnode->records[new_i].key = buf_kv.key;
        new_leafnode->records[new_i].ptr = buf_kv.val;
        iter++;
        new_i++;
      }
    }

    // step 3. Link the new_leaf to old_leaf's sibling
    new_leafnode->hdr.sibling_ptr = old_leafnode->hdr.sibling_ptr;

    // step 4. Flush the entire content
    clflush((char*)new_leafnode, sizeof(page));

    return new_leafnode;
  }

  inline void insert_key(entry_key_t key, char *ptr, int *num_entries,
                         bool flush = true, bool update_last_index = true) {
    // update switch_counter
    if (!IS_FORWARD(hdr.switch_counter))
      ++hdr.switch_counter;

    // FAST
    if (*num_entries == 0) { // this page is empty
      entry *new_entry = (entry *)&records[0];
      entry *array_end = (entry *)&records[1];
      new_entry->key = (entry_key_t)key;
      new_entry->ptr = (char *)ptr;

      array_end->ptr = (char *)nullptr;

      if (flush) {
        clflush((char *)this, CACHE_LINE_SIZE);
      }
    } else {
      int i = *num_entries - 1, inserted = 0, to_flush_cnt = 0;
      records[*num_entries + 1].ptr = records[*num_entries].ptr;
      if (flush) {
        if ((uint64_t) & (records[*num_entries + 1].ptr) % CACHE_LINE_SIZE == 0)
          clflush((char *)&(records[*num_entries + 1].ptr), sizeof(char *));
      }

      // FAST
      for (i = *num_entries - 1; i >= 0; i--) {
        if (key < records[i].key) {
          records[i + 1].ptr = records[i].ptr;
          records[i + 1].key = records[i].key;

          if (flush) {
            uint64_t records_ptr = (uint64_t)(&records[i + 1]);

            int remainder = records_ptr % CACHE_LINE_SIZE;
            bool do_flush =
                (remainder == 0) ||
                ((((int)(remainder + sizeof(entry)) / CACHE_LINE_SIZE) == 1) &&
                 ((remainder + sizeof(entry)) % CACHE_LINE_SIZE) != 0);
            if (do_flush) {
              clflush((char *)records_ptr, CACHE_LINE_SIZE);
              to_flush_cnt = 0;
            } else
              ++to_flush_cnt;
          }
        } else {
          records[i + 1].ptr = records[i].ptr;
          records[i + 1].key = key;
          records[i + 1].ptr = ptr;

          if (flush)
            clflush((char *)&records[i + 1], sizeof(entry));
          inserted = 1;
          break;
        }
      }
      if (inserted == 0) {
        page* lptr = hdr.leftmost_ptr;
        records[0].ptr = (char *)lptr;
        records[0].key = key;
        records[0].ptr = ptr;
        if (flush)
          clflush((char *)&records[0], sizeof(entry));
      }
    }

    if (update_last_index) {
      hdr.last_index = *num_entries;
    }
    ++(*num_entries);
  }

  // Insert a new key - FAST and FAIR
  page *store(page* parent, int pi, btree *bt, char *left, entry_key_t key, char *right, bool flush,
              bool with_lock, page *invalid_sibling = nullptr) {
    if (with_lock) {
      hdr.mtx->lock(); // Lock the write lock
    }
    if (hdr.is_deleted) {
      if (with_lock) {
        hdr.mtx->unlock();
      }

      return nullptr;
    }

    // If this node has a sibling node,
    if (hdr.sibling_ptr != nullptr && (hdr.sibling_ptr != invalid_sibling)) {
      // Compare this key with the first key of the sibling
      if (key > hdr.sibling_ptr->records[0].key) {
        if (with_lock) {
          hdr.mtx->unlock(); // Unlock the write lock
        }
        return hdr.sibling_ptr->store(nullptr, pi, bt, nullptr, key, right, true, with_lock,
                                      invalid_sibling);
      }
    }

    int num_entries = count();
    
    // !buflog: Obtain the bufnode according to the page address
    buflog::SortedBufNode* bufnode = nullptr;
    int bufnode_entries = 0;
    if (hdr.leftmost_ptr == nullptr) { // leafnode
      auto iter = btree::bufnode_table_.find(size_t(this));
      if (iter != btree::bufnode_table_.end()) {
        bufnode = reinterpret_cast<buflog::SortedBufNode*>(iter->second);
        bufnode_entries = bufnode->ValidCount();
        // ------ append to log ------
        // Slice* val_ptr = reinterpret_cast<Slice*>(right);
        // uint8_t checksum = buflog::Hasher::hash(val_ptr->data(), val_ptr->size()) & 0xFF;
        // // TODO: add next position
        // right = bt->datalog_.Append(*val_ptr, bt->datalog_.LogTail(), checksum, true);
        // printf("append addr: 0x%lx\n", right);
      }
    }

    // FAST
    if (num_entries + bufnode_entries < cardinality - 1) {
      // !buflog: insert to the bufnode if possible      
      if (bufnode != nullptr) { // the leafnode has bufnode, so it cannot be the first leafnode(leftmost leafnode)
        // insert to the bufnode            
        bool res = bufnode->Put(key, right);
        if (res == true) {
          // successfully inserted to bufnode
        } else {
          // bufnode is full, flush to pmem page          
          // {{{ ------------------ out-of-place merge ---------------------
          // step 1. merge a new leafnode (situation 1)
          bufnode->Sort();
          page* new_leafnode = merge_page_buffer(this, num_entries, bufnode);
          // step 2. change this leafnode to immutable (situation 2)
          hdr.immutable = true;
          clflush((char*)this, sizeof(header));
          // step 3. change the previous leafnode's sibling to new_leafnode (situation 3)
          page* pre_leafnode  = nullptr;
          int ppi;
          page* pre_parent = bt->btree_search_internal(records[0].key - 1, ppi); // find the previous leafnode
          if (ppi == -2) {
            printf("ppi should not be -2.\n");
            exit(1);
          } else if (ppi == -1) {
            // previous leafnode is the leftmost one
            pre_leafnode = pre_parent->hdr.leftmost_ptr;
          } else {
            char* tmp = pre_parent->records[ppi].ptr;
            pre_leafnode = (page*)tmp;
          }
          pre_leafnode->hdr.sibling_ptr = new_leafnode;
          clflush((char*)pre_leafnode, sizeof(header));
          // step 4. change the parent pointer of this page to new_leafnode (situation 4)
          if (pi == -2) {
            printf("pi should not be -2\n");
            exit(1);
          } else if (pi == -1) {
            // the parent pointer is the leftmost of parent
            parent->hdr.leftmost_ptr = new_leafnode;
            clflush((char*)parent, sizeof(header));
            // printf("pi should not be -1\n");
            // bt->printAll();
            // printf("the insert key is %d\n", key);
            // exit(1);
          } else {
            parent->records[pi].ptr = (char*)new_leafnode;
            clflush((char*)&parent->records[pi], sizeof(entry));
          }
          
          // step 5. move the bufnode to new_leafnode and erase old bufnode mapping in bufnode_table_
          bufnode->Reset();
          bufnode->Put(key, right);
          btree::bufnode_table_.insert({size_t(new_leafnode), (char*)bufnode});
          btree::bufnode_table_.erase((size_t(this)));
          //     ------------------ out-of-place merge --------------------- }}}

          // // {{{ ------------------ in-place merge ---------------------
          // bufnode->Sort();
          // auto iter = bufnode->sBegin();  
          // while (iter.Valid()) {
          //   auto& kv = *iter;
          //   insert_key(kv.key, kv.val, &num_entries, false);
          //   iter++;
          // }
          // bufnode->Reset();
          // bufnode->Put(key, right);
          // //     ------------------ in-place merge --------------------- }}}
        }
      } else { // the leafnode does not have bufnode, normal insertion
        // when there is no bufnode (internal node), insert directly
        insert_key(key, right, &num_entries, flush);
      }

      if (with_lock) {
        hdr.mtx->unlock(); // Unlock the write lock
      }

      return this;
    } else { // FAIR
      // overflow

      // !buflog: flush bufnode to page first
      if (bufnode != nullptr) {
        bufnode->Sort();
        auto iter = bufnode->sBegin();
        while (iter.Valid()) {
          auto& kv = *iter;
          insert_key(kv.key, kv.val, &num_entries, false);
          iter++;
        }
        bufnode->Reset();
      }

      // create a new node
      page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
      page* root = new (buf) page(hdr.level);
      page *sibling = root;
       int m = (int)ceil(num_entries / 2);
      entry_key_t split_key = records[m].key;

      // migrate half of keys into the sibling
      int sibling_cnt = 0;
      if (hdr.leftmost_ptr == nullptr) { // leaf node
        for (int i = m; i < num_entries; ++i) {
          sibling->insert_key(records[i].key, records[i].ptr, &sibling_cnt,
                              false);
        }

        // !buflog: create a bufnode for leaf page
        buflog::SortedBufNode* new_bufnode = new buflog::SortedBufNode();                              
        btree::bufnode_table_.insert({(size_t)sibling, (char*)new_bufnode});

      } else { // internal node
        for (int i = m + 1; i < num_entries; ++i) {
          sibling->insert_key(records[i].key, records[i].ptr, &sibling_cnt,
                              false);
        }
        char* rptr = records[m].ptr;
        sibling->hdr.leftmost_ptr = (page *)rptr;
      }

      sibling->hdr.sibling_ptr = hdr.sibling_ptr;
      clflush((char *)sibling, sizeof(page));

      hdr.sibling_ptr = sibling;
      clflush((char *)&hdr, sizeof(hdr));

      // set to nullptr
      if (IS_FORWARD(hdr.switch_counter))
        hdr.switch_counter += 2;
      else
        ++hdr.switch_counter;
      records[m].ptr = nullptr;
      clflush((char *)&records[m], sizeof(entry));

      hdr.last_index = m - 1;
      clflush((char *)&(hdr.last_index), sizeof(int16_t));

      num_entries = hdr.last_index + 1;

      page *ret = nullptr;

      // insert the key
      if (key < split_key) {
        insert_key(key, right, &num_entries);
        ret = this;
      } else {
        sibling->insert_key(key, right, &sibling_cnt);
        ret = sibling;
      }

      // Set a new root or insert the split key to the parent
      if (bt->root == this) { // only one node can update the root ptr
        page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
        page* root = new (buf) page((page *)this, split_key, sibling, hdr.level + 1);
        page *new_root = root;            
        bt->setNewRoot(new_root);

        if (with_lock) {
          hdr.mtx->unlock(); // Unlock the write lock
        }
      } else {
        if (with_lock) {
          hdr.mtx->unlock(); // Unlock the write lock
        }
        bt->btree_insert_internal(nullptr, split_key, (char *)sibling,
                                  hdr.level + 1);
      }

      return ret;
    }
  }

  // Search keys with linear search
  void linear_search_range(entry_key_t min, entry_key_t max,
                           unsigned long *buf) {
    int i, off = 0;
    uint8_t previous_switch_counter;
    page *current = this;

    while (current) {
      int old_off = off;
      do {
        previous_switch_counter = current->hdr.switch_counter;
        off = old_off;

        entry_key_t tmp_key;
        char *tmp_ptr;

        if (IS_FORWARD(previous_switch_counter)) {
          if ((tmp_key = current->records[0].key) > min) {
            if (tmp_key < max) {
              if ((tmp_ptr = current->records[0].ptr) != nullptr) {
                if (tmp_key == current->records[0].key) {
                  if (tmp_ptr) {
                    buf[off++] = (unsigned long)tmp_ptr;
                  }
                }
              }
            } else
              return;
          }

          for (i = 1; current->records[i].ptr != nullptr; ++i) {
            if ((tmp_key = current->records[i].key) > min) {
              if (tmp_key < max) {
                if ((tmp_ptr = current->records[i].ptr) !=
                    current->records[i - 1].ptr) {
                  if (tmp_key == current->records[i].key) {
                    if (tmp_ptr)
                      buf[off++] = (unsigned long)tmp_ptr;
                  }
                }
              } else
                return;
            }
          }
        } else {
          for (i = count() - 1; i > 0; --i) {
            if ((tmp_key = current->records[i].key) > min) {
              if (tmp_key < max) {
                if ((tmp_ptr = current->records[i].ptr) !=
                    current->records[i - 1].ptr) {
                  if (tmp_key == current->records[i].key) {
                    if (tmp_ptr)
                      buf[off++] = (unsigned long)tmp_ptr;
                  }
                }
              } else
                return;
            }
          }

          if ((tmp_key = current->records[0].key) > min) {
            if (tmp_key < max) {
              if ((tmp_ptr = current->records[0].ptr) != nullptr) {
                if (tmp_key == current->records[0].key) {
                  if (tmp_ptr) {
                    buf[off++] = (unsigned long)tmp_ptr;
                  }
                }
              }
            } else
              return;
          }
        }
      } while (previous_switch_counter != current->hdr.switch_counter);

      current = current->hdr.sibling_ptr;
    }
  }

  /**
   * @brief Find the `first key` in page that is <= input `key`
   * 
   * @param key 
   * @param bt 
   * @param pi : The position of the `first key` in this page
   *        -1 : means we should take the leftmost_ptr in this page
   * @return char* : The pointer pointed by the `first key`
   */
  char *linear_search(entry_key_t key, btree* bt, int& pi) {
    int i = 1;
    uint8_t previous_switch_counter;
    char *ret = nullptr;
    char *t;
    entry_key_t k;

    if (hdr.leftmost_ptr == nullptr) { // Search a leaf node
      do {
        previous_switch_counter = hdr.switch_counter;
        ret = nullptr;

        // search from left ro right
        if (IS_FORWARD(previous_switch_counter)) {
          if ((k = records[0].key) == key) {
            if ((t = records[0].ptr) != nullptr) {
              if (k == records[0].key) {
                ret = t;
                continue;
              }
            }
          }

          for (i = 1; records[i].ptr != nullptr; ++i) {
            if ((k = records[i].key) == key) {
              if (records[i - 1].ptr != (t = records[i].ptr)) {
                if (k == records[i].key) {
                  ret = t;
                  break;
                }
              }
            }
          }
        } else { // search from right to left
          for (i = count() - 1; i > 0; --i) {
            if ((k = records[i].key) == key) {
              if (records[i - 1].ptr != (t = records[i].ptr) && t) {
                if (k == records[i].key) {
                  ret = t;
                  break;
                }
              }
            }
          }

          if (!ret) {
            if ((k = records[0].key) == key) {
              if (nullptr != (t = records[0].ptr) && t) {
                if (k == records[0].key) {
                  ret = t;
                  continue;
                }
              }
            }
          }
        }
      } while (hdr.switch_counter != previous_switch_counter);

      if (ret) {
        return ret;
      }

      page* sptr = hdr.sibling_ptr;
      if ((t = (char *)sptr) && key >= ((page *)t)->records[0].key)
        return t;

      return nullptr;
    } 
    else { // internal node
      do {
        previous_switch_counter = hdr.switch_counter;
        ret = nullptr;

        if (IS_FORWARD(previous_switch_counter)) {
          if (key < (k = records[0].key)) {
            page* lptr = hdr.leftmost_ptr;
            if ((t = (char *)lptr) != records[0].ptr) {
              ret = t;
              pi = -1; // means we should take the leftmost_ptr in this page
              continue;
            }
          }

          for (i = 1; records[i].ptr != nullptr; ++i) {
            if (key < (k = records[i].key)) {
              if ((t = records[i - 1].ptr) != records[i].ptr) {
                ret = t;
                pi = i - 1;
                break;
              }
            }
          }

          if (!ret) {
            ret = records[i - 1].ptr;
            pi = i - 1;
            continue;
          }
        } else { // search from right to left
          for (i = count() - 1; i >= 0; --i) {
            if (key >= (k = records[i].key)) {
              if (i == 0) {
                page* lptr = hdr.leftmost_ptr;
                if ((char *)lptr != (t = records[i].ptr)) {
                  ret = t;
                  pi = i;
                  break;
                }
              } else {
                if (records[i - 1].ptr != (t = records[i].ptr)) {
                  ret = t;
                  pi = i;
                  break;
                }
              }
            }
          }
        }
      } while (hdr.switch_counter != previous_switch_counter);

      page* sptr = hdr.sibling_ptr;
      if ((t = (char *)sptr) != nullptr) {
        if (key >= ((page *)t)->records[0].key) {
          pi = 0;
          return t;
        }
      }

      if (ret) {
        return ret;
      } else {
        page* lptr = hdr.leftmost_ptr;
        pi = -1;
        return (char *)lptr;
      }
    }

    return nullptr;
  }

  // print a node
  void print() {
    if (hdr.leftmost_ptr == nullptr)
      printf("[%d] leaf 0x%lx \n", this->hdr.level, this);
    else
      printf("[%d] internal 0x%lx \n", this->hdr.level, this);
    printf("last_index: %d\n", hdr.last_index);
    printf("switch_counter: %d\n", hdr.switch_counter);
    printf("search direction: ");
    if (IS_FORWARD(hdr.switch_counter))
      printf("->\n");
    else
      printf("<-\n");

    if (hdr.leftmost_ptr != nullptr) {
      page* lptr = hdr.leftmost_ptr;
      printf("0x%lx ", lptr);
    }

    for (int i = 0; records[i].ptr != nullptr; ++i) {
      char* rptr = records[i].ptr;
      printf("%ld,0x%lx ", records[i].key, rptr);
    }

    page* sptr = hdr.sibling_ptr;
    printf("0x%lx ", sptr);

    printf("\n");
  }

  void printAll() {
    if (hdr.leftmost_ptr == nullptr) {
      printf("printing leaf node: ");
      print();
    } else {
      printf("printing internal node: ");
      print();
      hdr.leftmost_ptr->printAll();
      for (int i = 0; records[i].ptr != nullptr; ++i) {
        char* rptr = records[i].ptr;
        ((page *)rptr)->printAll();
      }
    }
  }
};

void DistroyBtree(void) {
  remove("/mnt/pmem/fastfair_sb");
  remove("/mnt/pmem/fastfair_desc");
  remove("/mnt/pmem/fastfair_basemd");
}

btree* CreateBtree(void) {
  printf("Create Pmem Fast&Fair\n");
  // Step1. Initialize pmem library
  bool res = RP_init("fastfair", FASTFAIR_PMEM_SIZE);
  btree* btree_root = nullptr;
  if (res) {    
      btree_root = RP_get_root<btree>(0);
      int recover_res = RP_recover();
      if (recover_res == 1) {
          printf("Ralloc Dirty open, recover\n");
      } else {                
          printf("Ralloc Clean restart\n");
      }
  } else {
      printf("Ralloc Clean create\n");
  }
  btree_root = (btree*)RP_malloc(sizeof(btree));
  RP_set_root(btree_root, 0);

  // Step2. Initialize members
  btree_root->height = 1;
  page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
  page* root = new (buf) page(0);
  btree_root->root = root;
  btree_root->datalog_addr_ = reinterpret_cast<char*>(RP_malloc(FASTFAIR_PMEM_DATALOG_SIZE));
  btree::datalog_.Create(btree_root->datalog_addr_, FASTFAIR_PMEM_DATALOG_SIZE);
  return btree_root;
}

btree* RecoverBtree(void) {
  // Step1. Open the pmem file
  bool res = RP_init("fastfair", FASTFAIR_PMEM_SIZE);
  
  if (res) {
      printf("Ralloc Prepare to recover\n");
      btree* btree_root = RP_get_root<btree>(0);
      int recover_res = RP_recover();
      if (recover_res == 1) {
          printf("Ralloc Dirty open, recover\n");
      } else {                
          printf("Ralloc Clean restart\n");
      }
  } else {
      printf("Ralloc Nothing to recover\n");
      // exit(1);
  }

  btree* btree_root = RP_get_root<btree>(0);
  btree::datalog_.Open(btree_root->datalog_addr_);
  return btree_root;
}

void btree::setNewRoot(page *new_root) {
  this->root = new_root;
  clflush((char *)&(this->root), sizeof(char *));
  ++height;
}

// find the first key that is <= input key, and renturn its pointer
char *btree::btree_search(entry_key_t key) {
  page *p = root;

  // search until to level 1, then p will be the pointer to leafnode
  int pi;
  while (p->hdr.level > 1) {    
    p = (page *)p->linear_search(key, this, pi);
  }
  if (p->hdr.level ==1) {
    // set p to the pointer to leafnode
    p = (page *)p->linear_search(key, this, pi);
  } 

  // !buflog: check the bufnode first if we can find one.
  auto iter = btree::bufnode_table_.find(size_t(p));
  if (iter != btree::bufnode_table_.end()) {
    buflog::SortedBufNode* bufnode = reinterpret_cast<buflog::SortedBufNode*>(iter->second);
    char *ret;
    bool res = bufnode->Get(key, ret);
    if (res == true) {
      return ret;
    }
  }

  page *t = nullptr;
  // if == sibling_ptr, continue searching the node on the linked-list
  int i;
  while ((t = (page *)p->linear_search(key, this, i)) == p->hdr.sibling_ptr) {
    p = t;
    if (!p) {
      break;
    }
  }

  if (!t) {
    // printf("NOT FOUND %lu, t = %x\n", key, t);
    return nullptr;
  }

  return (char *)t;
}

/**
 * @brief find the first key that is <= input key, 
 *        and renturn its pointer(here should be the pointer to the leafnode)
 *        !buflog uses this function to find the previous leafnode of an immutable
 *        leafnode as long as we set the `input key` 1 smaller than the first key in 
 *        current immutable leafnode.
 * @param key 
 * @param pi: The position of the `first key` in the parent 
 *        -2: the root node is the leafnode
 *        -1: should obtain the `parent->hdr.leftmost_ptr` as prev_leafnode
 *     other: `parent->records[pi].ptr` is the previous leafnode
 * @return page* : parent node of the `first key`
 */
page *btree::btree_search_internal(entry_key_t key, int &pi) {
  page *p = root;
  
  page* parent = nullptr;
  pi = -2;
  while (p->hdr.level > 0) {
    parent = p;
    p = (page *)p->linear_search(key, this, pi);
  }
  return parent;
}

// insert the key in the leaf node
void btree::btree_insert(entry_key_t key, char *right) { // need to be string
  page *p = root;

  // find the leaf node. Normally p will never be nullptr
  page* parent = nullptr;
  int pi = -2;
  while (p->hdr.leftmost_ptr != nullptr) { // still internal node
    parent = p;
    p = (page *)p->linear_search(key, this, pi);
  }

  if (!p->store(parent, pi, this, nullptr, key, right, true, true)) { // store
    btree_insert(key, right);
  }
}

// store the key into the node at the given level
void btree::btree_insert_internal(char *left, entry_key_t key, char *right,
                                  uint32_t level) {
  if (level > root->hdr.level)
    return;

  page *p = this->root;

  page* parent = nullptr;
  int pi = -2;
  while (p->hdr.level > level) {
    parent = p;
    p = (page *)p->linear_search(key, this, pi);
  }

  if (!p->store(parent, pi, this, nullptr, key, right, true, true)) {
    btree_insert_internal(left, key, right, level);
  }
}

void btree::btree_delete(entry_key_t key) {
  page *p = root;

  int pi = -2;
  while (p->hdr.leftmost_ptr != nullptr) {
    p = (page *)p->linear_search(key, this, pi);
  }

  page *t;
  while ((t = (page *)p->linear_search(key, this, pi)) == p->hdr.sibling_ptr) {
    p = t;
    if (!p)
      break;
  }

  if (p) {
    if (!p->remove(this, key)) {
      btree_delete(key);
    }
  } else {
    printf("not found the key to delete %lu\n", key);
  }
}

void btree::btree_delete_internal(entry_key_t key, char *ptr, uint32_t level,
                                  entry_key_t *deleted_key,
                                  bool *is_leftmost_node, page **left_sibling) {
  if (level > this->root->hdr.level)
    return;

  page *p = this->root;
  int pi = -2;
  while (p->hdr.level > level) {
    p = (page *)p->linear_search(key, this, pi);
  }

  p->hdr.mtx->lock();

  page* lptr = p->hdr.leftmost_ptr;
  if ((char *)lptr == ptr) {
    *is_leftmost_node = true;
    p->hdr.mtx->unlock();
    return;
  }

  *is_leftmost_node = false;

  for (int i = 0; p->records[i].ptr != nullptr; ++i) {
    if (p->records[i].ptr == ptr) {
      if (i == 0) {
        page* lptr = p->hdr.leftmost_ptr;
        if ((char *)lptr != p->records[i].ptr) {
          *deleted_key = p->records[i].key;
          *left_sibling = p->hdr.leftmost_ptr;
          p->remove(this, *deleted_key, false, false);
          break;
        }
      } else {
        if (p->records[i - 1].ptr != p->records[i].ptr) {
          *deleted_key = p->records[i].key;
          char* rptr = p->records[i - 1].ptr;
          *left_sibling = (page *)rptr;
          p->remove(this, *deleted_key, false, false);
          break;
        }
      }
    }
  }

  p->hdr.mtx->unlock();
}

// Function to search keys from "min" to "max"
void btree::btree_search_range(entry_key_t min, entry_key_t max,
                               unsigned long *buf) {
  page *p = root;

  int pi = -2;
  while (p) {
    if (p->hdr.leftmost_ptr != nullptr) {
      // The current page is internal
      p = (page *)p->linear_search(min, this, pi);
    } else {
      // Found a leaf
      p->linear_search_range(min, max, buf);

      break;
    }
  }
}

void btree::printAll() {
  pthread_mutex_lock(&print_mtx);
  int total_keys = 0;
  page *leftmost = root;
  printf("root: %x\n", leftmost);
  do {
    page *sibling = leftmost;
    printf("--------------- level %d --------------------------\n", sibling->hdr.level);
    while (sibling) {
      if (sibling->hdr.level == 0) {
        total_keys += sibling->hdr.last_index + 1;
      }
      sibling->print();
      sibling = sibling->hdr.sibling_ptr;
    }
    
    leftmost = leftmost->hdr.leftmost_ptr;
  } while (leftmost);

  printf("total number of keys: %d\n", total_keys);
  pthread_mutex_unlock(&print_mtx);
}

#endif
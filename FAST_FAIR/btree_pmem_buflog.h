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
const size_t FASTFAIR_PMEM_SIZE = ((100LU << 30));
const size_t FASTFAIR_PMEM_DATALOG_SIZE = ((16LU << 30));

// buflog
#include "src/buflog.h"

// #define BUFLOG_DEBUG

#ifdef BUFLOG_DEBUG
#include "src/logger.h"
#define BUFLOG_INFO(M, ...)\
do {\
    INFO(M, ##__VA_ARGS__);\
} while(0);
#define BUFLOG_ERROR(M, ...)\
do {\
    ERROR(M, ##__VA_ARGS__);\
} while(0);
#else
#define BUFLOG_INFO(M, ...)\
do {\
} while(0);
#define BUFLOG_ERROR(M, ...)\
do {\
} while(0);
#endif

#define CONFIG_BUFNODE
#define CONFIG_OUT_OF_PLACE_MERGE
#define CONFIG_DRAM_INNER

// #define CONFIG_APPEND_TO_LOG_TEST

#define PAGESIZE 512

#define CACHE_LINE_SIZE 64

#define IS_FORWARD(c) ((c & 1) == 0)

using entry_key_t = int64_t;

pthread_mutex_t print_mtx;

using namespace std;

#define FASTFAIR_CPU_RELAX()   asm volatile("pause\n": : :"memory")

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
  page* root;

  pptr<page> root_pmem_;
  page*      root_dram_;
  pptr<char> datalog_addr_;
  
public:
  inline static buflog::linkedredolog::DataLog datalog_;
  inline static turbo::unordered_map<size_t, char*> bufnode_table_;    // dram table to record bufnode

  ~btree() {
    btree::datalog_.CommitTail(true);
    RP_close();
  }
  friend btree* CreateBtree(void);
  friend btree* RecoverBtree(void);

  void setNewRoot(page *);
  void getNumberOfNodes();
  void btree_insert(entry_key_t, char *);
  void btree_insert_internal(char *, entry_key_t, char *, uint32_t);
  void btree_update_internal(entry_key_t key, char* new_leafnode_ptr, uint32_t level);
  void btree_update_prev_sibling(entry_key_t key, char* new_leafnode_ptr);
  // void btree_delete(entry_key_t);
  // void btree_delete_internal(entry_key_t, char *, uint32_t, entry_key_t *,
  //                            bool *, page **);
  char *btree_search(entry_key_t);
  page *btree_search_internal(entry_key_t, int& pi); // search key's leafnode
  void btree_search_range(entry_key_t, entry_key_t, unsigned long *);
  void printAll(int level);

  friend class page;
};

// static_assert(sizeof(btree) == 24, "btree size is not 24 byte");

// https://rigtorp.se/spinlock/
class HeadSpinLock {
public:
    std::atomic<bool> lock_ = {0};

    void inline lock() noexcept {
        for (;;) {
        // Optimistically assume the lock is free on the first try
        if (!lock_.exchange(true, std::memory_order_acquire)) {
            return;
        }
        // Wait for lock to be released without generating cache misses
        while (lock_.load(std::memory_order_relaxed)) {
            // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
            // hyper-threads
            __builtin_ia32_pause();
        }
        }
    }

    bool inline is_locked(void) noexcept {
        return lock_.load(std::memory_order_relaxed);
    }

    bool inline try_lock() noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load(std::memory_order_relaxed) &&
            !lock_.exchange(true, std::memory_order_acquire);
    }

    void inline unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
}; // end of class

class header {
private:
  union{
    pptr<page> leftmost_ptr_pmem;// 8 bytes
    page* leftmost_ptr_dram;
  };
  union {
    pptr<page> sibling_ptr_pmem; // 8 bytes  
    page* sibling_ptr_dram;
  };

public:
  struct {
    uint32_t level:26;
    uint32_t immutable:2;
    uint32_t is_dram:2;
    uint32_t is_deleted:2;
  };                      // 4 bytes
  
  HeadSpinLock mtx;       // 1 byte
  uint8_t switch_counter; // 1 byte
  int16_t last_index;     // 2 bytes

  page* page_pmem;        // for dram cache, this points to pmem page

  friend class page;
  friend class btree;

public:
  header(bool _is_dram) {    
    immutable = 0;
    is_dram = _is_dram;
    if (is_dram) {
      leftmost_ptr_dram = nullptr;
      sibling_ptr_dram  = nullptr;
    } else {
      leftmost_ptr_pmem = nullptr;
      sibling_ptr_pmem = nullptr;
    }    
    switch_counter = 0;
    last_index = -1;
    is_deleted = false;
  }

  inline page* GetLeftMostPtr() {
    if (is_dram) return leftmost_ptr_dram;
    else return leftmost_ptr_pmem;
  }

  inline void SetLeftMostPtr(page* leftmost) {
    if (is_dram) leftmost_ptr_dram = leftmost;
    else leftmost_ptr_pmem = leftmost;
  }

  inline page* GetSiblingPtr() {
    if (is_dram) return sibling_ptr_dram;
    else return sibling_ptr_pmem;
  }

  inline void SetSiblingPtr(page* sibling) {
    if (is_dram) sibling_ptr_dram = sibling;
    else sibling_ptr_pmem = sibling;
  }



  // TODO: fix the dram lock. Need to new mutex for each page after reboot.
  ~header() {}
};

static_assert(sizeof(header) == 32, "page header is not 32 byte");

class entry {
public:
  entry_key_t key; // 8 bytes
public:  
  union {
    pptr<char>  ptr_pmem; // 8 bytes
    char* ptr_dram;
  };

public:
  entry(bool is_dram) {
    key = LONG_MAX;
    if (is_dram) {
      ptr_dram = nullptr;
    } else {
      ptr_pmem = nullptr;
    }
  }

  inline char* GetPtr(bool is_dram) {
    if (is_dram) return ptr_dram;
    else return ptr_pmem;
  }

  inline void SetPtr(bool is_dram, char* ptr) {
    if (is_dram) ptr_dram = ptr;
    else ptr_pmem = ptr;
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

  page(uint32_t level, bool is_dram): 
    hdr(is_dram),
    records { {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, 
              {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, 
              {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram} } {
    hdr.level = level;
    if (is_dram) {
      records[0].ptr_dram = nullptr;
    } else {
      records[0].ptr_pmem = nullptr;
    }
    
  }

  // this is called when tree grows
  page(page *left, entry_key_t key, page *right, uint32_t level, bool is_dram):
    hdr(is_dram),
    records { {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, 
              {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, 
              {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram}, {is_dram} } {
    
    if (is_dram) {
      hdr.leftmost_ptr_dram = left;
      records[0].ptr_dram = (char *)right;
      records[1].ptr_dram = nullptr;
    } else {
      hdr.leftmost_ptr_pmem = left;
      records[0].ptr_pmem = (char *)right;
      records[1].ptr_pmem = nullptr;
    }
    hdr.level = level;
    records[0].key = key;
    
    hdr.last_index = 0;

    clflush((char *)this, sizeof(page));
  }

  inline int count() {
    uint8_t previous_switch_counter;
    int count = 0;
    do {
      previous_switch_counter = hdr.switch_counter;
      count = hdr.last_index + 1;

      while (count >= 0 && records[count].GetPtr(hdr.is_dram) != nullptr) {
        if (IS_FORWARD(previous_switch_counter))
          ++count;
        else
          --count;
      }

      if (count < 0) {
        count = 0;
        while (records[count].GetPtr(hdr.is_dram) != nullptr) {
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
    for (i = 0; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
      if (!shift && records[i].key == key) {
        page* lptr = hdr.GetLeftMostPtr();
        char* rptr = records[i - 1].GetPtr(hdr.is_dram);
        records[i].SetPtr(hdr.is_dram, (i == 0) ? (char *)lptr : rptr);
        shift = true;
      }

      if (shift) {
        records[i].key = records[i + 1].key;
        records[i].SetPtr(hdr.is_dram, records[i + 1].GetPtr(hdr.is_dram));

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

  /**
   * @brief Create a new page using existing page and full bufnode.
   *        Out-of-place merge.
   * !note: make sure the bufnode has been sorted before using.
   * @param p 
   * @param bufnode 
   * @return page* 
   */
  inline page* merge_page_buffer(int num_entreis, buflog::SortedBufNode* bufnode) {
    // step 1. Create new leafnode page
    page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
    page* new_leafnode = new (buf) page(hdr.level, false);

    BUFLOG_INFO("Create new leafnode 0x%lx for out-of-place merge", new_leafnode);

    // step 2. Merge the kv in old_leafnode and bufnode into new_leafnode
    auto iter = bufnode->sBegin();
    int num = 0;
    while (iter.Valid()) {
      new_leafnode->insert_key(iter->key, iter->val, &num, false);
      BUFLOG_INFO("COW merge %ld to new_leafnode: 0x%lx", iter->key, new_leafnode);
      ++iter;
    }
    for (int i = 0; i < num_entreis; i++) {
      new_leafnode->insert_key(records[i].key, records[i].GetPtr(false), &num, false);
      BUFLOG_INFO("COW merge %ld to new_leafnode: 0x%lx", records[i].key, new_leafnode);
    }
  
    // step 3. Link the new_leaf to old_leaf's sibling
    new_leafnode->hdr.SetSiblingPtr(hdr.GetSiblingPtr());

    // step 4. Flush the entire content
    clflush((char*)new_leafnode, sizeof(page));

    return new_leafnode;
  }

  inline void update_key(entry_key_t key, char* ptr) {
    int num = count();
    if (key < records[0].key) {
      hdr.SetLeftMostPtr((page*)ptr);
      return;
    }
    for (int i = 0; i < num; ++i) {
      if (key == records[i].key) {
        records[i].SetPtr(hdr.is_dram, ptr);
        if (!hdr.is_dram) clflush((char*)&records[i].ptr_pmem, 8);
        return;
      }
    }
    BUFLOG_ERROR("Fail to update_key for key %d at node 0x%lx. %s", key, this, ToString().c_str());
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
      new_entry->SetPtr(hdr.is_dram, (char *)ptr);

      array_end->SetPtr(hdr.is_dram, nullptr);

      if (flush) {
        clflush((char *)this, CACHE_LINE_SIZE);
      }
    } else {
      int i = *num_entries - 1, inserted = 0, to_flush_cnt = 0;
      records[*num_entries + 1].SetPtr(hdr.is_dram, records[*num_entries].GetPtr(hdr.is_dram));
      if (flush && !hdr.is_dram) {
        if ((uint64_t) & (records[*num_entries + 1].ptr_pmem) % CACHE_LINE_SIZE == 0)
          clflush((char *)&(records[*num_entries + 1].ptr_pmem), sizeof(char *));
      }

      // FAST
      for (i = *num_entries - 1; i >= 0; i--) {
        if (key < records[i].key) {
          records[i + 1].SetPtr(hdr.is_dram, records[i].GetPtr(hdr.is_dram));
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
          records[i + 1].SetPtr(hdr.is_dram, records[i].GetPtr(hdr.is_dram));
          records[i + 1].key = key;
          records[i + 1].SetPtr(hdr.is_dram, ptr);

          if (flush)
            clflush((char *)&records[i + 1], sizeof(entry));
          inserted = 1;
          break;
        }
      }
      if (inserted == 0) {
        page* lptr = hdr.GetLeftMostPtr();
        records[0].SetPtr(hdr.is_dram, (char *)lptr);
        records[0].key = key;
        records[0].SetPtr(hdr.is_dram, ptr);
        if (flush)
          clflush((char *)&records[0], sizeof(entry));
      }
    }

    if (update_last_index) {
      hdr.last_index = *num_entries;
    }
    ++(*num_entries);
  }


  page *update_sibling(entry_key_t key, char* new_ptr, bool with_lock) {
    if (with_lock) {
      hdr.mtx.lock(); // Lock the write lock
    }
    if (hdr.is_deleted) {
      if (with_lock) {
        hdr.mtx.unlock();
      }

      return nullptr;
    }

    page* sibl = hdr.GetSiblingPtr();
    // If this node has a sibling node,
    if (sibl != nullptr && sibl != (page*)new_ptr) {
      // Compare this key with the first key of the sibling
      if (key > sibl->records[0].key) {
        if (with_lock) {
          hdr.mtx.unlock(); // Unlock the write lock
        }
        return sibl->update_sibling(key, new_ptr, with_lock);
      }
    }
    assert(this != (page*)new_ptr);
    BUFLOG_INFO("link leafnode 0x%lx to leafnode 0x%lx", this, new_ptr);
    hdr.SetSiblingPtr((page*) new_ptr);
    clflush((char*)&hdr, sizeof(hdr));

    if (with_lock) {
      hdr.mtx.unlock(); // Unlock the write lock
    }
    return this;
  }

  page *update(entry_key_t key, char* new_ptr /* new leafnode ptr */, bool with_lock) {
    // only happens between leafnode and level 1 inner node
    if (with_lock) {
      hdr.mtx.lock(); // Lock the write lock
    }
    if (hdr.is_deleted) {
      if (with_lock) {
        hdr.mtx.unlock();
      }

      return nullptr;
    }
    
    // If this node has a sibling node,
    if (hdr.GetSiblingPtr() != nullptr) {
      // Compare this key with the first key of the sibling
      if (key > hdr.GetSiblingPtr()->records[0].key) {
        if (with_lock) {
          hdr.mtx.unlock(); // Unlock the write lock
        }
        return hdr.GetSiblingPtr()->update(key, new_ptr, with_lock);
      }
    }

    BUFLOG_INFO("Update node 0x%lx key %d 's ptr to 0x%lx", this, key, new_ptr);
    update_key(key, new_ptr);
    
#ifdef CONFIG_DRAM_INNER
    while(!hdr.page_pmem->hdr.mtx.try_lock()) FASTFAIR_CPU_RELAX();
    hdr.page_pmem->update_key(key, new_ptr);
    hdr.page_pmem->hdr.mtx.unlock();        
#endif

    if (with_lock) {
      hdr.mtx.unlock(); // Unlock the write lock
    }
    return this;
  }

  // Insert a new key - FAST and FAIR
  page *store(page* parent, int pi, btree *bt, char *left, entry_key_t key, char *right, bool flush,
              bool with_lock, page *invalid_sibling = nullptr, buflog::SortedBufNode* bufnode = nullptr) {
    if (with_lock) {
      hdr.mtx.lock(); // Lock the write lock
    }
    if (hdr.is_deleted) {
      if (with_lock) {
        hdr.mtx.unlock();
      }

      return nullptr;
    }

    // If this node has a sibling node,
    if (hdr.GetSiblingPtr() != nullptr && (hdr.GetSiblingPtr() != invalid_sibling)) {
      // Compare this key with the first key of the sibling
      if (key > hdr.GetSiblingPtr()->records[0].key || hdr.immutable == 1) {
        if (with_lock) {
          hdr.mtx.unlock(); // Unlock the write lock
        }
        page* sibl = hdr.GetSiblingPtr();  
        assert(sibl != this);      
        BUFLOG_INFO("Leafnode 0x%lx store in siblinng 0x%lx. key %ld, sbling: %ld, ptr 0x%lx, hdr.immutable: %d", this, sibl, key, hdr.GetSiblingPtr()->records[0].key, hdr.GetSiblingPtr()->records[0].GetPtr(hdr.is_dram), hdr.immutable);
        return sibl->store(nullptr, pi, bt, nullptr, key, right, true, with_lock,
                                      invalid_sibling, nullptr);
      }
    }

    int num_entries = count();
    int bufnode_entries = 0;
    if (bufnode) {
      bufnode_entries = bufnode->ValidCount();
    }
    // FAST
    if (num_entries + bufnode_entries < cardinality - 1) {
      // !buflog: merge bufnode to leafnode
      if (bufnode != nullptr) { // only the leafnode has bufnode
          #ifdef CONFIG_OUT_OF_PLACE_MERGE
          // {{{ ------------------ out-of-place merge ---------------------
          // step 1. merge a new leafnode 
          bufnode->Sort();
          page* new_leafnode = merge_page_buffer(num_entries, bufnode);
          // step 2. change this leafnode to immutable 
          hdr.immutable = 1;
          clflush((char*)this, sizeof(header));
          // step 3. change this immutable page's sibling to new_leafnode
          hdr.SetSiblingPtr(new_leafnode);
          clflush((char*)this, sizeof(header));
          // step 4. change parent pointer to new_leafnode
          if (with_lock) {
            hdr.mtx.unlock();
          }
          bt->btree_update_internal(bufnode->parentkey_, (char*)new_leafnode, 1);
          // step 5. link previous leafnode sibling to new_leafnode
          bt->btree_update_prev_sibling(bufnode->parentkey_ - 1, (char*)new_leafnode);          
          // step 6. move the bufnode to new_leafnode and erase old bufnode mapping in bufnode_table_
          bufnode->Reset();
          bufnode->Put(key, right);
          btree::bufnode_table_.Put(size_t(new_leafnode), (char*)bufnode);
          BUFLOG_INFO("Create leafnode 0x%lx bufnode 0x%lx mapping. %s", new_leafnode, bufnode, bufnode->ToStringValid().c_str());
          btree::bufnode_table_.Delete((size_t(this)));
          BUFLOG_INFO("Delete leafnode 0x%lx bufnode 0x%lx mapping", this, bufnode);
          //     ------------------ out-of-place merge --------------------- }}}
          #else
          // {{{ ------------------ in-place merge ---------------------
          bufnode->Sort();
          auto iter = bufnode->sBegin();  
          while (iter.Valid()) {
            auto& kv = *iter;
            insert_key(kv.key, kv.val, &num_entries, true);
            iter++;
          }
          bufnode->Reset();
          bufnode->Put(key, right);
          //     ------------------ in-place merge --------------------- }}}        
          #endif
      } 
      else {
        // 1. the leafnode does not have bufnode, normal insertion.
        // 2. inner-node
#ifdef CONFIG_DRAM_INNER
        if (hdr.GetLeftMostPtr() != nullptr) {
          // inner node
          page* rp = reinterpret_cast<page*>(right);
          BUFLOG_INFO("Insert key %ld to L%d inner node 0x%lx (dram: %d), right 0x%lx (dram: %d).",
            key, hdr.level, this, hdr.is_dram, rp, rp->hdr.is_dram);
          int num_entries_back = num_entries;  
          insert_key(key, right, &num_entries, false);
          if (rp->hdr.is_dram) {
            hdr.page_pmem->insert_key(key, (char*)rp->hdr.page_pmem, &num_entries_back);
          } else {
            hdr.page_pmem->insert_key(key, right, &num_entries_back);
          }
        } else {
          // leafnode
          insert_key(key, right, &num_entries, flush);
        }
#else
        insert_key(key, right, &num_entries, flush);
#endif        
      }

      if (with_lock) {
        hdr.mtx.unlock(); // Unlock the write lock
      }

      return this;
    } 
    else { // FAIR
      // overflow
      // step 1. create a new node     
      page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
      page* root = new (buf) page(hdr.level, false);
      page* sibling = root;
      page* sibling_dram = nullptr;

      int m = (int)ceil(num_entries / 2);
      entry_key_t split_key = records[m].key;

      // step 2. migrate half of keys into the sibling
      int sibling_cnt = 0;
      if (hdr.GetLeftMostPtr() == nullptr) { // leaf node
        BUFLOG_INFO("Begin Split at leafnode 0x%lx (dram: %d). Split key: %ld", this, hdr.is_dram, split_key);
        BUFLOG_INFO("Create new leafnode 0x%lx (dram: %d) for spliting", sibling, sibling->hdr.is_dram);
        // !buflog: migrate both bufnode and leafnode entries to sibling
        if (bufnode_entries == 0) { // no bufnode entries
          for (int i = m; i < num_entries; ++i) {
            BUFLOG_INFO("Leafnode 0x%lx accepts records[%d]: key %ld, ptr 0x%lx", 
            sibling, i, records[i].key, records[i].GetPtr(hdr.is_dram));
            sibling->insert_key(records[i].key, records[i].GetPtr(hdr.is_dram), &sibling_cnt,
                                false);
          }
          if (bufnode != nullptr) {
            bufnode->highkey_ = split_key;            
          }
          BUFLOG_INFO("No entries in bufnode: 0x%lx. %s", bufnode, bufnode != nullptr ? bufnode->ToStringValid().c_str() : "");
        }
        else { // both bufnode and leafnode entries
          int buf_i = 0;
          bufnode->Sort();
          auto buf_iter = bufnode->sBegin();
          // buf pointer to the key that is >= split_key
          while (buf_i < bufnode_entries && buf_iter->key < split_key) {
            buf_i++;
            buf_iter++;
          }
    
          for (int i = m; i < num_entries; ++i) {
            sibling->insert_key(records[i].key, records[i].GetPtr(hdr.is_dram), &sibling_cnt,
                                false);
            BUFLOG_INFO("Split merge leafnode %ld to sibling: 0x%lx", records[i].key, sibling);
          }
          
          while (buf_iter.Valid()) {
            sibling->insert_key(buf_iter->key, 
                                buf_iter->val, 
                                &sibling_cnt,
                                false);
            BUFLOG_INFO("Split merge bufnode %ld to sibling: 0x%lx", buf_iter->key, sibling);
            buf_iter++;
          }
          BUFLOG_INFO("Split. Bufnode 0x%lx valid before: %s", bufnode, bufnode->ToStringValid().c_str());
          bufnode->highkey_ = split_key;
          bufnode->MaskLastN(bufnode_entries - buf_i);          
          BUFLOG_INFO("Split. Bufnode 0x%lx valid remain: %s", bufnode, bufnode->ToStringValid().c_str());
          BUFLOG_INFO("Split. Bufnode entry %ld, leafnode entry %d, buf_i: %d, split m: %ld", bufnode_entries, num_entries, buf_i, m);          
        }
        
        // !buflog: create a bufnode for leaf page
        buflog::SortedBufNode* new_bufnode = new buflog::SortedBufNode();
        if (hdr.GetSiblingPtr() != nullptr) {
          new_bufnode->highkey_ = hdr.GetSiblingPtr()->records[0].key;          
        }
        new_bufnode->parentkey_ = split_key;
        btree::bufnode_table_.Put((size_t)sibling, (char*)new_bufnode);
        BUFLOG_INFO("End Split. Create bufnode 0x%lx. mapping to leafnode 0x%lx. %s", new_bufnode, sibling, new_bufnode->ToStringValid().c_str());
      } 
      else { // internal node
        BUFLOG_INFO("Begin Split at inner node 0x%lx (L%d, dram: %d). Split key: %ld, right: 0x%lx", 
        this, hdr.level, hdr.is_dram, split_key, records[m].ptr_dram);
#ifdef CONFIG_DRAM_INNER        
        sibling_dram = new page(hdr.level, true);
        sibling_dram->hdr.page_pmem = sibling;
        BUFLOG_INFO("Create dram cache node 0x%lx", sibling_dram);
        int sibling_cnt_pmem = 0;
        for (int i = m + 1; i < num_entries; ++i) {
          sibling_dram->insert_key(records[i].key, records[i].GetPtr(hdr.is_dram), &sibling_cnt, false);
          sibling->insert_key(hdr.page_pmem->records[i].key, hdr.page_pmem->records[i].GetPtr(false), &sibling_cnt_pmem);
        }
        char* rptr = records[m].GetPtr(true);
        assert(rptr);
        sibling_dram->hdr.SetLeftMostPtr((page *)rptr);
        BUFLOG_INFO("Set sibling dram 0x%lx (dram: %d) lefmost. hdr.leftmost_dram: 0x%lx, rptr: 0x%lx",
          sibling_dram, sibling_dram->hdr.is_dram, sibling_dram->hdr.leftmost_ptr_dram, rptr);
        char* rptr_pmem = hdr.page_pmem->records[m].GetPtr(false);
        sibling->hdr.SetLeftMostPtr((page*)rptr_pmem);
        BUFLOG_INFO("Set sibling pmem 0x%lx (dram: %d) lefmost. hdr.leftmost_pmem: 0x%lx, rptr_pmem: 0x%lx",
          sibling, sibling->hdr.is_dram, sibling->hdr.GetLeftMostPtr(), rptr_pmem);
#else
        for (int i = m + 1; i < num_entries; ++i) {
          sibling->insert_key(records[i].key, records[i].GetPtr(hdr.is_dram), &sibling_cnt,
                              false);
        }
        char* rptr = records[m].GetPtr(hdr.is_dram);
        sibling->hdr.SetLeftMostPtr((page *)rptr);
#endif
      }

      // Step 3. connect sibling to new splitted node
#ifdef CONFIG_DRAM_INNER
      if (hdr.GetLeftMostPtr() != nullptr) {
        // this is inner node        
        sibling_dram->hdr.SetSiblingPtr(hdr.GetSiblingPtr()); // link dram sibling
        sibling->hdr.SetSiblingPtr(hdr.page_pmem->hdr.GetSiblingPtr()); // link pmem sibling
        clflush((char *)sibling, sizeof(page));

        hdr.SetSiblingPtr(sibling_dram); // link dram
        hdr.page_pmem->hdr.SetSiblingPtr(sibling); // link pmem
        clflush((char *)&hdr.page_pmem->hdr, sizeof(hdr));

        BUFLOG_INFO("Set sibling dram 0x%lx (dram: %d) sibling. hdr.sibling: 0x%lx, sibling: 0x%lx",
          sibling_dram, sibling_dram->hdr.is_dram, sibling_dram->hdr.sibling_ptr_dram, hdr.sibling_ptr_dram);

        BUFLOG_INFO("Set sibling pmem 0x%lx (dram: %d) sibling. hdr.sibling: 0x%lx, sibling: 0x%lx",
          sibling, sibling->hdr.is_dram, sibling->hdr.GetSiblingPtr(), hdr.page_pmem->hdr.GetSiblingPtr());              
      } else {
        // leafnode        
        sibling->hdr.SetSiblingPtr(hdr.GetSiblingPtr());
        clflush((char *)sibling, sizeof(page));
        hdr.SetSiblingPtr(sibling);
        clflush((char *)&hdr, sizeof(hdr));
        BUFLOG_INFO("Set leafnode sibling pmem 0x%lx (dram: %d) sibling. hdr.sibling: 0x%lx",
          sibling, sibling->hdr.is_dram, sibling->hdr.GetSiblingPtr());
      }
#else
      sibling->hdr.SetSiblingPtr(hdr.GetSiblingPtr());
      clflush((char *)sibling, sizeof(page));
      hdr.SetSiblingPtr(sibling);
      clflush((char *)&hdr, sizeof(hdr));
#endif

      // set to nullptr
#ifdef CONFIG_DRAM_INNER
    if (hdr.GetLeftMostPtr() != nullptr) {
      // this is inner node
      BUFLOG_INFO("Set inner node m nullptr.");
      if (IS_FORWARD(hdr.switch_counter)) {
        hdr.switch_counter += 2;
        hdr.page_pmem->hdr.switch_counter += 2;
      }
      else {
        ++hdr.switch_counter;
        ++hdr.page_pmem->hdr.switch_counter;
      }
      records[m].SetPtr(hdr.is_dram, nullptr);
      hdr.page_pmem->records[m].SetPtr(false, nullptr);
      clflush((char *)&hdr.page_pmem->records[m], sizeof(entry));
      hdr.last_index = m - 1;
      hdr.page_pmem->hdr.last_index = m - 1;
      clflush((char *)&hdr.page_pmem->hdr.last_index, sizeof(int16_t));
    } 
    else {
      // lefnode
      BUFLOG_INFO("Set leafnode 0x%lx key %ld to nullptr.", this, records[m].key);
      if (IS_FORWARD(hdr.switch_counter))
        hdr.switch_counter += 2;
      else
        ++hdr.switch_counter;
      records[m].SetPtr(hdr.is_dram, nullptr);
      clflush((char *)&records[m], sizeof(entry));
      hdr.last_index = m - 1;
      clflush((char *)&(hdr.last_index), sizeof(int16_t));
    }
#else
      if (IS_FORWARD(hdr.switch_counter))
        hdr.switch_counter += 2;
      else
        ++hdr.switch_counter;
      records[m].SetPtr(hdr.is_dram, nullptr);
      clflush((char *)&records[m], sizeof(entry));

      hdr.last_index = m - 1;
      clflush((char *)&(hdr.last_index), sizeof(int16_t));
#endif

      num_entries = hdr.last_index + 1;

      // insert the key
      page *ret = nullptr;
#ifdef CONFIG_DRAM_INNER
      if (hdr.GetLeftMostPtr() != nullptr) {
        // this is inner node
        page* rp = reinterpret_cast<page*>(right); // right is pmem pointer is this inner node is at level 1
        BUFLOG_INFO("Insert key %ld to inner node L%d 0x%lx (dram: %d). right 0x%lx (dram: %d)",
          key, hdr.level, this, hdr.is_dram, rp, rp->hdr.is_dram);
        int num_entries_back = num_entries;
        if (key < split_key) {          
          insert_key(key, right, &num_entries); // insert dram or pmem 
          if (rp->hdr.is_dram) {
            BUFLOG_INFO("Left: L%d inner node insert pmem part of right", hdr.level);
            hdr.page_pmem->insert_key(key, (char*)rp->hdr.page_pmem, &num_entries_back); // insert pmem right
          } else {
            BUFLOG_INFO("Left: L%d inner node insert right", hdr.level);
            hdr.page_pmem->insert_key(key, right, &num_entries_back); // insert pmem right
          }
          ret = this;          
        } else {
          sibling_dram->insert_key(key, right, &num_entries, false);
          if (rp->hdr.is_dram) {
            BUFLOG_INFO("Right: L%d inner node insert pmem part of right", hdr.level);
            sibling->insert_key(key, (char*)rp->hdr.page_pmem, &num_entries_back);
          } else {
            BUFLOG_INFO("Right: L%d inner node insert right", hdr.level);
            sibling->insert_key(key, right, &num_entries_back);
          }
          ret = sibling_dram;
        }
      } else {
        // lefnode
        BUFLOG_INFO("Insert key %ld to leafnode L%d 0x%lx (dram: %d). right %ld",
          key, hdr.level, this, hdr.is_dram, right);
        if (key < split_key) {
          insert_key(key, right, &num_entries);
          ret = this;
        } else {
          sibling->insert_key(key, right, &sibling_cnt);
          ret = sibling;
        }
      }
#else      
      if (key < split_key) {
        insert_key(key, right, &num_entries);
        ret = this;
      } else {
        sibling->insert_key(key, right, &sibling_cnt);
        ret = sibling;
      }
#endif   

      // Set a new root or insert the split key to the parent
      if (bt->root == this) { // only one node can update the root ptr  
#ifdef CONFIG_DRAM_INNER
        page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
        page* root_pmem_old = hdr.is_dram ? hdr.page_pmem : this;        
        page* root_pmem = new (buf) page(root_pmem_old, split_key, sibling, hdr.level + 1, false);        
        page* root_dram = new page((page*)this, split_key, sibling_dram ? sibling_dram : sibling, hdr.level + 1, true);
        root_dram->hdr.page_pmem = root_pmem;
        bt->setNewRoot(root_dram); // passing new dram root page 
#else
        page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));
        page* root_pmem = new (buf) page((page *)this, split_key, sibling, hdr.level + 1, false);
        page *new_root = root_pmem;        
        bt->setNewRoot(new_root);
#endif
        if (with_lock) {
          hdr.mtx.unlock(); // Unlock the write lock
        }
      } else {
        if (with_lock) {
          hdr.mtx.unlock(); // Unlock the write lock
        }

#ifdef CONFIG_DRAM_INNER
        page* sib_right = hdr.GetLeftMostPtr() != nullptr ? sibling_dram : sibling;
        bt->btree_insert_internal(nullptr, split_key, (char *)sib_right,
                                  hdr.level + 1);
#else
        bt->btree_insert_internal(nullptr, split_key, (char *)sibling,
                                  hdr.level + 1);
#endif                                  
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
              if ((tmp_ptr = current->records[0].GetPtr(current->hdr.is_dram)) != nullptr) {
                if (tmp_key == current->records[0].key) {
                  if (tmp_ptr) {
                    buf[off++] = (unsigned long)tmp_ptr;
                  }
                }
              }
            } else
              return;
          }

          for (i = 1; current->records[i].GetPtr(current->hdr.is_dram) != nullptr; ++i) {
            if ((tmp_key = current->records[i].key) > min) {
              if (tmp_key < max) {
                if ((tmp_ptr = current->records[i].GetPtr(current->hdr.is_dram)) !=
                    current->records[i - 1].GetPtr(current->hdr.is_dram)) {
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
                if ((tmp_ptr = current->records[i].GetPtr(current->hdr.is_dram)) !=
                    current->records[i - 1].GetPtr(current->hdr.is_dram)) {
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
              if ((tmp_ptr = current->records[0].GetPtr(current->hdr.is_dram)) != nullptr) {
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

      current = current->hdr.GetSiblingPtr();
    }
  }

  /**
   * @brief Find the `first key` in page that is <= input `key`
   * 
   * @param key 
   * @param bt 
   * @param pi : The position of the `first key` in this page
   *        -1 : means we take the leftmost_ptr in this page
   *        -2 : means we take the sibling in this page
   * @return char* : The pointer pointed by the `first key`
   */
  char *linear_search(entry_key_t key, int& pi) {
    int i = 1;
    uint8_t previous_switch_counter;
    char *ret = nullptr;
    char *t;
    entry_key_t k;

    if (hdr.GetLeftMostPtr() == nullptr) { // Search a leaf node
      do {
        previous_switch_counter = hdr.switch_counter;
        ret = nullptr;

        // search from left ro right
        if (IS_FORWARD(previous_switch_counter)) {
          if ((k = records[0].key) == key) {
            if ((t = records[0].GetPtr(hdr.is_dram)) != nullptr) {
              if (k == records[0].key) {
                ret = t;
                continue;
              }
            }
          }

          for (i = 1; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
            if ((k = records[i].key) == key) {
              if (records[i - 1].GetPtr(hdr.is_dram) != (t = records[i].GetPtr(hdr.is_dram))) {
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
              if (records[i - 1].GetPtr(hdr.is_dram) != (t = records[i].GetPtr(hdr.is_dram)) && t) {
                if (k == records[i].key) {
                  ret = t;
                  break;
                }
              }
            }
          }

          if (!ret) {
            if ((k = records[0].key) == key) {
              if (nullptr != (t = records[0].GetPtr(hdr.is_dram)) && t) {
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

      page* sptr = hdr.GetSiblingPtr();
      if ((t = (char *)sptr) && key >= ((page *)t)->records[0].key) {
        pi = -2;
        return t;
      }

      DEBUG("Cannot find key smaller than %ld in leafnode 0x%lx. pi %d", key, this, pi);
      return nullptr;
    } 
    else { // internal node
      do {
        previous_switch_counter = hdr.switch_counter;
        ret = nullptr;

        if (IS_FORWARD(previous_switch_counter)) {
          if (key < (k = records[0].key)) {
            page* lptr = hdr.GetLeftMostPtr();
            if ((t = (char *)lptr) != records[0].GetPtr(hdr.is_dram)) {
              ret = t;
              pi = -1; // means we should take the leftmost_ptr in this page
              continue;
            }
          }

          for (i = 1; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
            if (key < (k = records[i].key)) {
              if ((t = records[i - 1].GetPtr(hdr.is_dram)) != records[i].GetPtr(hdr.is_dram)) {
                ret = t;
                pi = i - 1;
                break;
              }
            }
          }

          if (!ret) {
            ret = records[i - 1].GetPtr(hdr.is_dram);
            pi = i - 1;
            continue;
          }
        } else { // search from right to left
          for (i = count() - 1; i >= 0; --i) {
            if (key >= (k = records[i].key)) {
              if (i == 0) {
                page* lptr = hdr.GetLeftMostPtr();
                if ((char *)lptr != (t = records[i].GetPtr(hdr.is_dram))) {
                  ret = t;
                  pi = i;
                  break;
                }
              } else {
                if (records[i - 1].GetPtr(hdr.is_dram) != (t = records[i].GetPtr(hdr.is_dram))) {
                  ret = t;
                  pi = i;
                  break;
                }
              }
            }
          }
        }
      } while (hdr.switch_counter != previous_switch_counter);

      page* sptr = hdr.GetSiblingPtr();
      if ((t = (char *)sptr) != nullptr) {
        if (key >= ((page *)t)->records[0].key) {
          pi = -2;
          return t;
        }
      }

      if (ret) {
        return ret;
      } else {
        page* lptr = hdr.GetLeftMostPtr();
        pi = -1;
        return (char *)lptr;
      }
    }

    DEBUG("Cannot find key smaller than %ld in inner node 0x%lx. pi: %d", key, this, pi);
    return nullptr;
  }

  // print a node
  void print() {
    if (hdr.GetLeftMostPtr() == nullptr)
      printf("[%d] leaf 0x%lx. immutable: %s\n", this->hdr.level, this, hdr.immutable == 1 ? "true" : "false");
    else
      printf("[%d] internal 0x%lx \n", this->hdr.level, this);
    printf("last_index: %d\n", hdr.last_index);
    printf("switch_counter: %d\n", hdr.switch_counter);
    printf("search direction: ");
    if (IS_FORWARD(hdr.switch_counter))
      printf("->\n");
    else
      printf("<-\n");

    if (hdr.GetLeftMostPtr() != nullptr) {
      page* lptr = hdr.GetLeftMostPtr();
      printf("0x%lx ", lptr);
    }

    for (int i = 0; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
      char* rptr = records[i].GetPtr(hdr.is_dram);
      printf("%ld,0x%lx ", records[i].key, rptr);
    }

    page* sptr = hdr.GetSiblingPtr();
    printf("0x%lx ", sptr);

    printf("\n");
  }

  std::string ToString(void) {
    char buf[2048] = {0};
    for (int i = 0; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
      char* rptr = records[i].GetPtr(hdr.is_dram);
      sprintf(buf + strlen(buf),"%ld,0x%lx ", records[i].key, rptr);
    }
    return buf;
  }

  void printAll() {
    if (hdr.GetLeftMostPtr() == nullptr) {
      printf("printing leaf node: ");
      print();
    } else {
      printf("printing internal node: ");
      print();
      hdr.GetLeftMostPtr()->printAll();
      for (int i = 0; records[i].GetPtr(hdr.is_dram) != nullptr; ++i) {
        char* rptr = records[i].GetPtr(hdr.is_dram);
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
  // create pmem root page
  page* buf = reinterpret_cast<page*>(RP_malloc(sizeof(page)));  
  page* root = new (buf) page(0, false);
  btree_root->root = root;
  btree_root->root_pmem_ = root;

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
  btree_root->root = btree_root->root_pmem_;
  btree::datalog_.Open(btree_root->datalog_addr_);
  return btree_root;
}

void btree::setNewRoot(page* new_root) {
#ifdef CONFIG_DRAM_INNER
  // new_root is a dram node
  BUFLOG_INFO("Set new root page 0x%lx (dram: %d)", new_root, new_root->hdr.is_dram);
  this->root_dram_ = new_root;
  this->root_pmem_ = new_root->hdr.page_pmem;
  this->root = new_root;
  clflush((char*)&(this->root_pmem_), sizeof(char*));
  ++height;
#else
  this->root = new_root;
  this->root_pmem_ = new_root;
  clflush((char *)&(this->root_pmem_), sizeof(char *));
  ++height;
#endif
}

// find the first key that is <= input key, and renturn its pointer
char *btree::btree_search(entry_key_t key) {
  page *p = root;

  // search until to level 1, then p will be the pointer to leafnode
  int pi;
  while (p->hdr.level > 1) {    
    p = (page *)p->linear_search(key, pi);
  }
  if (p->hdr.level == 1) {
    p = (page *)p->linear_search(key, pi);
    while (pi == -2) {
      p = (page *)p->linear_search(key, pi);
    }
  }

  if (p == nullptr) {
    BUFLOG_INFO("btree search error.");
  }
  

  #ifdef CONFIG_BUFNODE
  // !buflog: check the bufnode first if we can find one.
  auto iter = btree::bufnode_table_.Find(size_t(p));
  buflog::SortedBufNode* bufnode = nullptr;
  if (iter != nullptr) {
    bufnode = reinterpret_cast<buflog::SortedBufNode*>(iter->second());
    char *ret;
    bool res = bufnode->Get(key, ret);
    if (res == true) {
      return ret;
    }
  }
  #endif

  page *t = nullptr;
  // if == sibling_ptr, continue searching the node on the linked-list
  int i;
  while ((t = (page *)p->linear_search(key, i)) == p->hdr.GetSiblingPtr()) {
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
  while (p->hdr.level >= 1) {
    parent = p;
    p = (page *)p->linear_search(key, pi);
  }
  return parent;
}

// insert the key in the leaf node
void btree::btree_insert(entry_key_t key, char *right) { // need to be string
  page *p = root;

  // find the leaf node. Normally p will never be nullptr
  page* parent = nullptr;
  int pi = -2;
  buflog::SortedBufNode* bufnode = nullptr;
  
  // find the leaf node
  while (p->hdr.level > 1) {    
    parent = p;
    p = (page *)p->linear_search(key, pi);
    assert(p);
  }
  if (p->hdr.level == 1) {
    // BUFLOG_INFO("Insert key %ld search level %d inner node 0x%lx (dram: %d)", key, p->hdr.level, p, p->hdr.is_dram);
    parent = p;
    p = (page *)p->linear_search(key, pi);
    assert(p);
    while (pi == -2) {
      BUFLOG_INFO("Insert key %ld search sibling. level %d inner node 0x%lx (dram: %d)", key, p->hdr.level, p, p->hdr.is_dram);
      parent == nullptr;
      p = (page *)p->linear_search(key, pi);
      assert(p);
    }
  }

  if (p == nullptr) {
    BUFLOG_INFO("btree insert error");
  }

  #ifdef CONFIG_BUFNODE
  // insert to bufnode without touching leafnode
  auto iter = btree::bufnode_table_.Find(size_t(p));
  int64_t highkey; 
  if (iter != nullptr) {
    bufnode = reinterpret_cast<buflog::SortedBufNode*>(iter->second());
    bufnode->Lock();
    highkey = bufnode->highkey_;
    if (highkey > 0 && key >= highkey) {
      bufnode->Unlock();
      // split just happened, do not use bufnode, insert directly so we cen go to the correct leafnode
      BUFLOG_INFO("key %ld is larger than bufnode 0x%lx highkey: %ld. %s", key, bufnode, highkey, bufnode->ToStringValid().c_str());
      bufnode = nullptr;
    }
    else {
      bool res = bufnode->Put(key, right);
      if (res) {
        // successfully insert to bufnode

        #ifdef CONFIG_APPEND_TO_LOG_TEST
        // !buflog: append to log
        BUFLOG_INFO("append to log tail: %ld", bufnode->next_);
        bufnode->next_ = datalog_.Append(buflog::kDataLogNodeValid, key, (size_t)right, bufnode->next_, true);
        #endif

        bufnode->Unlock();
        return;
      }
    }
  }
  #endif

  if (!p->store(parent, pi, this, nullptr, key, right, true, true, nullptr, bufnode)) { // store
    #ifdef CONFIG_BUFNODE
    if (bufnode) {
      bufnode->Unlock();
    }
    #endif
    printf("Fail store\n");
    btree_insert(key, right);
  } else {
    #ifdef CONFIG_BUFNODE
    if (bufnode) {
      bufnode->Unlock();
    }
    #endif
  }
  
}

// update the key pointer in the node at the given level
void btree::btree_update_internal(entry_key_t key, char *ptr,
                                  uint32_t level) {
  if (level > root->hdr.level)
    return;

  page *p = this->root;

  page* parent = nullptr;
  int pi = -2;
  while (p->hdr.level > level) {
    parent = p;
    p = (page *)p->linear_search(key, pi);
  }
  if (p->hdr.level == 0) {
    BUFLOG_ERROR("not update ptr at node 0x%lx, level %d", p, p->hdr.level);
    return;
  }
  if (!p->update(key, ptr, true)) {
    btree_update_internal(key, ptr, level);
  }
}



// update the key pointer in the node at the given level
void btree::btree_update_prev_sibling(entry_key_t key, char *ptr) {
 
  page *p = this->root;

  page* parent = nullptr;
  int pi = -2;

  // find the leaf node
  while (p->hdr.GetLeftMostPtr() != nullptr) {
    p = (page *)p->linear_search(key, pi);
  }

  if (!p->update_sibling(key, ptr, true)) {
    btree_update_prev_sibling(key, ptr);
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
    p = (page *)p->linear_search(key, pi);
  }

  BUFLOG_INFO("Inner node 0x%lx (L%d) insert key: %d, right: 0x%lx", p, p->hdr.level, key, right);
  if (!p->store(parent, pi, this, nullptr, key, right, true, true)) {
    btree_insert_internal(left, key, right, level);
  }
}

// Function to search keys from "min" to "max"
void btree::btree_search_range(entry_key_t min, entry_key_t max,
                               unsigned long *buf) {
  page *p = root;

  int pi = -2;
  while (p) {
    if (p->hdr.GetLeftMostPtr() != nullptr) {
      // The current page is internal
      p = (page *)p->linear_search(min, pi);
    } else {
      // Found a leaf
      p->linear_search_range(min, max, buf);

      break;
    }
  }
}

void btree::printAll(int level = 0) {
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
      auto iter = btree::bufnode_table_.Find(size_t(sibling));
      if (iter != nullptr) {
        auto bufnode = reinterpret_cast<buflog::SortedBufNode*>(iter->second());
        printf("bufnode 0x%lx, %s\n", bufnode, bufnode->ToString().c_str());
        bufnode->Print();
      }
      sibling = sibling->hdr.GetSiblingPtr();
    }
    
    leftmost = leftmost->hdr.GetSiblingPtr();
  } while (leftmost);

  printf("total number of keys: %d\n", total_keys);
  pthread_mutex_unlock(&print_mtx);
}

#endif
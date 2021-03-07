#include "FAST_FAIR/btree_pmem_buflog.h"
// #include "FAST_FAIR/btree_pmem.h"

#include <iostream>
#include "test_util.h"

// ralloc
#include "ralloc.hpp"
#include "pptr.hpp"

#include <libpmem.h>

#include "gflags/gflags.h"
using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

util::RandomGenerator gen;

// For hash table 
DEFINE_uint32(num, 5000000, "number of input");
DEFINE_uint32(threads, 1, "number of threads");
DEFINE_string(mode, "insert", "insert, recover");
DEFINE_uint32(valuesize, 32, "value size");
DEFINE_bool(print, false, "print tree or not");
DEFINE_int32(key, 52, "");
int main(int argc, char *argv[])
{
  ParseCommandLineFlags(&argc, &argv, true);
  
  struct timespec start, end, tmp;

  util::RandomKeyTrace trace(FLAGS_num);

  btree *bt = nullptr;
  
  if (FLAGS_mode == "insert") {  
    
    #if defined __BTREE_PMEM_H || defined __BTREE_PMEM_BUFLOG_H
    DistroyBtree();
    bt = CreateBtree();
    #else
    bt = new btree();
    #endif

    {
      util::IPMWatcher watcher("fastfair_insert");
      clock_gettime(CLOCK_MONOTONIC, &start);
      auto key_iter = trace.Begin();
      for (int i = 0; i < FLAGS_num; ++i) {
        size_t key = key_iter.Next();
        // Slice  val = gen.Generate(FLAGS_valuesize);
        // #if defined __BTREE_PMEM_BUFLOG_H
        // bt->btree_insert(key, (char*)&val);
        // #elif defined __BTREE_PMEM_H
        // char* buf = (char*)RP_malloc(FLAGS_valuesize);
        // pmem_memcpy_persist(buf, val.data(), val.size());
        // bt->btree_insert(key, buf);
        // #else
        bt->btree_insert(key, (char*)key);
        // #endif
        
      }
      clock_gettime(CLOCK_MONOTONIC, &end);
      long long elapsedTime =
          (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
      std::cout << "Insert " << FLAGS_num
            << " keys. time(uses) : " << elapsedTime / 1000 << endl;
    }
    
    {
      util::IPMWatcher watcher("fastfair_read");
      clock_gettime(CLOCK_MONOTONIC, &start);
      char* res = nullptr;
      int found = 0;
      auto key_iter2 = trace.Begin();
      for (int i = 0; i < FLAGS_num; ++i) {
        size_t key = key_iter2.Next();
        res = bt->btree_search(key);
        if (res != nullptr) {
          found++;
        } else {
          printf("%d th key not found\n", i);
        }
      }
      clock_gettime(CLOCK_MONOTONIC, &end);
      long long elapsedTime =
          (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
      std::cout << "Search " << FLAGS_num
            << " keys. Found " << found << ". time(uses) : " << elapsedTime / 1000 << endl;   
    }

    #if defined __BTREE_PMEM_BUFLOG_H
    if (FLAGS_print) {
      bt->printAll();
      int pi;
      int key = FLAGS_key;
      page* parent = bt->btree_search_internal(key, pi);
      page* leafnode_ptr = nullptr;
      if (pi == -1) {
        leafnode_ptr = parent->hdr.leftmost_ptr;
      } else if (pi == -2) {
        // 
      } else {
        char* tmp = parent->records[pi].ptr;
        leafnode_ptr = (page*)tmp;
      }
      printf("The address of the leafnode is 0x%x if we search key %d. It is the %d-th in the parent node 0x%x,\n", leafnode_ptr, key, pi, parent);
    }
    #endif
    
  } 
  else if (FLAGS_mode == "recover") {
      #if defined __BTREE_PMEM_H || defined __BTREE_PMEM_BUFLOG_H
      bt = RecoverBtree();
      #else
      break;
      #endif
      clock_gettime(CLOCK_MONOTONIC, &start);
      char* res = nullptr;
      int found = 0;
      auto key_iter = trace.Begin();
      for (int i = 0; i < FLAGS_num; ++i) {
        size_t key = key_iter.Next();
        res = bt->btree_search(key);
        if (res != nullptr) {
          found++;
        }
      }
      clock_gettime(CLOCK_MONOTONIC, &end);
      long long elapsedTime =
          (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
      std::cout << "Recover Search " << FLAGS_num
            << " keys. Found " << found << ". time(uses) : " << elapsedTime / 1000 << endl;    
      
      if (FLAGS_print) {
        bt->printAll();
      }
    } 
  return 0;
}

#include "FAST_FAIR/btree_pmem.h"

#include <iostream>
#include "test_util.h"

#include "gflags/gflags.h"
using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;


// For hash table 
DEFINE_uint32(num, 100000, "number of input");
DEFINE_uint32(threads, 1, "number of threads");
DEFINE_string(mode, "insert", "insert, recover");

int main(int argc, char *argv[])
{
  ParseCommandLineFlags(&argc, &argv, true);
  
  struct timespec start, end, tmp;

  util::RandomKeyTrace trace(FLAGS_num);

  // Initializing stats
  clflush_cnt = 0;
  search_time_in_insert = 0;
  clflush_time_in_insert = 0;
  gettime_cnt = 0;


  btree *bt = nullptr;
  if (FLAGS_mode == "insert") {           
    #ifdef __BTREE_PMEM_H
    bt = CreateBtree();
    #else
    bt = new btree();
    #endif
    clock_gettime(CLOCK_MONOTONIC, &start);
    auto key_iter = trace.Begin();
    for (int i = 0; i < FLAGS_num; ++i) {
      size_t key = key_iter.Next();
      bt->btree_insert(key, (char *)key);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long long elapsedTime =
        (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    std::cout << "Insert " << FLAGS_num
          << " keys. time(uses) : " << elapsedTime / 1000 << endl;

    clock_gettime(CLOCK_MONOTONIC, &start);
    char* res = nullptr;
    int found = 0;
    auto key_iter2 = trace.Begin();
    for (int i = 0; i < FLAGS_num; ++i) {
      size_t key = key_iter2.Next();
      res = bt->btree_search(key);
      if (res != nullptr) {
        found++;
      }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsedTime =
        (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    std::cout << "Search " << FLAGS_num
          << " keys. Found " << found << ". time(uses) : " << elapsedTime / 1000 << endl;   
  } 
  else if (FLAGS_mode == "recover") {
    #ifdef __BTREE_PMEM_H
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
  }  
  return 0;
}

db_test(test_ralloc)
db_test(test_fastfair)
db_test(test_skiplist)

# CONFIG_BUFNODE CONFIG_DRAM_INNER CONFIG_OUT_OF_PLACE_MERGE

# ======== for case study =======
db_exe(benchmark_cceh bench_cceh_spoton_buf SPOTON CONFIG_BUFNODE)
db_exe(benchmark_cceh bench_cceh_spoton_buf_log SPOTON CONFIG_BUFNODE CONFIG_LOG)
db_exe(benchmark_cceh bench_cceh_spoton_buf_otp SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE)
db_exe(benchmark_cceh bench_cceh_spoton_buf_otp_log SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_LOG)
db_exe(benchmark_cceh bench_cceh_spoton_buf_otp_di SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_DRAM_INNER)
db_exe(benchmark_cceh bench_cceh_spoton_buf_otp_di_log SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_DRAM_INNER CONFIG_LOG)

db_exe(benchmark_fastfair bench_fastfair PMEM)
db_exe(benchmark_fastfair bench_fastfair_spoton_di SPOTON CONFIG_DRAM_INNER)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf SPOTON CONFIG_BUFNODE)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf_log SPOTON CONFIG_BUFNODE CONFIG_LOG)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf_otp SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf_otp_log SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_LOG)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf_otp_di SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_DRAM_INNER)
db_exe(benchmark_fastfair bench_fastfair_spoton_buf_otp_di_log SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_DRAM_INNER CONFIG_LOG)

db_exe(benchmark_skiplist bench_skiplist PMEM)
db_exe(benchmark_skiplist bench_skiplist_spoton_di SPOTON CONFIG_DRAM_INNER)
db_exe(benchmark_skiplist bench_skiplist_spoton_buf_otp SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE)
db_exe(benchmark_skiplist bench_skiplist_spoton_buf_otp_log SPOTON CONFIG_BUFNODE CONFIG_OUT_OF_PLACE_MERGE CONFIG_LOG)
db_exe(benchmark_skiplist bench_skiplist_spoton_buf_otp_di SPOTON CONFIG_BUFNODE CONFIG_DRAM_INNER CONFIG_OUT_OF_PLACE_MERGE)
db_exe(benchmark_skiplist bench_skiplist_spoton_buf_otp_di_log SPOTON CONFIG_BUFNODE CONFIG_DRAM_INNER CONFIG_OUT_OF_PLACE_MERGE CONFIG_LOG)

# ======= sptree =======
db_exe(benchmark_sptree bench_sptree)

# ======= unit test =======
db_test(test_spoton_log)
db_exe(test_spoton_log_pmem)
db_test(test_spoton_bufnode)

db_test(test_bloomfilter)
db_test(test_sptree_layer)
db_test(test_sptree)
db_exe(sptree_example)
db_test(test_btree_pmem)
db_test(test_sptree_recover)
db_test(test_tbb_thread_pool)
# db_test(test_spoton_hashbuf)

db_exe(bench_log)
db_exe(benchmark_turbo bench_turbo)


# db_exe(var_hash_test var_hash_test1 CACHELINE1)
# db_exe(var_hash_test var_hash_test2 CACHELINE2)
# db_exe(var_hash_test var_hash_test3 CACHELINE3)
# db_exe(var_hash_test var_hash_test4 CACHELINE4)
# db_exe(var_hash_test var_hash_test5 CACHELINE5)
# db_exe(var_hash_test var_hash_test6 CACHELINE6)
# db_exe(var_hash_test var_hash_test7 CACHELINE7)
# db_exe(var_hash_test var_hash_test8 CACHELINE8)
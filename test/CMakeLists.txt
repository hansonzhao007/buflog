db_test(buflog_test)
db_test(ralloc_test)
db_test(fastfair_test)
db_test(inlineskiplist_test)

db_exe(cceh_bench bench_cceh_buflog BUFLOG)

db_exe(fastfair_bench bench_fastfair_pmem PMEM)
db_exe(fastfair_bench bench_fastfair_buflog PMEM_BUFLOG CONFIG_BUFNODE CONFIG_DRAM_INNER)

db_exe(inlineskiplist_bench bench_skiplist_pmem PMEM)
db_exe(inlineskiplist_bench bench_skiplist_buflog PMEM_BUFLOG CONFIG_BUFNODE CONFIG_DRAM_INNER)

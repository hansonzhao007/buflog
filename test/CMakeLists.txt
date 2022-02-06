db_test(buflog_test)
db_test(ralloc_test)
db_test(fastfair_test)
db_test(inlineskiplist_test)

db_exe(cceh_bench)

db_exe(fastfair_bench bench_fastfair_pmem PMEM)
db_exe(fastfair_bench bench_fastfair_buflog PMEM_BUFLOG)

db_exe(inlineskiplist_bench bench_skiplist_pmem PMEM)
db_exe(inlineskiplist_bench bench_skiplist_buflog BUFLOG)

db_test(buflog_test)
db_test(ralloc_test)
db_test(fastfair_test)
db_test(inlineskiplist_test)

db_exe(btree_bench)
db_exe(cceh_bench)
db_exe(inlineskiplist_bench)

set(P_CLHT_TEST clht_test.cpp ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/src/clht_lb_res_pmem.cpp ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/src/clht_gc.c
${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/external/sspfd/sspfd.c ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/external/ssmem/src/ssmem.c)
add_executable(test_pclht_test ${P_CLHT_TEST})
target_link_libraries(test_pclht_test ${TbbLib} ${JemallocLib} boost_system
    boost_thread pthread ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})


set(P_CLHT_BENCH clht_bench.cpp ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/src/clht_lb_res_pmem.cpp ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/src/clht_gc.c
${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/external/sspfd/sspfd.c ${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/external/ssmem/src/ssmem.c)
add_executable(clht_bench ${P_CLHT_BENCH})
target_link_libraries(clht_bench ${TbbLib} ${JemallocLib} boost_system
    boost_thread pthread ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})
find_library(JemallocLib jemalloc)
find_library(TbbLib tbb)
find_package (Threads)


add_library(spotontree STATIC
    ART_DRAM/Tree.cpp 
    bottomLayer.cc
    middleLayer.cc
    sptree.cc
)
target_link_libraries(spotontree ${TbbLib} ${JemallocLib} ${CMAKE_THREAD_LIBS_INIT})


set(SOURCE_FILES example2.cc)
add_executable(test_toplayer_test ${SOURCE_FILES})
target_link_libraries(test_toplayer_test spotontree)
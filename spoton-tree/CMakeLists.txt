if( NOT CMAKE_BUILD_TYPE )
    message(STATUS "No build type selected, default to Release")
    set( CMAKE_BUILD_TYPE Release)
else()
    message(STATUS "Build type is set to ${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -march=native -g")

find_library(JemallocLib jemalloc)
find_library(TbbLib tbb)
find_package (Threads)


add_library(spotontree STATIC
    ART_DRAM/Tree.cpp 
    bottomLayer.cc
    middleLayer.cc
    sptree.cc
)
target_link_libraries(spotontree ${TbbLib} ${JemallocLib} ${CMAKE_THREAD_LIBS_INIT} ralloc)


set(SOURCE_FILES example2.cc)
add_executable(test_toplayer_test ${SOURCE_FILES})
target_link_libraries(test_toplayer_test spotontree)
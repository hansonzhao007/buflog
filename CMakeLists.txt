cmake_minimum_required(VERSION 3.11)

# THIS HAS TO COME BEFORE THE PROJECT LINE
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
# THIS HAS TO COME BEFORE THE PROJECT LINE

# set the project name
project (index VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# default using release mode
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release) # RelWithDebInfo
endif()

# for P-CLHT
execute_process(COMMAND cat /proc/cpuinfo COMMAND grep clflush OUTPUT_VARIABLE ENABLE_CLFLUSH)
execute_process(COMMAND cat /proc/cpuinfo COMMAND grep clflushopt OUTPUT_VARIABLE ENABLE_CLFLUSHOPT)
execute_process(COMMAND cat /proc/cpuinfo COMMAND grep clwb OUTPUT_VARIABLE ENABLE_CLWB)
if(ENABLE_CLWB)
    add_definitions(-DCLWB)
    message(STATUS "Looking for clwb instruction - found")
elseif(ENABLE_CLFLUSHOPT)
    add_definitions(-DCLFLUSH_OPT)
    message(STATUS "Looking for clwb instruction - not found")
    message(STATUS "Looking for clflushopt instruction - found")
elseif(ENABLE_CLFLUSH)
    add_definitions(-DCLFLUSH)
    message(STATUS "Looking for clwb instruction - not found")
    message(STATUS "Looking for clflushopt instruction - not found")
    message(STATUS "Looking for clflush instruction - found")
else()
    message(FATAL_ERROR "Cannot find any flush instructions (clflush, clflushopt, clwb)")
endif()
include_directories(
  "${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/include"
  "${PROJECT_SOURCE_DIR}/RECIPE/P-CLHT/external/include"
)
find_library(JemallocLib jemalloc)
find_library(TbbLib tbb)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mrtm -mcx16 -mavx -mavx2 -mbmi2 -mlzcnt")
add_definitions(-DADD_PADDING)

add_definitions(-DBUFLOG)

# # add Intel PCM library
# add_custom_command( OUTPUT libPCM.a
#                     COMMAND make lib -j8
#                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/pcm)
# add_custom_target(pcm DEPENDS libPCM.a)
# link_directories(${PROJECT_SOURCE_DIR}/lib/pcm)
# list(APPEND THIRDPARTY_LIBS PCM)

# add liballoc.a library: https://github.com/urcs-sync/ralloc
execute_process(  COMMAND make libralloc.a -j8
                  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/ralloc/test
                  )
link_directories(${PROJECT_SOURCE_DIR}/lib/ralloc/test)
list(APPEND THIRDPARTY_LIBS ralloc)
include_directories(
  "${PROJECT_SOURCE_DIR}/lib/ralloc/src"
)
add_definitions(-DRALLOC)

# check existence of jemalloc
include(CheckIncludeFile)
check_include_file("jemalloc/jemalloc.h" HAVE_JEMALLOC)
if(HAVE_JEMALLOC)
  add_definitions(-DJEMALLOC)
  list(APPEND THIRDPARTY_LIBS jemalloc)
endif()

# # add gflags library
# add_subdirectory(lib/gflag)
# list(APPEND THIRDPARTY_LIBS gflags)
# include_directories(
#     "${CMAKE_BINARY_DIR}/lib/gflag/include"
# )

# # add gtest library
# add_subdirectory(lib/gtest)
list(APPEND THIRDPARTY_LIBS gtest gflags)
# include_directories(
#     "${PROJECT_SOURCE_DIR}/lib/gtest/googletest/include"
#     "${PROJECT_SOURCE_DIR}/lib/gtest/googlemock/include"
# )

if(CMAKE_COMPILER_IS_GNUCXX)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
                    OUTPUT_VARIABLE GCC_VERSION)
    if(GCC_VERSION VERSION_GREATER 7.5 OR GCC_VERSION VERSION_EQUAL 7.5)
        set(CMAKE_CXX_FLAGS "-Wall -Wextra")
    else(GCC_VERSION VERSION_GREATER 7.5 OR GCC_VERSION VERSION_EQUAL 7.5)
        set(CMAKE_CXX_FLAGS "-Wall -Wextra")
    endif(GCC_VERSION VERSION_GREATER 7.5 OR GCC_VERSION VERSION_EQUAL 7.5)
endif(CMAKE_COMPILER_IS_GNUCXX)
if(CMAKE_CXX_COMPILER MATCHES "clang")
    set(CMAKE_CXX_FLAGS "-Wall -Wextra")
endif(CMAKE_CXX_COMPILER MATCHES "clang")


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")


# add pthread support
find_package(Threads REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# add avx512 sse2 sse3 support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -mavx -mavx512f -msse -msse2 -msse3")

# In GCC, this will invoke the "-I" command
include_directories(
  "/usr/include"
  "/usr/local/include"
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/lib"
  "${PROJECT_SOURCE_DIR}/CCEH/src"
)

# add system local lib
link_directories(/usr/lib)
link_directories(/usr/local/lib)
link_directories("${PROJECT_SOURCE_DIR}/lib/")

list(APPEND THIRDPARTY_LIBS pmem vmem pmemobj util cceh)

# all the binary will be in the build folder
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# This will enable your add_test() under tests folder.
option(BUILD_TESTS "Build unit tests" ON)
enable_testing()
function(db_test test_file)
  get_filename_component(test_target_name "${test_file}" NAME_WE)
  set (case_name test_${test_target_name})
  add_executable("${case_name}" "${test_file}")
  target_link_libraries("${case_name}" ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})  # boost_thread boost_system
  target_sources("${case_name}"
      PRIVATE
      "${test_file}"
  )
  add_test(NAME "${case_name}" COMMAND "${case_name}")
endfunction(db_test)

function(db_exe src_file)
  get_filename_component(target_name "${src_file}" NAME_WE)
  set (case_name ${target_name})
  add_executable("${case_name}" "${src_file}")
  target_link_libraries("${case_name}" ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})
  target_sources("${case_name}"
      PRIVATE
      "${src_file}"
  )
endfunction(db_exe)

# print all include directories and lib directories
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "include dir='${dir}'")
endforeach()
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY LINK_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "lib dir='${dir}'")
endforeach()

foreach(tlib ${THIRDPARTY_LIBS})
  message(STATUS "lib name='${tlib}'")
endforeach()

# The add_subdirectory will add CMakeLists.txt under src and tests into your project.
add_subdirectory(CCEH/src)
add_subdirectory(test)
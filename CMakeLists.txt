cmake_minimum_required(VERSION 3.11)

# THIS HAS TO COME BEFORE THE PROJECT LINE
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
# THIS HAS TO COME BEFORE THE PROJECT LINE

# set the project name
project (index VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# default using release mode
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
    message(STATUS "build type set to release") # RelWithDebInfo
endif ()
string(TOLOWER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)
message(STATUS "build type: ${CMAKE_BUILD_TYPE}")


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

# # add gtest library
list(APPEND THIRDPARTY_LIBS gtest gflags)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -Wextra")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Wno-format")

# tbb
find_library(TbbLib tbb)
list(APPEND THIRDPARTY_LIBS  ${TbbLib})

# add pthread support
find_package(Threads REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# add avx512 sse2 sse3 support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx -msse -msse2 -msse3")

# In GCC, this will invoke the "-I" command
include_directories(
  "/usr/include"
  "/usr/local/include"
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/lib"
  "${PROJECT_SOURCE_DIR}/CCEH/src"
  "${PROJECT_SOURCE_DIR}/Skiplist"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/test"
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
  
  if (${ARGC} EQUAL 3)
    # db_exe(src_file binary_name macro)    
    set (case_name ${ARGV1})       
    add_executable("${case_name}" "${src_file}") 
    target_link_libraries("${case_name}" ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})
    target_compile_definitions("${case_name}" PRIVATE ${ARGV2}=1)    
  elseif (${ARGC} EQUAL 2)
    # db_exe(src_file macro)    
    add_executable("${case_name}" "${src_file}")
    target_link_libraries("${case_name}" ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})
    target_compile_definitions("${case_name}" PRIVATE ${ARGV1}=1)    
  else()
    # db_exe(src_file)    
    add_executable("${case_name}" "${src_file}")
    target_link_libraries("${case_name}" ${PROJECT_LINK_LIBS} ${THIRDPARTY_LIBS})        
  endif() 
  
  target_sources("${case_name}"
      PRIVATE
      "${src_file}"
  )
endfunction(db_exe)

# The add_subdirectory will add CMakeLists.txt under src and tests into your project.
add_subdirectory(CCEH/src)
add_subdirectory(test)
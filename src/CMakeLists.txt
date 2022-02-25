set(SPOTON_SRCS
    "spoton_bufnode.cc"
    "spoton_bufnode.h"
    "spoton_log.cc"
    "spoton_log.h"    
)

# Declare the library
add_library(spoton STATIC
    ${SPOTON_SRCS}
)

# Specify here the include directories exported by this library
target_include_directories(spoton PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
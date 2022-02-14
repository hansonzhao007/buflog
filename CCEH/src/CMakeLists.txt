# CMAKELISTS.TXT UNDER cceh
set(CCEH_SRCS
    "CCEH_spoton.cpp"
    "CCEH_spoton.h"
)

# Declare the library
add_library(cceh STATIC
    ${CCEH_SRCS}
)

# add_dependencies(cceh)

# Specify here the include directories exported by this library
target_include_directories(cceh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
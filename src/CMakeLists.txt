set(INC
        ../openfx/include
        ../openfx/Support/include
        ../openfx/Support/Library
        ../openfx/Support/Plugins/include
        ../SimplexNoise/src
        )

file(GLOB SUPPORT_LIB
        ../openfx/Support/Library/*.cpp)

set(SRC
        ${SUPPORT_LIB}
        ../SimplexNoise/src/SimplexNoise.cpp)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # Fixes compilation error in ofxsProcessing.H:137, see:
    # https://stackoverflow.com/questions/5004858/why-is-stdmin-failing-when-windows-h-is-included
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D NOMINMAX")
endif()

add_library(lic SHARED lic.cpp ${SRC})
target_include_directories(lic PRIVATE ${INC})
set_target_properties(lic PROPERTIES SUFFIX ".ofx")

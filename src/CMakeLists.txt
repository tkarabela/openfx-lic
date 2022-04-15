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

add_library(lic SHARED lic.cpp ${SRC})
target_include_directories(lic PRIVATE ${INC})
set_target_properties(lic PROPERTIES SUFFIX ".ofx")

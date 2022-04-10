set(INC
        ../openfx/include
        ../openfx/Support/include
        ../openfx/Support/Library
        ../openfx/Support/Plugins/include
        )

file(GLOB SUPPORT_LIB
        ../openfx/Support/Library/*.cpp)

set(SRC
        ${SUPPORT_LIB})

set(CMAKE_CXX_STANDARD 14)

add_library(openfx-invert SHARED invert.cpp ${SRC})
target_include_directories(openfx-invert PRIVATE ${INC})
set_target_properties(openfx-invert PROPERTIES SUFFIX ".ofx")

add_library(libghv STATIC
    src/ghv_decoder.cpp
)
target_compile_features(libghv PUBLIC cxx_std_17)
target_include_directories(libghv
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../native
)
if(MSVC)
  target_compile_options(libghv PRIVATE /O2 /W4)
else()
  target_compile_options(libghv PRIVATE -O3 -Wall -Wextra)
endif()
if(OpenMP_CXX_FOUND)
  target_link_libraries(libghv PUBLIC OpenMP::OpenMP_CXX)
endif()

# fetch 3rd party libraries
include(FetchContent)

FetchContent_Declare(
  googletest
  URL               https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
  URL_MD5           e82199374acdfda3f425331028eb4e2a

)

FetchContent_Declare(
  googlebenchmark
  URL               https://github.com/google/benchmark/archive/refs/tags/v1.6.2.tar.gz
  URL_MD5           14d14849e075af116143a161bc3b927b

)

FetchContent_Declare(
  absl
  URL               https://github.com/abseil/abseil-cpp/archive/refs/tags/20220623.0.tar.gz
  URL_MD5           955b6faedf32ec2ce1b7725561d15618
)



set(CMAKE_CXX_CLANG_TIDY "") # turn off clang-tidy for third-party code

add_subdirectory(googletest)
add_subdirectory(googlebenchmark)

add_subdirectory(absl)


# fetch 3rd party libraries
include(FetchContent)

set(CMAKE_CXX_CLANG_TIDY "") # turn off clang-tidy for third-party code


FetchContent_Declare(
  googletest
  URL               https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
  URL_MD5           e82199374acdfda3f425331028eb4e2a

)
FetchContent_MakeAvailable(googletest)


set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE INTERNAL "")
set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
FetchContent_Declare(
  googlebenchmark
  URL               https://github.com/google/benchmark/archive/refs/tags/v1.7.0.tar.gz
  URL_MD5           a259f985e7f8e4f5d2286a592eabd7e9

)
FetchContent_MakeAvailable(googlebenchmark)

FetchContent_Declare(
  absl
  URL               https://github.com/abseil/abseil-cpp/archive/refs/tags/20220623.0.tar.gz
  URL_MD5           955b6faedf32ec2ce1b7725561d15618
)
FetchContent_MakeAvailable(absl)


FetchContent_Declare(
  ctre
  URL               https://github.com/hanickadot/compile-time-regular-expressions/archive/refs/tags/v3.7.1.tar.gz
  URL_MD5           95a1297be479f287fd33b27da0d9218e
)
FetchContent_MakeAvailable(ctre)


FetchContent_Declare(
  fmt
  URL               https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.tar.gz
  URL_MD5           21fac48cae8f3b4a5783ae06b443973a
)
FetchContent_MakeAvailable(fmt)

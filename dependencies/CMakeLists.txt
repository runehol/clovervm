# fetch 3rd party libraries
include(FetchContent)

set(CMAKE_CXX_CLANG_TIDY "") # turn off clang-tidy for third-party code

option(CLOVERVM_BUILD_BENCHMARK_DEPS "Fetch and build googlebenchmark dependencies" ON)


FetchContent_Declare(
  googletest
  URL               https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  URL_MD5           b6f100bc2a5853a48046aa168ececf84
)
FetchContent_MakeAvailable(googletest)


if(CLOVERVM_BUILD_BENCHMARK_DEPS)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_WERROR OFF CACHE INTERNAL "")
  FetchContent_Declare(
    googlebenchmark
    URL               https://github.com/google/benchmark/archive/refs/tags/v1.8.5.tar.gz
    URL_MD5           708d91ce255e8af4c1d7dfec50dff178
  )
  FetchContent_MakeAvailable(googlebenchmark)
endif()

FetchContent_Declare(
  absl
  URL               https://github.com/abseil/abseil-cpp/archive/refs/tags/20260107.1.tar.gz
  URL_MD5           d032877f03483884299c50f527f3983e
)
FetchContent_MakeAvailable(absl)


FetchContent_Declare(
  ctre
  URL               https://github.com/hanickadot/compile-time-regular-expressions/archive/refs/tags/v3.10.0.tar.gz
  URL_MD5           e3f4f173228180b564b72f73fb4c065c
)
FetchContent_MakeAvailable(ctre)


FetchContent_Declare(
  fmt
  URL               https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
  URL_MD5           92eb6f492e4838e5f024ce5207beafc7
)
FetchContent_MakeAvailable(fmt)

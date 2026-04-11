# fetch 3rd party libraries
include(FetchContent)

set(CMAKE_CXX_CLANG_TIDY "") # turn off clang-tidy for third-party code

option(CLOVERVM_BUILD_BENCHMARK_DEPS "Fetch and build googlebenchmark dependencies" ON)


FetchContent_Declare(
  googletest
  URL               https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz

)
FetchContent_MakeAvailable(googletest)


if(CLOVERVM_BUILD_BENCHMARK_DEPS)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_WERROR OFF CACHE INTERNAL "")
  FetchContent_Declare(
    googlebenchmark
    URL               https://github.com/google/benchmark/archive/refs/tags/v1.8.5.tar.gz

  )
  FetchContent_MakeAvailable(googlebenchmark)
endif()

FetchContent_Declare(
  absl
  URL               https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.2.tar.gz
)
FetchContent_MakeAvailable(absl)


FetchContent_Declare(
  ctre
  URL               https://github.com/hanickadot/compile-time-regular-expressions/archive/refs/tags/v3.10.0.tar.gz
)
FetchContent_MakeAvailable(ctre)


FetchContent_Declare(
  fmt
  URL               https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.tar.gz
)
FetchContent_MakeAvailable(fmt)

# fetch 3rd party libraries
include(FetchContent)

set(CMAKE_CXX_CLANG_TIDY "") # turn off clang-tidy for third-party code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CLOVERVM_BUILD_BENCHMARK_DEPS "Fetch and build googlebenchmark dependencies" ON)


FetchContent_Declare(
  googletest
  URL               https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  URL_MD5           b6f100bc2a5853a48046aa168ececf84
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)


if(CLOVERVM_BUILD_BENCHMARK_DEPS)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_WERROR OFF CACHE INTERNAL "")
  FetchContent_Declare(
    googlebenchmark
    URL               https://github.com/google/benchmark/archive/refs/tags/v1.8.5.tar.gz
    URL_MD5           708d91ce255e8af4c1d7dfec50dff178
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
endif()

FetchContent_Declare(
  absl
  URL               https://github.com/abseil/abseil-cpp/archive/refs/tags/20260107.1.tar.gz
  URL_MD5           d032877f03483884299c50f527f3983e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)


FetchContent_Declare(
  ctre
  URL               https://github.com/hanickadot/compile-time-regular-expressions/archive/refs/tags/v3.10.0.tar.gz
  URL_MD5           e3f4f173228180b564b72f73fb4c065c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  cxxopts
  URL               https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.3.1.tar.gz
  URL_MD5           47dcaab8ea57feed39d79abad56a3ae9
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)


FetchContent_Declare(
  fmt
  URL               https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
  URL_MD5           92eb6f492e4838e5f024ce5207beafc7
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  fast_float
  URL               https://codeload.github.com/fastfloat/fast_float/tar.gz/refs/tags/v8.3.2
  URL_HASH          SHA256=71bb9e865b888df01351558db5a39586d13ea97ada162156b3a07784d8c1b26b
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  simdutf
  URL https://codeload.github.com/simdutf/simdutf/tar.gz/refs/tags/v9.3.2
  URL_HASH SHA256=d92a0a4ce216dcebf18af20fed7ea7279fa535683f790fdaeed39827a234f567
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Keep the dependency's library static without changing other project targets.
block()
  set(BUILD_SHARED_LIBS OFF)
  set(SIMDUTF_TESTS OFF CACHE BOOL "" FORCE)
  set(SIMDUTF_TOOLS OFF CACHE BOOL "" FORCE)
  set(SIMDUTF_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(SIMDUTF_ICONV OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  FetchContent_MakeAvailable(simdutf)
  if(TARGET simdutf-nostdlibcxx)
    set_target_properties(simdutf-nostdlibcxx PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
  install(FILES "${simdutf_SOURCE_DIR}/LICENSE-MIT"
    DESTINATION share/licenses/clovervm RENAME simdutf-LICENSE-MIT)
endblock()

block()
  set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
  FetchContent_MakeAvailable(googletest)
  if(CLOVERVM_BUILD_BENCHMARK_DEPS)
    FetchContent_MakeAvailable(googlebenchmark)
  endif()
  FetchContent_MakeAvailable(absl ctre cxxopts fmt fast_float)
endblock()

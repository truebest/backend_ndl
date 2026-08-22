cmake_minimum_required(VERSION 3.19)

set(BACKEND_NDL_VERSION 0.5.0)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(backend_ndl VERSION ${BACKEND_NDL_VERSION} LANGUAGES C)
  include(CTest)
  set(BACKEND_NDL_TOP_LEVEL ON)
else()
  set(BACKEND_NDL_TOP_LEVEL OFF)
endif()

include(GNUInstallDirs)

option(BACKEND_NDL_BUILD_TESTS "Build backend_ndl host tests" ${BUILD_TESTING})
option(BACKEND_NDL_INSTALL "Install backend_ndl development files" ${BACKEND_NDL_TOP_LEVEL})
option(BACKEND_NDL_ENABLE_TSAN "Run backend_ndl host tests under ThreadSanitizer" OFF)
option(BACKEND_NDL_BUILD_NDK_CONFORMANCE
  "Compare the private ABI with an available official webOS NDK header" ON)
set(BACKEND_NDL_NDK_INCLUDE_DIR "" CACHE PATH
  "Optional webOS NDK include root for DirectMedia ABI conformance tests")

set(BACKEND_NDL_BUILD_GTESTS ${BACKEND_NDL_BUILD_TESTS})
if(BACKEND_NDL_BUILD_TESTS AND CMAKE_CROSSCOMPILING)
  message(STATUS "backend_ndl: skipping host-only GoogleTest suite while cross-compiling")
  set(BACKEND_NDL_BUILD_GTESTS OFF)
endif()

set(_backend_ndl_ndk_include_dir "")
if(BACKEND_NDL_BUILD_GTESTS AND BACKEND_NDL_BUILD_NDK_CONFORMANCE)
  set(_backend_ndl_ndk_include_dir "${BACKEND_NDL_NDK_INCLUDE_DIR}")
  if(BACKEND_NDL_NDK_INCLUDE_DIR AND
     NOT EXISTS "${BACKEND_NDL_NDK_INCLUDE_DIR}/NDL/NDL_directmedia.h")
    message(FATAL_ERROR
      "BACKEND_NDL_NDK_INCLUDE_DIR does not contain NDL/NDL_directmedia.h: "
      "${BACKEND_NDL_NDK_INCLUDE_DIR}")
  endif()
  if(BACKEND_NDL_NDK_INCLUDE_DIR MATCHES "/sysroots/x86_64-starfishsdk-linux(/|$)")
    message(FATAL_ERROR
      "BACKEND_NDL_NDK_INCLUDE_DIR must point at the target webOS sysroot, not "
      "the x86_64 host-tools sysroot: ${BACKEND_NDL_NDK_INCLUDE_DIR}")
  endif()
  if(NOT _backend_ndl_ndk_include_dir)
    set(_backend_ndl_ndk_hints)
    if(DEFINED ENV{OECORE_TARGET_SYSROOT})
      list(APPEND _backend_ndl_ndk_hints "$ENV{OECORE_TARGET_SYSROOT}/usr/include")
    endif()
    if(DEFINED ENV{SDKTARGETSYSROOT})
      list(APPEND _backend_ndl_ndk_hints "$ENV{SDKTARGETSYSROOT}/usr/include")
    endif()
    find_path(_backend_ndl_ndk_include_dir
      NAMES NDL/NDL_directmedia.h
      HINTS ${_backend_ndl_ndk_hints}
    )
  endif()
  if(_backend_ndl_ndk_include_dir MATCHES "/sysroots/x86_64-starfishsdk-linux(/|$)")
    message(FATAL_ERROR
      "backend_ndl found DirectMedia in the x86_64 host-tools sysroot; set "
      "BACKEND_NDL_NDK_INCLUDE_DIR to the target sysroot usr/include directory")
  endif()
endif()

set(BACKEND_NDL_SOURCES
  src/backend_ndl.c
  src/backend_ndl_audio.c
  src/backend_ndl_video.c
)

add_library(backend_ndl STATIC ${BACKEND_NDL_SOURCES})
add_library(backend_ndl::backend_ndl ALIAS backend_ndl)
set_target_properties(backend_ndl PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS YES
  POSITION_INDEPENDENT_CODE YES
  EXPORT_NAME backend_ndl
)
target_include_directories(backend_ndl
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE src
)

find_package(Threads REQUIRED)
target_link_libraries(backend_ndl PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
  target_link_libraries(backend_ndl PRIVATE rt)
endif()

if(BACKEND_NDL_BUILD_TESTS)
  add_library(backend_ndl_test_support STATIC ${BACKEND_NDL_SOURCES})
  add_library(backend_ndl::test_support ALIAS backend_ndl_test_support)
  set_target_properties(backend_ndl_test_support PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
  )
  target_compile_definitions(backend_ndl_test_support PUBLIC BACKEND_NDL_TESTING=1)
  target_include_directories(backend_ndl_test_support
    PUBLIC include src
  )
  target_link_libraries(backend_ndl_test_support PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(backend_ndl_test_support PUBLIC rt)
  endif()

  if(BACKEND_NDL_BUILD_GTESTS)
    enable_language(CXX)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(test-backend-ndl tests/test_backend_ndl.cpp)
    target_link_libraries(test-backend-ndl PRIVATE backend_ndl::test_support GTest::gtest_main)
    gtest_discover_tests(test-backend-ndl TEST_PREFIX "backend-ndl.")

    add_executable(test-backend-ndl-concurrency tests/test_backend_ndl_concurrency.cpp)
    target_link_libraries(test-backend-ndl-concurrency PRIVATE
      backend_ndl::test_support
      GTest::gtest_main
    )
    gtest_discover_tests(test-backend-ndl-concurrency
      TEST_PREFIX "backend-ndl-concurrency."
    )

    add_executable(test-backend-ndl-abi tests/test_tv_abi_layout.cpp)
    target_compile_definitions(test-backend-ndl-abi PRIVATE
      BACKEND_NDL_TESTING=1
    )
    target_include_directories(test-backend-ndl-abi PRIVATE src)
    target_link_libraries(test-backend-ndl-abi PRIVATE GTest::gtest_main)
    gtest_discover_tests(test-backend-ndl-abi TEST_PREFIX "backend-ndl-abi.")

    set(_backend_ndl_gtest_targets
      test-backend-ndl
      test-backend-ndl-concurrency
      test-backend-ndl-abi
      test-backend-ndl-resolver
    )

    if(BACKEND_NDL_BUILD_NDK_CONFORMANCE AND _backend_ndl_ndk_include_dir)
      add_executable(test-backend-ndl-ndk-conformance
        tests/test_ndk_conformance.cpp)
      target_include_directories(test-backend-ndl-ndk-conformance PRIVATE src)
      target_compile_definitions(test-backend-ndl-ndk-conformance PRIVATE
        "BACKEND_NDL_NDK_CONFORMANCE_HEADER=\"${_backend_ndl_ndk_include_dir}/NDL/NDL_directmedia.h\""
      )
      target_link_libraries(test-backend-ndl-ndk-conformance PRIVATE GTest::gtest_main)
      gtest_discover_tests(test-backend-ndl-ndk-conformance
        TEST_PREFIX "backend-ndl-ndk-conformance."
      )
      list(APPEND _backend_ndl_gtest_targets test-backend-ndl-ndk-conformance)
      message(STATUS
        "backend_ndl: enabled NDK conformance against ${_backend_ndl_ndk_include_dir}")
    elseif(BACKEND_NDL_BUILD_NDK_CONFORMANCE)
      message(STATUS
        "backend_ndl: official NDK header unavailable; skipping optional conformance test")
    endif()

    add_library(backend_ndl_fake_facade SHARED tests/fake_ndl_facade.c)
    target_include_directories(backend_ndl_fake_facade PRIVATE src)
    set_target_properties(backend_ndl_fake_facade PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS YES
      OUTPUT_NAME NDL_directmedia
      VERSION 1.0.0
      SOVERSION 1
    )

    add_library(backend_ndl_fake_facade_global SHARED tests/fake_ndl_facade.c)
    target_include_directories(backend_ndl_fake_facade_global PRIVATE src)
    set_target_properties(backend_ndl_fake_facade_global PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS YES
      OUTPUT_NAME backend_ndl_global_fixture
    )

    add_library(backend_ndl_fake_facade_legacy SHARED tests/fake_ndl_facade_legacy.c)
    target_include_directories(backend_ndl_fake_facade_legacy PRIVATE src)
    set_target_properties(backend_ndl_fake_facade_legacy PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS YES
      OUTPUT_NAME NDL_directmedia_legacy
      VERSION 1.0.0
      SOVERSION 1
    )

    add_library(backend_ndl_fake_facade_incomplete SHARED
      tests/fake_ndl_facade_incomplete.c)
    set_target_properties(backend_ndl_fake_facade_incomplete PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS YES
      OUTPUT_NAME NDL_directmedia_incomplete
      VERSION 1.0.0
      SOVERSION 1
    )

    add_executable(test-backend-ndl-resolver tests/test_backend_ndl_resolver.cpp)
    target_compile_definitions(test-backend-ndl-resolver PRIVATE
      BACKEND_NDL_FAKE_FACADE_PATH="$<TARGET_FILE:backend_ndl_fake_facade>"
      BACKEND_NDL_FAKE_GLOBAL_FACADE_PATH="$<TARGET_FILE:backend_ndl_fake_facade_global>"
      BACKEND_NDL_FAKE_LEGACY_FACADE_PATH="$<TARGET_FILE:backend_ndl_fake_facade_legacy>"
      BACKEND_NDL_FAKE_INCOMPLETE_FACADE_PATH="$<TARGET_FILE:backend_ndl_fake_facade_incomplete>"
    )
    target_link_libraries(test-backend-ndl-resolver PRIVATE
      backend_ndl::test_support
      GTest::gtest_main
    )
    add_dependencies(test-backend-ndl-resolver
      backend_ndl_fake_facade
      backend_ndl_fake_facade_global
      backend_ndl_fake_facade_legacy
      backend_ndl_fake_facade_incomplete
    )
    gtest_discover_tests(test-backend-ndl-resolver
      TEST_PREFIX "backend-ndl-resolver."
    )

    add_test(NAME backend-ndl-feed-mutex-free
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-feed-locks.sh")

    foreach(target IN LISTS _backend_ndl_gtest_targets)
      set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
      )
    endforeach()

    if(BACKEND_NDL_ENABLE_TSAN)
      foreach(target backend_ndl_test_support test-backend-ndl
                     test-backend-ndl-concurrency test-backend-ndl-resolver)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer -O1 -g)
        target_link_options(${target} PRIVATE -fsanitize=thread)
      endforeach()
    endif()
  endif()
endif()

if(BACKEND_NDL_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS backend_ndl EXPORT backend_ndlTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
  install(DIRECTORY include/backend_ndl DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES LICENSE README.md THIRD_PARTY.md DESTINATION "${CMAKE_INSTALL_DATADIR}/backend_ndl")
  install(EXPORT backend_ndlTargets
    FILE backend_ndlTargets.cmake
    NAMESPACE backend_ndl::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/backend_ndl"
  )
  configure_package_config_file(
    cmake/backend_ndlConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/backend_ndlConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/backend_ndl"
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/backend_ndlConfigVersion.cmake"
    VERSION ${BACKEND_NDL_VERSION}
    COMPATIBILITY SameMinorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/backend_ndlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/backend_ndlConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/backend_ndl"
  )
endif()

if(BACKEND_NDL_TOP_LEVEL AND BACKEND_NDL_BUILD_TESTS AND BACKEND_NDL_INSTALL
   AND NOT CMAKE_CROSSCOMPILING)
  configure_file(
    tests/install-consumer/run.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/test-backend-ndl-install-consumer.cmake"
    @ONLY
  )
  add_test(NAME backend-ndl-install-consumer
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_CONFIG=$<CONFIG>"
      -P "${CMAKE_CURRENT_BINARY_DIR}/test-backend-ndl-install-consumer.cmake"
  )
endif()

cmake_minimum_required(VERSION 3.16)

project(Landor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The world layer is still header-only design contracts; the only implementation
# translation units are main.cpp and the build-selected storage backend. Headers
# are listed for IDE visibility; a future implementation file must be added here
# explicitly to be compiled.
add_executable(Landor
    main.cpp
    src/platform/storage/storage_filesystem.cpp

    src/storage/types.hpp

    src/world/coord.hpp
    src/world/area.hpp
    src/world/layer.hpp
    src/world/orientation.hpp
    src/world/tile.hpp
    src/world/patch.hpp
    src/world/placement.hpp
    src/world/patchset.hpp
    src/world/place.hpp
    src/world/map.hpp
    src/world/map_result.hpp
    src/world/chunk.hpp
    src/world/cache.hpp
    src/world/layer_source.hpp
    src/world/layer_fallback.hpp
    src/world/authored_layer_source.hpp
    src/storage/storage_contract.hpp
    src/platform/storage/storage_filesystem.hpp
)
target_include_directories(Landor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

include(GNUInstallDirs)
install(TARGETS Landor
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------------------------------------------------------------------
# Tests: GoogleTest is fetched on demand, nothing is vendored in the repo.
#
#   cmake -S . -B transient/build
#   cmake --build transient/build      # first configure downloads GoogleTest;
#                                      # the tarball lands in <build>/_deps and
#                                      # is reused afterwards
#   ctest --test-dir transient/build --output-on-failure
#                                      # --test-dir keeps ctest out of the
#                                      # source tree
#
# Options:
#   -DLANDOR_BUILD_TESTS=OFF           # skip the download altogether
#   -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
#                                      # use a local copy, no network at all
# ---------------------------------------------------------------------------
option(LANDOR_BUILD_TESTS "Build the unit tests (downloads GoogleTest on first configure)" ON)

if(LANDOR_BUILD_TESTS)
    # Extracted files get the extraction timestamp, not the archive's, so a URL
    # change triggers a rebuild instead of silently reusing stale headers.
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    include(FetchContent)

    FetchContent_Declare(googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    )

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)          # don't install gtest with Landor
    set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)          # TEST() only, no gmock

    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    add_executable(landor_tests
        # Compile tripwire: every header under src/ in one translation unit.
        tests/test_world_headers.cpp

        # Existing behavioural tests awaiting their separate layout move.
        tests/test_area.cpp
        tests/test_coord.cpp

        # Storage backend under test.
        src/platform/storage/storage_filesystem.cpp

        # Behavioural tests mirroring src/world/.
        tests/world/test_tile.cpp
        tests/world/test_chunk.cpp
        tests/world/test_cache.cpp
        tests/world/test_layer_source.cpp
        tests/world/test_layer_fallback.cpp
        tests/world/test_authored_layer_source.cpp
        tests/world/test_placement.cpp
        tests/world/test_map_placement.cpp
        tests/world/test_map_result.cpp

        # Behavioural tests mirroring src/platform/storage/.
        tests/platform/storage/test_storage_filesystem.cpp
    )
    target_include_directories(landor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(landor_tests PRIVATE GTest::gtest_main)
    gtest_discover_tests(landor_tests)
endif()

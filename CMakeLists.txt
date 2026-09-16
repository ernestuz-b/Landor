cmake_minimum_required(VERSION 3.16)

project(Landor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The world layer is currently header-only design contracts, so the only
# translation unit is main.cpp. Headers are listed for IDE visibility; a future
# implementation file must be added here explicitly to be compiled.
add_executable(Landor
    main.cpp

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

    # placeholders awaiting their interface drafts
    src/world/chunk.hpp
    src/world/cache.hpp
)

include(GNUInstallDirs)
install(TARGETS Landor
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

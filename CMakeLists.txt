cmake_minimum_required(VERSION 3.16)

project(Landor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Landor main.cpp
    src/world/cache.hpp src/world/cache.cpp
    src/world/istorage.hpp
    src/world/coord.hpp
    src/world/map.hpp src/world/map.cpp
    src/world/chunk.hpp src/world/chunk.cpp
    src/world/area.hpp src/world/area.cpp
    src/world/orientation.hpp src/world/orientation.cpp
    src/world/patch.hpp src/world/patch.cpp
    src/world/layer.hpp src/world/layer.cpp
    src/world/placement.hpp src/world/placement.cpp
    src/world/place.hpp src/world/place.cpp
    src/world/patchset.hpp src/world/patchset.cpp
)

include(GNUInstallDirs)
install(TARGETS Landor
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

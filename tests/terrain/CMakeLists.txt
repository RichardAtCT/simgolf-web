# Standalone build for the terrain renderer tests (separate from the game's
# CMakeLists.txt so it can be built and run on its own).
#
#   native:  cmake -B build/terrain-native -S tests/terrain && cmake --build build/terrain-native
#            build/terrain-native/terrain_render --game game --sample --out terrain.png
#   browser: emcmake cmake -B build/terrain-web -S tests/terrain && cmake --build build/terrain-web
#            node tools/boxedwine/serve.mjs build/terrain-web 8082   # open /terrain_test.html
#
# The web page preloads the user's Data/Textures, lighting files and saved
# games from SIMGOLF_GAME (never committed).
cmake_minimum_required(VERSION 3.20)
project(simgolf_terrain_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(SIMGOLF_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SIMGOLF_GAME "${SIMGOLF_REPO}/game" CACHE PATH "The user's SimGolf install")

set(TERRAIN_SOURCES
  ${SIMGOLF_REPO}/src/platform/terrain_dll.cpp
  ${SIMGOLF_REPO}/src/platform/terrain_gl.cpp
  course.cpp)

if(EMSCRIPTEN)
  add_executable(terrain_test web_main.cpp ${SIMGOLF_REPO}/src/platform/framebuffer.cpp ${TERRAIN_SOURCES})
  target_include_directories(terrain_test PRIVATE ${SIMGOLF_REPO}/src .)
  target_compile_options(terrain_test PRIVATE -Wall -Wextra -sUSE_SDL=2 -sUSE_SDL_IMAGE=2)
  set(preload "")
  foreach(f Data/Textures ParklandLighting.txt DesertLighting.txt TropicalLighting.txt LinksLighting.txt
            "Saved Games")
    list(APPEND preload "SHELL:--preload-file \"${SIMGOLF_GAME}/${f}@/game/${f}\"")
  endforeach()
  target_link_options(terrain_test PRIVATE
    -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 "-sSDL2_IMAGE_FORMATS=[\"png\"]"
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB
    ${preload}
    --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
  set_target_properties(terrain_test PROPERTIES SUFFIX ".html"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
else()
  add_executable(terrain_render render_main.cpp png.cpp ${TERRAIN_SOURCES})
  target_include_directories(terrain_render PRIVATE ${SIMGOLF_REPO}/src .)
  target_compile_options(terrain_render PRIVATE -Wall -Wextra)
endif()

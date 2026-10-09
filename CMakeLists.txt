cmake_minimum_required(VERSION 3.20)
project(simgolf LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with emcmake: emcmake cmake -B build/web")
endif()

# Converted assets (tools/convert-assets) are preloaded for development only.
# They're the user's own copy of the game and are never committed.
set(SIMGOLF_ASSETS "${CMAKE_SOURCE_DIR}/assets" CACHE PATH "Converted asset tree")

# Sounds aren't preloaded (90 MB): the page fetches them on first play from
# assets/ next to simgolf.html, and only a list of the files goes in the
# package so the game's DOS paths can be matched case-insensitively.
file(GLOB_RECURSE SIMGOLF_SOUND_FILES CONFIGURE_DEPENDS RELATIVE "${SIMGOLF_ASSETS}"
  "${SIMGOLF_ASSETS}/*.wav" "${SIMGOLF_ASSETS}/*.WAV")
list(REMOVE_DUPLICATES SIMGOLF_SOUND_FILES)
list(JOIN SIMGOLF_SOUND_FILES "\n" SIMGOLF_SOUND_LIST)
file(WRITE "${CMAKE_BINARY_DIR}/sound-files.txt" "${SIMGOLF_SOUND_LIST}\n")
file(CREATE_LINK "${SIMGOLF_ASSETS}" "${CMAKE_BINARY_DIR}/assets" SYMBOLIC)

add_executable(simgolf src/main.cpp src/platform/framebuffer.cpp src/platform/sound.cpp)
target_include_directories(simgolf PRIVATE src)
target_compile_options(simgolf PRIVATE -Wall -Wextra -sUSE_SDL=2 -sUSE_SDL_IMAGE=2)
target_link_options(simgolf PRIVATE
  -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 "-sSDL2_IMAGE_FORMATS=[\"png\"]"
  -sALLOW_MEMORY_GROWTH=1
  "SHELL:--preload-file \"${SIMGOLF_ASSETS}/Interface@/assets/Interface\""
  "SHELL:--preload-file \"${CMAKE_BINARY_DIR}/sound-files.txt@/assets/sound-files.txt\""
  --js-library "${CMAKE_SOURCE_DIR}/src/platform/sound_web.js"
  --shell-file "${CMAKE_SOURCE_DIR}/web/shell.html")
set_target_properties(simgolf PROPERTIES SUFFIX ".html"
  LINK_DEPENDS "${CMAKE_SOURCE_DIR}/src/platform/sound_web.js;${CMAKE_BINARY_DIR}/sound-files.txt")

# ---------------------------------------------------------------------------
# The translated game (see docs/porting.md). Generated C comes from
# tools/translate/run.sh and lives in build/gen (never committed, because it's
# derived from the decompiler output). The target only exists once it's there.
set(SIMGOLF_GEN "${CMAKE_SOURCE_DIR}/build/gen" CACHE PATH "Translator output")
set(SIMGOLF_GAME "${CMAKE_SOURCE_DIR}/game" CACHE PATH "The user's SimGolf install")
set(SIMGOLF_IMAGES "${SIMGOLF_GEN}/images" CACHE PATH "Module images (golf.bin, ...)")
# The GitHub Pages build (tools/pages/build.sh): no game data or module images
# in the package; web/pages/shell.html installs them from the player's own
# files into browser storage and hands them to the game at start.
option(SIMGOLF_PAGES "Build the self-contained GitHub Pages version" OFF)

if(EXISTS "${SIMGOLF_GEN}/golf/golf_protos.h")
  file(GLOB SIMGOLF_GEN_SRC CONFIGURE_DEPENDS "${SIMGOLF_GEN}/*/*.c")
  # Hand-written replacements for golf functions (// @override): compiled
  # like the generated code, against golf's Ghidra headers.
  file(GLOB SIMGOLF_GOLF_OVERRIDES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/port/golf/*.c")
  file(GLOB SIMGOLF_JGLD_OVERRIDES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/port/jgld/*.c")

  if(SIMGOLF_PAGES)
    configure_file(web/autosave.js "${CMAKE_BINARY_DIR}/autosave.js" COPYONLY)
    configure_file(web/pages/install-worker.js "${CMAKE_BINARY_DIR}/install-worker.js" COPYONLY)
    configure_file(web/pages/jgld-nops.json "${CMAKE_BINARY_DIR}/jgld-nops.json" COPYONLY)
    configure_file(web/pages/og.png "${CMAKE_BINARY_DIR}/og.png" COPYONLY)
    set(SIMGOLF_SHELL "${CMAKE_SOURCE_DIR}/web/pages/shell.html")
    set(SIMGOLF_DATA_OPTS
      "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/third_party/fonts@/fonts\""
      -sEXPORTED_RUNTIME_METHODS=HEAPU8,FS,addRunDependency,removeRunDependency)
  else()
    # Manifest of the game's files ("path<TAB>size"), fetched on first open.
    file(GLOB_RECURSE SIMGOLF_GAME_FILES RELATIVE "${SIMGOLF_GAME}" "${SIMGOLF_GAME}/*")
    set(SIMGOLF_MANIFEST "")
    foreach(f IN LISTS SIMGOLF_GAME_FILES)
      if(f MATCHES "^(Docs|EReg)/" OR f MATCHES "\\.(exe|dll|lib)$")
        continue()
      endif()
      file(SIZE "${SIMGOLF_GAME}/${f}" sz)
      string(APPEND SIMGOLF_MANIFEST "${f}\t${sz}\n")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/game.manifest" "${SIMGOLF_MANIFEST}")
    # Game files web/shell.html prefetches, in the order a first run reads them
    configure_file(web/preload.txt "${CMAKE_BINARY_DIR}/preload.txt" COPYONLY)
    file(CREATE_LINK "${SIMGOLF_GAME}" "${CMAKE_BINARY_DIR}/game" SYMBOLIC)
    # Save history panel loaded by web/shell.html
    configure_file(web/autosave.js "${CMAKE_BINARY_DIR}/autosave.js" COPYONLY)
    set(SIMGOLF_SHELL "${CMAKE_SOURCE_DIR}/web/shell.html")
    set(SIMGOLF_DATA_OPTS -gsource-map -sEXPORTED_RUNTIME_METHODS=HEAPU8
      "SHELL:--preload-file \"${SIMGOLF_IMAGES}@/image\""
      "SHELL:--preload-file \"${CMAKE_BINARY_DIR}/game.manifest@/game.manifest\""
      "SHELL:--preload-file \"${CMAKE_SOURCE_DIR}/third_party/fonts@/fonts\""
      "SHELL:--preload-file \"${CMAKE_BINARY_DIR}/sound-files.txt@/assets/sound-files.txt\"")
  endif()

  add_executable(golf
    src/port/port_main.c src/port/runtime.c src/port/crt.c src/port/fs.c
    src/port/win32.c src/port/gdi.c src/port/imports.c src/port/video.c src/port/hd.c
    src/platform/terrain_dll.cpp src/platform/terrain_gl.cpp src/platform/terrain_bridge.cpp
    src/platform/terrain_hd.cpp
    src/platform/sound.cpp
    ${SIMGOLF_GEN_SRC} ${SIMGOLF_GOLF_OVERRIDES} ${SIMGOLF_JGLD_OVERRIDES})
  target_compile_definitions(golf PRIVATE SIMGOLF_PORT_FS)
  target_include_directories(golf PRIVATE src third_party/stb)
  # Each module's generated files see only their own Ghidra headers.
  foreach(f IN LISTS SIMGOLF_GEN_SRC)
    get_filename_component(d "${f}" DIRECTORY)
    set_property(SOURCE "${f}" APPEND PROPERTY INCLUDE_DIRECTORIES "${d}")
  endforeach()
  set_property(SOURCE ${SIMGOLF_GOLF_OVERRIDES} APPEND PROPERTY INCLUDE_DIRECTORIES "${SIMGOLF_GEN}/golf")
  set_property(SOURCE ${SIMGOLF_JGLD_OVERRIDES} APPEND PROPERTY INCLUDE_DIRECTORIES "${SIMGOLF_GEN}/jgld")
  set_source_files_properties(${SIMGOLF_GEN_SRC} ${SIMGOLF_GOLF_OVERRIDES} ${SIMGOLF_JGLD_OVERRIDES} PROPERTIES COMPILE_OPTIONS
    "-w;-std=gnu11;-fno-strict-aliasing;-fwrapv;-fwrapv-pointer;-gline-tables-only;-O1;-fno-delete-null-pointer-checks;-fno-finite-loops;-Wno-error=int-conversion;-Wno-error=incompatible-pointer-types;-Wno-error=implicit-function-declaration;-Wno-error=incompatible-function-pointer-types;-Wno-error=return-type")
  target_compile_options(golf PRIVATE -fno-strict-aliasing -fwrapv -fwrapv-pointer -gline-tables-only -O1 -fno-delete-null-pointer-checks -fno-finite-loops)
  target_link_options(golf PRIVATE
    -sGLOBAL_BASE=272MB -sSTACK_SIZE=8MB -sINITIAL_MEMORY=384MB -sALLOW_MEMORY_GROWTH=1
    -sJSPI -sUSE_SDL=2 -sEXIT_RUNTIME=0 -lidbfs.js -sASSERTIONS=1 --profiling-funcs
    ${SIMGOLF_DATA_OPTS}
    --js-library "${CMAKE_SOURCE_DIR}/src/platform/sound_web.js"
    # SHELL: so CMake doesn't fold the repeated --js-library flag away
    "SHELL:--js-library \"${CMAKE_SOURCE_DIR}/src/platform/hd_web.js\""
    --shell-file "${SIMGOLF_SHELL}")
  target_compile_options(golf PRIVATE -sUSE_SDL=2)
  set_target_properties(golf PROPERTIES SUFFIX ".html"
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/src/platform/sound_web.js;${CMAKE_SOURCE_DIR}/src/platform/hd_web.js;${SIMGOLF_SHELL};${CMAKE_BINARY_DIR}/sound-files.txt")
endif()

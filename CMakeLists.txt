cmake_minimum_required(VERSION 3.21)
project(guikit_sdl VERSION 1.0.0 LANGUAGES CXX C)

if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(GUIKIT_BUILD_BACKEND_THORVG "Build GuiKit ThorVG software/vector rasterizer backend" ON)
option(GUIKIT_BUILD_BACKEND_SDL3 "Build GuiKit SDL3 + OpenGL ES3/WebGL2 backend" ON)
option(GUIKIT_SDL_BUILD_EXAMPLES "Build GuiKit SDL minimal example application" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_SDL_BUILD_TESTS "Build GuiKit SDL/ThorVG unit tests" ${PROJECT_IS_TOP_LEVEL})
option(GUIKIT_SDL_INSTALL "Enable CMake install targets for guikit_sdl" ${PROJECT_IS_TOP_LEVEL})

if (GUIKIT_SDL_BUILD_TESTS)
    enable_testing()
endif()

if (NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

if (NOT TARGET guikit::core)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/guikit/CMakeLists.txt")
        add_subdirectory(third_party/guikit EXCLUDE_FROM_ALL)
    else()
        find_package(guikit REQUIRED)
    endif()
endif()

set(GUIKIT_SDL_HAS_TEXT OFF)
if (TARGET guikit::text)
    set(GUIKIT_SDL_HAS_TEXT ON)
else()
    find_package(guikit_text QUIET)
    if (TARGET guikit::text)
        set(GUIKIT_SDL_HAS_TEXT ON)
    endif()
endif()

function(_guikit_sdl_add_bundled_sdl)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_TESTS OFF)
    set(SDL_EXAMPLES OFF)
    set(SDL_INSTALL ${GUIKIT_SDL_INSTALL})
    set(SDL_INSTALL_CPACK OFF)
    set(SDL_INSTALL_DOCS OFF)
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_AUDIO OFF)
    set(SDL_GPU OFF)
    set(SDL_RENDER_GPU OFF)
    set(SDL_CAMERA OFF)
    set(SDL_JOYSTICK OFF)
    set(SDL_HAPTIC OFF)
    set(SDL_HIDAPI OFF)
    set(SDL_POWER OFF)
    set(SDL_SENSOR OFF)
    set(SDL_DIALOG OFF)
    set(SDL_TRAY OFF)
    set(SDL_X11_XTEST OFF)
    if (GUIKIT_SDL_INSTALL)
        add_subdirectory(third_party/SDL)
    else()
        add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
    endif()
endfunction()

# ==============================================================================
# 1. Module: guikit::backend_thorvg
# ==============================================================================
set(GUIKIT_SDL_BUNDLED_THORVG OFF)
if (GUIKIT_BUILD_BACKEND_THORVG)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg/inc/thorvg.h")
        set(GUIKIT_SDL_BUNDLED_THORVG ON)
        include(cmake/ThorVG.cmake)
    else()
        find_package(thorvg REQUIRED)
    endif()

    add_library(guikit_backend_thorvg STATIC
        src/guikit/backend/thor_canvas.cpp
    )
    add_library(guikit::backend_thorvg ALIAS guikit_backend_thorvg)
    set_target_properties(guikit_backend_thorvg PROPERTIES EXPORT_NAME backend_thorvg)

    target_compile_features(guikit_backend_thorvg PUBLIC cxx_std_20)
    target_include_directories(guikit_backend_thorvg PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(guikit_backend_thorvg PUBLIC GUIKIT_HAS_BACKEND_THORVG=1)
    if (GUIKIT_SDL_HAS_TEXT AND TARGET guikit::text)
        target_link_libraries(guikit_backend_thorvg PUBLIC guikit::text)
    else()
        target_compile_definitions(guikit_backend_thorvg PUBLIC GUIKIT_HAS_TEXT_ENGINE=0 GUIKIT_HAS_FREETYPE=0)
    endif()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg/src/loaders/png/tvgLodePng.h")
        target_include_directories(guikit_backend_thorvg SYSTEM PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thorvg/src/loaders/png"
        )
    endif()
    target_link_libraries(guikit_backend_thorvg PUBLIC guikit::core PRIVATE thorvg)
    if (NOT EMSCRIPTEN)
        target_link_libraries(guikit_backend_thorvg PUBLIC Threads::Threads)
    endif()
    if (COMMAND keel_enforce_guidelines)
        keel_enforce_guidelines(guikit_backend_thorvg)
    endif()
endif()

# ==============================================================================
# 2. Module: guikit::backend_sdl3
# ==============================================================================
set(GUIKIT_SDL_BUNDLED_SDL3 OFF)
if (GUIKIT_BUILD_BACKEND_SDL3)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
        set(GUIKIT_SDL_BUNDLED_SDL3 ON)
        _guikit_sdl_add_bundled_sdl()
    else()
        find_package(SDL3 REQUIRED CONFIG)
    endif()

    add_library(guikit_backend_sdl3 STATIC
        src/guikit/backend/sdl_window_backend.cpp
        src/guikit/backend/sdl_canvas.cpp
        src/guikit/backend/sdl_gl_shader_engine.cpp
    )
    add_library(guikit::backend_sdl3 ALIAS guikit_backend_sdl3)
    set_target_properties(guikit_backend_sdl3 PROPERTIES EXPORT_NAME backend_sdl3)

    target_compile_features(guikit_backend_sdl3 PUBLIC cxx_std_20)
    target_include_directories(guikit_backend_sdl3 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(guikit_backend_sdl3 PUBLIC GUIKIT_HAS_BACKEND_SDL3=1)
    if (TARGET SDL3::SDL3-static)
        target_link_libraries(guikit_backend_sdl3 PUBLIC guikit::core PRIVATE SDL3::SDL3-static)
    else()
        target_link_libraries(guikit_backend_sdl3 PUBLIC guikit::core PRIVATE SDL3::SDL3)
    endif()
    if (COMMAND keel_enforce_guidelines)
        keel_enforce_guidelines(guikit_backend_sdl3
            HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
            STANDALONE_HEADERS ${GUIKIT_SDL_BUILD_TESTS}
            TEST_NAME guikit_sdl_header_purity_scan
        )
    endif()
endif()

# ==============================================================================
# 3. Umbrella Target: guikit::sdl (INTERFACE)
# ==============================================================================
add_library(guikit_sdl INTERFACE)
add_library(guikit::sdl ALIAS guikit_sdl)
set_target_properties(guikit_sdl PROPERTIES EXPORT_NAME sdl)

target_include_directories(guikit_sdl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(guikit_sdl INTERFACE guikit::core)
if (TARGET guikit::backend_thorvg)
    target_link_libraries(guikit_sdl INTERFACE guikit::backend_thorvg)
endif()
if (TARGET guikit::backend_sdl3)
    target_link_libraries(guikit_sdl INTERFACE guikit::backend_sdl3)
endif()

# --- Minimal Example ---
if (GUIKIT_SDL_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND TARGET guikit::backend_sdl3)
    add_executable(guikit_minimal
        examples/minimal/main.cpp
    )
    target_link_libraries(guikit_minimal PRIVATE guikit::sdl)
    if (COMMAND guikit_enforce_guidelines)
        guikit_enforce_guidelines(guikit_minimal)
    endif()
endif()

# --- CMake Install Rules ---
if (GUIKIT_SDL_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(_GUIKIT_SDL_EXPORT_TARGETS guikit_sdl)
    if (TARGET guikit_backend_thorvg)
        list(APPEND _GUIKIT_SDL_EXPORT_TARGETS guikit_backend_thorvg)
        if (GUIKIT_SDL_BUNDLED_THORVG AND TARGET thorvg)
            list(APPEND _GUIKIT_SDL_EXPORT_TARGETS thorvg)
        endif()
    endif()
    if (TARGET guikit_backend_sdl3)
        list(APPEND _GUIKIT_SDL_EXPORT_TARGETS guikit_backend_sdl3)
    endif()

    install(TARGETS ${_GUIKIT_SDL_EXPORT_TARGETS}
        EXPORT guikitSdlTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/guikit"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(EXPORT guikitSdlTargets
        FILE guikitSdlTargets.cmake
        NAMESPACE guikit::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_sdl"
    )
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/guikit_sdlConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_sdlConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_sdl"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_sdlConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_sdlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/guikit_sdlConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/guikit_sdl"
    )
endif()

# --- Unit Tests ---
if (GUIKIT_SDL_BUILD_TESTS)
    add_executable(test_sdl
        tests/test_sdl.cpp
    )
    target_link_libraries(test_sdl PRIVATE guikit::sdl)
    if (TARGET thorvg)
        target_link_libraries(test_sdl PRIVATE thorvg)
    endif()
    if (TARGET SDL3::SDL3-static)
        target_link_libraries(test_sdl PRIVATE SDL3::SDL3-static)
    elseif (TARGET SDL3::SDL3)
        target_link_libraries(test_sdl PRIVATE SDL3::SDL3)
    endif()
    if (COMMAND guikit_enforce_guidelines)
        guikit_enforce_guidelines(test_sdl ALLOW_HEADERS "SDL3/SDL.h")
    endif()
    add_test(NAME sdl COMMAND test_sdl)
endif()

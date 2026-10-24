# SPDX-License-Identifier: GPL-3.0-or-later
# MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

cmake_minimum_required(VERSION 3.25)
# VERSION is the only place the version is written (CMake, the settings app's csproj and
# the installer all read it).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION MACWIN_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
project(MacWin VERSION ${MACWIN_VERSION} LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The x86 preset builds into the x64 bin folder (MACWIN_BIN_DIR), so MacWin.exe finds
# MacWinHook32.dll / MacWinHelper32.exe next to itself.
if(MACWIN_BIN_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MACWIN_BIN_DIR})
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(MACWIN_BITS 64)
else()
  set(MACWIN_BITS 32)
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
# Test build: its own shared-object names and log (see src/common/names.h).
option(MACWIN_DEV_NAMES "Test build that shares no objects with an installed MacWin" OFF)
if(MACWIN_DEV_NAMES)
  # Test builds also get the hook DLL's OutputDebugString messages (DebugView).
  add_compile_definitions(MACWIN_DEV_NAMES MACWIN_DEBUG_LOG)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
                    -Wno-cast-function-type)
# Everything is linked statically: the hook DLL is loaded into other programs and
# must never drag libstdc++/libgcc/winpthread DLLs (or their versions) in with it.
add_link_options(-static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)

configure_file(src/common/version.h.in ${CMAKE_BINARY_DIR}/gen/version.h @ONLY)
include_directories(${CMAKE_BINARY_DIR}/gen)

# Version resource (author, copyright, version) for every shipped binary.
function(macwin_version_resource target description filename filetype)
  set(MW_RC_NAME ${target})
  set(MW_RC_DESCRIPTION ${description})
  set(MW_RC_FILENAME ${filename})
  set(MW_RC_FILETYPE ${filetype})
  configure_file(${CMAKE_SOURCE_DIR}/src/common/version.rc.in
                 ${CMAKE_BINARY_DIR}/gen/${target}_version.rc @ONLY)
  target_sources(${target} PRIVATE ${CMAKE_BINARY_DIR}/gen/${target}_version.rc)
endfunction()

# --- MinHook (BSD-2, third_party/minhook) ---
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)
target_compile_options(minhook PRIVATE -w)

# --- Hook DLL: loaded into every GUI process, kept as small as possible ---
add_library(MacWinHook SHARED
  src/hook/dllmain.cpp
  src/hook/loader.cpp
  src/hook/hooks.cpp
  src/hook/capture.cpp
  src/hook/ipc_client.cpp
  src/hook/effects/genie_hooks.cpp
  src/hook/effects/corners.cpp)
set_target_properties(MacWinHook PROPERTIES OUTPUT_NAME MacWinHook${MACWIN_BITS} PREFIX "")
target_include_directories(MacWinHook PRIVATE src)
target_compile_options(MacWinHook PRIVATE -fno-exceptions -fno-rtti)
# --kill-at: undecorated stdcall exports on x86 (MacWinCbtProc, not _MacWinCbtProc@12).
target_link_options(MacWinHook PRIVATE -Wl,--kill-at)
target_link_libraries(MacWinHook PRIVATE minhook dwmapi gdi32 user32)
macwin_version_resource(MacWinHook "MacWin hook (${MACWIN_BITS}-bit)" MacWinHook${MACWIN_BITS}.dll VFT_DLL)

# --- Host (64-bit only: owns the renderer, the tray icon and the hooks) ---
if(MACWIN_BITS EQUAL 64)
  file(READ src/host/render/genie.hlsl MACWIN_GENIE_HLSL)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/host/render/genie.hlsl)
  configure_file(src/host/render/genie_hlsl.h.in ${CMAKE_BINARY_DIR}/gen/genie_hlsl.h @ONLY)
  configure_file(src/host/host.rc.in ${CMAKE_BINARY_DIR}/gen/host.rc @ONLY)
  # The translations are embedded as RCDATA: a changed language file rebuilds the resource.
  file(GLOB MACWIN_I18N CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/i18n/*.json)
  set_source_files_properties(${CMAKE_BINARY_DIR}/gen/host.rc PROPERTIES
    OBJECT_DEPENDS "${MACWIN_I18N};${CMAKE_SOURCE_DIR}/src/host/MacWin.manifest")

  add_executable(MacWin WIN32
    src/host/main.cpp
    src/host/elevation.cpp
    ${CMAKE_BINARY_DIR}/gen/host.rc
    src/host/app_icon.cpp
    src/host/crash.cpp
    src/host/i18n.cpp
    src/host/log.cpp
    src/host/json.cpp
    src/host/settings_store.cpp
    src/host/updater.cpp
    src/host/injector.cpp
    src/host/helper32.cpp
    src/host/ipc_server.cpp
    src/host/taskbar_locator.cpp
    src/host/snapshot_cache.cpp
    src/host/win_events.cpp
    src/host/render/device.cpp
    src/host/render/genie_effect.cpp
    src/host/widgets/canvas.cpp
    src/host/widgets/location.cpp
    src/host/widgets/system_stats.cpp
    src/host/widgets/weather.cpp
    src/host/widgets/widget_config.cpp
    src/host/widgets/widget_draw.cpp
    src/host/widgets/widget_host.cpp
    src/host/widgets/widget_layout.cpp
    src/host/widgets/widget_snapshot.cpp)
  target_include_directories(MacWin PRIVATE src ${CMAKE_BINARY_DIR}/gen)
  target_link_options(MacWin PRIVATE -municode)
  target_link_libraries(MacWin PRIVATE d3d11 dxgi dwmapi shcore ole32 oleaut32 uuid
                        shell32 user32 gdi32 advapi32 comctl32 winhttp bcrypt wtsapi32
                        d2d1 dwrite windowscodecs runtimeobject)
  add_dependencies(MacWin MacWinHook)
  macwin_version_resource(MacWin "MacWin - macOS-style window animations" MacWin.exe VFT_APP)

  # Settings app (C# / WPF, .NET 10), published next to MacWin.exe.
  find_program(MACWIN_DOTNET dotnet)
  if(MACWIN_DOTNET)
    add_custom_target(MacWinSettings ALL
      COMMAND ${MACWIN_DOTNET} publish ${CMAKE_SOURCE_DIR}/settings-app/MacWin.Settings.csproj
              -c Release -o ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --nologo -v q
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/settings-app
      COMMENT "Building MacWin.Settings (WPF)"
      VERBATIM)
  endif()

  # Installer (Inno Setup 6): cmake --build --preset x64 --target MacWinSetup
  find_program(MACWIN_ISCC NAMES iscc ISCC PATHS "C:/Program Files (x86)/Inno Setup 6")
  if(MACWIN_ISCC)
    add_custom_target(MacWinSetup
      # The installer ships the 32-bit files too: (re)build the x86 preset first.
      COMMAND ${CMAKE_COMMAND} --preset x86
      COMMAND ${CMAKE_COMMAND} --build --preset x86
      COMMAND ${MACWIN_ISCC} /Q /DAppVersion=${PROJECT_VERSION} ${CMAKE_SOURCE_DIR}/installer/MacWin.iss
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Building MacWinSetup.exe"
      VERBATIM)
    add_dependencies(MacWinSetup MacWin MacWinHook MacWinTestWindow)
    if(TARGET MacWinSettings)
      add_dependencies(MacWinSetup MacWinSettings)
    endif()
  endif()

  # Development helper: a window that minimizes / restores itself (--cycle N).
  add_executable(MacWinTestWindow WIN32 tools/test_window.cpp tools/corner_test.cpp)
  target_link_options(MacWinTestWindow PRIVATE -municode)
  target_link_libraries(MacWinTestWindow PRIVATE user32 gdi32 dwmapi)
  macwin_version_resource(MacWinTestWindow "MacWin test window" MacWinTestWindow.exe VFT_APP)
else()
  # --- 32-bit side: MacWinHook32.dll (above) + the helper that gets it into 32-bit
  # programs. Rendering always stays in the 64-bit MacWin.exe.
  add_executable(MacWinHelper32 WIN32 src/helper32/main.cpp)
  target_include_directories(MacWinHelper32 PRIVATE src)
  target_link_options(MacWinHelper32 PRIVATE -municode)
  target_link_libraries(MacWinHelper32 PRIVATE user32 advapi32)
  add_dependencies(MacWinHelper32 MacWinHook)
  macwin_version_resource(MacWinHelper32 "MacWin helper for 32-bit programs" MacWinHelper32.exe VFT_APP)

  add_executable(MacWinTestWindow32 WIN32 tools/test_window.cpp tools/corner_test.cpp)
  target_link_options(MacWinTestWindow32 PRIVATE -municode)
  target_link_libraries(MacWinTestWindow32 PRIVATE user32 gdi32 dwmapi)
  macwin_version_resource(MacWinTestWindow32 "MacWin test window (32-bit)" MacWinTestWindow32.exe VFT_APP)
endif()

# --- Tests: ctest --test-dir build/x64 ---
if(MACWIN_BITS EQUAL 64)
  enable_testing()
  add_executable(MacWinTests tests/unit_tests.cpp src/host/json.cpp src/host/updater.cpp
                 src/host/i18n.cpp src/host/log.cpp src/host/widgets/location.cpp src/host/widgets/weather.cpp
                 src/host/widgets/widget_config.cpp src/host/widgets/widget_layout.cpp
                 src/host/render/genie_effect.cpp)
  target_include_directories(MacWinTests PRIVATE src ${CMAKE_BINARY_DIR}/gen)
  target_link_options(MacWinTests PRIVATE -municode)
  target_link_libraries(MacWinTests PRIVATE winhttp bcrypt shell32 ole32 runtimeobject shcore)
  set_target_properties(MacWinTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME unit COMMAND MacWinTests)
  add_test(NAME i18n COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/tools/check_i18n.cmake)
endif()

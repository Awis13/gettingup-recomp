cmake_minimum_required(VERSION 3.25)
project(gettingup_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

# iPad / Apple TV: the runtime's dylibs go into the app's Frameworks.
if(CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS)$")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
  set(CMAKE_INSTALL_RPATH "@executable_path/Frameworks")
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# x86recomp's runtime and HLE modules are shared libraries: rebuilding one
# does not relink the game.
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)

# x86recomp: the lifter, x87 model and runtime (pinned checkout in vendor/,
# with this project's loader changes on its `gettingup` branch).
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/x86recomp"
    CACHE PATH "x86recomp source tree")
set(DXVK_NATIVE_DIR "" CACHE PATH "dxvk-native install prefix (opt-in d3d9 backend)")

enable_testing()
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

# The game: runtime + HLE + lifted code (generated/, produced by
# recomp_lift from the user's own install; see README.md).
recomp_add_lifted(gu_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET gu_lifted)
  add_executable(gettingup src/main.cpp src/widescreen.cpp src/polish.cpp src/pad_glyphs.cpp $<TARGET_OBJECTS:gu_lifted>)
  if(RECOMP_IOS)
    # The .app with its Info.plist; tools/ios_bundle.sh adds the game files,
    # the runtime dylibs (Frameworks/), the icon, and signs it.
    if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
      set(GU_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/port/ios/Info-tvOS.plist")
      target_sources(gettingup PRIVATE src/apple_tv.cpp)
      target_link_libraries(gettingup PRIVATE "-framework CoreFoundation")
    else()
      set(GU_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/port/ios/Info.plist")
    endif()
    set_target_properties(gettingup PROPERTIES
      MACOSX_BUNDLE ON
      MACOSX_BUNDLE_INFO_PLIST "${GU_INFO_PLIST}"
      INSTALL_RPATH "@executable_path/Frameworks"
      BUILD_WITH_INSTALL_RPATH ON)
  endif()
  target_link_libraries(gettingup PRIVATE recomp_runtime recomp_mss32 recomp_bink
                        recomp_dinput8 recomp_xinput recomp_d3d8 recomp_dbghelp)
  set_target_properties(gettingup PROPERTIES ENABLE_EXPORTS ON)  # names in fault backtraces
endif()

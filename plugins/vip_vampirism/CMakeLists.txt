# vip_vampirism — Plugify C++ plugin: port of the Pisex VIP module "[VIP] Vampirism" (VIP_Vampirism).
#
# Pure s2sdk: no s2guard and no pisexcompat. All safety rules (the map gate, the slot/controller
# checks, the event-field reads and the health write) live in src/plugin.cpp; the engine-free half
# (slot range, self-damage test, the heal arithmetic) is src/vampirism_core.hpp, shared with the
# unit test.
#
#   plugify-vip_vampirism  the plugin shared library (entry "bin/vip_vampirism" -> bin/libvip_vampirism.so)
#   vip_vampirism_tests    engine-free unit tests: the slot range, the self-damage test and the heal
#                          arithmetic with its guards (build/tests/vip_vampirism_tests)

set(VIP_VAMPIRISM_VERSION "1.2.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
# In the monorepo plugins/vip is added first ("vip" < "vip_vampirism"), so vip_bindings already
# exists; a standalone exported repo only carries plugins/vip/include/ headers + vip.cpp and has no
# plugins/vip/CMakeLists.txt - define vip_bindings here when absent.
if(NOT TARGET vip_bindings)
    add_library(vip_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()

add_library(plugify-vip_vampirism SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(plugify-vip_vampirism PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated vip/s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-vip_vampirism SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-vip_vampirism PRIVATE
        plugify_sdk
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-vip_vampirism PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-vip_vampirism PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_vampirism"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-vip_vampirism PROPERTIES OUTPUT_NAME vip_vampirism)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-vip_vampirism PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vip_vampirism.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vip_vampirism.pplugin"
)

# --- tests ---------------------------------------------------------------------------------------
# Engine-free logic tests (no s2sdk binding, no pisexcompat): the header is compiled in directly
# and the test touches no engine symbol. Run by ./test.sh (build/tests).
add_executable(vip_vampirism_tests tests/vip_vampirism_tests.cpp)
target_include_directories(vip_vampirism_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(vip_vampirism_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_vampirism_tests PRIVATE _GLIBCXX_USE_CXX11_ABI=1)
target_link_libraries(vip_vampirism_tests PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(vip_vampirism_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

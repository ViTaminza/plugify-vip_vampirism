# vip_vampirism — Plugify C++ plugin: port of the Pisex VIP module "[VIP] Vampirism".
#
#   plugify-vip_vampirism  the plugin shared library (entry "bin/vip_vampirism" -> bin/libvip_vampirism.so)

set(VIP_VAMPIRISM_VERSION "1.0.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_*   (s2sdk_bindings)
#   __vip_*     (vip consumer bindings, generated into plugins/vip/include/external/plugify/include)
if(NOT TARGET vip_bindings)
    message(FATAL_ERROR "vip_vampirism needs the generated vip consumer bindings (plugins/vip/CMakeLists.txt, target vip_bindings)")
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
        pisexcompat
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

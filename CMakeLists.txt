cmake_minimum_required(VERSION 3.16)
project(Aquamarine VERSION 1.0.0 LANGUAGES C)

add_library(Aquamarine STATIC
    aq_blend.c
    aq_color.c
    aq_draw.c
    aq_gradient.c
    aq_path.c
    aq_render3d.c
    aq_surface.c
    aq_text.c
    aq_transform.c
    ../libunicode/core.c)
add_library(Aquamarine::Aquamarine ALIAS Aquamarine)

target_compile_features(Aquamarine PUBLIC c_std_11)
target_include_directories(Aquamarine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../libunicode>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../rinresource/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../RinOS-SDK/include>
    $<INSTALL_INTERFACE:include/aquamarine>
    $<INSTALL_INTERFACE:include>)
if(NOT MSVC)
    target_compile_options(Aquamarine PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS Aquamarine EXPORT AquamarineTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
        aq_blend.h aq_clip.h aq_color.h aq_cpp.h aq_draw.h aq_font_8x16.h
        aq_gradient.h aq_math.h aq_math3d.h aq_path.h aq_render3d.h
        aq_surface.h aq_text.h aq_transform.h aq_types.h aquamarine.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aquamarine)
install(EXPORT AquamarineTargets
        FILE AquamarineTargets.cmake
        NAMESPACE Aquamarine::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aquamarine)

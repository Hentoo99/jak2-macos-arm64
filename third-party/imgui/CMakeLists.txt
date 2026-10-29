set(IMGUI_SOURCES
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        imgui_impl_sdl3.cpp
        imgui_stdlib.cpp
        imgui_style.cpp)
if(NOT OG_TARGET_IOS)
        list(APPEND IMGUI_SOURCES imgui_impl_opengl3.cpp)
endif()
add_library(imgui ${IMGUI_SOURCES})

target_link_libraries(imgui SDL3::SDL3)
if(OPENGOAL_GRAPHICS_BACKEND STREQUAL "GLES")
    # The OpenGL3 backend draws with ANGLE's OpenGL ES 3.0 (see game/CMakeLists.txt).
    target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_ES3)
    target_include_directories(imgui PRIVATE "${OPENGOAL_ANGLE_DIR}/include")
    target_link_libraries(imgui og_angle)
endif()

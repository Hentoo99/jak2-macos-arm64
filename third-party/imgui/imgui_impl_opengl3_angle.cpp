// The OpenGL3 backend on iOS with ANGLE (OPENGOAL_GRAPHICS_BACKEND=GLES).
// imgui_impl_opengl3.cpp picks Apple's deprecated <OpenGLES/ES3/gl.h> on iOS; ANGLE provides
// OpenGL ES 3.0 through the standard <GLES3/gl3.h> instead, so hide the iOS target from it.
#include <TargetConditionals.h>
#undef TARGET_OS_IOS
#define TARGET_OS_IOS 0
#undef TARGET_OS_TV
#define TARGET_OS_TV 0
#ifndef IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_ES3
#endif
#include "imgui_impl_opengl3.cpp"

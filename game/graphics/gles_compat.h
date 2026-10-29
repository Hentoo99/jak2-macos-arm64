#pragma once

/*!
 * @file gles_compat.h
 * Run the desktop OpenGL renderer on an OpenGL ES 3.0 context (OPENGOAL_GRAPHICS_BACKEND=GLES).
 *
 * Force-included into every graphics source by game/CMakeLists.txt: it maps the few desktop-only
 * entry points and enums the renderer uses onto their ES 3.0 equivalents.  Adapted from ottsel
 * (game/graphics/ottsel_gles_compat.h, ISC, (c) 2026 Chris Sotraidis).
 */

#if defined(__cplusplus)
#include <string>

#include "third-party/glad/include/glad/glad.h"

// Time-of-day palettes are 1D textures on desktop; ES 3.0 has none, so they become 2D textures
// (scripts/translate_gl_shaders_es.py rewrites the shaders to match).
#undef GL_TEXTURE_1D
#define GL_TEXTURE_1D GL_TEXTURE_2D
// Palettes wider than OG_GLES_1D_ROW texels (the 8192-entry time-of-day tables) are folded into
// rows, because ES 3.0 only guarantees 2048-texel textures; shaders read texel i at (i % row, i /
// row).
#define OG_GLES_1D_ROW 1024
#undef glTexImage1D
#define glTexImage1D(target, level, ifmt, w, border, fmt, type, data) \
  og_gles_tex_image_1d(level, ifmt, w, border, fmt, type, data)
#undef glTexSubImage1D
#define glTexSubImage1D(target, level, x, w, fmt, type, data) \
  og_gles_tex_sub_image_1d(level, x, w, fmt, type, data)
// Only used to allocate palettes with no data, where the byte order does not matter.
#undef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8 GL_UNSIGNED_BYTE
// Byte order of GL_RGBA + 8_8_8_8_REV equals GL_UNSIGNED_BYTE on little-endian hosts.
#undef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV GL_UNSIGNED_BYTE
// ES 3.0 always restarts primitives at the maximum index; OpenGOAL only ever uses UINT32_MAX.
#undef glPrimitiveRestartIndex
#define glPrimitiveRestartIndex(index) ((void)(index))
#undef GL_PRIMITIVE_RESTART
#define GL_PRIMITIVE_RESTART 0x8D69 /* GL_PRIMITIVE_RESTART_FIXED_INDEX */
#undef glClearDepth
#define glClearDepth(d) glad_glClearDepthf((GLfloat)(d))
#undef glFramebufferTexture
#define glFramebufferTexture(target, attachment, tex, level) \
  glad_glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, tex, level)
#undef glColorMaski
#define glColorMaski(buf, r, g, b, a) glad_glColorMask(r, g, b, a)
#undef glMultiDrawElements
#define glMultiDrawElements og_gles_multi_draw_elements
#undef glGetTexLevelParameteriv
#define glGetTexLevelParameteriv og_gles_get_tex_level_parameteriv
#undef glTexImage2D
#define glTexImage2D og_gles_tex_image_2d
#undef glBindTexture
#define glBindTexture og_gles_bind_texture
#undef glActiveTexture
#define glActiveTexture og_gles_active_texture
// Debug-only desktop features.
#undef glPolygonMode
#define glPolygonMode(face, mode) ((void)0)
#undef glGetTexImage
#define glGetTexImage(target, level, fmt, type, pixels) ((void)0)
#undef glDebugMessageCallback
#define glDebugMessageCallback(cb, param) ((void)0)
#undef glDebugMessageControl
#define glDebugMessageControl(a, b, c, d, e, f) ((void)0)
#undef glTexImage2DMultisample
#define glTexImage2DMultisample(a, b, c, d, e, f) ((void)0)

// ANGLE's Metal backend drops primitive restart when an indexed strip reads a vertex buffer that
// is not GL_STATIC_DRAW (streamed sprites, effects and debug geometry join into long triangles).
// Those draws are turned into triangle lists here; see og_gles_draw_elements.
#undef glBufferData
#define glBufferData og_gles_buffer_data
#undef glBufferSubData
#define glBufferSubData og_gles_buffer_sub_data
#undef glDeleteBuffers
#define glDeleteBuffers og_gles_delete_buffers
#undef glDrawElements
#define glDrawElements og_gles_draw_elements

void og_gles_buffer_data(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
void og_gles_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
void og_gles_delete_buffers(GLsizei n, const GLuint* buffers);
void og_gles_draw_elements(GLenum mode, GLsizei count, GLenum type, const void* offset);
void og_gles_multi_draw_elements(GLenum mode,
                                 const GLsizei* count,
                                 GLenum type,
                                 const void* const* indices,
                                 GLsizei drawcount);
void og_gles_get_tex_level_parameteriv(GLenum target, GLint level, GLenum pname, GLint* out);
void og_gles_tex_image_2d(GLenum target,
                          GLint level,
                          GLint ifmt,
                          GLsizei w,
                          GLsizei h,
                          GLint border,
                          GLenum fmt,
                          GLenum type,
                          const void* data);
void og_gles_bind_texture(GLenum target, GLuint tex);
void og_gles_active_texture(GLenum unit);
void og_gles_tex_image_1d(GLint level,
                          GLint ifmt,
                          GLsizei w,
                          GLint border,
                          GLenum fmt,
                          GLenum type,
                          const void* data);
void og_gles_tex_sub_image_1d(GLint level,
                              GLint x,
                              GLsizei w,
                              GLenum fmt,
                              GLenum type,
                              const void* data);

//! Load the ES 3.0 entry points glad skips (it only loads desktop GL <= 3.0 for an ES 3.0 context).
void og_gles_load_missing(void* (*get_proc)(const char*));
#endif

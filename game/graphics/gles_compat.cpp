// OpenGL ES 3.0 glue for the OpenGL renderer (see gles_compat.h).  Adapted from ottsel
// (game/graphics/ottsel_gles.cpp, ISC, (c) 2026 Chris Sotraidis).
#include "game/graphics/gles_compat.h"

#include <unordered_map>
#include <utility>

namespace {
GLenum g_active_unit = GL_TEXTURE0;
std::unordered_map<GLenum, GLuint> g_bound_2d;                      // texture unit -> bound 2D id
std::unordered_map<GLuint, std::pair<GLint, GLint>> g_level0_size;  // 2D id -> level-0 size
}  // namespace

void og_gles_multi_draw_elements(GLenum mode,
                                 const GLsizei* count,
                                 GLenum type,
                                 const void* const* indices,
                                 GLsizei drawcount) {
  for (GLsizei i = 0; i < drawcount; i++) {
    glad_glDrawElements(mode, count[i], type, indices[i]);
  }
}

void og_gles_active_texture(GLenum unit) {
  g_active_unit = unit;
  glad_glActiveTexture(unit);
}

void og_gles_bind_texture(GLenum target, GLuint tex) {
  if (target == GL_TEXTURE_2D) {
    g_bound_2d[g_active_unit] = tex;
  }
  glad_glBindTexture(target, tex);
}

void og_gles_tex_image_2d(GLenum target,
                          GLint level,
                          GLint ifmt,
                          GLsizei w,
                          GLsizei h,
                          GLint border,
                          GLenum fmt,
                          GLenum type,
                          const void* data) {
  if (target == GL_TEXTURE_2D && level == 0) {
    g_level0_size[g_bound_2d[g_active_unit]] = {w, h};
  }
  glad_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
}

// ES 3.0 has no glGetTexLevelParameteriv: answer from the sizes recorded at upload.
void og_gles_get_tex_level_parameteriv(GLenum target, GLint level, GLenum pname, GLint* out) {
  auto it = g_level0_size.find(g_bound_2d[g_active_unit]);
  GLint w = it == g_level0_size.end() ? 0 : it->second.first;
  GLint h = it == g_level0_size.end() ? 0 : it->second.second;
  for (GLint i = 0; i < level; i++) {
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
  }
  (void)target;
  *out = pname == GL_TEXTURE_WIDTH ? w : pname == GL_TEXTURE_HEIGHT ? h : 0;
}

// All 1D textures are RGBA8 palettes (see OG_GLES_1D_ROW in gles_compat.h).
void og_gles_tex_image_1d(GLint level,
                          GLint ifmt,
                          GLsizei w,
                          GLint border,
                          GLenum fmt,
                          GLenum type,
                          const void* data) {
  const GLsizei cols = w < OG_GLES_1D_ROW ? w : OG_GLES_1D_ROW;
  const GLsizei rows = (w + OG_GLES_1D_ROW - 1) / OG_GLES_1D_ROW;
  og_gles_tex_image_2d(GL_TEXTURE_2D, level, ifmt, cols, rows, border, fmt, type, nullptr);
  if (data) {
    og_gles_tex_sub_image_1d(level, 0, w, fmt, type, data);
  }
}

void og_gles_tex_sub_image_1d(GLint level,
                              GLint x,
                              GLsizei w,
                              GLenum fmt,
                              GLenum type,
                              const void* data) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  // Whole rows in one call: each call costs ANGLE a staging buffer, and a time-of-day palette
  // (8192 texels) is eight rows.
  if (x % OG_GLES_1D_ROW == 0 && w >= OG_GLES_1D_ROW) {
    const GLsizei rows = w / OG_GLES_1D_ROW;
    glad_glTexSubImage2D(GL_TEXTURE_2D, level, 0, x / OG_GLES_1D_ROW, OG_GLES_1D_ROW, rows, fmt,
                         type, bytes);
    bytes += rows * OG_GLES_1D_ROW * 4;
    x += rows * OG_GLES_1D_ROW;
    w -= rows * OG_GLES_1D_ROW;
  }
  while (w > 0) {
    const GLint col = x % OG_GLES_1D_ROW;
    const GLsizei n = w < OG_GLES_1D_ROW - col ? w : OG_GLES_1D_ROW - col;
    glad_glTexSubImage2D(GL_TEXTURE_2D, level, col, x / OG_GLES_1D_ROW, n, 1, fmt, type, bytes);
    bytes += n * 4;
    x += n;
    w -= n;
  }
}

void og_gles_load_missing(void* (*get_proc)(const char*)) {
#define OG_GLES_LOAD(name, type) \
  if (!glad_##name)              \
  glad_##name = (type)get_proc(#name)
  OG_GLES_LOAD(glClearDepthf, PFNGLCLEARDEPTHFPROC);
  OG_GLES_LOAD(glUniformBlockBinding, PFNGLUNIFORMBLOCKBINDINGPROC);
  OG_GLES_LOAD(glGetUniformBlockIndex, PFNGLGETUNIFORMBLOCKINDEXPROC);
  OG_GLES_LOAD(glDrawArraysInstanced, PFNGLDRAWARRAYSINSTANCEDPROC);
  OG_GLES_LOAD(glDrawElementsInstanced, PFNGLDRAWELEMENTSINSTANCEDPROC);
  OG_GLES_LOAD(glVertexAttribDivisor, PFNGLVERTEXATTRIBDIVISORPROC);
  OG_GLES_LOAD(glCopyBufferSubData, PFNGLCOPYBUFFERSUBDATAPROC);
  OG_GLES_LOAD(glGetActiveUniformBlockiv, PFNGLGETACTIVEUNIFORMBLOCKIVPROC);
  OG_GLES_LOAD(glTexStorage2D, PFNGLTEXSTORAGE2DPROC);
#undef OG_GLES_LOAD
}

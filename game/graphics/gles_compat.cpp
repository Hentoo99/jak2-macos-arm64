// OpenGL ES 3.0 glue for the OpenGL renderer (see gles_compat.h).  Adapted from ottsel
// (game/graphics/ottsel_gles.cpp, ISC, (c) 2026 Chris Sotraidis).
#include "game/graphics/gles_compat.h"

#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace {
GLenum g_active_unit = GL_TEXTURE0;
std::unordered_map<GLenum, GLuint> g_bound_2d;                      // texture unit -> bound 2D id
std::unordered_map<GLuint, std::pair<GLint, GLint>> g_level0_size;  // 2D id -> level-0 size

std::unordered_map<GLuint, GLenum> g_array_usage;  // vertex buffer id -> usage of its last upload

//! A non-static index buffer's contents, and the same strips as a triangle list (made on demand).
struct StripIndices {
  std::vector<u32> indices;
  bool converted = false;
  GLuint triangles = 0;  // buffer holding the triangle list
  std::vector<u32>
      start;  // start[i]: first triangle-list index made from the strips at indices[i:]
};
std::unordered_map<GLuint, StripIndices> g_strips;  // index buffer id -> contents

GLuint bound_buffer(GLenum binding) {
  GLint id = 0;
  glad_glGetIntegerv(binding, &id);
  return (GLuint)id;
}

void convert_strips(StripIndices& s) {
  std::vector<u32> tris;
  s.start.resize(s.indices.size() + 1);
  u32 run = 0;  // vertices since the last restart
  for (size_t i = 0; i < s.indices.size(); i++) {
    s.start[i] = (u32)tris.size();
    const u32 v = s.indices[i];
    if (v == UINT32_MAX) {
      run = 0;
      continue;
    }
    if (++run >= 3) {
      // keep the strip's winding: odd triangles swap their first two vertices
      const u32 a = s.indices[i - 2], b = s.indices[i - 1];
      if (run % 2) {
        tris.insert(tris.end(), {a, b, v});
      } else {
        tris.insert(tris.end(), {b, a, v});
      }
    }
  }
  s.start.back() = (u32)tris.size();
  if (!s.triangles) {
    glad_glGenBuffers(1, &s.triangles);
  }
  const GLuint strips = bound_buffer(GL_ELEMENT_ARRAY_BUFFER_BINDING);
  glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.triangles);
  glad_glBufferData(GL_ELEMENT_ARRAY_BUFFER, tris.size() * sizeof(u32), tris.data(),
                    GL_STREAM_DRAW);
  glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, strips);
  s.converted = true;
}
}  // namespace

void og_gles_buffer_data(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
  glad_glBufferData(target, size, data, usage);
  if (target == GL_ARRAY_BUFFER) {
    g_array_usage[bound_buffer(GL_ARRAY_BUFFER_BINDING)] = usage;
  } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
    const GLuint id = bound_buffer(GL_ELEMENT_ARRAY_BUFFER_BINDING);
    if (usage == GL_STATIC_DRAW) {
      g_strips.erase(id);  // static draws restart correctly: no need for a copy
    } else {
      auto& s = g_strips[id];
      s.indices.resize(size / sizeof(u32));
      if (data) {
        memcpy(s.indices.data(), data, s.indices.size() * sizeof(u32));
      }
      s.converted = false;
    }
  }
}

void og_gles_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
  glad_glBufferSubData(target, offset, size, data);
  if (target == GL_ELEMENT_ARRAY_BUFFER) {
    auto it = g_strips.find(bound_buffer(GL_ELEMENT_ARRAY_BUFFER_BINDING));
    if (it != g_strips.end() && offset % sizeof(u32) == 0 &&
        (size_t)(offset + size) <= it->second.indices.size() * sizeof(u32)) {
      memcpy((u8*)it->second.indices.data() + offset, data, size);
      it->second.converted = false;
    }
  }
}

void og_gles_delete_buffers(GLsizei n, const GLuint* buffers) {
  for (GLsizei i = 0; i < n; i++) {
    g_array_usage.erase(buffers[i]);
    auto it = g_strips.find(buffers[i]);
    if (it != g_strips.end()) {
      if (it->second.triangles) {
        glad_glDeleteBuffers(1, &it->second.triangles);
      }
      g_strips.erase(it);
    }
  }
  glad_glDeleteBuffers(n, buffers);
}

void og_gles_draw_elements(GLenum mode, GLsizei count, GLenum type, const void* offset) {
  if (mode == GL_TRIANGLE_STRIP && type == GL_UNSIGNED_INT && count > 0) {
    GLint vertices = 0;
    glad_glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &vertices);
    auto usage = g_array_usage.find((GLuint)vertices);
    const GLuint strips = bound_buffer(GL_ELEMENT_ARRAY_BUFFER_BINDING);
    auto it = g_strips.find(strips);
    const size_t first = (size_t)offset / sizeof(u32);
    if (usage != g_array_usage.end() && usage->second != GL_STATIC_DRAW && it != g_strips.end() &&
        first + count <= it->second.indices.size() &&
        (first == 0 || it->second.indices[first - 1] == UINT32_MAX)) {
      auto& s = it->second;
      if (!s.converted) {
        convert_strips(s);
      }
      const u32 begin = s.start[first], end = s.start[first + count];
      glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.triangles);
      glad_glDrawElements(GL_TRIANGLES, (GLsizei)(end - begin), GL_UNSIGNED_INT,
                          (const void*)(size_t)(begin * sizeof(u32)));
      glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, strips);
      return;
    }
  }
  glad_glDrawElements(mode, count, type, offset);
}

void og_gles_multi_draw_elements(GLenum mode,
                                 const GLsizei* count,
                                 GLenum type,
                                 const void* const* indices,
                                 GLsizei drawcount) {
  for (GLsizei i = 0; i < drawcount; i++) {
    og_gles_draw_elements(mode, count[i], type, indices[i]);
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

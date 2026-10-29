#include "ObjectFileV3.h"

#include <cstring>
#include <stdexcept>

#include "common/link_types.h"

#include "fmt/format.h"

namespace goal_aot {
namespace {

constexpr size_t kHeaderSize = 68;

class Cursor {
 public:
  Cursor(std::span<const uint8_t> bytes, size_t pos, size_t end, int seg)
      : m_bytes(bytes), m_pos(pos), m_end(end), m_seg(seg) {}

  bool done() const { return m_pos >= m_end; }

  uint8_t u8() {
    need(1);
    return m_bytes[m_pos++];
  }

  uint32_t u32() {
    need(4);
    uint32_t v;
    memcpy(&v, m_bytes.data() + m_pos, 4);
    m_pos += 4;
    return v;
  }

  std::string name() {
    std::string out;
    while (true) {
      char c = (char)u8();
      if (!c) {
        break;
      }
      if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
        fail("non-printable character in symbol/type name");
      }
      out.push_back(c);
      if (out.size() >= 256) {
        fail("symbol/type name too long");
      }
    }
    if (out.empty()) {
      fail("empty symbol/type name");
    }
    return out;
  }

  [[noreturn]] void fail(const std::string& what) const {
    throw std::runtime_error(fmt::format("v3 object: {} link table: {} (at byte {})",
                                         SEGMENT_NAMES[m_seg], what, m_pos));
  }

 private:
  void need(size_t n) const {
    if (m_pos + n > m_end) {
      fail("truncated record");
    }
  }

  std::span<const uint8_t> m_bytes;
  size_t m_pos, m_end;
  int m_seg;
};

uint32_t read_u32(std::span<const uint8_t> bytes, size_t pos) {
  uint32_t v;
  memcpy(&v, bytes.data() + pos, 4);
  return v;
}

void check_offset(const V3Segment& seg,
                  int seg_id,
                  uint32_t offset,
                  uint32_t width,
                  const char* what) {
  if (offset % 4 || (uint64_t)offset + width > seg.data.size()) {
    throw std::runtime_error(
        fmt::format("v3 object: {} {} at {:#x} is outside the segment ({:#x} bytes)",
                    SEGMENT_NAMES[seg_id], what, offset, seg.data.size()));
  }
}

}  // namespace

V3Object parse_v3_object(std::span<const uint8_t> bytes) {
  if (bytes.size() < kHeaderSize || memcmp(bytes.data(), "GOAL", 4) != 0) {
    throw std::runtime_error("v3 object: missing GOAL header");
  }
  V3Object obj;
  memcpy(&obj.version_major, bytes.data() + 4, 2);
  memcpy(&obj.version_minor, bytes.data() + 6, 2);
  if (read_u32(bytes, 8) != 3 || read_u32(bytes, 12) != N_SEG) {
    throw std::runtime_error("v3 object: unsupported object version or segment count");
  }
  const uint32_t data_start = read_u32(bytes, 64);
  if (data_start < kHeaderSize || data_start > bytes.size()) {
    throw std::runtime_error("v3 object: invalid data start");
  }

  for (int seg_id = 0; seg_id < N_SEG; seg_id++) {
    auto& seg = obj.segments[seg_id];
    // The stored link offsets do not count the 4-byte "GOAL" magic.
    const size_t link_start = 4 + (size_t)read_u32(bytes, 16 + 8 * seg_id);
    const uint32_t link_word = read_u32(bytes, 20 + 8 * seg_id);
    const size_t code_start = data_start + (size_t)read_u32(bytes, 40 + 8 * seg_id);
    const size_t code_size = read_u32(bytes, 44 + 8 * seg_id);
    if (!(link_word & LINK_ARM64_EXECUTABLE_SIZE_FLAG)) {
      throw std::runtime_error("v3 object: missing ARM64 executable-size flag (x86 object?)");
    }
    seg.executable_size = link_word & ~LINK_ARM64_EXECUTABLE_SIZE_FLAG;
    if (code_start + code_size > bytes.size() || seg.executable_size > code_size ||
        seg.executable_size % 4) {
      throw std::runtime_error(
          fmt::format("v3 object: invalid {} segment bounds", SEGMENT_NAMES[seg_id]));
    }
    seg.data.assign(bytes.begin() + code_start, bytes.begin() + code_start + code_size);

    // Link tables are not sized in the header on ARM64, so read up to the
    // LINK_TABLE_END byte without crossing into the segment data.
    Cursor link(bytes, link_start, data_start, seg_id);
    bool terminated = false;
    while (!link.done()) {
      uint8_t kind = link.u8();
      if (kind == LINK_TABLE_END) {
        terminated = true;
        break;
      }
      switch (kind) {
        case LINK_SYMBOL_OFFSET: {
          V3SymbolLink rec;
          rec.name = link.name();
          uint32_t count = link.u32();
          for (uint32_t i = 0; i < count; i++) {
            rec.offsets.push_back(link.u32());
            check_offset(seg, seg_id, rec.offsets.back(), 4, "symbol link");
          }
          seg.symbol_links.push_back(std::move(rec));
        } break;
        case LINK_TYPE_PTR: {
          V3TypeLink rec;
          rec.name = link.name();
          rec.method_count = link.u8();
          uint32_t count = link.u32();
          for (uint32_t i = 0; i < count; i++) {
            rec.offsets.push_back(link.u32());
            check_offset(seg, seg_id, rec.offsets.back(), 4, "type link");
          }
          seg.type_links.push_back(std::move(rec));
        } break;
        case LINK_PTR: {
          V3PointerLink rec;
          rec.source = link.u32();
          rec.dest = link.u32();
          check_offset(seg, seg_id, rec.source, 4, "pointer link");
          if (rec.dest > seg.data.size()) {
            link.fail("pointer link destination outside the segment");
          }
          seg.pointer_links.push_back(rec);
        } break;
        case LINK_DISTANCE_TO_OTHER_SEG_64: {
          V3DistanceLink rec;
          rec.target_segment = link.u8();
          rec.source = link.u32();
          rec.dest = link.u32();
          rec.patch = link.u32();
          if (rec.target_segment >= N_SEG) {
            link.fail("distance link to an invalid segment");
          }
          check_offset(seg, seg_id, rec.patch, 8, "distance link");
          seg.distance_links.push_back(rec);
        } break;
        case LINK_DISTANCE_TO_OTHER_SEG_32:
          link.fail("x86 32-bit distance link in an ARM64 object");
        default:
          link.fail(fmt::format("unknown link kind {}", kind));
      }
    }
    if (!terminated) {
      throw std::runtime_error(
          fmt::format("v3 object: {} link table has no end marker", SEGMENT_NAMES[seg_id]));
    }
  }

  // Cross-segment destinations can only be checked once every segment is read.
  for (int seg_id = 0; seg_id < N_SEG; seg_id++) {
    for (const auto& d : obj.segments[seg_id].distance_links) {
      if (d.dest > obj.segments[d.target_segment].data.size()) {
        throw std::runtime_error(
            fmt::format("v3 object: {} distance link targets {:#x}, outside {}",
                        SEGMENT_NAMES[seg_id], d.dest, SEGMENT_NAMES[d.target_segment]));
      }
    }
  }
  return obj;
}

}  // namespace goal_aot

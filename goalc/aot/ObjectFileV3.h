#pragma once

/*!
 * @file ObjectFileV3.h
 * Reader for ARM64 GOAL v3 object files, as written by emitter::ObjectGenerator.
 *
 * Layout: "GOAL", u16 major, u16 minor, u32 version (3), u32 segment count (3),
 * {offset, size} link tables per segment, {offset, size} data per segment,
 * u32 start of data; then the link tables and the segment data, both in
 * reverse segment order.  On ARM64 each link size carries
 * LINK_ARM64_EXECUTABLE_SIZE_FLAG and the size of the segment's code prefix.
 *
 * Every offset in the parsed records is relative to the start of its segment.
 */

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace goal_aot {

struct V3SymbolLink {
  std::string name;
  std::vector<uint32_t> offsets;
};

struct V3TypeLink {
  std::string name;
  uint8_t method_count = 0;  // as stored: jak2 objects store (methods / 4)
  std::vector<uint32_t> offsets;
};

//! LINK_PTR: write (segment base + dest) at source.
struct V3PointerLink {
  uint32_t source = 0;
  uint32_t dest = 0;
};

//! LINK_DISTANCE_TO_OTHER_SEG_64: write (target seg base + dest) - (seg base + source)
//! as a 64-bit value at patch.
struct V3DistanceLink {
  uint8_t target_segment = 0;
  uint32_t source = 0;
  uint32_t dest = 0;
  uint32_t patch = 0;
};

struct V3Segment {
  std::vector<uint8_t> data;     // code prefix followed by static data
  uint32_t executable_size = 0;  // size of the code prefix
  std::vector<V3SymbolLink> symbol_links;
  std::vector<V3TypeLink> type_links;
  std::vector<V3PointerLink> pointer_links;
  std::vector<V3DistanceLink> distance_links;

  uint32_t static_size() const { return uint32_t(data.size()) - executable_size; }
};

struct V3Object {
  uint16_t version_major = 0;
  uint16_t version_minor = 0;
  std::array<V3Segment, 3> segments;
};

//! Parse and validate an ARM64 v3 object.  Throws std::runtime_error on any
//! malformed header, table, or record, including x86-only link kinds.
V3Object parse_v3_object(std::span<const uint8_t> bytes);

}  // namespace goal_aot

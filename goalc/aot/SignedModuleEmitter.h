#pragma once

/*!
 * @file SignedModuleEmitter.h
 * Turns ARM64 GOAL v3 objects (code model 'page') into Apple assembly that is
 * linked and code-signed into the executable (ADR-IOS-001 / ADR-IOS-002).
 *
 * For each segment of a module:
 *  - its code is emitted into __GOAL,__text.  Every ADRP pair is re-emitted
 *    against the segment's data label in the EE arena, and every function type
 *    tag becomes a constant, so the linked text needs no runtime patching;
 *  - its static data is emitted as a read-only template in __TEXT,__const,
 *    which the runtime loader copies into the arena and then links (symbols,
 *    types, pointers, distances) as data only.
 *
 * The arena layout is a separate assembly file that reserves the EE memory as a
 * zero-fill __ARENA segment and places each segment's data label at a fixed
 * offset, so that the ADRP pairs resolve at link time.
 */

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "goalc/aot/ObjectFileV3.h"

#include "third-party/json.hpp"

namespace goal_aot {

struct SignedModuleOptions {
  //! GOAL offset of the "function" type, written as every function's type tag.
  //! The runtime must verify it before running any signed code.
  uint32_t function_type = 0;
  //! Drop the debug segment (release builds load no debug segment).
  bool omit_debug_segment = false;
};

struct SignedSegment {
  bool present = false;  // false when empty or omitted
  uint32_t code_size = 0;
  uint32_t static_size = 0;
  std::string text_symbol;      // empty when code_size == 0
  std::string data_symbol;      // empty when static_size == 0
  std::string template_symbol;  // empty when static_size == 0
};

struct SignedModule {
  std::string name;
  std::string symbol_base;
  std::string sha256;
  std::array<SignedSegment, 3> segments;
  std::string assembly;
  //! Data-only link records for the loader, offsets relative to the segment's
  //! code start or data start as labelled.
  nlohmann::json manifest;
};

//! ASCII symbol stem for a module name ("gkernel-h" -> "goal_gkernel_dh").
std::string module_symbol_base(const std::string& module_name);

SignedModule emit_signed_module(const std::string& module_name,
                                std::span<const uint8_t> object_bytes,
                                const SignedModuleOptions& options);

struct ArenaOptions {
  uint32_t arena_size = 0;   // EE memory size
  uint32_t data_base = 0;    // first GOAL offset reserved for module data
  uint32_t data_limit = 0;   // end of the reservation (exclusive)
  uint32_t alignment = 256;  // alignment of each segment's data
};

struct ArenaLayout {
  std::string assembly;
  nlohmann::json manifest;  // module -> segment -> data offset
  uint32_t data_end = 0;
};

//! Assign every present segment's data a fixed arena offset.  Module order is
//! preserved, so the result is deterministic for a deterministic module list.
ArenaLayout emit_arena_layout(std::span<const SignedModule> modules, const ArenaOptions& options);

//! C source with a table of every module's symbols, for the loader and tests.
std::string emit_module_index(std::span<const SignedModule> modules, uint32_t function_type);

}  // namespace goal_aot

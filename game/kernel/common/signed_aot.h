#pragma once

/*!
 * @file signed_aot.h
 * Runtime side of the signed AOT image (docs/ios/DECISIONI.md, ADR-IOS-001).
 *
 * GOAL code is linked into the executable's __GOAL segment by jak2-aot-emit and
 * the EE memory is the executable's __ARENA segment, so the runtime never
 * writes or maps executable memory: objects from DGOs only provide data, which
 * is checked against the module's SHA-256 and linked into fixed arena regions.
 */

#include "common/aot/SignedModuleIndex.h"
#include "common/common_types.h"

namespace signed_aot {

//! EE base used by every GOAL pointer (the __ARENA segment).
u8* arena_base();

//! GOAL offset of a host address in the arena or in signed text (must be within 4 GiB).
u32 goal_offset(const void* host);

//! The signed module emitted from the object called `name`, or nullptr.
const goal_aot_module* find_module(const char* name);

//! Abort unless `object[0:size]` is the exact object `module` was emitted from.
void verify_module_object(const goal_aot_module& module, const u8* object, u32 size);

//! Abort unless the runtime's function type matches the tags in the signed code.
void verify_function_type(u32 runtime_function_type);

//! Bind the next free stub of `kind` to a native target and return its GOAL function.
u32 bind_export(goal_aot_bank_kind kind, const void* target, u64 extra = 0);
u32 nothing_function();
u32 zero_function();

//! Per-module load state, used to decide whether static data can be reset.
struct ModuleState {
  bool loaded = false;
  u32 heap = 0;  // GOAL offset of the kheapinfo the module was last loaded into
  u32 loads = 0;
};
ModuleState& module_state(const goal_aot_module& module);

[[noreturn]] void fatal(const char* fmt_message, const char* arg = "");

}  // namespace signed_aot

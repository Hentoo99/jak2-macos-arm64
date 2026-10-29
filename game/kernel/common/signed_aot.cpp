#include "signed_aot.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/log/log.h"
#include "common/util/sha256.h"

namespace signed_aot {
namespace {

struct Registry {
  std::unordered_map<std::string, const goal_aot_module*> by_name;
  std::unordered_map<const goal_aot_module*, ModuleState> states;
  u32 next_stub[GOAL_AOT_BANK_COUNT] = {};
};

Registry& registry() {
  static Registry r = [] {
    Registry out;
    for (u32 i = 0; i < goal_aot_module_count; i++) {
      out.by_name.emplace(goal_aot_modules[i].name, &goal_aot_modules[i]);
    }
    return out;
  }();
  return r;
}

std::mutex g_bank_mutex;

const char* bank_name(goal_aot_bank_kind kind) {
  switch (kind) {
    case GOAL_AOT_BANK_ARG:
      return "arg";
    case GOAL_AOT_BANK_ARG_PP:
      return "arg-pp";
    case GOAL_AOT_BANK_STACK:
      return "stack";
    case GOAL_AOT_BANK_MIPS2C:
      return "mips2c";
    default:
      return "?";
  }
}

}  // namespace

void fatal(const char* fmt_message, const char* arg) {
  std::fprintf(stderr, "[signed-aot] FATAL: ");
  std::fprintf(stderr, fmt_message, arg);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
  std::abort();
}

u8* arena_base() {
  return goal_aot_arena;
}

u32 goal_offset(const void* host) {
  const auto delta = (uintptr_t)host - (uintptr_t)goal_aot_arena;
  if ((uintptr_t)host < (uintptr_t)goal_aot_arena || delta > 0xFFFFFFFFull) {
    char value[24];
    std::snprintf(value, sizeof(value), "%p", host);
    fatal("address %s is not reachable from the EE arena", value);
  }
  return (u32)delta;
}

const goal_aot_module* find_module(const char* name) {
  auto& r = registry();
  auto it = r.by_name.find(name);
  return it == r.by_name.end() ? nullptr : it->second;
}

void verify_module_object(const goal_aot_module& module, const u8* object, u32 size) {
  // The linker stores the link block's type tag over the object's leading "GOAL" magic before
  // linking starts, so hash the object as it was written.
  if (size < 4) {
    fatal("object %s is truncated", module.name);
  }
  std::vector<u8> bytes(object, object + size);
  memcpy(bytes.data(), "GOAL", 4);
  const auto actual = sha256::hex_digest(bytes);
  if (actual != module.sha256) {
    std::fprintf(stderr, "[signed-aot] %s: object sha256 %s, signed module expects %s\n",
                 module.name, actual.c_str(), module.sha256);
    fatal("object %s does not match its signed code; rebuild the AOT image", module.name);
  }
}

void verify_function_type(u32 runtime_function_type) {
  if (runtime_function_type != goal_aot_function_type) {
    std::fprintf(stderr,
                 "[signed-aot] runtime function type is %#x, signed code was emitted with %#x\n",
                 runtime_function_type, goal_aot_function_type);
    char value[16];
    std::snprintf(value, sizeof(value), "%#x", runtime_function_type);
    fatal("function type mismatch: re-run jak2-aot-emit with --function-type %s", value);
  }
}

u32 bind_export(goal_aot_bank_kind kind, const void* target, u64 extra) {
  std::lock_guard<std::mutex> lock(g_bank_mutex);
  auto& r = registry();
  const auto& bank = goal_aot_export_banks[kind];
  const u32 index = r.next_stub[kind];
  if (index >= bank.count) {
    fatal("export bank '%s' is full; increase its size in jak2-aot-emit", bank_name(kind));
  }
  r.next_stub[kind] = index + 1;
  bank.slots[2 * index] = (u64)(uintptr_t)target;
  bank.slots[2 * index + 1] = extra;
  // The stub's code starts after its 4-byte type tag.
  return goal_offset(bank.stubs + (size_t)index * bank.stride + 4);
}

u32 nothing_function() {
  return goal_offset(goal_aot_nothing_stub + 4);
}

u32 zero_function() {
  return goal_offset(goal_aot_zero_stub + 4);
}

ModuleState& module_state(const goal_aot_module& module) {
  return registry().states[&module];
}

}  // namespace signed_aot

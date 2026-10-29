/*!
 * Round-trip check for jak2-aot-emit output (see scripts/aot_signed_roundtrip.sh).
 *
 * Linked together with a generated arena-layout.S, module-index.c and the module .S files,
 * this program reads the original v3 objects and checks, at the addresses dyld
 * chose for this process, that:
 *  - every ADRP pair in the signed text resolves to its static data in the arena;
 *  - every other code word is unchanged, except function type tags, which hold
 *    goal_aot_function_type;
 *  - each data template equals the object's static data;
 *  - module data sits inside the arena without overlaps, and all signed text is
 *    reachable as a u32 offset from the arena base (GOAL function pointers).
 *
 * Standalone on purpose: it re-reads the object header itself instead of using
 * the emitter's parser, so the two implementations check each other.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "common/aot/SignedModuleIndex.h"

namespace {

constexpr uint32_t kTagPlaceholder = 0xaeaeaeae;
constexpr uint32_t kExecFlag = 0x80000000;

int g_errors = 0;

template <typename... Args>
void error(const char* fmt, Args... args) {
  if (g_errors++ < 40) {
    std::printf("ERROR: ");
    std::printf(fmt, args...);
    std::printf("\n");
  }
}

uint32_t u32_at(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

int64_t sign_extend(uint64_t v, int bits) {
  uint64_t s = 1ull << (bits - 1);
  return (int64_t)((v ^ s) - s);
}

bool is_adrp(uint32_t w) {
  return (w & 0x9F000000u) == 0x90000000u;
}
bool is_ldr64(uint32_t w) {
  return (w & 0xFFC00000u) == 0xF9400000u;
}

// Address an ADRP pair computes when its first word sits at `pc`.
uint64_t pair_target(uint64_t pc, uint32_t adrp, uint32_t lo) {
  int64_t pages = sign_extend((((adrp >> 5) & 0x7FFFF) << 2) | ((adrp >> 29) & 3), 21);
  uint64_t lo12 = (lo >> 10) & 0xFFF;
  return (pc & ~0xFFFull) + pages * 0x1000 + (is_ldr64(lo) ? 8 * lo12 : lo12);
}

struct Segment {
  std::vector<uint8_t> data;
  uint32_t exec = 0;
};

bool read_object(const std::string& path, Segment out[3]) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 68 || std::memcmp(b.data(), "GOAL", 4) != 0) {
    return false;
  }
  uint32_t data_start = u32_at(&b[64]);
  for (int s = 0; s < 3; s++) {
    uint32_t link_word = u32_at(&b[20 + 8 * s]);
    uint32_t off = u32_at(&b[40 + 8 * s]), size = u32_at(&b[44 + 8 * s]);
    if (!(link_word & kExecFlag) || (size_t)data_start + off + size > b.size()) {
      return false;
    }
    out[s].exec = link_word & ~kExecFlag;
    out[s].data.assign(b.begin() + data_start + off, b.begin() + data_start + off + size);
  }
  return true;
}

void check_segment(const goal_aot_module& mod, int s, const Segment& orig) {
  const goal_aot_segment& seg = mod.segments[s];
  if (orig.data.empty()) {
    return;  // nothing emitted
  }
  if (seg.code_size != orig.exec || seg.static_size != orig.data.size() - orig.exec) {
    error("%s seg %d: sizes differ from the object", mod.name, s);
    return;
  }
  // static data template
  if (seg.static_size &&
      std::memcmp(seg.data_template, orig.data.data() + orig.exec, seg.static_size) != 0) {
    error("%s seg %d: data template differs from the object", mod.name, s);
  }
  // code
  for (uint32_t pc = 0; pc < orig.exec; pc += 4) {
    const uint32_t o = u32_at(orig.data.data() + pc);
    const uint32_t l = u32_at(seg.text + pc);
    if (is_adrp(o)) {
      const uint32_t olo = u32_at(orig.data.data() + pc + 4);
      const uint32_t llo = u32_at(seg.text + pc + 4);
      const uint64_t want = (uint64_t)seg.data + (pair_target(pc, o, olo) - orig.exec);
      const uint64_t got = pair_target((uint64_t)(seg.text + pc), l, llo);
      if ((l & 0x9F00001Fu) != (o & 0x9F00001Fu) || (llo & 0xFFC003FFu) != (olo & 0xFFC003FFu)) {
        error("%s seg %d +%#x: ADRP pair registers/opcodes changed", mod.name, s, pc);
      } else if (got != want) {
        error("%s seg %d +%#x: ADRP pair resolves to %#llx, expected %#llx", mod.name, s, pc,
              (unsigned long long)got, (unsigned long long)want);
      }
      pc += 4;
    } else if (o == kTagPlaceholder) {
      if (l != goal_aot_function_type) {
        error("%s seg %d +%#x: function tag %#x, expected %#x", mod.name, s, pc, l,
              goal_aot_function_type);
      }
    } else if (l != o) {
      error("%s seg %d +%#x: code word %#x, expected %#x", mod.name, s, pc, l, o);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: %s OBJ_DIR\n", argv[0]);
    return 2;
  }
  const uint64_t arena = (uint64_t)goal_aot_arena;
  struct Region {
    uint64_t begin, end;
    std::string what;
  };
  std::vector<Region> regions;
  uint64_t max_text_end = 0, code_bytes = 0, pairs_checked = 0;

  for (uint32_t i = 0; i < goal_aot_module_count; i++) {
    const goal_aot_module& mod = goal_aot_modules[i];
    Segment orig[3];
    if (!read_object(std::string(argv[1]) + "/" + mod.name + ".o", orig)) {
      error("%s: cannot read object", mod.name);
      continue;
    }
    for (int s = 0; s < 3; s++) {
      const goal_aot_segment& seg = mod.segments[s];
      if (seg.static_size) {
        regions.push_back({(uint64_t)seg.data, (uint64_t)seg.data + seg.static_size,
                           std::string(mod.name) + " seg " + std::to_string(s)});
        if ((uint64_t)seg.data % 16) {
          error("%s seg %d: data is not 16-byte aligned", mod.name, s);
        }
      }
      if (seg.code_size) {
        max_text_end = std::max(max_text_end, (uint64_t)seg.text + seg.code_size);
        code_bytes += seg.code_size;
        for (uint32_t pc = 0; pc < seg.code_size; pc += 4) {
          pairs_checked += is_adrp(u32_at(seg.text + pc));
        }
      }
      check_segment(mod, s, orig[s]);
    }
  }

  std::sort(regions.begin(), regions.end(),
            [](const Region& a, const Region& b) { return a.begin < b.begin; });
  for (size_t i = 0; i < regions.size(); i++) {
    if (regions[i].begin < arena || regions[i].end > arena + 0x100000000ull) {
      error("%s: data outside the arena", regions[i].what.c_str());
    }
    if (i && regions[i].begin < regions[i - 1].end) {
      error("%s overlaps %s", regions[i].what.c_str(), regions[i - 1].what.c_str());
    }
  }
  if (max_text_end && (max_text_end <= arena || max_text_end - arena > 0xFFFFFFFFull)) {
    error("signed text ends at arena%+lld, not reachable as a u32 GOAL offset",
          (long long)(max_text_end - arena));
  }

  std::printf(
      "%u modules, %llu bytes of code, %llu ADRP pairs, %zu data regions; "
      "text ends at arena+%#llx: %s (%d errors)\n",
      goal_aot_module_count, (unsigned long long)code_bytes, (unsigned long long)pairs_checked,
      regions.size(), (unsigned long long)(max_text_end - arena), g_errors ? "FAIL" : "OK",
      g_errors);
  return g_errors ? 1 : 0;
}

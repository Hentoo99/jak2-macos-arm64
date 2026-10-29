#include <cstring>
#include <string>
#include <vector>

#include "common/link_types.h"
#include "common/type_system/TypeSystem.h"
#include "common/util/sha256.h"

#include "goalc/aot/ObjectFileV3.h"
#include "goalc/aot/SignedModuleEmitter.h"
#include "goalc/debugger/DebugInfo.h"
#include "goalc/emitter/IGen.h"
#include "goalc/emitter/IGenARM64.h"
#include "goalc/emitter/ObjectGenerator.h"
#include "gtest/gtest.h"

using namespace emitter;
using namespace emitter::IGen::ARM64;

namespace {

std::string sha_of(const std::string& s) {
  return sha256::hex_digest(
      std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

// One main-segment function that loads a symbol literal with an ADRP pair, plus
// a static record holding a pointer back to the function.
std::vector<uint8_t> make_object(bool page_relative) {
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  gen.set_arm64_page_relative(page_relative);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "test-func";
  FunctionRecord func = gen.add_function_to_seg(MAIN_SEGMENT, &dbg);
  IR_Record ir = gen.add_ir(func);

  StaticRecord lit = gen.add_static_to_seg(MAIN_SEGMENT);
  auto& lit_data = gen.get_static_data(lit);
  lit_data.assign(8, 0);
  u32 no_offset = LINK_SYM_NO_OFFSET_FLAG;
  memcpy(lit_data.data(), &no_offset, 4);
  gen.link_static_symbol_ptr(lit, 0, "some-symbol");
  if (page_relative) {
    auto page = gen.add_instr(Instruction(adrp_gpr64(X16)), ir);
    gen.link_instruction_static(page, lit, 0);
    auto lo = gen.add_instr(Instruction(load64_gpr64_lo12(X16, X16)), ir);
    gen.link_instruction_static(lo, lit, 0);
  } else {
    auto ldr = gen.add_instr(Instruction(load64_pcRel_s32(X16, 4)), ir);
    gen.link_instruction_static(ldr, lit, 0);
  }
  gen.add_instr(Instruction(ret()), ir);

  StaticRecord table = gen.add_static_to_seg(MAIN_SEGMENT);
  gen.get_static_data(table).assign(4, 0);
  gen.link_static_pointer_to_function(table, 0, func);
  return gen.generate_data_v3(&ts).to_vector();
}

}  // namespace

TEST(Sha256, KnownVectors) {
  EXPECT_EQ(sha_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha_of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // 56 bytes: the length no longer fits in the first padding block.
  EXPECT_EQ(sha_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha_of(std::string(1000, 'a')),
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(AotSignedModule, SymbolBase) {
  EXPECT_EQ(goal_aot::module_symbol_base("gkernel-h"), "goal_gkernel_dh");
  EXPECT_EQ(goal_aot::module_symbol_base("a_b"), "goal_a__b");
  EXPECT_EQ(goal_aot::module_symbol_base("x.y"), "goal_x_x2ey");
}

TEST(AotSignedModule, ParsesGeneratedObject) {
  auto bytes = make_object(true);
  auto obj = goal_aot::parse_v3_object(bytes);
  const auto& main = obj.segments[MAIN_SEGMENT];
  ASSERT_GT(main.executable_size, 0u);
  // type tag + ADRP + LDR + RET
  EXPECT_EQ(main.executable_size % 4, 0u);
  ASSERT_EQ(main.symbol_links.size(), 1u);
  EXPECT_EQ(main.symbol_links[0].name, "some-symbol");
  ASSERT_EQ(main.symbol_links[0].offsets.size(), 1u);
  EXPECT_GE(main.symbol_links[0].offsets[0], main.executable_size);
  bool found_function_tag = false;
  for (const auto& t : main.type_links) {
    if (t.name == "function") {
      for (auto offset : t.offsets) {
        found_function_tag |= offset < main.executable_size;
      }
    }
  }
  EXPECT_TRUE(found_function_tag);
  ASSERT_EQ(main.pointer_links.size(), 1u);
  EXPECT_LT(main.pointer_links[0].dest, main.executable_size);  // points at the function
}

TEST(AotSignedModule, RejectsTruncatedObject) {
  auto bytes = make_object(true);
  bytes.resize(bytes.size() / 2);
  EXPECT_THROW(goal_aot::parse_v3_object(bytes), std::runtime_error);
  EXPECT_THROW(goal_aot::parse_v3_object(std::vector<uint8_t>(16, 0)), std::runtime_error);
}

TEST(AotSignedModule, EmitsPagePairsAgainstArenaData) {
  auto bytes = make_object(true);
  goal_aot::SignedModuleOptions options;
  options.function_type = 0x1234;
  auto mod = goal_aot::emit_signed_module("test-mod", bytes, options);
  EXPECT_EQ(mod.symbol_base, "goal_test_dmod");
  EXPECT_EQ(mod.sha256, sha256::hex_digest(bytes));
  const auto& seg = mod.segments[MAIN_SEGMENT];
  ASSERT_TRUE(seg.present);
  EXPECT_EQ(seg.text_symbol, "goal_test_dmod_s0_text");
  EXPECT_EQ(seg.data_symbol, "goal_test_dmod_s0_data");
  // The literal is the first static record.
  EXPECT_NE(mod.assembly.find("adrp x16, _goal_test_dmod_s0_data+0x0@PAGE\n"), std::string::npos);
  EXPECT_NE(mod.assembly.find("ldr x16, [x16, _goal_test_dmod_s0_data+0x0@PAGEOFF]\n"),
            std::string::npos);
  EXPECT_NE(mod.assembly.find(".long 0x00001234"), std::string::npos);  // function tag
  EXPECT_NE(mod.assembly.find("_goal_test_dmod_s0_template:"), std::string::npos);

  const auto& m = mod.manifest["segments"][0];
  EXPECT_EQ(m["function_tags"], 1);
  ASSERT_EQ(m["symbols"].size(), 1u);
  EXPECT_EQ(m["symbols"][0]["name"], "some-symbol");
  EXPECT_EQ(m["symbols"][0]["links"][0]["mode"], "offset-1");
  EXPECT_EQ(m["symbols"][0]["links"][0]["offset"], 0);
  ASSERT_EQ(m["pointers"].size(), 1u);
  EXPECT_EQ(m["pointers"][0]["target_region"], "code");
}

TEST(AotSignedModule, RejectsLiteralCodeModel) {
  auto bytes = make_object(false);
  goal_aot::SignedModuleOptions options;
  options.function_type = 0x1234;
  try {
    goal_aot::emit_signed_module("literal-mod", bytes, options);
    FAIL() << "expected the literal code model to be rejected";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("--arm64-code-model page"), std::string::npos);
  }
}

TEST(AotSignedModule, ArenaLayoutPlacesDataInOrder) {
  auto bytes = make_object(true);
  goal_aot::SignedModuleOptions options;
  options.function_type = 0x1234;
  std::vector<goal_aot::SignedModule> mods = {goal_aot::emit_signed_module("a", bytes, options),
                                              goal_aot::emit_signed_module("b", bytes, options)};
  goal_aot::ArenaOptions arena{0x10000000, 0xa000000, 0xa001000, 256};
  auto layout = goal_aot::emit_arena_layout(mods, arena);
  EXPECT_EQ(layout.manifest["a"]["main"], 0xa000000);
  EXPECT_EQ(layout.manifest["b"]["main"], 0xa000100);
  EXPECT_NE(layout.assembly.find(".set _goal_b_s0_data, _goal_aot_arena + 0xa000100"),
            std::string::npos);
  // A reservation that is too small is an error, not an overlap.
  goal_aot::ArenaOptions tiny{0x10000000, 0xa000000, 0xa000010, 256};
  EXPECT_THROW(goal_aot::emit_arena_layout(mods, tiny), std::runtime_error);

  auto index = goal_aot::emit_module_index(mods, 0x1234);
  EXPECT_NE(index.find("const uint32_t goal_aot_module_count = 2;"), std::string::npos);
  EXPECT_NE(index.find("const uint32_t goal_aot_function_type = 0x1234;"), std::string::npos);
}

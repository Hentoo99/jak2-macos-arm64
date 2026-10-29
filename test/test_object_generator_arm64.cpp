#include <cstring>
#include <memory>

#include "common/type_system/TypeSystem.h"

#include "goalc/debugger/DebugInfo.h"
#include "goalc/emitter/IGen.h"
#include "goalc/emitter/IGenARM64.h"
#include "goalc/emitter/ObjectGenerator.h"

#include "gtest/gtest.h"

using namespace emitter;
using namespace emitter::IGen;
using namespace emitter::IGen::ARM64;

namespace {

u32 read_word(const std::vector<u8>& data, size_t off) {
  u32 w;
  memcpy(&w, data.data() + off, 4);
  return w;
}

// Build an ARM64 object generator with one function: [B target, NOP] plus a
// within-function jump link from the branch to the NOP.
std::unique_ptr<ObjectGenerator> make_arm64_gen() {
  return std::make_unique<ObjectGenerator>(GameVersion::Jak2, InstructionSet::ARM64);
}

}  // namespace

TEST(ObjectGeneratorARM64, SameFunctionJumpForward) {
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "test-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg);
  IR_Record ir0 = gen.add_ir(func);
  InstructionRecord jump = gen.add_instr(Instruction(jmp_imm()), ir0);
  IR_Record ir1 = gen.add_ir(func);
  gen.add_instr(Instruction(nop()), ir1);
  gen.link_instruction_jump(jump, ir1);
  auto obj = gen.generate_data_v3(&ts);
  const auto& data = obj.segment_data.at(0);
  // layout: [0-3] type tag, [4-7] B, [8-11] NOP.
  // The B at offset 4 targets offset 8 -> imm26 = (8-4)/4 = 1.
  EXPECT_EQ(read_word(data, 4), 0x14000000u | 1u);
  // nop at 8
  EXPECT_EQ(read_word(data, 8), 0xD503201Fu);
}

TEST(ObjectGeneratorARM64, SameFunctionJumpBackward) {
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "back-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg);
  IR_Record ir0 = gen.add_ir(func);
  gen.add_instr(Instruction(nop()), ir0);
  IR_Record ir1 = gen.add_ir(func);
  InstructionRecord jump = gen.add_instr(Instruction(jmp_imm()), ir1);
  IR_Record ir2 = gen.add_ir(func);
  gen.add_instr(Instruction(nop()), ir2);
  gen.link_instruction_jump(jump, ir0);
  auto obj = gen.generate_data_v3(&ts);
  const auto& data = obj.segment_data.at(0);
  // [0-3] type tag; [4-7] nop; [8-11] B; [12-15] nop.
  // B at 8 targets 4 -> imm26 = (4-8)/4 = -1.
  EXPECT_EQ(read_word(data, 8) & 0x3FFFFFF, 0x3FFFFFFu);  // -1 in 26 bits
}

TEST(ObjectGeneratorARM64, SameFunctionJumpZeroOffset) {
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "zero-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg);
  IR_Record ir0 = gen.add_ir(func);
  InstructionRecord jump = gen.add_instr(Instruction(jmp_imm()), ir0);
  gen.link_instruction_jump(jump, ir0);
  auto obj = gen.generate_data_v3(&ts);
  EXPECT_EQ(read_word(obj.segment_data.at(0), 4), 0x14000000u);
}

TEST(ObjectGeneratorARM64, OutOfRangeJumpRejected) {
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "far-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg);
  IR_Record ir0 = gen.add_ir(func);
  InstructionRecord jump = gen.add_instr(Instruction(jmp_imm()), ir0);
  for (int i = 0; i < (1 << 25); i++) {
    IR_Record irc = gen.add_ir(func);
    gen.add_instr(Instruction(nop()), irc);
  }
  // target the LAST nop: ir_id (1 << 25) -> 2^27 bytes away (out of range).
  gen.link_instruction_jump(jump, gen.get_future_ir_record_in_same_func(ir0, 1 << 25));
  EXPECT_THROW(gen.generate_data_v3(&ts), std::runtime_error);
}

TEST(ObjectGeneratorARM64, X86RipFuncLinkRejected) {
  // RIP-to-function links are x86-specific: the ARM64 equivalent uses a
  // literal + function pointer link, so an ARM64 object that requests an x86
  // rip-func link must be rejected loudly.
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg1, dbg2;
  dbg1.name = "rip-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg1);
  IR_Record ir0 = gen.add_ir(func);
  gen.add_instr(Instruction(jmp_imm()), ir0);
  FunctionDebugInfo dbg2b;
  dbg2b.name = "target";
  FunctionRecord target = gen.add_function_to_seg(0, &dbg2b);
  IR_Record ir1 = gen.add_ir(target);
  gen.add_instr(Instruction(nop()), ir1);
  InstructionRecord rec{0, func.func_id, ir0.ir_id, 0};
  gen.link_instruction_to_function(rec, target);
  EXPECT_THROW(gen.generate_data_v3(&ts), std::runtime_error);
}

TEST(ObjectGeneratorARM64, PageRelativeStaticReference) {
  // In page-relative mode a literal is reached with ADRP + LDR and an address
  // with ADRP + ADD; both pairs must resolve to the static record, which sits
  // on a later page than the code.
  ObjectGenerator gen(GameVersion::Jak2, InstructionSet::ARM64);
  gen.set_arm64_page_relative(true);
  TypeSystem ts;
  ts.add_builtin_types(GameVersion::Jak2);
  FunctionDebugInfo dbg;
  dbg.name = "page-func";
  FunctionRecord func = gen.add_function_to_seg(0, &dbg);
  IR_Record ir0 = gen.add_ir(func);
  StaticRecord lit = gen.add_static_to_seg(0);
  gen.get_static_data(lit).assign(8, 0);
  auto load_page = gen.add_instr(Instruction(adrp_gpr64(X16)), ir0);
  gen.link_instruction_static(load_page, lit, 0);
  auto load_lo = gen.add_instr(Instruction(load64_gpr64_lo12(X16, X16)), ir0);
  gen.link_instruction_static(load_lo, lit, 0);
  auto addr_page = gen.add_instr(Instruction(adrp_gpr64(X17)), ir0);
  gen.link_instruction_static(addr_page, lit, 0);
  auto addr_lo = gen.add_instr(Instruction(add_gpr64_lo12(X17, X17)), ir0);
  gen.link_instruction_static(addr_lo, lit, 0);
  auto obj = gen.generate_data_v3(&ts);
  const auto& data = obj.segment_data.at(0);
  // The literal is the only static record, so it ends the segment.
  const s64 lit_location = (s64)data.size() - 8;
  ASSERT_GE(lit_location, 0x1000);

  auto adrp_page = [&](size_t pc) {
    u32 w = read_word(data, pc);
    EXPECT_EQ(w & 0x9F00001Fu, 0x90000000u | (w & 31));
    s64 imm = (s64)((((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3));
    imm = (imm ^ (1 << 20)) - (1 << 20);
    return ((s64)pc & ~0xFFFll) + imm * 0x1000;
  };
  // [0-3] type tag; [4] adrp x16; [8] ldr x16; [12] adrp x17; [16] add x17.
  u32 ldr = read_word(data, 8);
  EXPECT_EQ(ldr & 0xFFC003FFu, 0xF9400000u | (16 << 5) | 16);
  EXPECT_EQ(adrp_page(4) + 8 * ((ldr >> 10) & 0xFFF), lit_location);
  u32 add = read_word(data, 16);
  EXPECT_EQ(add & 0xFFC003FFu, 0x91000000u | (17 << 5) | 17);
  EXPECT_EQ(adrp_page(12) + ((add >> 10) & 0xFFF), lit_location);
}

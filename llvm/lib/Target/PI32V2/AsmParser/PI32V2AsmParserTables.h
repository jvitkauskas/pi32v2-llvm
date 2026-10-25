//===-- PI32V2AsmParserTables.h - Layout of the parser tables ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The rows of PI32V2ParserGen.inc (pi32v2-isa tools/llvm/gen_parser.py).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_ASMPARSER_PI32V2ASMPARSERTABLES_H
#define LLVM_LIB_TARGET_PI32V2_ASMPARSER_PI32V2ASMPARSERTABLES_H

#include <cstdint>

namespace llvm {

/// A numeric field: 0 uimm, 1 simm, 2 bit, 3 invbit, 4 modimm, 5 branch target.
struct PI32V2NumType {
  uint8_t Kind;
  uint8_t Width;
  int32_t Scale;
  int32_t Add;
  bool HasZeroAs;
  int32_t ZeroAs;
  bool HasRange;
  int64_t Lo, Hi;
  bool Invert;
  bool Abs32; // the field of a 32-bit constant load: takes a symbol
};

/// A placeholder of a template. Kind: 0 gpr, 1 gpr_lo, 2 gpr_hi, 3 pair,
/// 4 pair_regs, 5 sr, 6 rlist4, 7 rmask, 8 srmask, 9 numeric.
struct PI32V2Slot {
  char Field;
  uint8_t Kind;
  bool Plus;   // {+x}: "+value", or nothing for 0
  bool Negate; // the value is written negated (an `x - n` alias)
  uint16_t NumType;
  int8_t Backref; // the same field earlier in the template: must repeat
  const char *Lead;
  uint8_t Width;
};

/// An operand of the MCInst: Kind 0 register, 1 numeric, 2 raw immediate.
struct PI32V2Op {
  char Field;
  uint8_t Kind;
  uint16_t NumType;
};

struct PI32V2Template {
  const char *Text;
  unsigned Opcode;
  uint8_t Size;
  bool Alias;
  int8_t Tie;
  uint16_t Order;
  uint32_t SlotStart;
  uint16_t NumSlots;
  uint32_t OpStart;
  uint16_t NumOps;
  uint32_t RejectStart;
  uint16_t NumRejects;
  char SameFrom, SameTo; // an alias where field SameTo is field SameFrom
  bool Branch;           // has a branch target
  bool Abs32;            // has a 32-bit constant that takes a symbol
};

} // namespace llvm

#endif

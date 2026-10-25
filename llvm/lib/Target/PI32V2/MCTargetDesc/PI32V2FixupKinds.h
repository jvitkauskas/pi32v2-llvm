//===-- PI32V2FixupKinds.h - PI32V2 fixups ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// One fixup kind per place an expression can go (PI32V2FixupsGen.inc,
// generated from the instruction table): the fixup is at the instruction's
// first byte, as JieLi's relocations are, and its layout says which bits of
// the instruction hold the field.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2FIXUPKINDS_H
#define LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"
#include <cstdint>

namespace llvm {
namespace PI32V2 {
enum Fixups {
  FirstFixup = FirstTargetFixupKind - 1,
#define PI32V2_GEN_FIXUP_ENUM
#include "PI32V2FixupsGen.inc"
  LastFixup,
  NumTargetFixupKinds = LastFixup - FirstTargetFixupKind
};

struct PI32V2FixupLayout {
  const char *Name;
  uint8_t InsnBytes;   // the instruction's size
  bool PCRel;          // the field is (target - end of instruction) / 2
  uint8_t Reloc;       // the ELF relocation, 0 if it must be resolved here
  uint8_t Width;       // bits in the field
  uint8_t Bits[32];    // Inst bit of each field bit, most significant first
};

/// Relaxation: the operands of the larger form. Kind 0: operand Value of the
/// smaller form; 1: register Value; 2: immediate Value.
struct PI32V2RelaxOp {
  uint8_t Kind;
  int64_t Value;
};

struct PI32V2Relax {
  unsigned From, To;
  uint32_t ProgStart;
  uint8_t ProgLen;
};

const PI32V2FixupLayout &fixupLayout(unsigned Kind);
/// The fixup kind of the expression operand of Opcode, or -1 if it has none.
int fixupKindForOpcode(unsigned Opcode);
} // namespace PI32V2
} // namespace llvm

#endif

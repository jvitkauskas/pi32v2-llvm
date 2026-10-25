//===- PI32V2.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's pi32v2 (AC79 series). The relocations are JieLi's linker's, as
// pi32v2-isa docs/LINKER.md section 3 has them: P is the address of the
// instruction's first halfword; an instruction is halfwords in order, each
// little-endian; a branch counts from the end of the instruction in
// halfwords and overwrites its field.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class PI32V2 final : public TargetInfo {
public:
  PI32V2(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

PI32V2::PI32V2(Ctx &ctx) : TargetInfo(ctx) {
  defaultImageBase = 0;
  trapInstr = {0, 0, 0, 0}; // nop
}

RelExpr PI32V2::getRelExpr(RelType type, const Symbol &s,
                           const uint8_t *loc) const {
  switch (type) {
  case R_PI32V2_NONE:
    return R_NONE;
  case R_PI32V2_LONG_JUMP_17M2:
  case R_PI32V2_LONG_JUMP_23M2:
  case R_PI32V2_EXTEND_JUMP_17M2:
  case R_PI32V2_EXTEND_CALL_32M2:
    return R_PC;
  default:
    return R_ABS;
  }
}

static void halfword(uint8_t *loc, unsigned k, uint16_t v) {
  write16le(loc + 2 * k, v);
}

void PI32V2::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  int64_t v = int64_t(int32_t(val));
  switch (rel.type) {
  case R_PI32V2_LONG_JUMP_17M2:
  case R_PI32V2_LONG_JUMP_23M2:
  case R_PI32V2_EXTEND_JUMP_17M2:
  case R_PI32V2_EXTEND_CALL_32M2: {
    bool ext = rel.type == R_PI32V2_EXTEND_JUMP_17M2 ||
               rel.type == R_PI32V2_EXTEND_CALL_32M2;
    int64_t d = v - (ext ? 6 : 4);
    checkAlignment(ctx, loc, d, 2, rel);
    d >>= 1;
    if (rel.type == R_PI32V2_LONG_JUMP_17M2) {
      checkInt(ctx, loc, d, 16, rel);
      halfword(loc, 1, d);
    } else if (rel.type == R_PI32V2_LONG_JUMP_23M2) {
      checkInt(ctx, loc, d, 22, rel);
      halfword(loc, 0, (read16le(loc) & 0xFFC0) | ((d >> 16) & 0x3F));
      halfword(loc, 1, d);
    } else if (rel.type == R_PI32V2_EXTEND_JUMP_17M2) {
      checkInt(ctx, loc, d, 16, rel);
      halfword(loc, 2, d);
    } else { // no range check, as JieLi's linker
      halfword(loc, 1, (read16le(loc + 2) & 1) | ((d & 0x7FFF) << 1));
      halfword(loc, 2, d >> 15);
    }
    break;
  }
  case R_PI32V2_MOV_ABS32:
  case R_PI32V2_GOT:
  case R_PI32V2_GOTFUNCDESC:
    halfword(loc, 1, val);
    halfword(loc, 2, val >> 16);
    break;
  case R_PI32V2_MOV_ABS16:
  case R_PI32V2_GOT16:
  case R_PI32V2_GOTFUNCDESC16:
    checkInt(ctx, loc, v, 16, rel);
    halfword(loc, 1, val);
    break;
  case R_PI32V2_ABS8:
    checkInt(ctx, loc, v, 8, rel);
    *loc = val;
    break;
  case R_PI32V2_ABS16:
    checkInt(ctx, loc, v, 16, rel);
    write16le(loc, val);
    break;
  case R_PI32V2_ABS32:
  case R_PI32V2_FUNCDESC:
  case R_PI32V2_FUNCDESCVALUE:
  case R_PI32V2_REL32:
    write32le(loc, val);
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation "
             << rel.type;
  }
}

void elf::setPI32V2TargetInfo(Ctx &ctx) { ctx.target.reset(new PI32V2(ctx)); }

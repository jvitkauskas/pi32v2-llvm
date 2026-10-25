//===-- PI32V2Disassembler.cpp - PI32V2 disassembler ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// No encoding is a prefix of a longer one (the table's check), so the 16-,
// 32- and 48-bit tables can be tried in turn.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "pi32v2-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class PI32V2Disassembler : public MCDisassembler {
public:
  PI32V2Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};
} // namespace

static MCDisassembler *createPI32V2Disassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new PI32V2Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePI32V2Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getThePI32V2Target(),
                                         createPI32V2Disassembler);
}

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(PI32V2::R0 + RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRLoRegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(PI32V2::R0 + RegNo));
  return MCDisassembler::Success;
}

// r8..r15, encoded as the register number - 8.
static DecodeStatus DecodeGPRHiRegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(PI32V2::R8 + RegNo));
  return MCDisassembler::Success;
}

static const unsigned PairTable[8] = {
    PI32V2::R1_R0,   PI32V2::R3_R2,   PI32V2::R5_R4,   PI32V2::R7_R6,
    PI32V2::R9_R8,   PI32V2::R11_R10, PI32V2::R13_R12, PI32V2::R15_R14};

static DecodeStatus DecodeGPRPairRegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(PairTable[RegNo]));
  return MCDisassembler::Success;
}

static const unsigned SRTable[16] = {
    PI32V2::RETI,  PI32V2::RETE,  PI32V2::RETX, PI32V2::RETS,
    PI32V2::SR4,   PI32V2::PSR,   PI32V2::CNUM, PI32V2::USPN,
    PI32V2::SSPN,  PI32V2::ICFGN, PI32V2::ICFGS, PI32V2::ICFG,
    PI32V2::USP,   PI32V2::SSP,   PI32V2::SP,   PI32V2::PC};

static DecodeStatus DecodeSRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(SRTable[RegNo]));
  return MCDisassembler::Success;
}

#define PI32V2_GEN_DECODERS
#include "PI32V2OperandsGen.inc"

#include "PI32V2GenDisassemblerTables.inc"

DecodeStatus PI32V2Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                raw_ostream &CS) const {
  uint64_t Insn = 0;
  for (unsigned Words = 1; Words <= 3; ++Words) {
    if (Bytes.size() < 2 * Words)
      break;
    Insn = (Insn << 16) | Bytes[2 * Words - 2] |
           (uint64_t(Bytes[2 * Words - 1]) << 8);
    const uint8_t *Table = Words == 1   ? DecoderTablePI32V216
                           : Words == 2 ? DecoderTablePI32V232
                                        : DecoderTablePI32V248;
    MI.clear();
    DecodeStatus S = decodeInstruction(Table, MI, Insn, Address, this, STI);
    if (S != MCDisassembler::Fail) {
      Size = 2 * Words;
      return S;
    }
  }
  Size = Bytes.size() < 2 ? Bytes.size() : 2;
  return MCDisassembler::Fail;
}

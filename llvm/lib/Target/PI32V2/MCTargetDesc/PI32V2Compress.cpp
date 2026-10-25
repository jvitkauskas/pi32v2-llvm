//===-- PI32V2Compress.cpp - PI32V2 shorter forms -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The code generator selects the general forms; JieLi's assembler would put
// the same lines in the smallest form that takes them. The rules
// (PI32V2CompressGen.inc, generated with pi32v2-isa's assembler) give, for a
// long form, the short forms in the order that assembler prefers; the first
// whose operands fit is used.
//
//===----------------------------------------------------------------------===//

#include "PI32V2BaseInfo.h"
#include "PI32V2MCTargetDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {
struct PI32V2CompressRule {
  unsigned Long, Short;
  int8_t EqA, EqB;  // these operands of the long form must be equal
  int8_t FixOp;     // this operand of the long form must be FixVal
  int64_t FixVal;
  uint8_t NumOps;
  uint8_t Prog[6];  // the short form's operands: operands of the long form
};

bool fitsNum(int64_t V, bool Signed, unsigned Width, int Scale, int Add,
             bool HasZeroAs, int ZeroAs) {
  if (HasZeroAs && V == ZeroAs)
    return true;
  int64_t D = V - Add;
  if (D % Scale)
    return false;
  int64_t R = D / Scale;
  if (Signed)
    return R >= -(int64_t(1) << (Width - 1)) && R < (int64_t(1) << (Width - 1));
  return R >= 0 && R < (int64_t(1) << Width) && !(HasZeroAs && R == 0);
}

#define PI32V2_GEN_COMPRESS
#include "PI32V2CompressGen.inc"

// A register mask as a 16-bit form's range (rlist4): r3 down to r<k>, or r<k>
// down to r4; -1 if it is neither.
int rangeOf(uint64_t Mask) {
  for (unsigned K = 0; K < 4; ++K)
    if (Mask == (0xFu & ~((1u << K) - 1)))
      return K;
  for (unsigned K = 4; K < 16; ++K)
    if (Mask == (((2u << K) - 1) & ~0xFu))
      return K;
  return -1;
}

// A push or pop of a mask that is one range has a 16-bit form (the generated
// rules map operands one to one, a mask to a range is not one of them).
bool compressList(MCInst &Inst) {
  struct ListForm {
    unsigned Mask, Range, Alone; // Alone: only rets or pc, no general register
  };
  static const ListForm Forms[] = {
      {PI32V2::PUSH_RMASK, PI32V2::PUSH_RL, 0},
      {PI32V2::POP_RMASK, PI32V2::POP_RL, 0},
      {PI32V2::PUSH_RETS_RMASK, PI32V2::PUSH_RETS_RL, PI32V2::PUSH_RETS},
      {PI32V2::POP_PC_RMASK, PI32V2::POP_PC_RL, PI32V2::POP_PC},
      {PI32V2::POP_RETS_RMASK, PI32V2::POP_RETS_RL, 0},
  };
  for (const ListForm &F : Forms) {
    if (F.Mask != Inst.getOpcode() || Inst.getNumOperands() != 1 ||
        !Inst.getOperand(0).isImm())
      continue;
    uint64_t Mask = Inst.getOperand(0).getImm();
    unsigned Opc;
    int K = rangeOf(Mask);
    if (Mask == 0 && F.Alone)
      Opc = F.Alone;
    else if (K >= 0)
      Opc = F.Range;
    else
      return false;
    MCInst S;
    S.setOpcode(Opc);
    S.setLoc(Inst.getLoc());
    if (Opc == F.Range)
      S.addOperand(MCOperand::createImm(K));
    Inst = S;
    return true;
  }
  return false;
}

// A byte or halfword extension as a bit field (the code generator's) is the
// 16-bit .b0/.l form when both registers are r0..r7.
bool compressExt(MCInst &Inst, const MCRegisterInfo &MRI) {
  unsigned Opc = Inst.getOpcode();
  if ((Opc != PI32V2::SEXTRA_RRII && Opc != PI32V2::UEXTRA_RRII) ||
      !Inst.getOperand(2).isImm() || !Inst.getOperand(3).isImm() ||
      Inst.getOperand(2).getImm() != 0)
    return false;
  int64_t L = Inst.getOperand(3).getImm();
  const MCRegisterClass &Lo = MRI.getRegClass(PI32V2::GPRLoRegClassID);
  if ((L != 8 && L != 16) || !Lo.contains(Inst.getOperand(0).getReg()) ||
      !Lo.contains(Inst.getOperand(1).getReg()))
    return false;
  bool S = Opc == PI32V2::SEXTRA_RRII;
  MCInst E;
  E.setOpcode(L == 8 ? (S ? PI32V2::EXT_B0_S : PI32V2::EXT_B0_U)
                     : (S ? PI32V2::EXT_L_S : PI32V2::EXT_L_U));
  E.setLoc(Inst.getLoc());
  E.addOperand(Inst.getOperand(0));
  E.addOperand(Inst.getOperand(1));
  Inst = E;
  return true;
}

bool same(const MCOperand &A, const MCOperand &B) {
  if (A.isReg() && B.isReg())
    return A.getReg() == B.getReg();
  if (A.isImm() && B.isImm())
    return A.getImm() == B.getImm();
  return false;
}

// The sampled compression table has no negative or >=32 samples and cannot express
// a semantic alias whose assembly spelling differs (xor -1 versus not).
bool compressNegAndNot(MCInst &Inst, const MCRegisterInfo &MRI) {
  const MCRegisterClass &Lo = MRI.getRegClass(PI32V2::GPRLoRegClassID);
  unsigned Opc = Inst.getOpcode();
  if (Opc == PI32V2::ADD_S14_RRI && Inst.getNumOperands() == 3 &&
      Inst.getOperand(0).isReg() && Inst.getOperand(1).isReg() &&
      Lo.contains(Inst.getOperand(0).getReg()) &&
      Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg() &&
      Inst.getOperand(2).isImm()) {
    int64_t V = Inst.getOperand(2).getImm();
    if (V >= 32 && V <= 127) {
      Inst.setOpcode(V < 64 ? PI32V2::ADD_RI_U5_HI : PI32V2::ADD_RI_U6_HI);
      return true;
    }
  }
  if ((Opc == PI32V2::LI_S16 || Opc == PI32V2::LI_U32) &&
      Inst.getNumOperands() == 2 && Inst.getOperand(0).isReg() &&
      Lo.contains(Inst.getOperand(0).getReg()) &&
      Inst.getOperand(1).isImm()) {
    int64_t V = Inst.getOperand(1).getImm();
    if (Opc == PI32V2::LI_U32)
      V = int32_t(uint32_t(V));
    if (V >= -32 && V < 0) {
      Inst.setOpcode(PI32V2::LI_NEG5);
      Inst.getOperand(1).setImm(V);
      return true;
    }
  }
  if (Opc == PI32V2::XOR_MODIMM_RRI && Inst.getNumOperands() == 3 &&
      Inst.getOperand(0).isReg() && Inst.getOperand(1).isReg() &&
      Lo.contains(Inst.getOperand(0).getReg()) &&
      Lo.contains(Inst.getOperand(1).getReg()) &&
      Inst.getOperand(2).isImm() &&
      uint32_t(Inst.getOperand(2).getImm()) == UINT32_MAX) {
    MCInst Short;
    Short.setOpcode(PI32V2::NOT_RR);
    Short.setLoc(Inst.getLoc());
    Short.addOperand(Inst.getOperand(0));
    Short.addOperand(Inst.getOperand(1));
    Inst = Short;
    return true;
  }
  return false;
}
} // namespace

bool PI32V2::compressInst(MCInst &Inst, const MCInstrInfo &MII,
                          const MCRegisterInfo &MRI) {
  // An expression keeps its form (and its relocation), except a branch
  // target: a branch starts in its shortest form and grows by relaxation, as
  // JieLi's assembler does it.
  const MCInstrDesc &LD = MII.get(Inst.getOpcode());
  for (unsigned K = 0; K < Inst.getNumOperands(); ++K) {
    const MCOperand &MO = Inst.getOperand(K);
    if (!MO.isReg() && !MO.isImm() &&
        !(MO.isExpr() && K < LD.getNumOperands() &&
          LD.operands()[K].OperandType == MCOI::OPERAND_PCREL))
      return false;
  }
  if (compressList(Inst) || compressExt(Inst, MRI) ||
      compressNegAndNot(Inst, MRI))
    return true;
  for (const PI32V2CompressRule &R : CompressRules) {
    if (R.Long != Inst.getOpcode())
      continue;
    if (R.EqA >= 0 && !same(Inst.getOperand(R.EqA), Inst.getOperand(R.EqB)))
      continue;
    if (R.FixOp >= 0 && (!Inst.getOperand(R.FixOp).isImm() ||
                         Inst.getOperand(R.FixOp).getImm() != R.FixVal))
      continue;
    const MCInstrDesc &D = MII.get(R.Short);
    if (D.getNumOperands() != R.NumOps)
      continue;
    MCInst S;
    S.setOpcode(R.Short);
    S.setLoc(Inst.getLoc());
    bool Ok = true;
    for (unsigned K = 0; K < R.NumOps && Ok; ++K) {
      const MCOperand &MO = Inst.getOperand(R.Prog[K]);
      int16_t RC = D.operands()[K].RegClass;
      bool PCRel = D.operands()[K].OperandType == MCOI::OPERAND_PCREL;
      if (MO.isReg())
        Ok = RC >= 0 && MRI.getRegClass(RC).contains(MO.getReg());
      else if (MO.isExpr())
        Ok = PCRel; // the target: relaxation makes it reach
      else
        Ok = RC < 0 && !PCRel && immFits(R.Short, K, MO.getImm());
      S.addOperand(MO);
    }
    if (Ok) {
      Inst = S;
      return true;
    }
  }
  return false;
}

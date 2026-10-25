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

bool same(const MCOperand &A, const MCOperand &B) {
  if (A.isReg() && B.isReg())
    return A.getReg() == B.getReg();
  if (A.isImm() && B.isImm())
    return A.getImm() == B.getImm();
  return false;
}
} // namespace

bool PI32V2::compressInst(MCInst &Inst, const MCInstrInfo &MII,
                          const MCRegisterInfo &MRI) {
  for (const MCOperand &MO : Inst)
    if (!MO.isReg() && !MO.isImm())
      return false; // an expression keeps its form (and its relocation)
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
      if (MO.isReg())
        Ok = RC >= 0 && MRI.getRegClass(RC).contains(MO.getReg());
      else
        Ok = RC < 0 && immFits(R.Short, K, MO.getImm());
      S.addOperand(MO);
    }
    if (Ok) {
      Inst = S;
      return true;
    }
  }
  return false;
}

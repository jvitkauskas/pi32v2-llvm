//===-- PI32V2Parallel.cpp - PI32V2 parallel issue ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parallel issue: `A #` and a 16-bit load or store B after it go as one packet
// (pi32v2-isa docs/COMPILER.md, tried on the FM-1). B reads the registers as
// they were before A, and A's result stays where B loads the same register, so
// A must write none of B's registers; A may read one B writes (as in JieLi's
// `r1 += r4 #` / `r4 = [r0+56]`). B is one of the short forms on r0..r7 (an
// added register r8..r15); A an ALU, move, multiply, float or bit-field form
// with a # twin (the twin is the same size).
//
//===----------------------------------------------------------------------===//

#include "PI32V2BaseInfo.h"
#include "PI32V2MCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <mutex>

using namespace llvm;

unsigned PI32V2::parallelTwin(unsigned Opc, const MCInstrInfo &MII) {
  static DenseMap<unsigned, unsigned> Twins;
  static std::once_flag Once;
  std::call_once(Once, [&] {
    StringMap<unsigned> ByName;
    for (unsigned O = 0, E = MII.getNumOpcodes(); O != E; ++O)
      ByName[MII.getName(O)] = O;
    for (unsigned O = 0, E = MII.getNumOpcodes(); O != E; ++O) {
      StringRef N = MII.getName(O);
      if (!N.consume_back("_P"))
        continue;
      auto It = ByName.find(N);
      if (It == ByName.end())
        continue;
      const MCInstrDesc &B = MII.get(It->second), &P = MII.get(O);
      // (only these forms have twins; the generated short forms carry no
      // side-effect flags, so those are not asked)
      if (B.getSize() != P.getSize() ||
          B.getNumOperands() != P.getNumOperands() || B.mayLoad() ||
          B.mayStore() || B.isBranch() || B.isCall() || B.isReturn() ||
          B.isTerminator())
        continue;
      bool Other = false;
      for (MCPhysReg R : B.implicit_uses())
        Other |= R != PI32V2::PSR;
      for (MCPhysReg R : B.implicit_defs())
        Other |= R != PI32V2::PSR;
      if (!Other)
        Twins[It->second] = O;
    }
  });
  return Twins.lookup(Opc);
}

bool PI32V2::isParallelB(const MCInst &B, const MCRegisterInfo &MRI) {
  bool Pi = false;
  switch (B.getOpcode()) {
  case PI32V2::LD_BU_PI:
  case PI32V2::LD_HU_PI:
  case PI32V2::LD_W_PI:
  case PI32V2::ST_B_PI:
  case PI32V2::ST_H_PI:
  case PI32V2::ST_W_PI:
    Pi = true;
    break;
  case PI32V2::LD_W_SP:
  case PI32V2::ST_W_SP:
  case PI32V2::LD_BU:
  case PI32V2::LD_BU_PIN:
  case PI32V2::LD_HU:
  case PI32V2::LD_HU_PIN:
  case PI32V2::LD_W:
  case PI32V2::LD_W_PIN:
  case PI32V2::ST_B:
  case PI32V2::ST_B_PIN:
  case PI32V2::ST_H:
  case PI32V2::ST_H_PIN:
  case PI32V2::ST_W:
  case PI32V2::ST_W_PIN:
    break;
  default:
    return false;
  }
  const MCRegisterClass &Lo = MRI.getRegClass(PI32V2::GPRLoRegClassID);
  const MCRegisterClass &Hi = MRI.getRegClass(PI32V2::GPRHiRegClassID);
  for (const MCOperand &O : B)
    if (O.isReg() && !Lo.contains(O.getReg()) && !(Pi && Hi.contains(O.getReg())))
      return false;
  return true;
}

bool PI32V2::canIssueParallel(const MCInst &A, const MCInst &B,
                              const MCInstrInfo &MII,
                              const MCRegisterInfo &MRI) {
  if (!parallelTwin(A.getOpcode(), MII) || !isParallelB(B, MRI))
    return false;
  const MCRegisterClass &GPR = MRI.getRegClass(PI32V2::GPRRegClassID);
  const MCRegisterClass &Pair = MRI.getRegClass(PI32V2::GPRPairRegClassID);
  for (const MCOperand &O : A)
    if (O.isReg() && !GPR.contains(O.getReg()) && !Pair.contains(O.getReg()))
      return false;
  unsigned NDefs = MII.get(A.getOpcode()).getNumDefs();
  for (unsigned K = 0; K != NDefs; ++K) {
    if (!A.getOperand(K).isReg())
      continue;
    for (const MCOperand &O : B)
      if (O.isReg() && MRI.regsOverlap(A.getOperand(K).getReg(), O.getReg()))
        return false;
  }
  return true;
}

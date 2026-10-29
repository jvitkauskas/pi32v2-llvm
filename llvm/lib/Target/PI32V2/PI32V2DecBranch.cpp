//===-- PI32V2DecBranch.cpp - PI32V2 decrement-and-branch -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `if (--r != 0) goto T` is one 4-byte instruction (dbnz) where the add and
// the branch take 4 bytes only with r in r0..r7 and a near target, and it
// leaves the flags alone. JieLi's compiler ends its counted loops with it.
// This pass, run on the final code, joins
//
//   r = r + -1 ... if (r != 0) goto T          (r not touched in between)
//
// into PseudoDBNZ. The add's flags are not read by anything after it.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-dec-branch"

namespace {
class PI32V2DecBranch : public MachineFunctionPass {
public:
  static char ID;
  PI32V2DecBranch() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PI32V2 decrement and branch"; }
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const PI32V2InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  bool convert(MachineBasicBlock &B);
  bool flagsRead(const MachineBasicBlock &B) const;
};
} // namespace

char PI32V2DecBranch::ID = 0;

FunctionPass *llvm::createPI32V2DecBranchPass() { return new PI32V2DecBranch(); }

// True if the first use of psr in B (before anything writes it) is a read.
bool PI32V2DecBranch::flagsRead(const MachineBasicBlock &B) const {
  for (const MachineInstr &MI : B) {
    if (MI.isDebugInstr())
      continue;
    if (MI.readsRegister(PI32V2::PSR, TRI))
      return true;
    if (MI.modifiesRegister(PI32V2::PSR, TRI))
      return false;
  }
  // flags may be read further on: only the blocks themselves are looked at
  return false;
}

bool PI32V2DecBranch::convert(MachineBasicBlock &B) {
  MachineBasicBlock::iterator BrI = B.getFirstTerminator();
  if (BrI == B.end() || std::next(BrI) != B.end())
    return false;
  MachineInstr &Br = *BrI;
  if ((Br.getOpcode() != PI32V2::BNE_RI && Br.getOpcode() != PI32V2::BNE_RI_L) ||
      Br.getOperand(1).getImm() != 0)
    return false;
  Register R = Br.getOperand(0).getReg();
  // the decrement: the last write of r in the block, nothing in between reads
  // or writes r or reads the flags
  MachineInstr *Dec = nullptr;
  for (auto I = BrI; I != B.begin();) {
    MachineInstr &MI = *--I;
    if (MI.isDebugInstr())
      continue;
    if (MI.getOpcode() == PI32V2::ADD_S14_RRI && MI.getOperand(0).getReg() == R &&
        MI.getOperand(1).getReg() == R && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == -1) {
      Dec = &MI;
      break;
    }
    if (MI.readsRegister(R, TRI) || MI.modifiesRegister(R, TRI) ||
        MI.readsRegister(PI32V2::PSR, TRI) || MI.isCall() || MI.isInlineAsm() ||
        MI.hasUnmodeledSideEffects() || MI.isBundle())
      return false;
  }
  if (!Dec)
    return false;
  // The flags the add wrote are not read after the branch
  for (MachineBasicBlock *S : B.successors())
    if (flagsRead(*S))
      return false;
  BuildMI(B, BrI, Br.getDebugLoc(), TII->get(PI32V2::PseudoDBNZ), R)
      .addReg(R)
      .add(Br.getOperand(2));
  Dec->eraseFromParent();
  Br.eraseFromParent();
  return true;
}

bool PI32V2DecBranch::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  const auto &ST = MF.getSubtarget<PI32V2Subtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  // the offset is 16 bits: leave out functions that might not fit
  uint64_t Size = 0;
  for (const MachineBasicBlock &B : MF)
    for (const MachineInstr &MI : B)
      Size += TII->getInstSizeInBytes(MI);
  if (Size >= 30000)
    return false;
  bool Changed = false;
  for (MachineBasicBlock &B : MF)
    Changed |= convert(B);
  return Changed;
}

//===-- PI32V2AnchorBase.cpp - PI32V2 base register anchors ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 16-bit loads and stores reach 5 bits of offset in units of the access
// size (words -64..60, halfwords -32..30, bytes -16..15); every access farther
// from its base is 4 bytes. A run of accesses at, say, [r0+120] .. [r0+132]
// becomes
//
//   r1 = [++r0=120]      (the first access, which steps the base to its offset)
//   r2 = [r0+4]          (the others, in 16 bits)
//   ...
//
// as JieLi's compiler does it. When the base is needed afterwards the
// pre-increment costs a copy of it, which the saving has to cover. Before
// register allocation, on the selected accesses of one block.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-anchor-base"

static cl::opt<bool> EnableAnchor("pi32v2-anchor-base", cl::Hidden, cl::init(true),
                                  cl::desc("Anchor far accesses with a pre-increment"));

namespace {
class PI32V2AnchorBase : public MachineFunctionPass {
public:
  static char ID;
  PI32V2AnchorBase() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PI32V2 base register anchors"; }

private:
  struct Access {
    MachineInstr *MI;
    Register Base;
    int64_t Off;
    unsigned Size;
    bool Short; // has a 16-bit form (an unsigned load, a store)
    bool Store;
    unsigned Pre; // the pre-increment pseudo
  };
  bool describe(MachineInstr &MI, Access &A) const;
  bool convert(MachineBasicBlock &B);
  MachineRegisterInfo *MRI = nullptr;
  const PI32V2InstrInfo *TII = nullptr;
};
} // namespace

char PI32V2AnchorBase::ID = 0;

FunctionPass *llvm::createPI32V2AnchorBasePass() { return new PI32V2AnchorBase(); }

bool PI32V2AnchorBase::describe(MachineInstr &MI, Access &A) const {
  A.MI = &MI;
  A.Short = true;
  A.Store = false;
  unsigned BaseIdx = 1, OffIdx = 2;
  switch (MI.getOpcode()) {
  case PI32V2::LD_W_L: A.Size = 4; A.Pre = PI32V2::LDW_PRE; break;
  case PI32V2::LD_HU_L: A.Size = 2; A.Pre = PI32V2::LDHU_PRE; break;
  case PI32V2::LD_BU_L: A.Size = 1; A.Pre = PI32V2::LDBU_PRE; break;
  case PI32V2::LD_HS_L: A.Size = 2; A.Pre = PI32V2::LDHS_PRE; A.Short = false; break;
  case PI32V2::LD_BS_L: A.Size = 1; A.Pre = PI32V2::LDBS_PRE; A.Short = false; break;
  case PI32V2::ST_W_L: A.Size = 4; A.Pre = PI32V2::STW_PRE; A.Store = true; break;
  case PI32V2::ST_H_L: A.Size = 2; A.Pre = PI32V2::STH_PRE; A.Store = true; break;
  case PI32V2::ST_B_L: A.Size = 1; A.Pre = PI32V2::STB_PRE; A.Store = true; break;
  default: return false;
  }
  if (A.Store) {
    BaseIdx = 0;
    OffIdx = 1;
  }
  if (!MI.getOperand(BaseIdx).isReg() || !MI.getOperand(OffIdx).isImm() ||
      !MI.getOperand(BaseIdx).getReg().isVirtual())
    return false;
  A.Base = MI.getOperand(BaseIdx).getReg();
  A.Off = MI.getOperand(OffIdx).getImm();
  return true;
}

// the 16-bit forms' offset range for an access of Size bytes
static bool fitsShort(int64_t Off, unsigned Size) {
  return Off % Size == 0 && isInt<5>(Off / int64_t(Size));
}

bool PI32V2AnchorBase::convert(MachineBasicBlock &B) {
  SmallVector<Access, 32> Acc;
  for (MachineInstr &MI : B) {
    Access A;
    if (describe(MI, A))
      Acc.push_back(A);
  }
  bool Changed = false;
  SmallPtrSet<MachineInstr *, 16> Done;
  for (unsigned I = 0; I < Acc.size(); ++I) {
    Access &First = Acc[I];
    if (Done.count(First.MI) || fitsShort(First.Off, First.Size))
      continue; // the anchor is a 32-bit access anyway
    // the later accesses on this base that the step brings in range
    SmallVector<unsigned, 8> Members;
    for (unsigned J = I + 1; J < Acc.size(); ++J) {
      Access &A = Acc[J];
      if (A.Base != First.Base || Done.count(A.MI) || !A.Short ||
          fitsShort(A.Off, A.Size))
        continue;
      if (fitsShort(A.Off - First.Off, A.Size))
        Members.push_back(J);
    }
    if (Members.empty())
      continue;
    // The base after the anchor: used by anything else than the members?
    unsigned Uses = 0, Inside = 1; // (the anchor itself)
    for (MachineInstr &U : MRI->use_nodbg_instructions(First.Base)) {
      (void)U;
      ++Uses;
    }
    for (unsigned J : Members)
      ++Inside;
    bool Live = Uses > Inside;
    // a use before the anchor, in this block, does not keep it alive
    if (Live) {
      bool After = false;
      for (MachineInstr &U : MRI->use_nodbg_instructions(First.Base)) {
        bool IsMember = &U == First.MI;
        for (unsigned J : Members)
          IsMember |= &U == Acc[J].MI;
        if (IsMember)
          continue;
        if (U.getParent() != &B) {
          After = true;
          break;
        }
        bool Seen = false;
        for (MachineInstr &M : B) {
          if (&M == First.MI)
            Seen = true;
          if (&M == &U) {
            After |= Seen;
            break;
          }
        }
        if (After)
          break;
      }
      Live = After;
    }
    int Saved = 2 * int(Members.size()) - (Live ? 2 : 0);
    if (Saved < 2)
      continue;
    // convert
    MachineInstr &F = *First.MI;
    DebugLoc DL = F.getDebugLoc();
    Register Wb = MRI->createVirtualRegister(&PI32V2::GPRRegClass);
    MachineInstrBuilder MIB;
    if (First.Store) {
      MIB = BuildMI(B, F, DL, TII->get(First.Pre), Wb)
                .addReg(First.Base)
                .addImm(First.Off)
                .add(F.getOperand(2));
    } else {
      MIB = BuildMI(B, F, DL, TII->get(First.Pre), F.getOperand(0).getReg())
                .addReg(Wb, RegState::Define)
                .addReg(First.Base)
                .addImm(First.Off);
    }
    MIB.cloneMemRefs(F);
    F.eraseFromParent();
    Done.insert(MIB.getInstr());
    for (unsigned J : Members) {
      MachineInstr &M = *Acc[J].MI;
      unsigned BaseIdx = Acc[J].Store ? 0 : 1;
      M.getOperand(BaseIdx).setReg(Wb);
      M.getOperand(BaseIdx + 1).setImm(Acc[J].Off - First.Off);
      Done.insert(&M);
    }
    Changed = true;
  }
  return Changed;
}

bool PI32V2AnchorBase::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableAnchor || skipFunction(MF.getFunction()))
    return false;
  const auto &ST = MF.getSubtarget<PI32V2Subtarget>();
  TII = ST.getInstrInfo();
  MRI = &MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &B : MF)
    Changed |= convert(B);
  return Changed;
}

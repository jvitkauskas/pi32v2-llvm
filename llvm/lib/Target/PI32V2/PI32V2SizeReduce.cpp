//===-- PI32V2SizeReduce.cpp - Recolor local physical live ranges --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "pi32v2-size-reduce"

static cl::opt<bool> EnableSizeReduce(
    "pi32v2-enable-size-reduce", cl::Hidden, cl::init(true),
    cl::desc("Recolor local PI32V2 live ranges for shorter encodings"));

static cl::opt<bool> EnableGlobalRecolor(
    "pi32v2-global-recolor", cl::Hidden, cl::init(true),
    cl::desc("Swap PI32V2 callee-saved register assignments for size"));

namespace {
class PI32V2SizeReduce : public MachineFunctionPass {
public:
  static char ID;
  PI32V2SizeReduce() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "PI32V2 size reduction"; }
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs().setTracksLiveness();
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  const PI32V2InstrInfo *TII = nullptr;
  const PI32V2RegisterInfo *TRI = nullptr;
  unsigned encodedSize(const MachineInstr &MI, Register From = Register(),
                       Register To = Register(), bool Swap = false) const;
  bool recolorCalleeSaved(MachineFunction &MF);
  Register recolor(MachineInstr &Def, Register From, const LiveRegUnits &Live,
                   const LiveRegUnits &LiveOut);
};
} // namespace

char PI32V2SizeReduce::ID = 0;

INITIALIZE_PASS(PI32V2SizeReduce, DEBUG_TYPE, "PI32V2 size reduction", false, false)

FunctionPass *llvm::createPI32V2SizeReducePass() {
  return new PI32V2SizeReduce();
}

// Use the emitter's compression rules rather than approximating the immediate
// and register constraints. Pseudos are unchanged in cost by a high-to-low
// recoloring; do not count any possible savings from them.
unsigned PI32V2SizeReduce::encodedSize(const MachineInstr &MI, Register From,
                                     Register To, bool Swap) const {
  if (MI.isPseudo() || MI.isInlineAsm() || MI.isDebugInstr())
    return 0;
  MCInst Inst;
  Inst.setOpcode(MI.getOpcode());
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (MO.isReg()) {
      Register R = MO.getReg();
      if (R == From)
        R = To;
      else if (Swap && R == To)
        R = From;
      Inst.addOperand(MCOperand::createReg(R));
    } else if (MO.isImm())
      Inst.addOperand(MCOperand::createImm(MO.getImm()));
    else
      return MI.getDesc().getSize();
  }
  PI32V2::compressInst(Inst, *TII, *TRI);
  return TII->get(Inst.getOpcode()).getSize();
}

bool PI32V2SizeReduce::recolorCalleeSaved(MachineFunction &MF) {
  bool Changed = false;
  unsigned FirstSaved = MF.getFunction().hasFnAttribute("interrupt") ? 0 : 4;
  // Both sides of a swap are callee-saved. Calls preserve them, and PEI has
  // not yet chosen the save mask. Never change a fixed physical operand or
  // split an allocated register pair.
  for (unsigned Round = 0; Round != 4; ++Round) {
    unsigned Eligible = 0xFFF0;
    unsigned Used = 0;
    unsigned Saved = TRI->getFrameRegister(MF) == PI32V2::R7 ? 1 << 7 : 0;
    for (auto LI : MF.front().liveins())
      for (unsigned R = 4; R != 16; ++R)
        if (TRI->regsOverlap(LI.PhysReg, PI32V2::R0 + R))
          Eligible &= ~(1 << R);
    SmallVector<std::tuple<MachineInstr *, unsigned, unsigned>, 64> Instructions;
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr()) {
          // A pair debug location cannot describe a scalar half moved away
          // from its partner. Scalar locations can be renamed with the value.
          for (const MachineOperand &MO : MI.operands())
            if (MO.isReg() && MO.getReg())
              for (unsigned R = 4; R != 16; ++R)
                if (MO.getReg() != PI32V2::R0 + R &&
                    TRI->regsOverlap(MO.getReg(), PI32V2::R0 + R))
                  Eligible &= ~(1 << R);
          continue;
        }
        unsigned Regs = 0;
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isRegMask()) {
            for (unsigned R = FirstSaved; R != 16; ++R)
              if (MO.clobbersPhysReg(PI32V2::R0 + R)) {
                Eligible &= ~(1 << R);
                Used |= 1 << R;
                Saved |= 1 << R;
              }
            continue;
          }
          if (!MO.isReg() || !MO.getReg())
            continue;
          for (unsigned R = FirstSaved; R != 16; ++R) {
            Register Reg = PI32V2::R0 + R;
            if (!TRI->regsOverlap(MO.getReg(), Reg))
              continue;
            Regs |= 1 << R;
            Used |= 1 << R;
            if (MO.isDef())
              Saved |= 1 << R;
            if (MO.getReg() != Reg || !MO.isRenamable() || MO.isImplicit() ||
                MO.getSubReg() || MI.isInlineAsm() ||
                (MI.isPseudo() && !MI.isCopy())) {
              Eligible &= ~(1 << R);
              continue;
            }
            const TargetRegisterClass *RC =
                TII->getRegClass(MI.getDesc(), MO.getOperandNo());
            if (RC && !RC->hasSubClassEq(&PI32V2::GPRRegClass))
              Eligible &= ~(1 << R);
          }
        }
        if (Regs)
          Instructions.emplace_back(&MI, encodedSize(MI), Regs);
      }
    for (unsigned R = 4; R != 16; ++R)
      if (MF.getRegInfo().isReserved(PI32V2::R0 + R))
        Eligible &= ~(1 << R);

    auto SaveSize = [](unsigned Mask) {
      // Both push and pop: a range down to r4 (or ending at r3 in an
      // interrupt) is two bytes each, a sparse mask four each.
      if (!Mask)
        return 0;
      if (Mask & 15) {
        for (unsigned K = 0; K != 4; ++K)
          if (Mask == (15u & ~((1u << K) - 1)))
            return 4;
        return 8;
      }
      return isPowerOf2_32((Mask | 15) + 1) ? 4 : 8;
    };
    int BestSaving = 0;
    Register BestLow, BestHigh;
    for (unsigned Low = 4; Low != 8; ++Low)
      for (unsigned High = 8; High != 16; ++High) {
        if (!(Eligible & (1 << Low)) || !(Eligible & (1 << High)) ||
            !(Used & (1 << High)))
          continue;
        Register L = PI32V2::R0 + Low, H = PI32V2::R0 + High;
        unsigned NewSaved = Saved;
        if (bool(Saved & (1 << Low)) != bool(Saved & (1 << High)))
          NewSaved ^= (1 << Low) | (1 << High);
        int Saving = SaveSize(Saved) - SaveSize(NewSaved);
        for (auto [MI, Size, Regs] : Instructions) {
          if (!(Regs & ((1 << Low) | (1 << High))))
            continue;
          Saving += int(Size) - int(encodedSize(*MI, H, L, true));
          // A zero-compare branch with an MBB target is not lowered by
          // encodedSize. Conservatively charge for widening it, but do not
          // assume a saving: relaxation may need the long form anyway.
          if ((MI->getOpcode() == PI32V2::BEQ_RI ||
               MI->getOpcode() == PI32V2::BNE_RI ||
               MI->getOpcode() == PI32V2::BEQ_RI_L ||
               MI->getOpcode() == PI32V2::BNE_RI_L) &&
              MI->getOperand(0).getReg() == L &&
              MI->getOperand(1).isImm() && MI->getOperand(1).getImm() == 0)
            Saving -= 2;
        }
        if (Saving > BestSaving) {
          BestSaving = Saving;
          BestLow = L;
          BestHigh = H;
        }
      }
    if (!BestLow)
      break;
    auto Replace = [&](Register R) {
      return R == BestLow ? BestHigh : R == BestHigh ? BestLow : R;
    };
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB)
        for (MachineOperand &MO : MI.operands())
          if (MO.isReg() && Replace(MO.getReg()) != MO.getReg()) {
            bool Renamable = MO.isRenamable();
            MO.setReg(Replace(MO.getReg()));
            MO.setIsRenamable(Renamable);
          }
      std::vector<MachineBasicBlock::RegisterMaskPair> OldLiveIns;
      MBB.clearLiveIns(OldLiveIns);
      for (auto LI : OldLiveIns)
        MBB.addLiveIn(Replace(LI.PhysReg), LI.LaneMask);
      MBB.sortUniqueLiveIns();
    }
    Changed = true;
  }
  return Changed;
}

Register PI32V2SizeReduce::recolor(MachineInstr &Def, Register From,
                                 const LiveRegUnits &Live,
                                 const LiveRegUnits &LiveOut) {
  MachineBasicBlock &MBB = *Def.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  // This pass only changes values defined locally. A read/modify/write at the
  // start would require changing the incoming value too.
  if (Def.readsRegister(From, TRI))
    return Register();

  auto Begin = Def.getIterator(), End = std::next(Begin);
  // Include dependent read/modify/writes, stopping before an independent def.
  for (; End != MBB.end(); ++End)
    if (!End->isDebugInstr() && End->modifiesRegister(From, TRI) &&
        !End->readsRegister(From, TRI))
      break;
  if (End == MBB.end() && !LiveOut.available(From))
    return Register();

  auto Last = Begin;
  for (auto I = Begin; I != End; ++I) {
    if (I->isDebugInstr())
      continue;
    for (const MachineOperand &MO : I->operands()) {
      if (!MO.isReg() || !MO.getReg() || !TRI->regsOverlap(MO.getReg(), From))
        continue;
      // Do not split register pairs, change ABI registers, or violate fixed
      // instruction constraints. All operands of this local value must agree.
      if (MO.getReg() != From || !MO.isRenamable() || MO.getSubReg() ||
          MO.isImplicit() || I->isInlineAsm())
        return Register();
      Last = I;
    }
  }
  End = std::next(Last);

  LiveRegUnits Before = Live;
  Before.stepBackward(Def);
  LiveRegUnits Touched(*TRI);
  unsigned OldSize = 0;
  for (auto I = Begin; I != End; ++I) {
    if (!I->isDebugInstr())
      Touched.accumulate(*I);
    OldSize += encodedSize(*I);
  }

  Register Best;
  unsigned BestSize = OldSize;
  for (MCPhysReg R : PI32V2::GPRLoRegClass) {
    if (MRI.isReserved(R) || !Before.available(R) || !Touched.available(R))
      continue;
    // Do not open a new callee save for a local encoding saving. The pass runs
    // before prologue/epilogue insertion, so existing saves are still flexible.
    if (R >= PI32V2::R4 && !MRI.isPhysRegUsed(R))
      continue;
    bool Legal = true;
    unsigned Size = 0;
    for (auto I = Begin; I != End && Legal; ++I) {
      if (!I->isDebugInstr()) {
        for (const MachineOperand &MO : I->explicit_operands()) {
          if (!MO.isReg() || MO.getReg() != From)
            continue;
          const TargetRegisterClass *RC =
              TII->getRegClass(I->getDesc(), MO.getOperandNo());
          if (RC && !RC->contains(R)) {
            Legal = false;
            break;
          }
        }
      }
      Size += encodedSize(*I, From, Register(R));
    }
    if (Legal && Size < BestSize) {
      Best = R;
      BestSize = Size;
    }
  }
  if (!Best)
    return Register();
  for (auto I = Begin; I != End; ++I)
    for (MachineOperand &MO : I->operands())
      if (MO.isReg() && MO.getReg() == From) {
        bool Renamable = MO.isRenamable();
        MO.setReg(Best);
        MO.setIsRenamable(Renamable);
      }
  return Best;
}

bool PI32V2SizeReduce::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableSizeReduce || skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<PI32V2Subtarget>().getInstrInfo();
  TRI = &TII->getRegisterInfo();
  bool Changed = EnableGlobalRecolor && recolorCalleeSaved(MF);
  for (MachineBasicBlock &MBB : MF) {
    LiveRegUnits LiveOut(*TRI);
    LiveOut.addLiveOuts(MBB);
    LiveRegUnits Live = LiveOut;
    for (MachineInstr &MI : llvm::reverse(MBB)) {
      if (MI.isDebugInstr())
        continue;
      SmallVector<Register, 2> Defs;
      for (const MachineOperand &MO : MI.explicit_operands())
        if (MO.isReg() && MO.isDef() && MO.isRenamable() &&
            PI32V2::GPRHiRegClass.contains(MO.getReg()) &&
            !is_contained(Defs, MO.getReg()))
          Defs.push_back(MO.getReg());
      for (Register R : Defs)
        if (Register To = recolor(MI, R, Live, LiveOut)) {
          if (!Live.available(R)) {
            Live.removeReg(R);
            Live.addReg(To);
          }
          Changed = true;
        }
      Live.stepBackward(MI);
    }
  }
  return Changed;
}

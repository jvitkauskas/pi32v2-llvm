//===-- PI32V2FrameLowering.cpp - PI32V2 frames ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// As JieLi's compiler does it (pi32v2-isa docs/ABI.md section 6): one push of
// rets and the saved registers, sp += -n for the rest of the frame, and the
// matching pop, which loads rets into pc and so returns. A function that
// saves nothing returns with rts. r7 is the frame base when the frame has
// dynamic objects.
//
//===----------------------------------------------------------------------===//

#include "PI32V2FrameLowering.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2MachineFunctionInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

PI32V2FrameLowering::PI32V2FrameLowering(const PI32V2Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4), 0,
                          Align(4)),
      STI(STI) {}

bool PI32V2FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken();
}

// r7 is callee-saved (docs/ABI.md section 4): a function that makes it its
// frame base saves it like any other.
void PI32V2FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                               BitVector &SavedRegs,
                                               RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (hasFP(MF))
    SavedRegs.set(PI32V2::R7);
}

bool PI32V2FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

static unsigned savedBytes(const MachineFunction &MF) {
  return 4 * MF.getFrameInfo().getCalleeSavedInfo().size();
}

static bool isInterrupt(const MachineFunction &MF) {
  return MF.getFunction().hasFnAttribute("interrupt");
}

// An interrupt handler's {psr, reti} (special registers 5 and 0), pushed
// first and popped last, as JieLi's compiler does (docs/ABI.md section 7.1).
static constexpr unsigned InterruptSRMask = (1u << 5) | (1u << 0);

static unsigned varArgsBytes(const MachineFunction &MF) {
  return MF.getInfo<PI32V2MachineFunctionInfo>()->getVarArgsSaveSize();
}

// sp += Amount, in steps the instruction can take (-4096..4092).
static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     const DebugLoc &DL, const PI32V2InstrInfo &TII,
                     int64_t Amount, MachineInstr::MIFlag Flag) {
  while (Amount) {
    int64_t Step = Amount < -4096 ? -4096 : Amount > 4092 ? 4092 : Amount;
    BuildMI(MBB, I, DL, TII.get(PI32V2::ADD_SP_I_L)).addImm(Step).setMIFlag(Flag);
    Amount -= Step;
  }
}

void PI32V2FrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PI32V2InstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL;
  // A variadic function first pushes the argument registers its named
  // arguments did not use, so they continue the stack arguments.
  auto *FI = MF.getInfo<PI32V2MachineFunctionInfo>();
  if (unsigned VA = varArgsBytes(MF)) {
    uint16_t Mask = 0;
    for (unsigned R = FI->getFirstVarArgReg(); R < 4; ++R)
      Mask |= 1u << R;
    auto B = BuildMI(MBB, I, DL, TII.get(PI32V2::PUSH_RMASK))
                 .addImm(Mask)
                 .setMIFlag(MachineInstr::FrameSetup);
    for (unsigned R = FI->getFirstVarArgReg(); R < 4; ++R)
      B.addReg(PI32V2::R0 + R, RegState::Implicit);
    (void)VA;
  }
  if (isInterrupt(MF))
    BuildMI(MBB, MBB.begin(), DL, TII.get(PI32V2::PUSH_SRL))
        .addImm(InterruptSRMask)
        .setMIFlag(MachineInstr::FrameSetup);
  // After the push of the saved registers.
  while (I != MBB.end() && I->getFlag(MachineInstr::FrameSetup))
    ++I;
  uint64_t StackSize = alignTo(MFI.getStackSize(), getStackAlign());
  MFI.setStackSize(StackSize);
  int64_t Rest = int64_t(StackSize) - savedBytes(MF) - varArgsBytes(MF);
  adjustSP(MBB, I, DL, TII, -Rest, MachineInstr::FrameSetup);
  if (hasFP(MF))
    BuildMI(MBB, I, DL, TII.get(PI32V2::MOV_R_SR), PI32V2::R7)
        .addReg(PI32V2::SP)
        .setMIFlag(MachineInstr::FrameSetup);
}

void PI32V2FrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PI32V2InstrInfo &TII = *STI.getInstrInfo();
  // Before the pop of the saved registers (or the return).
  MachineBasicBlock::iterator I = MBB.getFirstTerminator();
  while (I != MBB.begin() && std::prev(I)->getFlag(MachineInstr::FrameDestroy))
    --I;
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  int64_t Rest = int64_t(MFI.getStackSize()) - savedBytes(MF) - varArgsBytes(MF);
  if (hasFP(MF))
    BuildMI(MBB, I, DL, TII.get(PI32V2::MOV_SR_R), PI32V2::SP)
        .addReg(PI32V2::R7)
        .setMIFlag(MachineInstr::FrameDestroy);
  adjustSP(MBB, I, DL, TII, Rest, MachineInstr::FrameDestroy);
  // The pushed argument registers, after the saved registers' pop.
  if (unsigned VA = varArgsBytes(MF))
    adjustSP(MBB, MBB.getFirstTerminator(), DL, TII, VA,
             MachineInstr::FrameDestroy);
  if (isInterrupt(MF))
    BuildMI(MBB, MBB.getFirstTerminator(), DL, TII.get(PI32V2::POP_SRL))
        .addImm(InterruptSRMask)
        .setMIFlag(MachineInstr::FrameDestroy);
}

MachineBasicBlock::iterator PI32V2FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = I->getOperand(0).getImm();
    if (Amount) {
      Amount = alignTo(Amount, getStackAlign());
      if (I->getOpcode() == PI32V2::ADJCALLSTACKDOWN)
        Amount = -Amount;
      adjustSP(MBB, I, I->getDebugLoc(), *STI.getInstrInfo(), Amount,
               MachineInstr::NoFlags);
    }
  }
  return MBB.erase(I);
}

static uint16_t gprMask(ArrayRef<CalleeSavedInfo> CSI, bool &Rets) {
  uint16_t Mask = 0;
  Rets = false;
  for (const CalleeSavedInfo &I : CSI) {
    MCRegister R = I.getReg();
    if (R == PI32V2::RETS)
      Rets = true;
    else
      Mask |= 1u << (R - PI32V2::R0);
  }
  return Mask;
}

bool PI32V2FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;
  const PI32V2InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL;
  bool Rets;
  uint16_t Mask = gprMask(CSI, Rets);
  for (const CalleeSavedInfo &I : CSI)
    if (!MBB.isLiveIn(I.getReg()))
      MBB.addLiveIn(I.getReg());
  auto B = BuildMI(MBB, MI, DL,
                   TII.get(Rets ? PI32V2::PUSH_RETS_RMASK : PI32V2::PUSH_RMASK))
               .addImm(Mask)
               .setMIFlag(MachineInstr::FrameSetup);
  for (const CalleeSavedInfo &I : CSI)
    B.addReg(I.getReg(), RegState::Implicit);
  return true;
}

bool PI32V2FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;
  const PI32V2InstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  bool Rets;
  uint16_t Mask = gprMask(CSI, Rets);
  // A variadic function releases its pushed argument registers after the
  // pop, so its pop does not return.
  bool IsReturn = MI != MBB.end() && MI->getOpcode() == PI32V2::PseudoRET &&
                  !varArgsBytes(*MBB.getParent()) &&
                  !isInterrupt(*MBB.getParent()); // a handler returns with rti
  if (Rets && IsReturn) {
    // {pc, ...} = [sp++]: restores and returns; the return's uses move to it.
    auto B = BuildMI(MBB, MI, DL, TII.get(PI32V2::POP_PC_RMASK))
                 .addImm(Mask)
                 .setMIFlag(MachineInstr::FrameDestroy);
    for (const CalleeSavedInfo &I : CSI)
      if (I.getReg() != PI32V2::RETS)
        B.addReg(I.getReg(), RegState::ImplicitDefine);
    // Every register the return uses, the result registers included (they
    // are its explicit variadic operands), stays used by the pop.
    for (const MachineOperand &MO : MI->operands())
      if (MO.isReg() && MO.isUse())
        B.addReg(MO.getReg(), RegState::Implicit);
    MBB.erase(MI);
    return true;
  }
  auto B = BuildMI(MBB, MI, DL,
                   TII.get(Rets ? PI32V2::POP_RETS_RMASK : PI32V2::POP_RMASK))
               .addImm(Mask)
               .setMIFlag(MachineInstr::FrameDestroy);
  for (const CalleeSavedInfo &I : CSI)
    B.addReg(I.getReg(), RegState::ImplicitDefine);
  return true;
}

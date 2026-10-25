//===-- PI32V2RegisterInfo.cpp - PI32V2 registers -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2RegisterInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "PI32V2GenRegisterInfo.inc"

using namespace llvm;

PI32V2RegisterInfo::PI32V2RegisterInfo() : PI32V2GenRegisterInfo(PI32V2::RETS) {}

const MCPhysReg *
PI32V2RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_PI32V2_SaveList;
}

const uint32_t *
PI32V2RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                         CallingConv::ID) const {
  return CSR_PI32V2_RegMask;
}

BitVector PI32V2RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  for (MCPhysReg R : {PI32V2::RETI, PI32V2::RETE, PI32V2::RETX, PI32V2::SR4,
                      PI32V2::PSR, PI32V2::CNUM, PI32V2::USPN, PI32V2::SSPN,
                      PI32V2::ICFGN, PI32V2::ICFGS, PI32V2::ICFG, PI32V2::USP,
                      PI32V2::SSP, PI32V2::SP, PI32V2::PC})
    Reserved.set(R);
  if (MF.getSubtarget().getFrameLowering()->hasFP(MF))
    Reserved.set(PI32V2::R7); // the frame base (docs/ABI.md section 6.3)
  return Reserved;
}

Register PI32V2RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return MF.getSubtarget().getFrameLowering()->hasFP(MF) ? PI32V2::R7
                                                          : PI32V2::SP;
}

// A pseudo with a frame index becomes the sp form of the access when the
// offset fits it, else the address is computed into a scratch register.
bool PI32V2RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                             int SPAdj, unsigned FIOperandNum,
                                             RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const PI32V2InstrInfo &TII =
      *MF.getSubtarget<PI32V2Subtarget>().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FI = MI.getOperand(FIOperandNum).getIndex();
  int64_t Offset = MFI.getObjectOffset(FI) + MFI.getStackSize() + SPAdj +
                   MI.getOperand(FIOperandNum + 1).getImm();
  bool FP = getFrameRegister(MF) == PI32V2::R7;
  if (FP)
    Offset = MFI.getObjectOffset(FI) + MI.getOperand(FIOperandNum + 1).getImm() +
             MFI.getStackSize();

  unsigned Opc = MI.getOpcode();
  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum).ChangeToRegister(getFrameRegister(MF), false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  struct SPForm {
    unsigned Pseudo, SPOp, RegOp;
    int Scale;
    int64_t Max;
    bool Store;
  };
  static const SPForm Forms[] = {
      {PI32V2::LDW_FI, PI32V2::LD_W_SP_L, PI32V2::LD_W_L, 4, 4092, false},
      {PI32V2::LDBU_FI, PI32V2::LD_BU_SP_L, PI32V2::LD_BU_L, 1, 4095, false},
      {PI32V2::LDBS_FI, PI32V2::LD_BS_SP_L, PI32V2::LD_BS_L, 1, 4095, false},
      {PI32V2::LDHU_FI, PI32V2::LD_HU_SP_L, PI32V2::LD_HU_L, 2, 4094, false},
      {PI32V2::LDHS_FI, PI32V2::LD_HS_SP_L, PI32V2::LD_HS_L, 2, 4094, false},
      {PI32V2::STW_FI, PI32V2::ST_W_SP_L, PI32V2::ST_W_L, 4, 4092, true},
      {PI32V2::STB_FI, PI32V2::ST_B_SP_L, PI32V2::ST_B_L, 1, 4095, true},
      {PI32V2::STH_FI, PI32V2::ST_H_SP_L, PI32V2::ST_H_L, 2, 4094, true},
  };

  // The address of the slot in a register: r = sp + n, or the frame base.
  auto Address = [&](Register Dst) {
    if (FP) {
      BuildMI(MBB, II, DL, TII.get(PI32V2::ADD_S14_RRI), Dst)
          .addReg(PI32V2::R7)
          .addImm(Offset);
      return;
    }
    if (Offset < 0 || Offset > 4095)
      report_fatal_error("PI32V2: stack frame larger than 4 KiB");
    BuildMI(MBB, II, DL, TII.get(PI32V2::ADD_R_SP_I_L), Dst).addImm(Offset);
  };

  if (Opc == PI32V2::ADDR_FI) {
    Register Dst = MI.getOperand(0).getReg();
    Address(Dst);
    MI.eraseFromParent();
    return true;
  }
  for (const SPForm &F : Forms) {
    if (F.Pseudo != Opc)
      continue;
    Register Val = MI.getOperand(0).getReg();
    bool Kill = F.Store && MI.getOperand(0).isKill();
    if (!FP && Offset >= 0 && Offset <= F.Max && Offset % F.Scale == 0) {
      if (F.Store)
        BuildMI(MBB, II, DL, TII.get(F.SPOp))
            .addImm(Offset)
            .addReg(Val, getKillRegState(Kill))
            .setMemRefs(MI.memoperands());
      else
        BuildMI(MBB, II, DL, TII.get(F.SPOp), Val)
            .addImm(Offset)
            .setMemRefs(MI.memoperands());
      MI.eraseFromParent();
      return true;
    }
    Register Tmp = RS->scavengeRegisterBackwards(PI32V2::GPRRegClass, II,
                                                 false, 0);
    Address(Tmp);
    if (F.Store)
      BuildMI(MBB, II, DL, TII.get(F.RegOp))
          .addReg(Tmp, RegState::Kill)
          .addImm(0)
          .addReg(Val, getKillRegState(Kill))
          .setMemRefs(MI.memoperands());
    else
      BuildMI(MBB, II, DL, TII.get(F.RegOp), Val)
          .addReg(Tmp, RegState::Kill)
          .addImm(0)
          .setMemRefs(MI.memoperands());
    MI.eraseFromParent();
    return true;
  }
  report_fatal_error("PI32V2: frame index in an unexpected instruction");
}

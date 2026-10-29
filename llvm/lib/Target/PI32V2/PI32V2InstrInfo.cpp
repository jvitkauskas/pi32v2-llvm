//===-- PI32V2InstrInfo.cpp - PI32V2 instruction info ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2InstrInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "PI32V2GenInstrInfo.inc"

using namespace llvm;

PI32V2InstrInfo::PI32V2InstrInfo(const PI32V2Subtarget &STI)
    : PI32V2GenInstrInfo(STI, RI, PI32V2::ADJCALLSTACKDOWN,
                         PI32V2::ADJCALLSTACKUP),
      RI() {}

void PI32V2InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, Register Dst,
                                  Register Src, bool KillSrc,
                                  bool RenamableDest, bool RenamableSrc) const {
  bool DG = PI32V2::GPRRegClass.contains(Dst),
       SG = PI32V2::GPRRegClass.contains(Src);
  if (DG && SG)
    BuildMI(MBB, I, DL, get(PI32V2::MOV_RR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (PI32V2::GPRPairRegClass.contains(Dst) &&
           PI32V2::GPRPairRegClass.contains(Src))
    BuildMI(MBB, I, DL, get(PI32V2::MOV_PAIR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (DG && PI32V2::SRRegClass.contains(Src))
    BuildMI(MBB, I, DL, get(PI32V2::MOV_R_SR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (SG && PI32V2::SRRegClass.contains(Dst))
    BuildMI(MBB, I, DL, get(PI32V2::MOV_SR_R), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else
    report_fatal_error("PI32V2: impossible register copy");
}

static MachineMemOperand *frameMMO(MachineBasicBlock &MBB, int FI,
                                   MachineMemOperand::Flags F) {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI), F,
                                 MFI.getObjectSize(FI),
                                 MFI.getObjectAlign(FI));
}

void PI32V2InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register Src,
    bool IsKill, int FI, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  if (PI32V2::GPRPairRegClass.hasSubClassEq(RC) ||
      (Src.isPhysical() && PI32V2::GPRPairRegClass.contains(Src))) {
    // A pair: its two words, the low one first (a virtual pair by its
    // sub-registers, a physical one by its halves).
    for (unsigned K = 0; K < 2; ++K) {
      unsigned Idx = K ? PI32V2::sub_hi : PI32V2::sub_lo;
      auto B = BuildMI(MBB, I, DL, get(PI32V2::STW_FI));
      if (Src.isVirtual())
        B.addReg(Src, getKillRegState(IsKill && K == 1), Idx);
      else
        B.addReg(RI.getSubReg(Src, Idx), getKillRegState(IsKill));
      B.addFrameIndex(FI)
          .addImm(4 * K)
          .addMemOperand(frameMMO(MBB, FI, MachineMemOperand::MOStore))
          .setMIFlag(Flags);
    }
    return;
  }
  BuildMI(MBB, I, DL, get(PI32V2::STW_FI))
      .addReg(Src, getKillRegState(IsKill))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(frameMMO(MBB, FI, MachineMemOperand::MOStore))
      .setMIFlag(Flags);
}

void PI32V2InstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register Dst,
    int FI, const TargetRegisterClass *RC, Register VReg, unsigned SubReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  if (PI32V2::GPRPairRegClass.hasSubClassEq(RC) ||
      (Dst.isPhysical() && PI32V2::GPRPairRegClass.contains(Dst))) {
    for (unsigned K = 0; K < 2; ++K) {
      unsigned Idx = K ? PI32V2::sub_hi : PI32V2::sub_lo;
      auto B = BuildMI(MBB, I, DL, get(PI32V2::LDW_FI));
      if (Dst.isVirtual()) // the first half's definition leaves the other undefined
        B.addReg(Dst, K == 0 ? RegState::Define | RegState::Undef : RegState::Define, Idx);
      else
        B.addReg(RI.getSubReg(Dst, Idx), RegState::Define);
      B.addFrameIndex(FI)
          .addImm(4 * K)
          .addMemOperand(frameMMO(MBB, FI, MachineMemOperand::MOLoad))
          .setMIFlag(Flags);
    }
    return;
  }
  BuildMI(MBB, I, DL, get(PI32V2::LDW_FI), Dst)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(frameMMO(MBB, FI, MachineMemOperand::MOLoad))
      .setMIFlag(Flags);
}

unsigned PI32V2InstrInfo::inverseBranch(unsigned Opc) {
  switch (Opc) {
  case PI32V2::BEQ_RR: return PI32V2::BNE_RR;
  case PI32V2::BNE_RR: return PI32V2::BEQ_RR;
  case PI32V2::BGE_RR: return PI32V2::BLT_RR;
  case PI32V2::BLT_RR: return PI32V2::BGE_RR;
  case PI32V2::BGT_RR: return PI32V2::BLE_RR;
  case PI32V2::BLE_RR: return PI32V2::BGT_RR;
  case PI32V2::BGES_RR: return PI32V2::BLTS_RR;
  case PI32V2::BLTS_RR: return PI32V2::BGES_RR;
  case PI32V2::BGTS_RR: return PI32V2::BLES_RR;
  case PI32V2::BLES_RR: return PI32V2::BGTS_RR;
  case PI32V2::BEQ_RI: return PI32V2::BNE_RI;
  case PI32V2::BNE_RI: return PI32V2::BEQ_RI;
  case PI32V2::BGE_RI: return PI32V2::BLT_RI;
  case PI32V2::BLT_RI: return PI32V2::BGE_RI;
  case PI32V2::BGT_RI: return PI32V2::BLE_RI;
  case PI32V2::BLE_RI: return PI32V2::BGT_RI;
  case PI32V2::BGES_RI: return PI32V2::BLTS_RI;
  case PI32V2::BLTS_RI: return PI32V2::BGES_RI;
  case PI32V2::BGTS_RI: return PI32V2::BLES_RI;
  case PI32V2::BLES_RI: return PI32V2::BGTS_RI;
  case PI32V2::BEQ_RI_L: return PI32V2::BNE_RI_L;
  case PI32V2::BNE_RI_L: return PI32V2::BEQ_RI_L;
  case PI32V2::BGE_RI_L: return PI32V2::BLT_RI_L;
  case PI32V2::BLT_RI_L: return PI32V2::BGE_RI_L;
  case PI32V2::BGT_RI_L: return PI32V2::BLE_RI_L;
  case PI32V2::BLE_RI_L: return PI32V2::BGT_RI_L;
  case PI32V2::BGES_RI_L: return PI32V2::BLTS_RI_L;
  case PI32V2::BLTS_RI_L: return PI32V2::BGES_RI_L;
  case PI32V2::BGTS_RI_L: return PI32V2::BLES_RI_L;
  case PI32V2::BLES_RI_L: return PI32V2::BGTS_RI_L;
  case PI32V2::BFEQ_RR: return PI32V2::BFUNE_RR;
  case PI32V2::BFUNE_RR: return PI32V2::BFEQ_RR;
  case PI32V2::BFLT_RR: return PI32V2::BFUGE_RR;
  case PI32V2::BFUGE_RR: return PI32V2::BFLT_RR;
  case PI32V2::BFLE_RR: return PI32V2::BFUGT_RR;
  case PI32V2::BFUGT_RR: return PI32V2::BFLE_RR;
  case PI32V2::BFGT_RR: return PI32V2::BFULE_RR;
  case PI32V2::BFULE_RR: return PI32V2::BFGT_RR;
  case PI32V2::BFGE_RR: return PI32V2::BFULT_RR;
  case PI32V2::BFULT_RR: return PI32V2::BFGE_RR;
  case PI32V2::BFEQ_RR_L: return PI32V2::BFUNE_RR_L;
  case PI32V2::BFUNE_RR_L: return PI32V2::BFEQ_RR_L;
  case PI32V2::BFLT_RR_L: return PI32V2::BFUGE_RR_L;
  case PI32V2::BFUGE_RR_L: return PI32V2::BFLT_RR_L;
  case PI32V2::BFLE_RR_L: return PI32V2::BFUGT_RR_L;
  case PI32V2::BFUGT_RR_L: return PI32V2::BFLE_RR_L;
  case PI32V2::BFGT_RR_L: return PI32V2::BFULE_RR_L;
  case PI32V2::BFULE_RR_L: return PI32V2::BFGT_RR_L;
  case PI32V2::BFGE_RR_L: return PI32V2::BFULT_RR_L;
  case PI32V2::BFULT_RR_L: return PI32V2::BFGE_RR_L;
  case PI32V2::BEQ_AND_RI: return PI32V2::BNE_AND_RI;
  case PI32V2::BNE_AND_RI: return PI32V2::BEQ_AND_RI;
  case PI32V2::BEQ_AND_RR: return PI32V2::BNE_AND_RR;
  case PI32V2::BNE_AND_RR: return PI32V2::BEQ_AND_RR;
  case PI32V2::BEQ_AND_MODIMM_RI_L: return PI32V2::BNE_AND_MODIMM_RI_L;
  case PI32V2::BNE_AND_MODIMM_RI_L: return PI32V2::BEQ_AND_MODIMM_RI_L;
  default: return 0;
  }
}

// Cond: the branch opcode, then its two registers.
bool PI32V2InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *&TBB,
                                    MachineBasicBlock *&FBB,
                                    SmallVectorImpl<MachineOperand> &Cond,
                                    bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;
  MachineInstr *Last = &*I;
  MachineInstr *Prev = nullptr;
  if (I != MBB.begin()) {
    MachineBasicBlock::iterator J = std::prev(I);
    while (J != MBB.begin() && J->isDebugInstr())
      --J;
    if (isUnpredicatedTerminator(*J))
      Prev = &*J;
  }
  auto IsGoto = [](const MachineInstr &M) {
    return M.getOpcode() == PI32V2::GOTO_S || M.getOpcode() == PI32V2::GOTO_L;
  };
  if (!Prev) {
    if (IsGoto(*Last)) {
      TBB = Last->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranch(Last->getOpcode())) {
      TBB = Last->getOperand(2).getMBB();
      Cond.push_back(MachineOperand::CreateImm(Last->getOpcode()));
      Cond.push_back(Last->getOperand(0));
      Cond.push_back(Last->getOperand(1));
      return false;
    }
    return true;
  }
  // Something before the last terminator that is also a terminator.
  if (I != MBB.begin()) {
    MachineBasicBlock::iterator J = std::prev(MachineBasicBlock::iterator(Prev));
    if (Prev != &*MBB.begin() && isUnpredicatedTerminator(*J))
      return true;
  }
  if (isCondBranch(Prev->getOpcode()) && IsGoto(*Last)) {
    TBB = Prev->getOperand(2).getMBB();
    Cond.push_back(MachineOperand::CreateImm(Prev->getOpcode()));
    Cond.push_back(Prev->getOperand(0));
    Cond.push_back(Prev->getOperand(1));
    FBB = Last->getOperand(0).getMBB();
    return false;
  }
  if (IsGoto(*Prev) && IsGoto(*Last)) {
    TBB = Prev->getOperand(0).getMBB();
    if (AllowModify)
      Last->eraseFromParent();
    return false;
  }
  return true;
}

unsigned PI32V2InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                       int *BytesRemoved) const {
  unsigned Count = 0, Bytes = 0;
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    unsigned Opc = I->getOpcode();
    if (Opc != PI32V2::GOTO_S && Opc != PI32V2::GOTO_L && !isCondBranch(Opc))
      break;
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  if (BytesRemoved)
    *BytesRemoved = Bytes;
  return Count;
}

unsigned PI32V2InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                       MachineBasicBlock *TBB,
                                       MachineBasicBlock *FBB,
                                       ArrayRef<MachineOperand> Cond,
                                       const DebugLoc &DL,
                                       int *BytesAdded) const {
  unsigned Count = 0, Bytes = 0;
  if (Cond.empty()) {
    BuildMI(&MBB, DL, get(PI32V2::GOTO_S)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = 2;
    return 1;
  }
  BuildMI(&MBB, DL, get(Cond[0].getImm()))
      .add(Cond[1])
      .add(Cond[2])
      .addMBB(TBB);
  ++Count;
  Bytes += get(Cond[0].getImm()).getSize();
  if (FBB) {
    BuildMI(&MBB, DL, get(PI32V2::GOTO_S)).addMBB(FBB);
    ++Count;
    Bytes += 2;
  }
  if (BytesAdded)
    *BytesAdded = Bytes;
  return Count;
}

bool PI32V2InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  unsigned Inv = inverseBranch(Cond[0].getImm());
  if (!Inv)
    return true;
  Cond[0].setImm(Inv);
  return false;
}

unsigned PI32V2InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case PI32V2::PseudoCALL:
    return isPI32V2LargeProgram() ? 6 : 4; // call.l (call.xl) at most
  case PI32V2::PseudoTAIL:
    return 4; // goto.l at most
  case PI32V2::PseudoDBNZ:
    return 4;
  case PI32V2::PseudoCALLR:
  case PI32V2::PseudoRET:
    return 2;
  case PI32V2::PseudoBRJT: // at most: li, the scaling, a load, goto, the table
    return 6 + 4 + 4 + 2 + 3 +
           4 * MI.getMF()->getJumpTableInfo()->getJumpTables()[MI.getOperand(3).getIndex()].MBBs.size();
  case PI32V2::LDW_POST: case PI32V2::LDHU_POST: case PI32V2::LDHS_POST:
  case PI32V2::LDBU_POST: case PI32V2::LDBS_POST: case PI32V2::STW_POST:
  case PI32V2::STH_POST: case PI32V2::STB_POST:
  case PI32V2::LDW_PRE: case PI32V2::LDHU_PRE: case PI32V2::LDHS_PRE:
  case PI32V2::LDBU_PRE: case PI32V2::LDBS_PRE: case PI32V2::STW_PRE:
  case PI32V2::STH_PRE: case PI32V2::STB_PRE:
    return 4; // the .pin.l form at most
  case PI32V2::SELECT_AND:
  case PI32V2::SELECT_ANDI:
  case PI32V2::SELECT_CC:
  case PI32V2::SELECT_CC_RI:
  case PI32V2::SELECT_CC_RRI:
  case PI32V2::SELECT_CC_RII:
    return 4 + 6; // the opener and a move or li
  default:
    return MI.getDesc().getSize();
  }
}

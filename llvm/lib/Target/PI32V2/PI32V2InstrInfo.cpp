//===-- PI32V2InstrInfo.cpp - PI32V2 instruction info ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2InstrInfo.h"
#include "PI32V2.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOutliner.h"
#include "llvm/IR/Module.h"
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
  MachineInstrBuilder Copy;
  if (DG && SG)
    Copy = BuildMI(MBB, I, DL, get(PI32V2::MOV_RR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (PI32V2::GPRPairRegClass.contains(Dst) &&
           PI32V2::GPRPairRegClass.contains(Src))
    Copy = BuildMI(MBB, I, DL, get(PI32V2::MOV_PAIR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (DG && PI32V2::SRRegClass.contains(Src))
    Copy = BuildMI(MBB, I, DL, get(PI32V2::MOV_R_SR), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else if (SG && PI32V2::SRRegClass.contains(Dst))
    Copy = BuildMI(MBB, I, DL, get(PI32V2::MOV_SR_R), Dst)
        .addReg(Src, getKillRegState(KillSrc));
  else
    report_fatal_error("PI32V2: impossible register copy");
  Copy->getOperand(0).setIsRenamable(RenamableDest);
  Copy->getOperand(1).setIsRenamable(RenamableSrc);
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
  // A terminator in an if block's part (PI32V2IfBlocks, at the end) runs only
  // in its part: the block falls through otherwise. Not analyzable. (The parts
  // are 7 instructions at most.)
  unsigned Back = 0;
  for (MachineBasicBlock::iterator J = I;; --J) {
    if (!J->isDebugInstr()) {
      if (PI32V2::isIfOpener(J->getOpcode()))
        return true;
      if (++Back > 8)
        break;
    }
    if (J == MBB.begin())
      break;
  }
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

// A 32-bit constant (li.u32, li.modimm) made again where it is used, not kept
// in a register (and spilled in a loop), but only for speed: each copy is 6 or
// 4 bytes, a reload 2.
bool PI32V2InstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  if ((MI.getOpcode() == PI32V2::LI_U32 || MI.getOpcode() == PI32V2::LI_MODIMM) &&
      MI.getMF()->getFunction().hasOptSize())
    return false;
  return TargetInstrInfo::isReMaterializableImpl(MI);
}

static bool isCommutableSelect(unsigned Opc) {
  return Opc == PI32V2::SELECT_CC || Opc == PI32V2::SELECT_CC_RI ||
         Opc == PI32V2::SELECT_AND;
}

// A select's two values (operands 3 and 4, t and f).
bool PI32V2InstrInfo::findCommutedOpIndices(const MachineInstr &MI,
                                            unsigned &SrcOpIdx1,
                                            unsigned &SrcOpIdx2) const {
  if (!isCommutableSelect(MI.getOpcode()))
    return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
  return fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, 3, 4);
}

// dst = f; if (a cc b) { dst = t } is dst = t; if (a !cc b) { dst = f }: the
// inverse condition holds exactly when the condition does not, NaN included
// (an ordered float comparison becomes an unordered one).
MachineInstr *PI32V2InstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                      bool NewMI,
                                                      unsigned OpIdx1,
                                                      unsigned OpIdx2) const {
  if (!isCommutableSelect(MI.getOpcode()))
    return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  unsigned Cond = MI.getOperand(5).getImm();
  bool Float = Cond & PI32V2::SelectFloat;
  ISD::CondCode Inv = ISD::getSetCCInverse(ISD::CondCode(Cond & ~PI32V2::SelectFloat),
                                           Float ? MVT::f32 : MVT::i32);
  MachineInstr *R =
      TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  if (R)
    R->getOperand(5).setImm(unsigned(Inv) | (Float ? PI32V2::SelectFloat : 0));
  return R;
}

unsigned PI32V2InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case PI32V2::PseudoCALL:
    return isPI32V2LargeProgram() ? 6 : 4; // call.l (call.xl) at most
  case PI32V2::PseudoTAIL:
    return 4; // goto.l at most
  case PI32V2::PseudoDBNZ:
    return 4;
  case PI32V2::PseudoTRAP:
    return 4; // btbclr, then a 16-bit goto (never in an if block)
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

// A post-increment pseudo's form; its written-back base operand is not one of
// the form's (the form says [b++=n]).
unsigned PI32V2InstrInfo::postIncForm(unsigned Opc, unsigned &Skip) {
  Skip = 1;
  switch (Opc) {
  case PI32V2::LDW_POST: return PI32V2::LD_W_PIN_L;
  case PI32V2::LDHU_POST: return PI32V2::LD_HU_PIN_L;
  case PI32V2::LDHS_POST: return PI32V2::LD_HS_PIN_L;
  case PI32V2::LDBU_POST: return PI32V2::LD_BU_PIN_L;
  case PI32V2::LDBS_POST: return PI32V2::LD_BS_PIN_L;
  case PI32V2::LDW_PRE: return PI32V2::LD_W_PRE;
  case PI32V2::LDHU_PRE: return PI32V2::LD_HU_PRE;
  case PI32V2::LDHS_PRE: return PI32V2::LD_HS_PRE;
  case PI32V2::LDBU_PRE: return PI32V2::LD_BU_PRE;
  case PI32V2::LDBS_PRE: return PI32V2::LD_BS_PRE;
  }
  Skip = 0;
  switch (Opc) {
  case PI32V2::STW_POST: return PI32V2::ST_W_PIN_L;
  case PI32V2::STH_POST: return PI32V2::ST_H_PIN_L;
  case PI32V2::STB_POST: return PI32V2::ST_B_PIN_L;
  case PI32V2::STW_PRE: return PI32V2::ST_W_PRE;
  case PI32V2::STH_PRE: return PI32V2::ST_H_PRE;
  case PI32V2::STB_PRE: return PI32V2::ST_B_PRE;
  }
  return 0;
}

//===----------------------------------------------------------------------===//
// The machine outliner.
//
// A call writes its return address to rets and touches nothing else, the
// stack included, so a sequence runs the same in a function of its own as
// where it was: the stack pointer, the flags and every register are as the
// sequence left them when the function returns with rts. What a sequence may
// not do is read or write rets, which now holds the way back. So:
//
//   Tail   the sequence ends in a return (rts, the epilogue's pop into pc, or
//          a tail call): a goto reaches the function, which returns for its
//          caller. rets is untouched.
//   Thunk  the sequence ends in a call: the function ends in a goto to the
//          callee, which returns to after the call that reached the function.
//   Call   anything else without a call in it: a call reaches it and it ends
//          in rts. Only where rets is dead: in a function that saved it,
//          which a function that makes calls has.
//   Frame  anything else with calls in it: as Call, but the function saves
//          rets around them, [--sp] = rets ... pc = [sp++]. That moves the
//          stack by a word while the sequence runs, so nothing in it may
//          use sp, and none of its calls may take arguments on the stack.
//
// A sequence that ends in a return may have calls in it as it is: its
// function saved rets before it, and the return takes that.
//
// The passes after the outliner (PI32V2TargetMachine.cpp) see an outlined
// function's registers as live to its end.
//===----------------------------------------------------------------------===//

namespace {
enum OutlineKind { OutlineTail, OutlineThunk, OutlineCall, OutlineFrame };
} // namespace

static bool isReturnLike(const MachineInstr &MI) {
  return MI.isReturn() || MI.getOpcode() == PI32V2::POP_PC_RMASK;
}

static bool touchesRets(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.getReg() == PI32V2::RETS)
      return true;
  return MI.getDesc().hasImplicitUseOfPhysReg(PI32V2::RETS) ||
         MI.getDesc().hasImplicitDefOfPhysReg(PI32V2::RETS);
}

bool PI32V2InstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
    return false;
  // A function in a section of its own may have to be all there; a handler
  // returns with rti; and far calls and gotos are not what this counts with.
  if (F.hasSection() || F.hasFnAttribute("interrupt") ||
      F.hasFnAttribute(Attribute::Naked) || isPI32V2LargeProgram())
    return false;
  return MF.getProperties().hasTracksLiveness();
}

bool PI32V2InstrInfo::shouldOutlineFromFunctionByDefault(
    MachineFunction &MF) const {
  return MF.getFunction().hasMinSize();
}

outliner::InstrType
PI32V2InstrInfo::getOutliningTypeImpl(const MachineModuleInfo &MMI,
                                      MachineBasicBlock::iterator &MIT,
                                      unsigned Flags) const {
  const MachineInstr &MI = *MIT;
  switch (MI.getOpcode()) {
  case PI32V2::PseudoCALL: // the last of a sequence only: see below
  case PI32V2::PseudoTAIL:
  case PI32V2::PseudoRET:
  case PI32V2::POP_PC_RMASK:
    return outliner::InstrType::Legal;
  case PI32V2::PseudoCALLR:
  case PI32V2::PseudoBRJT:
    return outliner::InstrType::Illegal;
  default:
    break;
  }
  if (MI.isCall() || MI.isCFIInstruction() || touchesRets(MI))
    return outliner::InstrType::Illegal;
  return outliner::InstrType::Legal;
}

std::optional<std::unique_ptr<outliner::OutlinedFunction>>
PI32V2InstrInfo::getOutliningCandidateInfo(
    const MachineModuleInfo &MMI,
    std::vector<outliner::Candidate> &RepeatedSequenceLocs,
    unsigned MinRepeats) const {
  outliner::Candidate &First = RepeatedSequenceLocs[0];
  const MachineInstr &Last = First.back();
  bool InnerCalls = false, UsesSP = false;
  for (const MachineInstr &MI : First) {
    if (&MI == &Last)
      break;
    if (isReturnLike(MI))
      return std::nullopt;
    if (MI.isCall())
      InnerCalls = true;
    else if (MI.readsRegister(PI32V2::SP, &RI) ||
             MI.modifiesRegister(PI32V2::SP, &RI))
      UsesSP = true;
  }

  unsigned Kind = OutlineCall, FrameOverhead = 2; // rts
  if (isReturnLike(Last)) {
    Kind = OutlineTail;
    FrameOverhead = 0;
  } else if (Last.getOpcode() == PI32V2::PseudoCALL && !InnerCalls) {
    Kind = OutlineThunk;
    FrameOverhead = 0;
  } else {
    if (InnerCalls || Last.isCall()) {
      Kind = OutlineFrame;
      FrameOverhead = 2 + 2; // [--sp] = rets, pc = [sp++]
      if (UsesSP || (!Last.isCall() &&
                     (Last.readsRegister(PI32V2::SP, &RI) ||
                      Last.modifiesRegister(PI32V2::SP, &RI))))
        return std::nullopt;
    }
    // The call to the function overwrites rets; and with a frame, the stack
    // is a word further down for the calls in the sequence.
    llvm::erase_if(RepeatedSequenceLocs, [&](outliner::Candidate &C) {
      if (Kind == OutlineFrame &&
          (!C.getMF()->getFrameInfo().isMaxCallFrameSizeComputed() ||
           C.getMF()->getFrameInfo().getMaxCallFrameSize()))
        return true;
      return !C.isAvailableAcrossAndOutOfSeq(PI32V2::RETS, RI);
    });
    if (RepeatedSequenceLocs.size() < MinRepeats)
      return std::nullopt;
  }
  // call.l, or goto.l: either is 4 bytes at most.
  for (outliner::Candidate &C : RepeatedSequenceLocs)
    C.setCallInfo(Kind, 4);
  unsigned SequenceSize = 0;
  for (const MachineInstr &MI : RepeatedSequenceLocs[0])
    SequenceSize += getInstSizeInBytes(MI);
  return std::make_unique<outliner::OutlinedFunction>(
      RepeatedSequenceLocs, SequenceSize, FrameOverhead, Kind);
}

void PI32V2InstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  // The sequence came from one place and stands for all of them: what was
  // dead or last used there is not everywhere.
  SmallVector<Register, 16> Defined;
  for (MachineInstr &MI : MBB) {
    // What a call does not preserve is no longer what the sequence wrote.
    for (const MachineOperand &MO : MI.operands())
      if (MO.isRegMask())
        llvm::erase_if(Defined, [&](Register R) {
          return MO.clobbersPhysReg(R.asMCReg());
        });
    for (MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (MO.isDef()) {
        MO.setIsDead(false);
        if (!is_contained(Defined, MO.getReg()))
          Defined.push_back(MO.getReg());
      } else {
        MO.setIsKill(false);
      }
    }
  }
  if (OF.FrameConstructionID == OutlineTail)
    return;
  // The way back.
  MBB.addLiveIn(PI32V2::RETS);
  MachineInstr *End;
  if (OF.FrameConstructionID == OutlineFrame) {
    BuildMI(MBB, MBB.begin(), DebugLoc(), get(PI32V2::PUSH_RETS_RMASK))
        .addImm(0);
    End = BuildMI(MBB, MBB.end(), DebugLoc(), get(PI32V2::POP_PC_RMASK))
              .addImm(0);
  } else if (OF.FrameConstructionID == OutlineThunk) {
    // The call becomes a goto to its callee, with all it had.
    MachineInstr &Call = MBB.back();
    MachineInstrBuilder Tail =
        BuildMI(MBB, MBB.end(), DebugLoc(), get(PI32V2::PseudoTAIL));
    for (const MachineOperand &MO : Call.operands())
      if (!MO.isReg() || (MO.getReg() != PI32V2::RETS &&
                          !(MO.isImplicit() && MO.getReg() == PI32V2::SP)))
        Tail.add(MO);
    Call.eraseFromParent();
    End = Tail;
  } else {
    End = BuildMI(MBB, MBB.end(), DebugLoc(), get(PI32V2::PseudoRET));
  }
  // What the sequence wrote is for the code after it, which this function
  // can't see: live to the end.
  for (Register R : Defined)
    if (R != PI32V2::RETS && !End->readsRegister(R, &RI))
      End->addOperand(MF, MachineOperand::CreateReg(R, /*isDef=*/false,
                                                    /*isImp=*/true));
}

MachineBasicBlock::iterator PI32V2InstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  unsigned Opc = C.CallConstructionID == OutlineTail ? PI32V2::PseudoTAIL
                                                     : PI32V2::PseudoCALL;
  MachineInstrBuilder Call = BuildMI(MF, DebugLoc(), get(Opc))
                                 .addGlobalAddress(M.getNamedValue(MF.getName()));
  // The outliner says which registers the sequence reads and writes. What
  // the calls in it leave undefined, they say: so does this one.
  if (Opc == PI32V2::PseudoCALL)
    for (const MachineInstr &MI : C)
      for (const MachineOperand &MO : MI.operands())
        if (MO.isRegMask())
          Call.addRegMask(MO.getRegMask());
  It = MBB.insert(It, Call);
  return It;
}

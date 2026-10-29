//===-- PI32V2RegisterInfo.cpp - PI32V2 registers -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2RegisterInfo.h"
#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2MachineFunctionInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

#define GET_REGINFO_TARGET_DESC
#include "PI32V2GenRegisterInfo.inc"

using namespace llvm;

static cl::opt<bool> EnableTwoAddrHints(
    "pi32v2-twoaddr-hints", cl::Hidden, cl::init(true),
    cl::desc("Prefer registers that enable PI32V2 two-address encodings"));

static cl::opt<bool> EnableLowHints(
    "pi32v2-low-hints", cl::Hidden, cl::init(true),
    cl::desc("Prefer low registers for repeatedly compressible PI32V2 values"));

bool PI32V2MachineFunctionInfo::hasAllocationLoop(const MachineFunction &MF) const {
  if (!HasAllocationLoop) {
    MachineDominatorTree MDT(const_cast<MachineFunction &>(MF));
    MachineLoopInfo MLI(MDT);
    HasAllocationLoop = !MLI.empty();
  }
  return *HasAllocationLoop;
}

// Test the actual compression rules with distinct representative registers.
// Unallocated operands are optimistically low, but remain distinct so this
// does not accidentally count two-address savings as low-register savings.
static bool benefitsFromLowRegister(const MachineInstr &MI, Register VirtReg,
                                    const PI32V2InstrInfo &TII,
                                    const PI32V2RegisterInfo &TRI,
                                    const VirtRegMap &VRM) {
  if (MI.isPseudo() || MI.isInlineAsm() || MI.getDesc().getSize() <= 2)
    return false;
  SmallVector<std::pair<Register, Register>, 4> Regs;
  unsigned Used = 0;
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (MO.isReg()) {
      if (MO.getSubReg())
        return false;
      Register R = MO.getReg();
      if (R == VirtReg)
        continue;
      Register P = R.isVirtual() ? Register(VRM.getPhys(R)) : R;
      if (P) {
        if (!PI32V2::GPRRegClass.contains(P))
          return false;
        Used |= 1u << TRI.getEncodingValue(P);
      }
      if (R.isVirtual() && !is_contained(Regs, std::pair(R, P)))
        Regs.emplace_back(R, P);
    } else if (!MO.isImm()) {
      return false;
    }
  }
  for (auto &[R, P] : Regs) {
    if (P)
      continue;
    unsigned N = 0;
    while (N != 8 && (Used & (1 << N)))
      ++N;
    if (N == 8)
      return false;
    P = PI32V2::R0 + N;
    Used |= 1 << N;
  }
  unsigned Lo = 0, Hi = 8;
  while (Lo != 8 && (Used & (1 << Lo)))
    ++Lo;
  while (Hi != 16 && (Used & (1 << Hi)))
    ++Hi;
  if (Lo == 8 || Hi == 16)
    return false;
  auto Size = [&](unsigned N) {
    MCInst Inst;
    Inst.setOpcode(MI.getOpcode());
    for (const MachineOperand &MO : MI.explicit_operands()) {
      if (MO.isImm()) {
        Inst.addOperand(MCOperand::createImm(MO.getImm()));
        continue;
      }
      Register R = MO.getReg();
      if (R == VirtReg)
        R = PI32V2::R0 + N;
      else if (R.isVirtual())
        for (auto [V, P] : Regs)
          if (R == V) {
            R = P;
            break;
          }
      Inst.addOperand(MCOperand::createReg(R));
    }
    PI32V2::compressInst(Inst, TII, TRI);
    return TII.get(Inst.getOpcode()).getSize();
  };
  return Size(Lo) < Size(Hi);
}

PI32V2RegisterInfo::PI32V2RegisterInfo() : PI32V2GenRegisterInfo(PI32V2::RETS) {}

const MCPhysReg *
PI32V2RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (MF && MF->getFunction().hasFnAttribute("interrupt"))
    return CSR_PI32V2_Interrupt_SaveList;
  return CSR_PI32V2_SaveList;
}

const uint32_t *
PI32V2RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                         CallingConv::ID) const {
  return CSR_PI32V2_RegMask;
}

BitVector PI32V2RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // rets too: never allocated, only saved (a callee-saved register) and read
  // (__builtin_return_address), a read the machine sinker may move to a block
  // where rets is not tracked live
  for (MCPhysReg R : {PI32V2::RETI, PI32V2::RETE, PI32V2::RETX, PI32V2::RETS,
                      PI32V2::SR4,
                      PI32V2::PSR, PI32V2::CNUM, PI32V2::USPN, PI32V2::SSPN,
                      PI32V2::ICFGN, PI32V2::ICFGS, PI32V2::ICFG, PI32V2::USP,
                      PI32V2::SSP, PI32V2::SP, PI32V2::PC})
    Reserved.set(R);
  if (MF.getSubtarget().getFrameLowering()->hasFP(MF))
    Reserved.set(PI32V2::R7); // the frame base (docs/ABI.md section 6.3)
  return Reserved;
}

bool PI32V2RegisterInfo::getRegAllocationHints(
    Register VirtReg, ArrayRef<MCPhysReg> Order,
    SmallVectorImpl<MCPhysReg> &Hints, const MachineFunction &MF,
    const VirtRegMap *VRM, const LiveRegMatrix *Matrix) const {
  bool Ret = TargetRegisterInfo::getRegAllocationHints(VirtReg, Order, Hints,
                                                      MF, VRM, Matrix);
  // Even soft preferences can affect greedy eviction and splitting. Keep the
  // speed allocator's choices for functions not explicitly optimized for size.
  if (!MF.getFunction().hasOptSize())
    return Ret;
  bool UseLowHints = EnableLowHints;
  if (!VRM || (!EnableTwoAddrHints && !UseLowHints))
    return Ret;

  // -Os is also used for real-time audio loops. Even a hint on a value used
  // outside a loop can evict a different value and create copies/spills inside
  // it. Preserve the allocator's choices throughout functions with loops;
  // post-RA recoloring can still shorten their encodings without new copies.
  if (MF.getInfo<PI32V2MachineFunctionInfo>()->hasAllocationLoop(MF))
    return Ret;

  // Keep copy hints first. These soft preferences can retain copies in
  // loop-free functions. The MC compressor recognizes
  // either source of the commutative operations, without an explicit commute.
  SmallVector<MCPhysReg, 8> TwoAddrHints;
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget<PI32V2Subtarget>().getInstrInfo();
  SmallVector<const MachineInstr *, 4> LowUses;
  auto Phys = [&](const MachineOperand &MO) -> Register {
    Register R = MO.getReg();
    return R.isVirtual() ? Register(VRM->getPhys(R)) : R;
  };
  for (const MachineOperand &MO : MRI.reg_nodbg_operands(VirtReg)) {
    if (MO.getSubReg() || MO.isImplicit())
      continue;
    const MachineInstr &MI = *MO.getParent();
    if (UseLowHints && LowUses.size() < 2 &&
        !is_contained(LowUses, &MI) &&
        benefitsFromLowRegister(MI, VirtReg, TII, *this, *VRM))
      LowUses.push_back(&MI);
    if (!EnableTwoAddrHints)
      continue;
    unsigned Sources = 0;
    bool Low = true;
    switch (MI.getOpcode()) {
    default:
      continue;
    case PI32V2::ADD_RRR_L:
    case PI32V2::MUL_RRR:
      // += and *= can use every GPR (unlike the three-address forms).
      Low = false;
      Sources = (1 << 1) | (1 << 2);
      break;
    case PI32V2::AND_RRR:
    case PI32V2::OR_RRR:
    case PI32V2::XOR_RRR:
      Sources = (1 << 1) | (1 << 2);
      break;
    case PI32V2::LSL_RRR:
    case PI32V2::LSR_RRR:
    case PI32V2::ASR_RRR:
      Sources = 1 << 1;
      break;
    case PI32V2::ADD_S14_RRI: {
      int64_t Imm = MI.getOperand(2).getImm();
      if (Imm < -128 || Imm > 127 || (Imm >= 0 && Imm < 32))
        continue; // 0..31 also has a short three-address form.
      Sources = 1 << 1;
      break;
    }
    case PI32V2::AND_MODIMM_RRI:
    case PI32V2::AND_NMODIMM_RRI:
      if (!MI.getOperand(2).isImm() ||
          !isPowerOf2_32(~uint32_t(MI.getOperand(2).getImm())))
        continue;
      Sources = 1 << 1;
      break;
    case PI32V2::OR_MODIMM_RRI:
    case PI32V2::XOR_MODIMM_RRI:
      if (!MI.getOperand(2).isImm() ||
          !isPowerOf2_32(uint32_t(MI.getOperand(2).getImm())))
        continue;
      Sources = 1 << 1;
      break;
    }

    unsigned Op = MO.getOperandNo();
    for (unsigned Src = 1; Src <= 2; ++Src) {
      if (!(Sources & (1 << Src)) || (Op != 0 && Op != Src))
        continue;
      const MachineOperand &Other = MI.getOperand(Op == 0 ? Src : 0);
      if (Other.getSubReg())
        continue;
      Register R = Phys(Other);
      if (!R || (Low && !PI32V2::GPRLoRegClass.contains(R)) ||
          !is_contained(Order, R))
        continue;
      if (Low && MI.getOperand(2).isReg()) {
        Register OtherSrc = Phys(MI.getOperand(Src == 1 ? 2 : 1));
        if (OtherSrc && !PI32V2::GPRLoRegClass.contains(OtherSrc))
          continue;
      }
      if (!is_contained(TwoAddrHints, R))
        TwoAddrHints.push_back(R);
    }
  }
  for (MCPhysReg R : Order)
    if (is_contained(TwoAddrHints, R) && !is_contained(Hints, R))
      Hints.push_back(R);
  if (LowUses.size() >= 2) {
    // A retained copy costs two bytes; two compact instructions can save
    // four. Preserve low copy/two-address hints ahead of other low registers.
    SmallVector<MCPhysReg, 16> NewHints;
    for (MCPhysReg R : Hints)
      if (PI32V2::GPRLoRegClass.contains(R))
        NewHints.push_back(R);
    for (MCPhysReg R : Order)
      if (PI32V2::GPRLoRegClass.contains(R) && !is_contained(NewHints, R))
        NewHints.push_back(R);
    for (MCPhysReg R : Hints)
      if (!is_contained(NewHints, R))
        NewHints.push_back(R);
    Hints.assign(NewHints.begin(), NewHints.end());
  }
  return Ret;
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
  auto Address = [&](Register Dst, bool Renamable) {
    RegState DefFlags = RegState::Define | getRenamableRegState(Renamable);
    if (!FP && Offset >= 0 && Offset <= 4095) {
      BuildMI(MBB, II, DL, TII.get(PI32V2::ADD_R_SP_I_L))
          .addReg(Dst, DefFlags).addImm(Offset);
      return;
    }
    // r = r7 + n, or further than r = sp + n reaches: r = sp; then as many
    // r = r + n as the offset needs.
    Register Base = PI32V2::R7;
    if (!FP) {
      BuildMI(MBB, II, DL, TII.get(PI32V2::MOV_R_SR))
          .addReg(Dst, DefFlags).addReg(PI32V2::SP);
      Base = Dst;
    }
    int64_t Left = Offset;
    do {
      int64_t Step = std::clamp<int64_t>(Left, -8192, 8188);
      BuildMI(MBB, II, DL, TII.get(PI32V2::ADD_S14_RRI))
          .addReg(Dst, DefFlags)
          .addReg(Base, getKillRegState(Base == Dst) |
                            getRenamableRegState(Base == Dst && Renamable))
          .addImm(Step);
      Base = Dst;
      Left -= Step;
    } while (Left);
  };

  if (Opc == PI32V2::ADDR_FI) {
    Register Dst = MI.getOperand(0).getReg();
    Address(Dst, MI.getOperand(0).isRenamable());
    MI.eraseFromParent();
    return true;
  }
  for (const SPForm &F : Forms) {
    if (F.Pseudo != Opc)
      continue;
    Register Val = MI.getOperand(0).getReg();
    bool Kill = F.Store && MI.getOperand(0).isKill();
    RegState ValFlags = getRenamableRegState(MI.getOperand(0).isRenamable());
    // With the frame base, [r7+n] itself when n is in reach (9 bits of
    // steps): no register for the address.
    if (FP && Offset % F.Scale == 0 && Offset >= -256 * F.Scale &&
        Offset <= 255 * F.Scale) {
      if (F.Store)
        BuildMI(MBB, II, DL, TII.get(F.RegOp))
            .addReg(PI32V2::R7)
            .addImm(Offset)
            .addReg(Val, getKillRegState(Kill) | ValFlags)
            .setMemRefs(MI.memoperands());
      else
        BuildMI(MBB, II, DL, TII.get(F.RegOp))
            .addReg(Val, RegState::Define | ValFlags)
            .addReg(PI32V2::R7)
            .addImm(Offset)
            .setMemRefs(MI.memoperands());
      MI.eraseFromParent();
      return true;
    }
    if (!FP && Offset >= 0 && Offset <= F.Max && Offset % F.Scale == 0) {
      if (F.Store)
        BuildMI(MBB, II, DL, TII.get(F.SPOp))
            .addImm(Offset)
            .addReg(Val, getKillRegState(Kill) | ValFlags)
            .setMemRefs(MI.memoperands());
      else
        BuildMI(MBB, II, DL, TII.get(F.SPOp))
            .addReg(Val, RegState::Define | ValFlags)
            .addImm(Offset)
            .setMemRefs(MI.memoperands());
      MI.eraseFromParent();
      return true;
    }
    // A load computes the address in its own destination; a store needs a
    // free register (the emergency slot of processFunctionBeforeFrameFinalized
    // makes sure there is one).
    Register Tmp = F.Store ? RS->scavengeRegisterBackwards(PI32V2::GPRRegClass,
                                                           II, false, 0)
                           : Val;
    Address(Tmp, !F.Store && MI.getOperand(0).isRenamable());
    if (F.Store)
      BuildMI(MBB, II, DL, TII.get(F.RegOp))
          .addReg(Tmp, RegState::Kill)
          .addImm(0)
          .addReg(Val, getKillRegState(Kill) | ValFlags)
          .setMemRefs(MI.memoperands());
    else
      BuildMI(MBB, II, DL, TII.get(F.RegOp))
          .addReg(Val, RegState::Define | ValFlags)
          .addReg(Tmp, getKillRegState(Tmp != Val) | ValFlags)
          .addImm(0)
          .setMemRefs(MI.memoperands());
    MI.eraseFromParent();
    return true;
  }
  report_fatal_error("PI32V2: frame index in an unexpected instruction");
}

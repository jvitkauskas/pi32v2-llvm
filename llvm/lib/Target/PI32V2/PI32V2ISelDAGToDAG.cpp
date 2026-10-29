//===-- PI32V2ISelDAGToDAG.cpp - PI32V2 instruction selection -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2Subtarget.h"
#include "PI32V2TargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-isel"
#define PASS_NAME "PI32V2 DAG->DAG Pattern Instruction Selection"

namespace {
class PI32V2DAGToDAGISel : public SelectionDAGISel {
public:
  PI32V2DAGToDAGISel(PI32V2TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<PI32V2Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *N) override;
  bool tryInsert(SDNode *N);

  // A stack slot, plus a constant.
  bool selectAddrFI(SDValue Addr, SDValue &Base, SDValue &Off);
  // A register plus an offset that the access's form holds (simm9 times the
  // access size); anything else is a register with offset 0.
  bool selectAddr(SDValue Addr, SDValue &Base, SDValue &Off, int Scale,
                  unsigned Bits = 9);
  bool selectAddrW(SDValue A, SDValue &B, SDValue &O) {
    return selectAddr(A, B, O, 4);
  }
  bool selectAddrH(SDValue A, SDValue &B, SDValue &O) {
    return selectAddr(A, B, O, 2);
  }
  bool selectAddrB(SDValue A, SDValue &B, SDValue &O) {
    return selectAddr(A, B, O, 1);
  }
  // sp plus a constant (an outgoing argument's slot): the [sp+n] forms,
  // n a multiple of the access size up to Max.
  bool selectAddrSP(SDValue A, SDValue &O, int Scale, int64_t Max) {
    SDValue Root = A;
    int64_t C = 0;
    if (CurDAG->isBaseWithConstantOffset(A)) {
      Root = A.getOperand(0);
      C = cast<ConstantSDNode>(A.getOperand(1))->getSExtValue();
    }
    if (Root.getOpcode() != ISD::CopyFromReg ||
        cast<RegisterSDNode>(Root.getOperand(1))->getReg() != PI32V2::SP ||
        C < 0 || C > Max || C % Scale)
      return false;
    O = CurDAG->getTargetConstant(C, SDLoc(A), MVT::i32);
    return true;
  }
  bool selectAddrSPW(SDValue A, SDValue &O) { return selectAddrSP(A, O, 4, 4092); }
  bool selectAddrSPH(SDValue A, SDValue &O) { return selectAddrSP(A, O, 2, 2046); }
  bool selectAddrSPB(SDValue A, SDValue &O) { return selectAddrSP(A, O, 1, 4095); }
  // The memory read-modify-write forms: a register plus a word offset in 6
  // bits ([r+n] |= ..., n in -128..124).
  bool selectAddrRMW(SDValue A, SDValue &B, SDValue &O) {
    // not at sp (outgoing arguments): [sp+n] = r is shorter than sp in a
    // register and the constant store
    SDValue Root = CurDAG->isBaseWithConstantOffset(A) ? A.getOperand(0) : A;
    if (Root.getOpcode() == ISD::CopyFromReg &&
        cast<RegisterSDNode>(Root.getOperand(1))->getReg() == PI32V2::SP)
      return false;
    return selectAddr(A, B, O, 4, 6);
  }

private:
  const PI32V2Subtarget *Subtarget = nullptr;

#include "PI32V2GenDAGISel.inc"
};

class PI32V2DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  PI32V2DAGToDAGISelLegacy(PI32V2TargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<PI32V2DAGToDAGISel>(TM, OptLevel)) {}
};
} // namespace

char PI32V2DAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(PI32V2DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createPI32V2ISelDag(PI32V2TargetMachine &TM,
                                        CodeGenOptLevel OptLevel) {
  return new PI32V2DAGToDAGISelLegacy(TM, OptLevel);
}

static bool isFrameAddr(SDValue Addr) {
  if (isa<FrameIndexSDNode>(Addr))
    return true;
  return Addr.getOpcode() == ISD::ADD &&
         isa<FrameIndexSDNode>(Addr.getOperand(0)) &&
         isa<ConstantSDNode>(Addr.getOperand(1));
}

bool PI32V2DAGToDAGISel::selectAddrFI(SDValue Addr, SDValue &Base,
                                      SDValue &Off) {
  SDLoc DL(Addr);
  if (auto *FI = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i32);
    Off = CurDAG->getTargetConstant(0, DL, MVT::i32);
    return true;
  }
  if (!isFrameAddr(Addr))
    return false;
  auto *FI = cast<FrameIndexSDNode>(Addr.getOperand(0));
  Base = CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i32);
  Off = CurDAG->getSignedTargetConstant(
      cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue(), DL, MVT::i32);
  return true;
}

bool PI32V2DAGToDAGISel::selectAddr(SDValue Addr, SDValue &Base, SDValue &Off,
                                    int Scale, unsigned Bits) {
  if (isFrameAddr(Addr))
    return false;
  SDLoc DL(Addr);
  // Keep the aligned portion of an OR address in one register and use the
  // low displacement in the access.  In particular, GPIO port addresses
  // (p << 6) | (0x50000 + k) can share (p << 6) | 0x50000.
  // This does not assume that the variable part is disjoint from the high
  // constant: only the low six bits must be zero.
  if (Addr.getOpcode() == ISD::OR &&
      Addr.getValueType() == MVT::i32) {
    if (auto *C = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      uint32_t V = C->getZExtValue();
      unsigned Low = V & 63;
      if (Low && (V & ~63u) && !PI32V2::modImmRawFor(V).has_value() &&
          Low % Scale == 0 && isIntN(Bits, Low / Scale) &&
          CurDAG->MaskedValueIsZero(Addr.getOperand(0), APInt(32, 63)) &&
          PI32V2::modImmRawFor(V & ~63u).has_value()) {
        SDValue High(CurDAG->getMachineNode(
            PI32V2::LI_MODIMM, DL, MVT::i32,
            CurDAG->getTargetConstant(V & ~63u, DL, MVT::i32)), 0);
        Base = SDValue(CurDAG->getMachineNode(
            PI32V2::OR_RRR, DL, MVT::i32, Addr.getOperand(0), High), 0);
        Off = CurDAG->getTargetConstant(Low, DL, MVT::i32);
        return true;
      }
    }
  }
  if (CurDAG->isBaseWithConstantOffset(Addr) &&
      !isa<FrameIndexSDNode>(Addr.getOperand(0))) {
    int64_t C = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    if (C % Scale == 0 && isIntN(Bits, C / Scale)) {
      Base = Addr.getOperand(0);
      Off = CurDAG->getSignedTargetConstant(C, DL, MVT::i32);
      return true;
    }
  }
  Base = Addr;
  Off = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

// (a & ~M) | Y with M one run of bits and Y inside it: a field insert,
// `a <= insert(s, p:position, l:length)`, 4 bytes where the mask, the shift
// and the or take 6 or more. The value is the low l bits of s, so the shift of
// the field into place and its mask are not needed.
bool PI32V2DAGToDAGISel::tryInsert(SDNode *N) {
  if (N->getValueType(0) != MVT::i32)
    return false;
  for (unsigned I = 0; I < 2; ++I) {
    SDValue X = N->getOperand(I), Y = N->getOperand(1 - I);
    if (X.getOpcode() != ISD::AND || !isa<ConstantSDNode>(X.getOperand(1)))
      continue;
    uint32_t M = ~uint32_t(X.getConstantOperandVal(1));
    if (!M || !isShiftedMask_32(M))
      continue;
    unsigned P = llvm::countr_zero(M), L = llvm::popcount(M);
    // a whole top part, from P to the end, is a zero extension and a shift
    // and an or just as well; a one-bit field is an or and a mask
    if (P + L >= 32 || L < 2 || !CurDAG->MaskedValueIsZero(Y, APInt(32, ~M)))
      continue;
    // The field of Y at P: a shift left by P gives the value itself, an and
    // that keeps the field's bits is no change to it.
    SDValue S = Y;
    if (S.getOpcode() == ISD::AND && isa<ConstantSDNode>(S.getOperand(1)) &&
        (uint32_t(S.getConstantOperandVal(1)) & M) == M)
      S = S.getOperand(0);
    SDLoc DL(N);
    if (P == 0) {
      // the low L bits of S go in
    } else if (S.getOpcode() == ISD::SHL &&
               isa<ConstantSDNode>(S.getOperand(1)) &&
               S.getConstantOperandVal(1) == P) {
      S = S.getOperand(0);
    } else {
      S = SDValue(CurDAG->getMachineNode(
                      PI32V2::LSR_RRI_L, DL, MVT::i32, S,
                      CurDAG->getTargetConstant(P, DL, MVT::i32)),
                  0);
    }
    SDValue Ops[] = {X.getOperand(0), S,
                     CurDAG->getTargetConstant(P, DL, MVT::i32),
                     CurDAG->getTargetConstant(L, DL, MVT::i32)};
    ReplaceNode(N, CurDAG->getMachineNode(PI32V2::INSERT_RRII, DL, MVT::i32,
                                          Ops));
    return true;
  }
  // X | field: the field of Y, masked to its width, into zero bits of X
  // (`(b & 0x7f) << 7`, `(b << 7) & 0x3f80`): the mask is not needed
  for (unsigned I = 0; I < 2; ++I) {
    SDValue X = N->getOperand(I), Y = N->getOperand(1 - I);
    SDValue B;
    uint32_t M = 0;
    unsigned P = 0;
    if (Y.getOpcode() == ISD::AND && isa<ConstantSDNode>(Y.getOperand(1)) &&
        Y.getOperand(0).getOpcode() == ISD::SHL &&
        isa<ConstantSDNode>(Y.getOperand(0).getOperand(1))) {
      M = Y.getConstantOperandVal(1);
      P = Y.getOperand(0).getConstantOperandVal(1);
      B = Y.getOperand(0).getOperand(0);
    } else if (Y.getOpcode() == ISD::SHL &&
               isa<ConstantSDNode>(Y.getOperand(1)) &&
               Y.getOperand(0).getOpcode() == ISD::AND &&
               isa<ConstantSDNode>(Y.getOperand(0).getOperand(1))) {
      P = Y.getConstantOperandVal(1);
      uint32_t Low = Y.getOperand(0).getConstantOperandVal(1);
      if (P >= 32 || !isMask_32(Low))
        continue;
      M = Low << P; // (the bits shifted out do not matter)
      B = Y.getOperand(0).getOperand(0);
      if (popcount(Low) + P > 32)
        continue;
    } else
      continue;
    if (!M || !isShiftedMask_32(M) || P >= 32 || llvm::countr_zero(M) != P)
      continue;
    unsigned L = llvm::popcount(M);
    if (P + L >= 32 || L < 2 || !CurDAG->MaskedValueIsZero(X, APInt(32, M)))
      continue;
    SDLoc DL(N);
    SDValue Ops[] = {X, B, CurDAG->getTargetConstant(P, DL, MVT::i32),
                     CurDAG->getTargetConstant(L, DL, MVT::i32)};
    ReplaceNode(N, CurDAG->getMachineNode(PI32V2::INSERT_RRII, DL, MVT::i32,
                                          Ops));
    return true;
  }
  return false;
}

void PI32V2DAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }
  if (N->getOpcode() == ISD::FrameIndex) {
    // The address of a stack slot: sp + n, when the frame is laid out.
    SDLoc DL(N);
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i32);
    ReplaceNode(N, CurDAG->getMachineNode(
                       PI32V2::ADDR_FI, DL, MVT::i32, TFI,
                       CurDAG->getTargetConstant(0, DL, MVT::i32)));
    return;
  }
  if (auto *LD = dyn_cast<LoadSDNode>(N);
      LD && LD->getAddressingMode() == ISD::POST_INC) {
    // value, new base, chain = [b++=n]
    bool S = LD->getExtensionType() == ISD::SEXTLOAD;
    unsigned Opc;
    switch (LD->getMemoryVT().getSimpleVT().SimpleTy) {
    case MVT::i8: Opc = S ? PI32V2::LDBS_POST : PI32V2::LDBU_POST; break;
    case MVT::i16: Opc = S ? PI32V2::LDHS_POST : PI32V2::LDHU_POST; break;
    default: Opc = PI32V2::LDW_POST; break;
    }
    SDLoc DL(N);
    SDValue Step = CurDAG->getSignedTargetConstant(
        cast<ConstantSDNode>(LD->getOffset())->getSExtValue(), DL, MVT::i32);
    MachineSDNode *M = CurDAG->getMachineNode(
        Opc, DL, LD->getValueType(0), MVT::i32, MVT::Other,
        {LD->getBasePtr(), Step, LD->getChain()});
    CurDAG->setNodeMemRefs(M, {LD->getMemOperand()});
    ReplaceNode(N, M);
    return;
  }
  if (auto *ST = dyn_cast<StoreSDNode>(N);
      ST && ST->getAddressingMode() == ISD::POST_INC) {
    // new base, chain = ([b++=n] = v)
    unsigned Opc;
    switch (ST->getMemoryVT().getSimpleVT().SimpleTy) {
    case MVT::i8: Opc = PI32V2::STB_POST; break;
    case MVT::i16: Opc = PI32V2::STH_POST; break;
    default: Opc = PI32V2::STW_POST; break;
    }
    SDLoc DL(N);
    SDValue Step = CurDAG->getSignedTargetConstant(
        cast<ConstantSDNode>(ST->getOffset())->getSExtValue(), DL, MVT::i32);
    MachineSDNode *M = CurDAG->getMachineNode(
        Opc, DL, MVT::i32, MVT::Other,
        {ST->getBasePtr(), Step, ST->getValue(), ST->getChain()});
    CurDAG->setNodeMemRefs(M, {ST->getMemOperand()});
    ReplaceNode(N, M);
    return;
  }
  if (N->getOpcode() == PI32V2ISD::UDIV64 ||
      N->getOpcode() == PI32V2ISD::SDIV64 ||
      N->getOpcode() == PI32V2ISD::SHL64 ||
      N->getOpcode() == PI32V2ISD::SRL64 ||
      N->getOpcode() == PI32V2ISD::SRA64) {
    // the dividend as a register pair, the quotient a pair
    SDLoc DL(N);
    SDValue Ops[] = {
        CurDAG->getTargetConstant(PI32V2::GPRPairRegClassID, DL, MVT::i32),
        N->getOperand(0),
        CurDAG->getTargetConstant(PI32V2::sub_lo, DL, MVT::i32),
        N->getOperand(1),
        CurDAG->getTargetConstant(PI32V2::sub_hi, DL, MVT::i32)};
    SDValue X(CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::i64,
                                     Ops),
              0);
    unsigned Opc;
    switch (N->getOpcode()) {
    case PI32V2ISD::UDIV64: Opc = PI32V2::DIV_U_PPR; break;
    case PI32V2ISD::SDIV64: Opc = PI32V2::DIV_S_PPR; break;
    case PI32V2ISD::SHL64: Opc = PI32V2::LSL_PAIR_R; break;
    case PI32V2ISD::SRL64: Opc = PI32V2::LSR_PAIR_R; break;
    default: Opc = PI32V2::ASR_PAIR_R; break;
    }
    SDValue Q(CurDAG->getMachineNode(Opc, DL, MVT::i64, X, N->getOperand(2)),
              0);
    if (!SDValue(N, 0).use_empty())
      ReplaceUses(SDValue(N, 0), CurDAG->getTargetExtractSubreg(
                                     PI32V2::sub_lo, DL, MVT::i32, Q));
    if (!SDValue(N, 1).use_empty())
      ReplaceUses(SDValue(N, 1), CurDAG->getTargetExtractSubreg(
                                     PI32V2::sub_hi, DL, MVT::i32, Q));
    CurDAG->RemoveDeadNode(N);
    return;
  }
  if (N->getOpcode() == PI32V2ISD::BRJT) {
    SDLoc DL(N);
    SDNode *M = CurDAG->getMachineNode(
        PI32V2::PseudoBRJT, DL, MVT::i32, MVT::i32, MVT::Other,
        {N->getOperand(1), N->getOperand(2), N->getOperand(0)});
    ReplaceUses(SDValue(N, 0), SDValue(M, 2));
    CurDAG->RemoveDeadNode(N);
    return;
  }
  if (N->getOpcode() == ISD::UMUL_LOHI || N->getOpcode() == ISD::SMUL_LOHI) {
    // r(2n+1)_r(2n) = a * b: the low word in the even register.
    SDLoc DL(N);
    unsigned Opc = N->getOpcode() == ISD::UMUL_LOHI ? PI32V2::MUL_U_PRR
                                                    : PI32V2::MUL_S_PRR;
    SDValue Pair(CurDAG->getMachineNode(Opc, DL, MVT::i64, N->getOperand(0),
                                        N->getOperand(1)),
                 0);
    if (!SDValue(N, 0).use_empty())
      ReplaceUses(SDValue(N, 0), CurDAG->getTargetExtractSubreg(
                                     PI32V2::sub_lo, DL, MVT::i32, Pair));
    if (!SDValue(N, 1).use_empty())
      ReplaceUses(SDValue(N, 1), CurDAG->getTargetExtractSubreg(
                                     PI32V2::sub_hi, DL, MVT::i32, Pair));
    CurDAG->RemoveDeadNode(N);
    return;
  }
  if (N->getOpcode() == ISD::OR && tryInsert(N))
    return;
  SelectCode(N);
}

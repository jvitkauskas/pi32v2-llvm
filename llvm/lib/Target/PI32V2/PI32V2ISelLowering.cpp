//===-- PI32V2ISelLowering.cpp - PI32V2 DAG lowering ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Calls follow JieLi's C convention (pi32v2-isa docs/ABI.md): arguments in
// r0..r3 and then at sp, the caller's outgoing area; results in r0 and r1.
// Only i32 is a legal type: 64-bit values are pairs of words, floating point
// goes to library calls.
//
//===----------------------------------------------------------------------===//

#include "PI32V2ISelLowering.h"
#include "PI32V2.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2MachineFunctionInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

using PI32V2::SelectFloat;

#define DEBUG_TYPE "pi32v2-lower"

#define GET_CALLING_CONV_IMPL
#include "PI32V2GenCallingConv.inc"

PI32V2TargetLowering::PI32V2TargetLowering(const TargetMachine &TM,
                                           const PI32V2Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i32, &PI32V2::GPRRegClass);
  if (STI.hasFPRev1())
    addRegisterClass(MVT::f32, &PI32V2::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());
  // Inline asm uses a single pair register while ordinary i64 values remain
  // split into words. Only copies at that boundary need custom legalization.
  setOperationAction(ISD::CopyFromReg, MVT::i64, Custom);
  setOperationAction(ISD::CopyToReg, MVT::i64, Custom);

  setStackPointerRegisterToSaveRestore(PI32V2::SP);
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  // A memset of a constant size is stores up to JieLi's limits, 8 at -Os and
  // 16 at -O2 (32 and 64 bytes of words; memcpy and memmove are inline at
  // least as far as there). Firmware runs code from RAM that must not call
  // into flash, memset included, and relies on these being stores.
  MaxStoresPerMemsetOptSize = 8;
  MaxStoresPerMemset = 16;
  // Atomics: read-modify-write operations are __sync_* calls, as JieLi's
  // compiler makes them (the runtime provides them); an aligned load or store
  // of up to a word is one access; a fence is csync.
  setMaxAtomicSizeInBitsSupported(32);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32})
    for (unsigned Op :
         {ISD::ATOMIC_CMP_SWAP, ISD::ATOMIC_SWAP, ISD::ATOMIC_LOAD_ADD,
          ISD::ATOMIC_LOAD_SUB, ISD::ATOMIC_LOAD_AND, ISD::ATOMIC_LOAD_OR,
          ISD::ATOMIC_LOAD_XOR, ISD::ATOMIC_LOAD_NAND, ISD::ATOMIC_LOAD_MIN,
          ISD::ATOMIC_LOAD_MAX, ISD::ATOMIC_LOAD_UMIN, ISD::ATOMIC_LOAD_UMAX})
      setOperationAction(Op, VT, LibCall);

  for (unsigned Op : {ISD::GlobalAddress, ISD::ExternalSymbol,
                      ISD::BlockAddress, ISD::JumpTable, ISD::ConstantPool})
    setOperationAction(Op, MVT::i32, Custom);

  setOperationAction(ISD::BR_CC, MVT::i32, Legal);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);
  setOperationAction(ISD::SETCC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);

  for (unsigned Op :
       {ISD::SREM, ISD::UREM, ISD::SDIVREM, ISD::UDIVREM, ISD::MULHS,
        ISD::MULHU, ISD::ROTL,
        ISD::CTPOP, ISD::CTTZ, ISD::DYNAMIC_STACKALLOC,
        ISD::STACKSAVE, ISD::STACKRESTORE})
    setOperationAction(Op, MVT::i32, Expand);
  // 64-bit add and subtract through the carry flag (add then addc, sub then
  // subc); 32 x 32 -> 64 multiplies into a register pair (ISelDAGToDAG).
  for (unsigned Op : {ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE,
                      ISD::UMUL_LOHI, ISD::SMUL_LOHI, ISD::SMIN, ISD::SMAX,
                      ISD::UMIN, ISD::UMAX})
    setOperationAction(Op, MVT::i32, Legal);
  setOperationAction(ISD::ABS_MIN_POISON, MVT::i32, Legal);
  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32}) {
    for (ISD::MemIndexedMode M : {ISD::POST_INC, ISD::PRE_INC}) {
      setIndexedLoadAction(M, VT, Legal);
      setIndexedStoreAction(M, VT, Legal);
    }
  }
  if (STI.hasFPRev1()) {
    for (ISD::MemIndexedMode M : {ISD::POST_INC, ISD::PRE_INC}) {
      setIndexedLoadAction(M, MVT::f32, Legal);
      setIndexedStoreAction(M, MVT::f32, Legal);
    }
  }
  // A 64-bit division by a divisor that fits in 32 bits is div.u.ppr or
  // div.s.ppr (ReplaceNodeResults); others are library calls.
  for (unsigned Op : {ISD::UDIV, ISD::SDIV, ISD::UREM, ISD::SREM})
    setOperationAction(Op, MVT::i64, Custom);
  // 64-bit shifts by a register: the pair shifts (lowerShiftParts)
  for (unsigned Op : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Op, MVT::i32, Custom);
  // (and by a constant: ReplaceNodeResults)
  for (unsigned Op : {ISD::SHL, ISD::SRL, ISD::SRA})
    setOperationAction(Op, MVT::i64, Custom);
  setTargetDAGCombine({ISD::BR_CC, ISD::SELECT_CC});
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i32, Custom);
  setOperationAction(ISD::FRAMEADDR, MVT::i32, Custom);
  for (unsigned Op : {ISD::VAARG, ISD::VACOPY, ISD::VAEND})
    setOperationAction(Op, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  // sext_inreg is a sextra (PI32V2InstrInfo.td).
  // Single-precision float in the (f) forms (PI32V2InstrInfo.td); what they do
  // not do is integer code or a library call.
  if (STI.hasFPRev1()) {
    for (unsigned Op :
         {ISD::FNEG, ISD::FABS, ISD::FCOPYSIGN, ISD::FREM, ISD::FSQRT,
          ISD::FSIN, ISD::FCOS, ISD::FSINCOS, ISD::FTAN, ISD::FPOW,
          ISD::FPOWI, ISD::FLDEXP, ISD::FFREXP, ISD::FLOG, ISD::FLOG2,
          ISD::FLOG10, ISD::FEXP, ISD::FEXP2, ISD::FEXP10, ISD::FCEIL,
          ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT, ISD::FNEARBYINT,
          ISD::FROUND, ISD::FROUNDEVEN, ISD::FMA, ISD::FMAD, ISD::FMINNUM,
          ISD::FMAXNUM, ISD::FMINIMUM, ISD::FMAXIMUM, ISD::FMINIMUMNUM,
          ISD::FMAXIMUMNUM, ISD::FCANONICALIZE, ISD::LRINT, ISD::LLRINT,
          ISD::LROUND, ISD::LLROUND, ISD::FP16_TO_FP, ISD::FP_TO_FP16,
          ISD::BF16_TO_FP, ISD::FP_TO_BF16, ISD::IS_FPCLASS,
          ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT})
      setOperationAction(Op, MVT::f32, Expand);
    setOperationAction(ISD::BR_CC, MVT::f32, Legal);
    setOperationAction(ISD::SETCC, MVT::f32, Expand);
    setOperationAction(ISD::SELECT, MVT::f32, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
    setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
    // The long iff forms of these reset the unit (pi32v2-isa docs/ISA.md):
    // made of the others.
    for (ISD::CondCode CC : {ISD::SETONE, ISD::SETUEQ, ISD::SETUO, ISD::SETO})
      setCondCodeAction(CC, MVT::f32, Expand);
  }
  for (unsigned Ext : {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD})
    setLoadExtAction(Ext, MVT::i32, MVT::i1, Promote);
}

SDValue PI32V2TargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::CopyToReg:
    return lowerCopyToPair(Op, DAG);
  case ISD::GlobalAddress:
  case ISD::ExternalSymbol:
  case ISD::BlockAddress:
  case ISD::JumpTable:
  case ISD::ConstantPool:
    return lowerAddress(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSelectCC(Op, DAG);
  case ISD::BR_JT: {
    // the index and the table, for PseudoBRJT (tbb/tbh as JieLi's compiler)
    SDLoc DL(Op);
    SDValue Table = Op.getOperand(1);
    if (Table.getOpcode() == PI32V2ISD::WRAPPER)
      Table = Table.getOperand(0);
    unsigned JTI = cast<JumpTableSDNode>(Table)->getIndex();
    return DAG.getNode(PI32V2ISD::BRJT, DL, MVT::Other, Op.getOperand(0),
                       Op.getOperand(2), DAG.getTargetJumpTable(JTI, MVT::i32));
  }
  case ISD::SHL_PARTS:
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS: {
    // r(2n+1)_r(2n) <<= count (>>=, >>>=): a count is its low 8 bits, 0..63
    // as C needs them
    SDLoc DL(Op);
    unsigned Opc = Op.getOpcode() == ISD::SHL_PARTS   ? PI32V2ISD::SHL64
                   : Op.getOpcode() == ISD::SRL_PARTS ? PI32V2ISD::SRL64
                                                      : PI32V2ISD::SRA64;
    SDValue R = DAG.getNode(Opc, DL, DAG.getVTList(MVT::i32, MVT::i32),
                            Op.getOperand(0), Op.getOperand(1),
                            Op.getOperand(2));
    return DAG.getMergeValues({R.getValue(0), R.getValue(1)}, DL);
  }
  case ISD::VASTART: {
    // va_list is a pointer to the first unnamed word.
    MachineFunction &MF = DAG.getMachineFunction();
    auto *FI = MF.getInfo<PI32V2MachineFunctionInfo>();
    SDLoc DL(Op);
    SDValue Addr = DAG.getFrameIndex(FI->getVarArgsFrameIndex(), MVT::i32);
    const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
    return DAG.getStore(Op.getOperand(0), DL, Addr, Op.getOperand(1),
                        MachinePointerInfo(SV));
  }
  case ISD::RETURNADDR:
  case ISD::FRAMEADDR: {
    // Only of the current function: rets as it was on entry, and the frame
    // base (r7, which such a function keeps). Callers' frames have no chain
    // to walk.
    MachineFunction &MF = DAG.getMachineFunction();
    MachineFrameInfo &MFI = MF.getFrameInfo();
    SDLoc DL(Op);
    bool RA = Op.getOpcode() == ISD::RETURNADDR;
    if (RA)
      MFI.setReturnAddressIsTaken(true);
    else
      MFI.setFrameAddressIsTaken(true);
    if (Op.getConstantOperandVal(0) != 0) {
      DAG.getContext()->emitError(
          "PI32V2: only __builtin_return_address(0) and "
          "__builtin_frame_address(0) are supported");
      return DAG.getConstant(0, DL, MVT::i32);
    }
    if (!RA)
      return DAG.getCopyFromReg(DAG.getEntryNode(), DL, PI32V2::R7, MVT::i32);
    Register Reg = MF.addLiveIn(PI32V2::RETS, &PI32V2::GPRRegClass);
    return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, MVT::i32);
  }
  default:
    report_fatal_error("PI32V2: unexpected operation to lower");
  }
}

// An address is a 32-bit constant: r = sym (li.u32, relocation MOV_ABS32).
SDValue PI32V2TargetLowering::lowerAddress(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue T;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Op)) {
    // The symbol alone, an offset added: accesses to one global share its
    // address and reach the fields by offset ([r+n]), as JieLi's compiler
    // does.
    SDValue Base = DAG.getNode(
        PI32V2ISD::WRAPPER, DL, VT,
        DAG.getTargetGlobalAddress(G->getGlobal(), DL, VT, 0));
    if (int64_t Off = G->getOffset())
      return DAG.getNode(ISD::ADD, DL, VT, Base,
                         DAG.getSignedConstant(Off, DL, VT));
    return Base;
  }
  if (auto *E = dyn_cast<ExternalSymbolSDNode>(Op))
    T = DAG.getTargetExternalSymbol(E->getSymbol(), VT);
  else if (auto *B = dyn_cast<BlockAddressSDNode>(Op))
    T = DAG.getTargetBlockAddress(B->getBlockAddress(), VT, B->getOffset());
  else if (auto *J = dyn_cast<JumpTableSDNode>(Op))
    T = DAG.getTargetJumpTable(J->getIndex(), VT);
  else {
    auto *C = cast<ConstantPoolSDNode>(Op);
    T = C->isMachineConstantPoolEntry()
            ? DAG.getTargetConstantPool(C->getMachineCPVal(), VT, C->getAlign(),
                                        C->getOffset())
            : DAG.getTargetConstantPool(C->getConstVal(), VT, C->getAlign(),
                                        C->getOffset());
  }
  return DAG.getNode(PI32V2ISD::WRAPPER, DL, VT, T);
}

// A range check, (x + c1) u< c2, also is (x + (c1 - c2)) u>= -c2: LLVM writes
// `a < lo || a > hi` one way, JieLi's compiler the other; take the one whose
// constants the add and the compare hold (LHS, RHS and CC are updated).
static bool rewriteRangeCheck(SDValue &LHS, SDValue &RHS, ISD::CondCode &CC,
                              SelectionDAG &DAG, const SDLoc &DL) {
  if (LHS.getOpcode() != ISD::ADD || LHS.getValueType() != MVT::i32)
    return false;
  auto *C1 = dyn_cast<ConstantSDNode>(LHS.getOperand(1));
  auto *C2 = dyn_cast<ConstantSDNode>(RHS);
  if (!C1 || !C2)
    return false;
  uint32_t A = C1->getZExtValue(), B = C2->getZExtValue();
  ISD::CondCode K = CC;
  // u> b is u>= b + 1, u<= b is u< b + 1
  if (K == ISD::SETUGT || K == ISD::SETULE) {
    if (B == 0xFFFFFFFFu)
      return false;
    ++B;
    K = K == ISD::SETUGT ? ISD::SETUGE : ISD::SETULT;
  }
  if ((K != ISD::SETULT && K != ISD::SETUGE) || B == 0)
    return false;
  auto Cost = [](uint32_t Add, uint32_t Cmp) {
    return (isInt<14>(int32_t(Add)) ? 0 : 1) + (isUInt<12>(Cmp) ? 0 : 1);
  };
  uint32_t A2 = A - B, B2 = 0u - B;
  if (Cost(A2, B2) >= Cost(A, B))
    return false;
  LHS = DAG.getNode(ISD::ADD, DL, MVT::i32, LHS.getOperand(0),
                    DAG.getConstant(A2, DL, MVT::i32));
  RHS = DAG.getConstant(B2, DL, MVT::i32);
  CC = K == ISD::SETULT ? ISD::SETUGE : ISD::SETULT;
  return true;
}

SDValue PI32V2TargetLowering::lowerSelectCC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  // A float compare is marked in the condition: the branch is an iff.
  bool Float = LHS.getValueType() == MVT::f32;
  if (!Float)
    rewriteRangeCheck(LHS, RHS, CC, DAG, DL);
  unsigned Cond = CC;
  if (Float)
    Cond |= SelectFloat;
  return DAG.getNode(PI32V2ISD::SELECT_CC, DL, Op.getValueType(), LHS, RHS,
                     Op.getOperand(2), Op.getOperand(3),
                     DAG.getConstant(Cond, DL, MVT::i32));
}

// ---- calls ------------------------------------------------------------------

SDValue PI32V2TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_PI32V2);

  for (CCValAssign &VA : ArgLocs) {
    SDValue V;
    if (VA.isRegLoc()) {
      Register VReg = RegInfo.createVirtualRegister(&PI32V2::GPRRegClass);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      V = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
    } else {
      // The caller's outgoing area: at the incoming sp.
      int FI = MFI.CreateFixedObject(4, VA.getLocMemOffset(), true);
      V = DAG.getLoad(VA.getLocVT(), DL, Chain,
                      DAG.getFrameIndex(FI, MVT::i32),
                      MachinePointerInfo::getFixedStack(MF, FI));
    }
    if (VA.getLocInfo() == CCValAssign::BCvt)
      V = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), V);
    else if (VA.getValVT() != VA.getLocVT())
      V = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), V);
    InVals.push_back(V);
  }

  if (IsVarArg) {
    // The unused argument registers are pushed just below the incoming sp
    // (PI32V2FrameLowering::emitPrologue); the first unnamed word is the
    // first of them, or the first stack word after the named ones.
    auto *FI = MF.getInfo<PI32V2MachineFunctionInfo>();
    static const MCPhysReg ArgRegs[] = {PI32V2::R0, PI32V2::R1, PI32V2::R2,
                                        PI32V2::R3};
    unsigned First = CCInfo.getFirstUnallocated(ArgRegs);
    FI->setFirstVarArgReg(First);
    if (First < 4) {
      int Bytes = 4 * (4 - int(First));
      int Save = MFI.CreateFixedObject(Bytes, -Bytes, true);
      FI->setVarArgsSaveIndex(Save);
      FI->setVarArgsFrameIndex(Save);
    } else {
      FI->setVarArgsFrameIndex(
          MFI.CreateFixedObject(4, CCInfo.getStackSize(), true));
    }
  }
  return Chain;
}

SDValue PI32V2TargetLowering::PerformDAGCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case ISD::BR_CC: {
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
    SDValue LHS = N->getOperand(2), RHS = N->getOperand(3);
    if (!rewriteRangeCheck(LHS, RHS, CC, DAG, DL))
      return SDValue();
    return DAG.getNode(ISD::BR_CC, DL, MVT::Other, N->getOperand(0),
                       DAG.getCondCode(CC), LHS, RHS, N->getOperand(4));
  }
  case ISD::SELECT_CC: {
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(4))->get();
    SDValue LHS = N->getOperand(0), RHS = N->getOperand(1);
    if (!rewriteRangeCheck(LHS, RHS, CC, DAG, DL))
      return SDValue();
    return DAG.getNode(ISD::SELECT_CC, DL, N->getValueType(0), LHS, RHS,
                       N->getOperand(2), N->getOperand(3), DAG.getCondCode(CC));
  }
  default:
    return SDValue();
  }
}

static cl::opt<unsigned> PairShiftMin("pi32v2-pair-shift-min", cl::Hidden,
                                      cl::init(1));
static cl::opt<unsigned> PairShiftMaxInsts("pi32v2-pair-shift-max-insts",
                                           cl::Hidden, cl::init(4000));
static cl::opt<unsigned> PairShiftMax("pi32v2-pair-shift-max", cl::Hidden,
                                      cl::init(31));

void PI32V2TargetLowering::ReplaceNodeResults(SDNode *N,
                                              SmallVectorImpl<SDValue> &Results,
                                              SelectionDAG &DAG) const {
  unsigned Opc = N->getOpcode();
  if (Opc == ISD::CopyFromReg) {
    expandCopyFromPair(N, Results, DAG);
    return;
  }
  if ((Opc == ISD::SHL || Opc == ISD::SRL || Opc == ISD::SRA) &&
      N->getValueType(0) == MVT::i64) {
    // r1_r0 <<= n (4 bytes) for a constant count the word moves do not do
    // in two instructions (32 and over)
    auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!C || C->getZExtValue() < PairShiftMin || C->getZExtValue() > PairShiftMax)
      return;
    // (a huge function has no registers left for pairs: its spills cost more
    // than the shifts save)
    if (DAG.getMachineFunction().getFunction().getInstructionCount() >
        PairShiftMaxInsts)
      return;
    SDLoc DL(N);
    auto [Lo, Hi] = DAG.SplitScalar(N->getOperand(0), DL, MVT::i32, MVT::i32);
    unsigned PO = Opc == ISD::SHL   ? PI32V2ISD::SHL64
                  : Opc == ISD::SRL ? PI32V2ISD::SRL64
                                    : PI32V2ISD::SRA64;
    SDValue R = DAG.getNode(PO, DL, DAG.getVTList(MVT::i32, MVT::i32), Lo, Hi,
                            DAG.getConstant(C->getZExtValue(), DL, MVT::i32));
    Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, R.getValue(0),
                                  R.getValue(1)));
    return;
  }
  bool Signed = Opc == ISD::SDIV || Opc == ISD::SREM;
  if (N->getValueType(0) != MVT::i64 ||
      (Opc != ISD::UDIV && Opc != ISD::SDIV && Opc != ISD::UREM &&
       Opc != ISD::SREM))
    return; // the default: a library call
  SDValue X = N->getOperand(0), D = N->getOperand(1);
  // the divisor must be a 32-bit value, zero- or sign-extended
  if (Signed ? DAG.ComputeNumSignBits(D) < 33
             : DAG.computeKnownBits(D).countMinLeadingZeros() < 32)
    return;
  SDLoc DL(N);
  auto [XLo, XHi] = DAG.SplitScalar(X, DL, MVT::i32, MVT::i32);
  SDValue D32 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, D);
  SDValue Q = DAG.getNode(Signed ? PI32V2ISD::SDIV64 : PI32V2ISD::UDIV64, DL,
                          DAG.getVTList(MVT::i32, MVT::i32), XLo, XHi, D32);
  SDValue Q64 = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Q.getValue(0),
                            Q.getValue(1));
  if (Opc == ISD::UDIV || Opc == ISD::SDIV) {
    Results.push_back(Q64);
    return;
  }
  // x - q * d (C's remainder: q truncates toward zero)
  Results.push_back(
      DAG.getNode(ISD::SUB, DL, MVT::i64, X,
                  DAG.getNode(ISD::MUL, DL, MVT::i64, Q64, D)));
}

static bool indexedParts(SDNode *N, SDNode *Op, SDValue &Base,
                         SDValue &Offset) {
  EVT VT;
  if (auto *LD = dyn_cast<LoadSDNode>(N))
    VT = LD->getMemoryVT();
  else if (auto *ST = dyn_cast<StoreSDNode>(N))
    VT = ST->getMemoryVT();
  else
    return false;
  if (Op->getOpcode() != ISD::ADD || !VT.isSimple())
    return false;
  auto *C = dyn_cast<ConstantSDNode>(Op->getOperand(1));
  int64_t Size = VT.getStoreSize();
  if (!C || (Size != 1 && Size != 2 && Size != 4))
    return false;
  int64_t Step = C->getSExtValue();
  if (Step % Size || !isInt<9>(Step / Size))
    return false;
  Base = Op->getOperand(0);
  Offset = Op->getOperand(1);
  return true;
}

bool PI32V2TargetLowering::getPostIndexedAddressParts(
    SDNode *N, SDNode *Op, SDValue &Base, SDValue &Offset,
    ISD::MemIndexedMode &AM, SelectionDAG &DAG) const {
  AM = ISD::POST_INC;
  return indexedParts(N, Op, Base, Offset);
}

bool PI32V2TargetLowering::getPreIndexedAddressParts(
    SDNode *N, SDValue &Base, SDValue &Offset, ISD::MemIndexedMode &AM,
    SelectionDAG &DAG) const {
  SDValue Ptr = N->getOpcode() == ISD::LOAD
                    ? cast<LoadSDNode>(N)->getBasePtr()
                    : cast<StoreSDNode>(N)->getBasePtr();
  AM = ISD::PRE_INC;
  return Ptr->getOpcode() == ISD::ADD && indexedParts(N, Ptr.getNode(), Base, Offset);
}

// CodeGenPrepare asks this one: an address it cannot fold stays a pointer
// computed once and kept in a register (a struct field 2 KB away, hoisted out
// of a loop: hundreds of live pointers in a big function). A far offset from
// a base register is an addi next to the access, so it counts as folded.
bool PI32V2TargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                 const AddrMode &AM, Type *Ty,
                                                 unsigned AS,
                                                 Instruction *I) const {
  if (isExactAddressingMode(DL, AM, Ty))
    return true;
  return !AM.BaseGV && AM.Scale == 0 && isInt<17>(AM.BaseOffs);
}

bool PI32V2TargetLowering::isExactAddressingMode(const DataLayout &DL,
                                                 const AddrMode &AM,
                                                 Type *Ty) const {
  if (AM.BaseGV)
    return false; // an address is a li of its own
  uint64_t Size = Ty && Ty->isSized() ? DL.getTypeStoreSize(Ty) : 4;
  // (a pair is two words; an access of 3 bytes, or of none, is split into or
  // checked as bytes: a plain base register must always be legal)
  int64_t Unit = Size >= 4 ? 4 : Size == 2 ? 2 : 1;
  unsigned Regs = AM.HasBaseReg;
  if (AM.Scale == 1)
    ++Regs;
  else if (AM.Scale == Unit && Unit != 1 && Size == uint64_t(Unit)) {
    // [b+i<<1], [b+i<<2]: no offset
    return AM.HasBaseReg && AM.BaseOffs == 0;
  } else if (AM.Scale != 0)
    return false;
  if (Regs > 1)
    return AM.BaseOffs == 0; // [b+i]
  return AM.BaseOffs % Unit == 0 && isInt<9>(AM.BaseOffs / Unit);
}

EVT PI32V2TargetLowering::getOptimalMemOpType(
    LLVMContext &Context, const MemOp &Op,
    const AttributeList &FuncAttributes) const {
  Align A = Op.isFixedDstAlign() ? Op.getDstAlign() : Align(4);
  if (Op.isMemcpyOrMemmove())
    A = std::min(A, Op.getSrcAlign());
  if (A >= 4 && Op.size() >= 4)
    return MVT::i32;
  if (A >= 2 && Op.size() >= 2)
    return MVT::i16;
  return MVT::i8;
}

std::pair<const TargetRegisterClass *, uint8_t>
PI32V2TargetLowering::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                              MVT VT) const {
  if (VT == MVT::i64 || VT == MVT::Untyped)
    return {&PI32V2::GPRRegClass, 2};
  return TargetLowering::findRepresentativeClass(TRI, VT);
}

// N's only use is a copy to the result register, whose only use is the return.
bool PI32V2TargetLowering::isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
  if (N->getNumValues() != 1 || !N->hasNUsesOfValue(1, 0))
    return false;
  SDNode *Copy = *N->user_begin();
  if (Copy->getOpcode() == ISD::BITCAST)
    return isUsedByReturnOnly(Copy, Chain);
  if (Copy->getOpcode() != ISD::CopyToReg ||
      Copy->getOperand(Copy->getNumOperands() - 1).getValueType() == MVT::Glue)
    return false;
  bool HasRet = false;
  for (SDNode *U : Copy->users()) {
    if (U->getOpcode() != PI32V2ISD::RET)
      return false;
    HasRet = true;
  }
  if (!HasRet)
    return false;
  Chain = Copy->getOperand(0);
  return true;
}

SDValue PI32V2TargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  MachineFunction &MF = DAG.getMachineFunction();
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_PI32V2);
  unsigned Bytes = alignTo(CCInfo.getStackSize(), 4);

  // A call in tail position becomes a goto after the epilogue, as JieLi's
  // compiler does it, when nothing of the caller's frame is still needed: the
  // arguments all in registers, a direct callee, no struct returned through
  // memory, and an ordinary caller (not variadic, not an interrupt handler).
  if (CLI.IsTailCall && isPI32V2LargeProgram()) {
    if (CLI.CB && CLI.CB->isMustTailCall())
      report_fatal_error("PI32V2: no tail calls with -pi32v2-large-program");
    CLI.IsTailCall = false;
  }
  if (CLI.IsTailCall) {
    const Function &F = MF.getFunction();
    bool Ok = Bytes == 0 && !F.isVarArg() && !F.hasStructRetAttr() &&
              !F.hasFnAttribute("interrupt") && CLI.CallConv == F.getCallingConv() &&
              (isa<GlobalAddressSDNode>(Callee) ||
               isa<ExternalSymbolSDNode>(Callee));
    for (const ISD::OutputArg &O : CLI.Outs)
      Ok &= !O.Flags.isByVal() && !O.Flags.isSRet();
    if (!Ok && CLI.CB && CLI.CB->isMustTailCall())
      report_fatal_error("PI32V2: this musttail call cannot be a tail call");
    CLI.IsTailCall = Ok;
  }
  bool Tail = CLI.IsTailCall;

  // A struct passed by value (byval) is one pointer word, in the argument's
  // register or stack slot, to a copy the caller makes in its own frame; the
  // callee may write to it (pi32v2-isa docs/ABI.md, section 5.4). The copies
  // come before the call sequence: a large one is a call to memcpy.
  SmallVector<SDValue, 4> ByValCopies;
  for (unsigned I = 0; I < CLI.Outs.size(); ++I) {
    ISD::ArgFlagsTy Flags = CLI.Outs[I].Flags;
    if (!Flags.isByVal())
      continue;
    unsigned Size = Flags.getByValSize();
    Align A = Flags.getNonZeroByValAlign();
    int FI = MF.getFrameInfo().CreateStackObject(std::max(Size, 1u), A,
                                                 /*isSpillSlot=*/false);
    SDValue Copy = DAG.getFrameIndex(FI, MVT::i32);
    if (Size)
      Chain = DAG.getMemcpy(Chain, DL, Copy, CLI.OutVals[I],
                            DAG.getConstant(Size, DL, MVT::i32), A, A,
                            /*isVol=*/false, /*AlwaysInline=*/false,
                            /*CI=*/nullptr, /*OverrideTailCall=*/false,
                            MachinePointerInfo::getFixedStack(MF, FI),
                            MachinePointerInfo());
    ByValCopies.push_back(Copy);
  }

  if (!Tail)
    Chain = DAG.getCALLSEQ_START(Chain, Bytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 4> Regs;
  SmallVector<SDValue, 8> Stores;
  SDValue SP;
  for (unsigned I = 0, NextCopy = 0; I < ArgLocs.size(); ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue V = CLI.OutVals[I];
    if (CLI.Outs[I].Flags.isByVal())
      V = ByValCopies[NextCopy++];
    switch (VA.getLocInfo()) {
    case CCValAssign::SExt:
      V = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), V);
      break;
    case CCValAssign::ZExt:
      V = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), V);
      break;
    case CCValAssign::AExt:
      V = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), V);
      break;
    case CCValAssign::BCvt:
      V = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), V);
      break;
    default:
      break;
    }
    if (VA.isRegLoc()) {
      Regs.push_back({VA.getLocReg(), V});
      continue;
    }
    if (!SP)
      SP = DAG.getCopyFromReg(Chain, DL, PI32V2::SP, MVT::i32);
    SDValue Addr = DAG.getNode(ISD::ADD, DL, MVT::i32, SP,
                               DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));
    Stores.push_back(DAG.getStore(Chain, DL, V, Addr, MachinePointerInfo()));
  }
  if (!Stores.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);

  SDValue Glue;
  for (auto &R : Regs) {
    Chain = DAG.getCopyToReg(Chain, DL, R.first, R.second, Glue);
    Glue = Chain.getValue(1);
  }

  // An llvm.* function that is not an intrinsic of this LLVM (one of JieLi's
  // that AutoUpgrade does not know) has no definition anywhere.
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    if (G->getGlobal()->getName().starts_with("llvm."))
      report_fatal_error("PI32V2: call to an unknown intrinsic, " +
                         G->getGlobal()->getName());
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i32,
                                        G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32);

  SmallVector<SDValue, 8> Ops = {Chain, Callee};
  for (auto &R : Regs)
    Ops.push_back(DAG.getRegister(R.first, R.second.getValueType()));
  if (!Tail)
    Ops.push_back(DAG.getRegisterMask(
        Subtarget.getRegisterInfo()->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue)
    Ops.push_back(Glue);
  if (Tail) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(PI32V2ISD::TAIL, DL, MVT::Other, Ops);
  }

  SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(PI32V2ISD::CALL, DL, VTs, Ops);
  Glue = Chain.getValue(1);
  Chain = DAG.getCALLSEQ_END(Chain, Bytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  SmallVector<CCValAssign, 2> RetLocs;
  CCState RetInfo(CLI.CallConv, CLI.IsVarArg, MF, RetLocs, *DAG.getContext());
  RetInfo.AnalyzeCallResult(CLI.Ins, RetCC_PI32V2);
  for (CCValAssign &VA : RetLocs) {
    SDValue V =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    Chain = V.getValue(1);
    Glue = V.getValue(2);
    if (VA.getLocInfo() == CCValAssign::BCvt)
      V = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), V);
    else if (VA.getValVT() != VA.getLocVT())
      V = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), V);
    InVals.push_back(V);
  }
  return Chain;
}

bool PI32V2TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 2> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_PI32V2);
}

SDValue PI32V2TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 2> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_PI32V2);

  SDValue Glue;
  SmallVector<SDValue, 4> Ops = {Chain};
  for (unsigned I = 0; I < RVLocs.size(); ++I) {
    CCValAssign &VA = RVLocs[I];
    SDValue V = OutVals[I];
    if (VA.getLocInfo() == CCValAssign::BCvt)
      V = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), V);
    else if (VA.getLocInfo() == CCValAssign::SExt)
      V = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), V);
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      V = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), V);
    else if (VA.getLocInfo() == CCValAssign::AExt)
      V = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), V);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), V, Glue);
    Glue = Chain.getValue(1);
    Ops.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }
  Ops[0] = Chain;
  if (Glue)
    Ops.push_back(Glue);
  return DAG.getNode(PI32V2ISD::RET, DL, MVT::Other, Ops);
}

unsigned PI32V2TargetLowering::getNumRegisters(
    LLVMContext &Context, EVT VT, std::optional<MVT> RegisterVT) const {
  // An explicit register type comes from an inline-asm constraint. Each
  // operand occupies one word or one pair, even when the value is narrower.
  if (RegisterVT && VT.getSizeInBits() <= 64 &&
      (*RegisterVT == MVT::i32 || *RegisterVT == MVT::f32 ||
       *RegisterVT == MVT::i64))
    return 1;
  return TargetLowering::getNumRegisters(Context, VT, RegisterVT);
}

bool PI32V2TargetLowering::splitValueIntoRegisterParts(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
    unsigned NumParts, MVT PartVT,
    std::optional<CallingConv::ID> CallConv) const {
  if (CallConv || PartVT != MVT::i64 || NumParts != 1)
    return false;
  EVT VT = Val.getValueType();
  if (VT.isFloatingPoint())
    Val = DAG.getNode(ISD::BITCAST, DL,
                      MVT::getIntegerVT(VT.getSizeInBits()), Val);
  Parts[0] = DAG.getZExtOrTrunc(Val, DL, MVT::i64);
  return true;
}

SDValue PI32V2TargetLowering::joinRegisterPartsIntoValue(
    SelectionDAG &DAG, const SDLoc &DL, const SDValue *Parts, unsigned NumParts,
    MVT PartVT, EVT ValueVT, std::optional<CallingConv::ID> CallConv) const {
  // A word constraint on a 64-bit integer truncates inputs and zero-extends
  // outputs, as JieLi's w/W constraints do.
  if (!CallConv && NumParts == 1 && PartVT == MVT::i32 &&
      ValueVT == MVT::i64)
    return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Parts[0]);
  return SDValue();
}

// Copies at the inline-asm boundary carry an opaque pair through the DAG.
// Its words are legal i32 values; i64 arithmetic still uses its usual expansion.
SDValue PI32V2TargetLowering::lowerCopyToPair(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Value = Op.getOperand(2);
  auto [Lo, Hi] = DAG.SplitScalar(Value, DL, MVT::i32, MVT::i32);
  SDValue Pair(DAG.getMachineNode(
                   TargetOpcode::REG_SEQUENCE, DL, MVT::Untyped,
                   {DAG.getTargetConstant(PI32V2::GPRPairRegClassID, DL, MVT::i32),
                    Lo, DAG.getTargetConstant(PI32V2::sub_lo, DL, MVT::i32),
                    Hi, DAG.getTargetConstant(PI32V2::sub_hi, DL, MVT::i32)}),
               0);
  Register Reg = cast<RegisterSDNode>(Op.getOperand(1))->getReg();
  if (Op->getNumValues() == 2) {
    SDValue Glue = Op->getNumOperands() == 4 ? Op.getOperand(3) : SDValue();
    return DAG.getCopyToReg(Op.getOperand(0), DL, Reg, Pair, Glue);
  }
  return DAG.getCopyToReg(Op.getOperand(0), DL, Reg, Pair);
}

void PI32V2TargetLowering::expandCopyFromPair(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  SDLoc DL(N);
  Register Reg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
  SDValue Pair;
  if (N->getNumValues() == 3) {
    SDValue Glue = N->getNumOperands() == 3 ? N->getOperand(2) : SDValue();
    Pair = DAG.getCopyFromReg(N->getOperand(0), DL, Reg, MVT::Untyped, Glue);
  } else {
    Pair = DAG.getCopyFromReg(N->getOperand(0), DL, Reg, MVT::Untyped);
  }
  SDValue Lo = DAG.getTargetExtractSubreg(PI32V2::sub_lo, DL, MVT::i32, Pair);
  SDValue Hi = DAG.getTargetExtractSubreg(PI32V2::sub_hi, DL, MVT::i32, Pair);
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Lo, Hi));
  Results.push_back(Pair.getValue(1));
  if (N->getNumValues() == 3)
    Results.push_back(Pair.getValue(2));
}

TargetLowering::ConstraintType
PI32V2TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'w':
    case 'W':
    case 'd':
    case 'D':
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
PI32V2TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                   StringRef Constraint,
                                                   MVT VT) const {
  // JieLi's word constraints select a bank of eight GPRs; its doubleword
  // constraints select the four aligned pairs in that bank. The generic
  // register constraint chooses a word or pair according to the value type.
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return {0U, VT.getSizeInBits() == 64 ? &PI32V2::GPRPairRegClass
                                         : &PI32V2::GPRRegClass};
    case 'w':
      return {0U, &PI32V2::GPRLoRegClass};
    case 'W':
      return {0U, &PI32V2::GPRHiRegClass};
    case 'd':
      return {0U, &PI32V2::GPRPairLoRegClass};
    case 'D':
      return {0U, &PI32V2::GPRPairHiRegClass};
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

// ---- select -----------------------------------------------------------------

// The compare-and-branch of a condition, as the brcc patterns choose it.
unsigned PI32V2::branchForCond(unsigned Cond) {
  if (Cond & SelectFloat) {
    switch (ISD::CondCode(Cond & ~SelectFloat)) {
    case ISD::SETOEQ: case ISD::SETEQ: return PI32V2::BFEQ_RR;
    case ISD::SETUNE: case ISD::SETNE: return PI32V2::BFUNE_RR;
    case ISD::SETOLT: case ISD::SETLT: return PI32V2::BFLT_RR;
    case ISD::SETUGE: return PI32V2::BFUGE_RR;
    case ISD::SETOLE: case ISD::SETLE: return PI32V2::BFLE_RR;
    case ISD::SETUGT: return PI32V2::BFUGT_RR;
    case ISD::SETOGT: case ISD::SETGT: return PI32V2::BFGT_RR;
    case ISD::SETULE: return PI32V2::BFULE_RR;
    case ISD::SETOGE: case ISD::SETGE: return PI32V2::BFGE_RR;
    case ISD::SETULT: return PI32V2::BFULT_RR;
    default:
      report_fatal_error("PI32V2: unsupported float condition in a select");
    }
  }
  switch (ISD::CondCode(Cond)) {
  case ISD::SETEQ: return PI32V2::BEQ_RR;
  case ISD::SETNE: return PI32V2::BNE_RR;
  case ISD::SETUGE: return PI32V2::BGE_RR;
  case ISD::SETULT: return PI32V2::BLT_RR;
  case ISD::SETUGT: return PI32V2::BGT_RR;
  case ISD::SETULE: return PI32V2::BLE_RR;
  case ISD::SETGE: return PI32V2::BGES_RR;
  case ISD::SETLT: return PI32V2::BLTS_RR;
  case ISD::SETGT: return PI32V2::BGTS_RR;
  case ISD::SETLE: return PI32V2::BLES_RR;
  default:
    report_fatal_error("PI32V2: unsupported condition in a select");
  }
}

// The compare-with-constant form of an integer compare-two-registers branch.
unsigned PI32V2::branchWithImm(unsigned Br) {
  switch (Br) {
  case PI32V2::BEQ_RR: return PI32V2::BEQ_RI;
  case PI32V2::BNE_RR: return PI32V2::BNE_RI;
  case PI32V2::BGE_RR: return PI32V2::BGE_RI;
  case PI32V2::BLT_RR: return PI32V2::BLT_RI;
  case PI32V2::BGT_RR: return PI32V2::BGT_RI;
  case PI32V2::BLE_RR: return PI32V2::BLE_RI;
  case PI32V2::BGES_RR: return PI32V2::BGES_RI;
  case PI32V2::BLTS_RR: return PI32V2::BLTS_RI;
  case PI32V2::BGTS_RR: return PI32V2::BGTS_RI;
  case PI32V2::BLES_RR: return PI32V2::BLES_RI;
  default:
    report_fatal_error("PI32V2: no compare-with-constant branch");
  }
}


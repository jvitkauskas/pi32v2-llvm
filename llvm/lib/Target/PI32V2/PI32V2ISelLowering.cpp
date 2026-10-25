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
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2MachineFunctionInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-lower"

#define GET_CALLING_CONV_IMPL
#include "PI32V2GenCallingConv.inc"

PI32V2TargetLowering::PI32V2TargetLowering(const TargetMachine &TM,
                                           const PI32V2Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i32, &PI32V2::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(PI32V2::SP);
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMaxAtomicSizeInBitsSupported(0);

  for (unsigned Op : {ISD::GlobalAddress, ISD::ExternalSymbol,
                      ISD::BlockAddress, ISD::JumpTable, ISD::ConstantPool})
    setOperationAction(Op, MVT::i32, Custom);

  setOperationAction(ISD::BR_CC, MVT::i32, Legal);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::SETCC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);

  for (unsigned Op :
       {ISD::SREM, ISD::UREM, ISD::SDIVREM, ISD::UDIVREM, ISD::MULHS,
        ISD::MULHU, ISD::SMUL_LOHI, ISD::UMUL_LOHI, ISD::ROTL, ISD::ROTR,
        ISD::CTPOP, ISD::CTLZ, ISD::CTTZ, ISD::BSWAP, ISD::SHL_PARTS,
        ISD::SRA_PARTS, ISD::SRL_PARTS, ISD::DYNAMIC_STACKALLOC,
        ISD::STACKSAVE, ISD::STACKRESTORE})
    setOperationAction(Op, MVT::i32, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  for (unsigned Op : {ISD::VAARG, ISD::VACOPY, ISD::VAEND})
    setOperationAction(Op, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  for (MVT VT : {MVT::i1, MVT::i8, MVT::i16})
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
  for (unsigned Ext : {ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD})
    setLoadExtAction(Ext, MVT::i32, MVT::i1, Promote);
}

SDValue PI32V2TargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
  case ISD::ExternalSymbol:
  case ISD::BlockAddress:
  case ISD::JumpTable:
  case ISD::ConstantPool:
    return lowerAddress(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSelectCC(Op, DAG);
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
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Op))
    T = DAG.getTargetGlobalAddress(G->getGlobal(), DL, VT, G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Op))
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

SDValue PI32V2TargetLowering::lowerSelectCC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  return DAG.getNode(PI32V2ISD::SELECT_CC, DL, Op.getValueType(),
                     Op.getOperand(0), Op.getOperand(1), Op.getOperand(2),
                     Op.getOperand(3), DAG.getConstant(CC, DL, MVT::i32));
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

SDValue PI32V2TargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  MachineFunction &MF = DAG.getMachineFunction();
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CLI.IsTailCall = false;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_PI32V2);
  unsigned Bytes = alignTo(CCInfo.getStackSize(), 4);
  Chain = DAG.getCALLSEQ_START(Chain, Bytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 4> Regs;
  SmallVector<SDValue, 8> Stores;
  SDValue SP;
  for (unsigned I = 0; I < ArgLocs.size(); ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue V = CLI.OutVals[I];
    if (CLI.Outs[I].Flags.isByVal())
      report_fatal_error("PI32V2: byval arguments are not supported yet");
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

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i32,
                                        G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32);

  SmallVector<SDValue, 8> Ops = {Chain, Callee};
  for (auto &R : Regs)
    Ops.push_back(DAG.getRegister(R.first, R.second.getValueType()));
  Ops.push_back(DAG.getRegisterMask(
      Subtarget.getRegisterInfo()->getCallPreservedMask(MF, CLI.CallConv)));
  if (Glue)
    Ops.push_back(Glue);

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

// ---- select -----------------------------------------------------------------

// The compare-and-branch of a condition, as the brcc patterns choose it.
static unsigned branchFor(ISD::CondCode CC) {
  switch (CC) {
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

MachineBasicBlock *
PI32V2TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  assert(MI.getOpcode() == PI32V2::SELECT_CC && "unexpected instruction");
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction *MF = BB->getParent();
  MachineFunction::iterator It = ++BB->getIterator();

  //   BB:    if (a cc b) goto Sink      (the true value)
  //   False: fall through to Sink       (the false value)
  //   Sink:  dst = phi [t, BB], [f, False]
  MachineBasicBlock *False = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *Sink = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, False);
  MF->insert(It, Sink);
  Sink->splice(Sink->begin(), BB,
               std::next(MachineBasicBlock::iterator(MI)), BB->end());
  Sink->transferSuccessorsAndUpdatePHIs(BB);
  BB->addSuccessor(False);
  BB->addSuccessor(Sink);
  False->addSuccessor(Sink);

  ISD::CondCode CC = ISD::CondCode(MI.getOperand(5).getImm());
  BuildMI(BB, DL, TII.get(branchFor(CC)))
      .addReg(MI.getOperand(1).getReg())
      .addReg(MI.getOperand(2).getReg())
      .addMBB(Sink);
  BuildMI(*Sink, Sink->begin(), DL, TII.get(PI32V2::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(3).getReg())
      .addMBB(BB)
      .addReg(MI.getOperand(4).getReg())
      .addMBB(False);
  MI.eraseFromParent();
  return Sink;
}

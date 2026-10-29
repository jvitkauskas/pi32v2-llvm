//===-- PI32V2AsmPrinter.cpp - PI32V2 assembly and object output ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Machine instructions become MC instructions: the pseudos of calls and
// returns their real forms (a call starts as call.s; the assembler backend
// relaxes it), symbols and blocks expressions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2InstPrinter.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2TargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "PI32V2Subtarget.h"
#include "PI32V2InstrInfo.h"
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

static cl::opt<bool> NoCompress(
    "pi32v2-no-compress", cl::Hidden,
    cl::desc("PI32V2: keep the general forms instead of the shorter ones"));

namespace {
class PI32V2AsmPrinter : public AsmPrinter {
public:
  static char ID;
  PI32V2AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "PI32V2 Assembly Printer"; }
  void emitInstruction(const MachineInstr *MI) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;

private:
  // The instructions still to come in the open if block's parts.
  unsigned IfThen = 0, IfElse = 0;
  void emitOne(const MachineInstr *MI);
  void emitBRJT(const MachineInstr *MI);
  void emitJumpTableInfo() override {} // the tables are inline (emitBRJT)
  void emitSelect(const MachineInstr *MI);
  void closeIfBlock();
  MCOperand lowerSymbol(const MachineOperand &MO, MCSymbol *Sym) const;
  bool lowerOperand(const MachineOperand &MO, MCOperand &Out) const;
};
} // namespace

char PI32V2AsmPrinter::ID = 0;

INITIALIZE_PASS(PI32V2AsmPrinter, "pi32v2-asm-printer",
                "PI32V2 Assembly Printer", false, false)

MCOperand PI32V2AsmPrinter::lowerSymbol(const MachineOperand &MO,
                                        MCSymbol *Sym) const {
  MCContext &Ctx = OutContext;
  const MCExpr *E = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && !MO.isMBB() && MO.getOffset())
    E = MCBinaryExpr::createAdd(
        E, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(E);
}

bool PI32V2AsmPrinter::lowerOperand(const MachineOperand &MO,
                                    MCOperand &Out) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return false;
    Out = MCOperand::createReg(MO.getReg());
    return true;
  case MachineOperand::MO_Immediate:
    Out = MCOperand::createImm(MO.getImm());
    return true;
  case MachineOperand::MO_MachineBasicBlock:
    Out = lowerSymbol(MO, MO.getMBB()->getSymbol());
    return true;
  case MachineOperand::MO_GlobalAddress:
    Out = lowerSymbol(MO, getSymbol(MO.getGlobal()));
    return true;
  case MachineOperand::MO_ExternalSymbol:
    Out = lowerSymbol(MO, GetExternalSymbolSymbol(MO.getSymbolName()));
    return true;
  case MachineOperand::MO_BlockAddress:
    Out = lowerSymbol(MO, GetBlockAddressSymbol(MO.getBlockAddress()));
    return true;
  case MachineOperand::MO_JumpTableIndex:
    Out = lowerSymbol(MO, GetJTISymbol(MO.getIndex()));
    return true;
  case MachineOperand::MO_ConstantPoolIndex:
    Out = lowerSymbol(MO, GetCPISymbol(MO.getIndex()));
    return true;
  case MachineOperand::MO_RegisterMask:
    return false;
  default:
    report_fatal_error("PI32V2: unknown operand type");
  }
}

// An inline asm operand: a register by its name, a number, a symbol.
bool PI32V2AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                       const char *ExtraCode, raw_ostream &OS) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    OS << PI32V2InstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  default:
    return true;
  }
}

// An if block's closing lines in assembly text (an object needs none: the
// opener holds the counts): after the then part "} else {" or "}", after the
// else part "}".
void PI32V2AsmPrinter::closeIfBlock() {
  if (!IfThen && !IfElse)
    return;
  bool Text = OutStreamer->hasRawTextSupport();
  if (IfThen) {
    if (--IfThen)
      return;
    if (Text)
      OutStreamer->emitRawText(IfElse ? "\t} else {" : "\t}");
    return;
  }
  if (--IfElse == 0 && Text)
    OutStreamer->emitRawText("\t}");
}

void PI32V2AsmPrinter::emitInstruction(const MachineInstr *MI) {
  unsigned Opc = MI->getOpcode();
  if (Opc == PI32V2::SELECT_CC || Opc == PI32V2::SELECT_CC_RI ||
      Opc == PI32V2::SELECT_CC_RRI || Opc == PI32V2::SELECT_CC_RII ||
      Opc == PI32V2::SELECT_AND || Opc == PI32V2::SELECT_ANDI) {
    emitSelect(MI); // never inside another block
    return;
  }
  emitOne(MI);
  if (PI32V2::isIfOpener(MI->getOpcode())) {
    IfThen = MI->getOperand(2).getImm();
    IfElse = MI->getOperand(3).getImm();
    return;
  }
  closeIfBlock();
}

// dst = f; if (a cc b) { dst = t }: the destination is f's register.
void PI32V2AsmPrinter::emitSelect(const MachineInstr *MI) {
  unsigned Opc = MI->getOpcode();
  bool ImmB = Opc == PI32V2::SELECT_CC_RI || Opc == PI32V2::SELECT_CC_RII;
  bool ImmT = Opc == PI32V2::SELECT_CC_RRI || Opc == PI32V2::SELECT_CC_RII;
  // (a bit test: the mask as b, eq or ne)
  Register Dst = MI->getOperand(0).getReg();
  bool And = Opc == PI32V2::SELECT_AND || Opc == PI32V2::SELECT_ANDI;
  ImmT |= Opc == PI32V2::SELECT_ANDI;
  ImmB |= And;
  if (!ImmT && MI->getOperand(3).getReg() == Dst)
    return; // both values in one register
  unsigned Opener;
  if (And)
    Opener = MI->getOperand(5).getImm() == ISD::SETEQ ? PI32V2::IF_EQ_AND_RM
                                                      : PI32V2::IF_NE_AND_RM;
  else {
    unsigned Br = PI32V2::branchForCond(MI->getOperand(5).getImm());
    if (ImmB)
      Br = PI32V2::branchWithImm(Br);
    Opener = PI32V2::openerForBranch(Br);
  }
  MCInst Set;
  if (ImmT) {
    Set.setOpcode(PI32V2::LI_S16);
    Set.addOperand(MCOperand::createReg(Dst));
    Set.addOperand(MCOperand::createImm(MI->getOperand(3).getImm()));
  } else {
    Set.setOpcode(PI32V2::MOV_RR);
    Set.addOperand(MCOperand::createReg(Dst));
    Set.addOperand(MCOperand::createReg(MI->getOperand(3).getReg()));
  }
  if (!NoCompress)
    PI32V2::compressInst(Set, *MF->getSubtarget().getInstrInfo(),
                         *OutContext.getRegisterInfo());
  // A register of r0..r7 compared with zero skips the move by a 2-byte
  // branch (beqz, bnez); an opener takes 4 (as in JieLi's compiler).
  Register A = MI->getOperand(1).getReg();
  unsigned CC = And ? 0 : MI->getOperand(5).getImm();
  if (ImmB && !And && MI->getOperand(2).getImm() == 0 &&
      (CC == ISD::SETEQ || CC == ISD::SETNE) &&
      PI32V2::GPRLoRegClass.contains(A)) {
    MCSymbol *Skip = OutContext.createTempSymbol("sel");
    MCInst Br;
    Br.setOpcode(CC == ISD::SETEQ ? PI32V2::BNE_RI : PI32V2::BEQ_RI);
    Br.addOperand(MCOperand::createReg(A));
    Br.addOperand(MCOperand::createImm(0));
    Br.addOperand(
        MCOperand::createExpr(MCSymbolRefExpr::create(Skip, OutContext)));
    if (!NoCompress)
      PI32V2::compressInst(Br, *MF->getSubtarget().getInstrInfo(),
                           *OutContext.getRegisterInfo());
    EmitToStreamer(*OutStreamer, Br);
    EmitToStreamer(*OutStreamer, Set);
    OutStreamer->emitLabel(Skip);
    return;
  }
  MCInst If;
  If.setOpcode(Opener);
  If.addOperand(MCOperand::createReg(A));
  If.addOperand(ImmB ? MCOperand::createImm(MI->getOperand(2).getImm())
                     : MCOperand::createReg(MI->getOperand(2).getReg()));
  If.addOperand(MCOperand::createImm(1));
  If.addOperand(MCOperand::createImm(0));
  EmitToStreamer(*OutStreamer, If);
  IfThen = 1;
  IfElse = 0;
  EmitToStreamer(*OutStreamer, Set);
  closeIfBlock();
}

// A post-increment pseudo's form; its written-back base operand is not one of
// the form's (the form says [b++=n]).
static unsigned postForm(unsigned Opc, unsigned &Skip) {
  Skip = 1;
  switch (Opc) {
  case PI32V2::LDW_POST: return PI32V2::LD_W_PIN_L;
  case PI32V2::LDHU_POST: return PI32V2::LD_HU_PIN_L;
  case PI32V2::LDHS_POST: return PI32V2::LD_HS_PIN_L;
  case PI32V2::LDBU_POST: return PI32V2::LD_BU_PIN_L;
  case PI32V2::LDBS_POST: return PI32V2::LD_BS_PIN_L;
  }
  Skip = 0;
  switch (Opc) {
  case PI32V2::STW_POST: return PI32V2::ST_W_PIN_L;
  case PI32V2::STH_POST: return PI32V2::ST_H_PIN_L;
  case PI32V2::STB_POST: return PI32V2::ST_B_PIN_L;
  }
  return 0;
}

// A switch. The table follows the instruction, so the entries are the forward
// distances of the cases: tbb (bytes) or tbh (halfwords, the index doubled),
// target = the table's start + 2 * entry, as JieLi's compiler makes them. A
// case placed before the switch, or one too far for halfwords: the addresses
// themselves, loaded from an inline table.
void PI32V2AsmPrinter::emitBRJT(const MachineInstr *MI) {
  Register Idx = MI->getOperand(2).getReg(), Tmp = MI->getOperand(1).getReg();
  const std::vector<MachineBasicBlock *> &Cases =
      MF->getJumpTableInfo()->getJumpTables()[MI->getOperand(3).getIndex()].MBBs;
  const PI32V2InstrInfo &TII = *MF->getSubtarget<PI32V2Subtarget>().getInstrInfo();
  // the layout from here: the most bytes up to each block (a branch may grow)
  DenseMap<const MachineBasicBlock *, uint64_t> At;
  bool Seen = false;
  uint64_t Pos = 0;
  for (const MachineBasicBlock &B : *MF) {
    if (&B == MI->getParent()) {
      Seen = true;
      continue;
    }
    if (!Seen)
      continue;
    At[&B] = Pos;
    for (const MachineInstr &I : B)
      Pos += I.isBranch() && !I.isIndirectBranch() ? 6 : TII.getInstSizeInBytes(I);
    Pos += 1; // alignment
  }
  uint64_t Far = 0;
  bool Forward = true;
  for (const MachineBasicBlock *C : Cases) {
    auto It = At.find(C);
    if (It == At.end()) {
      Forward = false;
      break;
    }
    Far = std::max(Far, It->second);
  }
  auto Emit = [&](unsigned Opc, std::initializer_list<MCOperand> Ops) {
    MCInst I;
    I.setOpcode(Opc);
    for (const MCOperand &O : Ops)
      I.addOperand(O);
    if (!NoCompress)
      PI32V2::compressInst(I, *MF->getSubtarget().getInstrInfo(),
                           *OutContext.getRegisterInfo());
    EmitToStreamer(*OutStreamer, I);
  };
  MCOperand R = MCOperand::createReg(Idx);
  uint64_t N = Cases.size();
  auto Entries = [&](unsigned Size) {
    MCSymbol *Start = OutContext.createTempSymbol("jts");
    OutStreamer->emitLabel(Start);
    for (const MachineBasicBlock *C : Cases)
      OutStreamer->emitValue(
          MCBinaryExpr::createDiv(
              MCBinaryExpr::createSub(MCSymbolRefExpr::create(C->getSymbol(), OutContext),
                                      MCSymbolRefExpr::create(Start, OutContext),
                                      OutContext),
              MCConstantExpr::create(2, OutContext), OutContext),
          Size);
    OutStreamer->emitValueToAlignment(Align(2));
  };
  if (Forward && N + 1 + Far <= 2 * 255) {
    Emit(PI32V2::TBB, {R});
    Entries(1);
    return;
  }
  if (Forward && 2 * N + 2 + Far <= 2 * 65535) {
    Emit(PI32V2::LSL_RRI_L, {R, R, MCOperand::createImm(1)});
    Emit(PI32V2::TBH, {R});
    Entries(2);
    return;
  }
  MCSymbol *Table = OutContext.createTempSymbol("jta");
  Emit(PI32V2::LI_U32, {MCOperand::createReg(Tmp),
                        MCOperand::createExpr(MCSymbolRefExpr::create(Table, OutContext))});
  Emit(PI32V2::LD_W_IDXS, {R, MCOperand::createReg(Tmp), R});
  Emit(PI32V2::GOTO_R, {R});
  OutStreamer->emitValueToAlignment(Align(4));
  OutStreamer->emitLabel(Table);
  for (const MachineBasicBlock *C : Cases)
    OutStreamer->emitValue(MCSymbolRefExpr::create(C->getSymbol(), OutContext), 4);
}

void PI32V2AsmPrinter::emitOne(const MachineInstr *MI) {
  MCInst Inst;
  unsigned Opc = MI->getOpcode();
  unsigned Skip;
  if (unsigned Form = postForm(Opc, Skip)) {
    Inst.setOpcode(Form);
    for (unsigned K = 0; K < MI->getNumOperands(); ++K) {
      MCOperand Op;
      if (K != Skip && lowerOperand(MI->getOperand(K), Op))
        Inst.addOperand(Op);
    }
    if (!NoCompress)
      PI32V2::compressInst(Inst, *MF->getSubtarget().getInstrInfo(),
                           *OutContext.getRegisterInfo());
    EmitToStreamer(*OutStreamer, Inst);
    return;
  }
  switch (Opc) {
  case PI32V2::PseudoBRJT:
    emitBRJT(MI);
    return;
  case PI32V2::SELECT_AND:
  case PI32V2::SELECT_ANDI:
  case PI32V2::SELECT_CC:
  case PI32V2::SELECT_CC_RI:
  case PI32V2::SELECT_CC_RRI:
  case PI32V2::SELECT_CC_RII:
    emitSelect(MI);
    return;
  case PI32V2::PseudoCALL:
    Opc = PI32V2::CALL_S;
    break;
  case PI32V2::PseudoCALLR:
    Opc = PI32V2::CALL_R;
    break;
  case PI32V2::PseudoTAIL:
    Opc = PI32V2::GOTO_S;
    break;
  case PI32V2::PseudoRET:
    // an interrupt handler returns to reti
    Inst.setOpcode(MF->getFunction().hasFnAttribute("interrupt") ? PI32V2::RTI
                                                                  : PI32V2::RTS);
    EmitToStreamer(*OutStreamer, Inst);
    return;
  default:
    break;
  }
  Inst.setOpcode(Opc);
  bool Call = MI->getOpcode() == PI32V2::PseudoCALL ||
              MI->getOpcode() == PI32V2::PseudoCALLR ||
              MI->getOpcode() == PI32V2::PseudoTAIL;
  for (const MachineOperand &MO : MI->operands()) {
    MCOperand Op;
    if (!lowerOperand(MO, Op))
      continue;
    Inst.addOperand(Op);
    if (Call) // the target only; the rest are the argument registers
      break;
  }
  if (!NoCompress)
    PI32V2::compressInst(Inst, *MF->getSubtarget().getInstrInfo(),
                         *OutContext.getRegisterInfo());
  // In an if block a goto is the 32-bit form, as JieLi's compiler emits it
  // (it never puts the 16-bit one there, even for a near target).
  if ((IfThen || IfElse) && Inst.getOpcode() == PI32V2::GOTO_S)
    Inst.setOpcode(PI32V2::GOTO_L);
  EmitToStreamer(*OutStreamer, Inst);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePI32V2AsmPrinter() {
  RegisterAsmPrinter<PI32V2AsmPrinter> X(getThePI32V2Target());
}

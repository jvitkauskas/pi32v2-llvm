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
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
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

void PI32V2AsmPrinter::emitInstruction(const MachineInstr *MI) {
  MCInst Inst;
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
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
  EmitToStreamer(*OutStreamer, Inst);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePI32V2AsmPrinter() {
  RegisterAsmPrinter<PI32V2AsmPrinter> X(getThePI32V2Target());
}

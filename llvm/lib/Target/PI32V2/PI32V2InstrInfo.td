//===-- PI32V2InstrInfo.td - PI32V2 instructions -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Every form of the pi32v2-isa instruction table, generated.
include "PI32V2InstrGen.td"

//===----------------------------------------------------------------------===//
// Selection DAG nodes
//===----------------------------------------------------------------------===//

def SDT_PI32V2Call : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def SDT_PI32V2CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_PI32V2CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_PI32V2Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def SDT_PI32V2SelectCC : SDTypeProfile<1, 5, [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>,
                                              SDTCisSameAs<3, 4>, SDTCisVT<5, i32>]>;

def PI32V2call : SDNode<"PI32V2ISD::CALL", SDT_PI32V2Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def PI32V2ret : SDNode<"PI32V2ISD::RET", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def PI32V2wrapper : SDNode<"PI32V2ISD::WRAPPER", SDT_PI32V2Wrapper>;
def PI32V2selectcc : SDNode<"PI32V2ISD::SELECT_CC", SDT_PI32V2SelectCC>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_PI32V2CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_PI32V2CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Pseudo instructions: the code generator's, never in an object file
//===----------------------------------------------------------------------===//

class PI32V2Pseudo<dag outs, dag ins, list<dag> pattern = []>
    : PI32V2InstBase<0, outs, ins, ""> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let Pattern = pattern;
  let hasSideEffects = 0;
}

def calltarget : Operand<i32>;
def i32imm_op : Operand<i32>;

let Defs = [SP], Uses = [SP], hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : PI32V2Pseudo<(outs), (ins i32imm_op:$amt, i32imm_op:$amt2),
                                    [(callseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : PI32V2Pseudo<(outs), (ins i32imm_op:$amt1, i32imm_op:$amt2),
                                  [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// call: CALL_S (relaxed to CALL_L by the assembler backend), call.r.
let isCall = 1, Defs = [RETS], Uses = [SP], hasSideEffects = 1 in {
def PseudoCALL : PI32V2Pseudo<(outs), (ins calltarget:$dst, variable_ops)>;
def PseudoCALLR : PI32V2Pseudo<(outs), (ins GPR:$dst, variable_ops),
                               [(PI32V2call GPR:$dst)]>;
}
def : Pat<(PI32V2call tglobaladdr:$dst), (PseudoCALL tglobaladdr:$dst)>;
def : Pat<(PI32V2call texternalsym:$dst), (PseudoCALL texternalsym:$dst)>;

// return: rts, or the epilogue's {pc, ...} = [sp++].
let isReturn = 1, isTerminator = 1, isBarrier = 1, Uses = [RETS] in
def PseudoRET : PI32V2Pseudo<(outs), (ins variable_ops), [(PI32V2ret)]>;

// Stack slots: sp is not a general register, so a frame index becomes the
// [sp+n] form of the access (or an address computed from sp) when the frame
// is laid out (PI32V2RegisterInfo::eliminateFrameIndex).
let mayLoad = 1 in {
def LDW_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;
def LDBU_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;
def LDBS_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;
def LDHU_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;
def LDHS_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;
}
let mayStore = 1 in {
def STW_FI : PI32V2Pseudo<(outs), (ins GPR:$r, i32imm_op:$fi, i32imm_op:$off)>;
def STB_FI : PI32V2Pseudo<(outs), (ins GPR:$r, i32imm_op:$fi, i32imm_op:$off)>;
def STH_FI : PI32V2Pseudo<(outs), (ins GPR:$r, i32imm_op:$fi, i32imm_op:$off)>;
}
def ADDR_FI : PI32V2Pseudo<(outs GPR:$r), (ins i32imm_op:$fi, i32imm_op:$off)>;

// select: a branch diamond (PI32V2TargetLowering::EmitInstrWithCustomInserter).
let usesCustomInserter = 1 in
def SELECT_CC : PI32V2Pseudo<(outs GPR:$dst),
                             (ins GPR:$a, GPR:$b, GPR:$t, GPR:$f, i32imm_op:$cc),
                             [(set GPR:$dst, (PI32V2selectcc GPR:$a, GPR:$b,
                                                             GPR:$t, GPR:$f, imm:$cc))]>;

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

def simm14 : ImmLeaf<i32, [{ return isInt<14>(Imm); }]>;
def simm16 : ImmLeaf<i32, [{ return isInt<16>(Imm); }]>;
def shamt : ImmLeaf<i32, [{ return Imm > 0 && Imm < 32; }]>;
def modimm : ImmLeaf<i32, [{ return PI32V2::modImmRawFor(uint32_t(Imm)).has_value(); }]>;

// Constants and addresses.
def : Pat<(i32 simm16:$v), (LI_S16 imm:$v)>;
def : Pat<(i32 imm:$v), (LI_U32 imm:$v)>;
def : Pat<(PI32V2wrapper tglobaladdr:$a), (LI_U32 tglobaladdr:$a)>;
def : Pat<(PI32V2wrapper texternalsym:$a), (LI_U32 texternalsym:$a)>;
def : Pat<(PI32V2wrapper tblockaddress:$a), (LI_U32 tblockaddress:$a)>;
def : Pat<(PI32V2wrapper tjumptable:$a), (LI_U32 tjumptable:$a)>;
def : Pat<(PI32V2wrapper tconstpool:$a), (LI_U32 tconstpool:$a)>;

// Arithmetic and logic.
def : Pat<(add GPR:$a, GPR:$b), (ADD_RRR_L GPR:$a, GPR:$b)>;
def : Pat<(add GPR:$a, simm14:$n), (ADD_S14_RRI GPR:$a, imm:$n)>;
def : Pat<(sub GPR:$a, GPR:$b), (SUB_RRR_L GPR:$a, GPR:$b)>;
def : Pat<(and GPR:$a, GPR:$b), (AND_RRR GPR:$a, GPR:$b)>;
def : Pat<(and GPR:$a, modimm:$n), (AND_MODIMM_RRI GPR:$a, imm:$n)>;
def : Pat<(or GPR:$a, GPR:$b), (OR_RRR GPR:$a, GPR:$b)>;
def : Pat<(or GPR:$a, modimm:$n), (OR_MODIMM_RRI GPR:$a, imm:$n)>;
def : Pat<(xor GPR:$a, GPR:$b), (XOR_RRR GPR:$a, GPR:$b)>;
def : Pat<(xor GPR:$a, modimm:$n), (XOR_MODIMM_RRI GPR:$a, imm:$n)>;
def : Pat<(shl GPR:$a, GPR:$b), (LSL_RRR GPR:$a, GPR:$b)>;
def : Pat<(srl GPR:$a, GPR:$b), (LSR_RRR GPR:$a, GPR:$b)>;
def : Pat<(sra GPR:$a, GPR:$b), (ASR_RRR GPR:$a, GPR:$b)>;
def : Pat<(shl GPR:$a, shamt:$n), (LSL_RRI_L GPR:$a, imm:$n)>;
def : Pat<(srl GPR:$a, shamt:$n), (LSR_RRI_L GPR:$a, imm:$n)>;
def : Pat<(sra GPR:$a, shamt:$n), (ASR_RRI_L GPR:$a, imm:$n)>;
def : Pat<(mul GPR:$a, GPR:$b), (MUL_RRR GPR:$a, GPR:$b)>;
def : Pat<(sdiv GPR:$a, GPR:$b), (DIV_S_RRR GPR:$a, GPR:$b)>;
def : Pat<(udiv GPR:$a, GPR:$b), (DIV_U_RRR GPR:$a, GPR:$b)>;

// Memory: base register plus offset (PI32V2DAGToDAGISel::selectAddr*), or a
// stack slot.
def AddrW : ComplexPattern<iPTR, 2, "selectAddrW", [], []>;
def AddrH : ComplexPattern<iPTR, 2, "selectAddrH", [], []>;
def AddrB : ComplexPattern<iPTR, 2, "selectAddrB", [], []>;
def AddrFI : ComplexPattern<iPTR, 2, "selectAddrFI", [frameindex], []>;

def : Pat<(i32 (load (AddrFI i32:$fi, i32:$off))), (LDW_FI $fi, $off)>;
def : Pat<(i32 (zextloadi8 (AddrFI i32:$fi, i32:$off))), (LDBU_FI $fi, $off)>;
def : Pat<(i32 (extloadi8 (AddrFI i32:$fi, i32:$off))), (LDBU_FI $fi, $off)>;
def : Pat<(i32 (sextloadi8 (AddrFI i32:$fi, i32:$off))), (LDBS_FI $fi, $off)>;
def : Pat<(i32 (zextloadi16 (AddrFI i32:$fi, i32:$off))), (LDHU_FI $fi, $off)>;
def : Pat<(i32 (extloadi16 (AddrFI i32:$fi, i32:$off))), (LDHU_FI $fi, $off)>;
def : Pat<(i32 (sextloadi16 (AddrFI i32:$fi, i32:$off))), (LDHS_FI $fi, $off)>;
def : Pat<(store GPR:$v, (AddrFI i32:$fi, i32:$off)), (STW_FI GPR:$v, $fi, $off)>;
def : Pat<(truncstorei8 GPR:$v, (AddrFI i32:$fi, i32:$off)), (STB_FI GPR:$v, $fi, $off)>;
def : Pat<(truncstorei16 GPR:$v, (AddrFI i32:$fi, i32:$off)), (STH_FI GPR:$v, $fi, $off)>;

def : Pat<(i32 (load (AddrW GPR:$b, i32:$n))), (LD_W_L GPR:$b, $n)>;
def : Pat<(i32 (zextloadi8 (AddrB GPR:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (extloadi8 (AddrB GPR:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (sextloadi8 (AddrB GPR:$b, i32:$n))), (LD_BS_L GPR:$b, $n)>;
def : Pat<(i32 (zextloadi16 (AddrH GPR:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (extloadi16 (AddrH GPR:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (sextloadi16 (AddrH GPR:$b, i32:$n))), (LD_HS_L GPR:$b, $n)>;
// The store forms take the base first and the value last.
def : Pat<(store GPR:$v, (AddrW GPR:$b, i32:$n)), (ST_W_L GPR:$b, $n, GPR:$v)>;
def : Pat<(truncstorei8 GPR:$v, (AddrB GPR:$b, i32:$n)), (ST_B_L GPR:$b, $n, GPR:$v)>;
def : Pat<(truncstorei16 GPR:$v, (AddrH GPR:$b, i32:$n)), (ST_H_L GPR:$b, $n, GPR:$v)>;

// Branches: compare two registers and branch; `if` compares unsigned, `ifs`
// signed (pi32v2-isa docs/ISA.md). The assembler backend relaxes the short
// forms.
multiclass BrCC<CondCode cc, Instruction I> {
  def : Pat<(brcc cc, GPR:$a, GPR:$b, bb:$t), (I GPR:$a, GPR:$b, bb:$t)>;
}
defm : BrCC<SETEQ, BEQ_RR>;
defm : BrCC<SETNE, BNE_RR>;
defm : BrCC<SETUGE, BGE_RR>;
defm : BrCC<SETULT, BLT_RR>;
defm : BrCC<SETUGT, BGT_RR>;
defm : BrCC<SETULE, BLE_RR>;
defm : BrCC<SETGE, BGES_RR>;
defm : BrCC<SETLT, BLTS_RR>;
defm : BrCC<SETGT, BGTS_RR>;
defm : BrCC<SETLE, BLES_RR>;
def : Pat<(br bb:$t), (GOTO_S bb:$t)>;
def : Pat<(brind GPR:$a), (GOTO_R GPR:$a)>;

// Atomic loads and stores of up to a word are single accesses; a fence is csync.
def : Pat<(i32 (atomic_load_32 (AddrW GPR:$b, i32:$n))), (LD_W_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_zext_16 (AddrH GPR:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_zext_8 (AddrB GPR:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_32 (AddrFI i32:$fi, i32:$off))), (LDW_FI $fi, $off)>;
def : Pat<(atomic_store_32 GPR:$v, (AddrW GPR:$b, i32:$n)), (ST_W_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_16 GPR:$v, (AddrH GPR:$b, i32:$n)), (ST_H_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_8 GPR:$v, (AddrB GPR:$b, i32:$n)), (ST_B_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_32 GPR:$v, (AddrFI i32:$fi, i32:$off)), (STW_FI GPR:$v, $fi, $off)>;
def : Pat<(atomic_fence timm, timm), (CSYNC)>;

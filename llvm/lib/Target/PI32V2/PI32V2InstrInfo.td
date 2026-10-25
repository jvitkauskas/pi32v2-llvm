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
// dst = (a cc b) ? t : f: dst, t and f of one type, a and b of one type.
def SDT_PI32V2SelectCC : SDTypeProfile<1, 5, [SDTCisSameAs<0, 3>, SDTCisSameAs<1, 2>,
                                              SDTCisSameAs<3, 4>, SDTCisVT<5, i32>]>;

def PI32V2call : SDNode<"PI32V2ISD::CALL", SDT_PI32V2Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def PI32V2tail : SDNode<"PI32V2ISD::TAIL", SDT_PI32V2Call,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
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
                               [(PI32V2call i32:$dst)]>;
}
def : Pat<(PI32V2call tglobaladdr:$dst), (PseudoCALL tglobaladdr:$dst)>;
def : Pat<(PI32V2call texternalsym:$dst), (PseudoCALL texternalsym:$dst)>;

// A tail call: goto (relaxed to the long form by the assembler backend), after
// the epilogue (whose pop restores rets instead of returning).
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP],
    hasSideEffects = 1 in
def PseudoTAIL : PI32V2Pseudo<(outs), (ins calltarget:$dst, variable_ops)>;
def : Pat<(PI32V2tail tglobaladdr:$dst), (PseudoTAIL tglobaladdr:$dst)>;
def : Pat<(PI32V2tail texternalsym:$dst), (PseudoTAIL texternalsym:$dst)>;

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
                             (ins GPR:$a, GPR:$b, GPR:$t, GPR:$f, i32imm_op:$cc)>;
// Integer or float values, compared as integers or floats.
foreach V = [i32, f32] in
foreach C = [i32, f32] in
def : Pat<(V (PI32V2selectcc (C GPR:$a), (C GPR:$b), (V GPR:$t), (V GPR:$f),
                             imm:$cc)),
          (SELECT_CC GPR:$a, GPR:$b, GPR:$t, GPR:$f, imm:$cc)>;

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
def : Pat<(mul GPR:$a, modimm:$n), (MUL_MODIMM_RRI GPR:$a, imm:$n)>;
def : Pat<(ctlz GPR:$a), (CLZ_RR GPR:$a)>;
def : Pat<(bswap GPR:$a), (REV8_RR GPR:$a)>;
def : Pat<(rotr GPR:$a, GPR:$n), (ROR_RRR GPR:$a, GPR:$n)>;
def : Pat<(rotr GPR:$a, shamt:$n), (ROR_RRI GPR:$a, imm:$n)>;

// Bit fields: sextra/uextra(x, p:position, l:length), length 1..31. A sign
// or zero extension of a byte or halfword is one too (the assembler backend
// puts it in the 16-bit .b0/.l forms when the registers allow).
def : Pat<(sext_inreg GPR:$a, i8), (SEXTRA_RRII GPR:$a, 0, 8)>;
def : Pat<(sext_inreg GPR:$a, i16), (SEXTRA_RRII GPR:$a, 0, 16)>;
def : Pat<(sext_inreg GPR:$a, i1), (SEXTRA_RRII GPR:$a, 0, 1)>;
def lowmask : ImmLeaf<i32, [{ return isMask_32(Imm) && Imm != -1; }]>;
def masklen : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(llvm::popcount(uint32_t(N->getZExtValue())),
                                   SDLoc(N), MVT::i32);
}]>;
def lowmask_nomod : ImmLeaf<i32, [{
  return isMask_32(Imm) && Imm != -1 &&
         !PI32V2::modImmRawFor(uint32_t(Imm)).has_value();
}]>;
// (x >> p) & (2^l - 1), the field inside the word
def fieldfits : PatFrag<(ops node:$x, node:$p, node:$m),
                        (and (srl node:$x, node:$p), node:$m), [{
  uint64_t P = cast<ConstantSDNode>(N->getOperand(0)->getOperand(1))->getZExtValue();
  uint64_t M = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  return P + llvm::popcount(M) <= 32;
}]>;
def : Pat<(fieldfits GPR:$a, shamt:$p, lowmask:$m),
          (UEXTRA_RRII GPR:$a, imm:$p, (masklen imm:$m))>;
def : Pat<(and GPR:$a, 255), (UEXTRA_RRII GPR:$a, 0, 8)>;
def : Pat<(and GPR:$a, 65535), (UEXTRA_RRII GPR:$a, 0, 16)>;
def : Pat<(and GPR:$a, lowmask_nomod:$m), (UEXTRA_RRII GPR:$a, 0, (masklen imm:$m))>;
// (x << a) >> b with b >= a: the bits a..31-a... of x, at p = b - a, l = 32 - b
def shlsr_p : SDNodeXForm<srl, [{
  uint64_t A = cast<ConstantSDNode>(N->getOperand(0)->getOperand(1))->getZExtValue();
  uint64_t B = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  return CurDAG->getTargetConstant(B - A, SDLoc(N), MVT::i32);
}]>;
def shlsr_l : SDNodeXForm<srl, [{
  uint64_t B = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  return CurDAG->getTargetConstant(32 - B, SDLoc(N), MVT::i32);
}]>;
def shlsr : PatFrag<(ops node:$x, node:$a, node:$b), (srl (shl node:$x, node:$a), node:$b), [{
  uint64_t A = cast<ConstantSDNode>(N->getOperand(0)->getOperand(1))->getZExtValue();
  uint64_t B = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  return B >= A;
}]>;
def shlsra : PatFrag<(ops node:$x, node:$a, node:$b), (sra (shl node:$x, node:$a), node:$b), [{
  uint64_t A = cast<ConstantSDNode>(N->getOperand(0)->getOperand(1))->getZExtValue();
  uint64_t B = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  return B >= A;
}]>;
def : Pat<(shlsr:$n GPR:$x, shamt, shamt),
          (UEXTRA_RRII GPR:$x, (shlsr_p $n), (shlsr_l $n))>;
def : Pat<(shlsra:$n GPR:$x, shamt, shamt),
          (SEXTRA_RRII GPR:$x, (shlsr_p $n), (shlsr_l $n))>;

// The carry chain of a 64-bit add or subtract: psr's C, which every add and
// subtract writes (C = no borrow for a subtract, subc subtracts !C).
def : Pat<(addc i32:$a, i32:$b), (ADD_RRR_L GPR:$a, GPR:$b)>;
def : Pat<(adde i32:$a, i32:$b), (ADDC_RRR GPR:$a, GPR:$b)>;
def : Pat<(subc i32:$a, i32:$b), (SUB_RRR_L GPR:$a, GPR:$b)>;
def : Pat<(sube i32:$a, i32:$b), (SUBC_RRR GPR:$a, GPR:$b)>;
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
def : Pat<(store (i32 GPR:$v), (AddrFI i32:$fi, i32:$off)), (STW_FI GPR:$v, $fi, $off)>;
def : Pat<(truncstorei8 (i32 GPR:$v), (AddrFI i32:$fi, i32:$off)), (STB_FI GPR:$v, $fi, $off)>;
def : Pat<(truncstorei16 (i32 GPR:$v), (AddrFI i32:$fi, i32:$off)), (STH_FI GPR:$v, $fi, $off)>;

def : Pat<(i32 (load (AddrW i32:$b, i32:$n))), (LD_W_L GPR:$b, $n)>;
def : Pat<(i32 (zextloadi8 (AddrB i32:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (extloadi8 (AddrB i32:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (sextloadi8 (AddrB i32:$b, i32:$n))), (LD_BS_L GPR:$b, $n)>;
def : Pat<(i32 (zextloadi16 (AddrH i32:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (extloadi16 (AddrH i32:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (sextloadi16 (AddrH i32:$b, i32:$n))), (LD_HS_L GPR:$b, $n)>;
// The store forms take the base first and the value last.
def : Pat<(store (i32 GPR:$v), (AddrW i32:$b, i32:$n)), (ST_W_L GPR:$b, $n, GPR:$v)>;
def : Pat<(truncstorei8 (i32 GPR:$v), (AddrB i32:$b, i32:$n)), (ST_B_L GPR:$b, $n, GPR:$v)>;
def : Pat<(truncstorei16 (i32 GPR:$v), (AddrH i32:$b, i32:$n)), (ST_H_L GPR:$b, $n, GPR:$v)>;

// Branches: compare two registers and branch; `if` compares unsigned, `ifs`
// signed (pi32v2-isa docs/ISA.md). The assembler backend relaxes the short
// forms.
multiclass BrCC<CondCode cc, Instruction I> {
  def : Pat<(brcc cc, i32:$a, i32:$b, bb:$t), (I GPR:$a, GPR:$b, bb:$t)>;
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
// Compared with a constant: the forms with a 10-bit constant (signed for ==,
// != and ifs, unsigned for if), else those with 12 bits (the assembler
// backend relaxes either for the branch distance).
def simm10 : ImmLeaf<i32, [{ return isInt<10>(Imm); }]>;
def uimm10 : ImmLeaf<i32, [{ return isUInt<10>(Imm); }]>;
def simm12 : ImmLeaf<i32, [{ return isInt<12>(Imm); }]>;
def uimm12 : ImmLeaf<i32, [{ return isUInt<12>(Imm); }]>;
multiclass BrCCImm<CondCode cc, ImmLeaf Short, Instruction I, ImmLeaf Long,
                   Instruction IL> {
  def : Pat<(brcc cc, i32:$a, Short:$n, bb:$t), (I GPR:$a, imm:$n, bb:$t)>;
  def : Pat<(brcc cc, i32:$a, Long:$n, bb:$t), (IL GPR:$a, imm:$n, bb:$t)>;
}
defm : BrCCImm<SETEQ, simm10, BEQ_RI, simm12, BEQ_RI_L>;
defm : BrCCImm<SETNE, simm10, BNE_RI, simm12, BNE_RI_L>;
defm : BrCCImm<SETUGE, uimm10, BGE_RI, uimm12, BGE_RI_L>;
defm : BrCCImm<SETULT, uimm10, BLT_RI, uimm12, BLT_RI_L>;
defm : BrCCImm<SETUGT, uimm10, BGT_RI, uimm12, BGT_RI_L>;
defm : BrCCImm<SETULE, uimm10, BLE_RI, uimm12, BLE_RI_L>;
defm : BrCCImm<SETGE, simm10, BGES_RI, simm12, BGES_RI_L>;
defm : BrCCImm<SETLT, simm10, BLTS_RI, simm12, BLTS_RI_L>;
defm : BrCCImm<SETGT, simm10, BGTS_RI, simm12, BGTS_RI_L>;
defm : BrCCImm<SETLE, simm10, BLES_RI, simm12, BLES_RI_L>;
def : Pat<(br bb:$t), (GOTO_S bb:$t)>;
def : Pat<(brind i32:$a), (GOTO_R GPR:$a)>;

// Atomic loads and stores of up to a word are single accesses; a fence is csync.
def : Pat<(i32 (atomic_load_32 (AddrW i32:$b, i32:$n))), (LD_W_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_zext_16 (AddrH i32:$b, i32:$n))), (LD_HU_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_zext_8 (AddrB i32:$b, i32:$n))), (LD_BU_L GPR:$b, $n)>;
def : Pat<(i32 (atomic_load_32 (AddrFI i32:$fi, i32:$off))), (LDW_FI $fi, $off)>;
def : Pat<(atomic_store_32 (i32 GPR:$v), (AddrW i32:$b, i32:$n)), (ST_W_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_16 (i32 GPR:$v), (AddrH i32:$b, i32:$n)), (ST_H_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_8 (i32 GPR:$v), (AddrB i32:$b, i32:$n)), (ST_B_L GPR:$b, $n, GPR:$v)>;
def : Pat<(atomic_store_32 (i32 GPR:$v), (AddrFI i32:$fi, i32:$off)), (STW_FI GPR:$v, $fi, $off)>;
def : Pat<(atomic_fence timm, timm), (CSYNC)>;

// Single-precision float (FeatureFPRev1: -mcpu=r3 and ac791n), as JieLi's
// compiler does it: the values in general registers, + - * / and the
// conversions in the (f) forms, compares by iff branches; negation and abs are
// integer operations on the sign bit, doubles and the rest library calls.
def fpimm_bits : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(
      N->getValueAPF().bitcastToAPInt().getSExtValue(), SDLoc(N), MVT::i32);
}]>;
def fpimm_s16 : PatLeaf<(fpimm), [{
  return isInt<16>(N->getValueAPF().bitcastToAPInt().getSExtValue());
}]>;
def : Pat<(f32 fpimm_s16:$v), (LI_S16 (fpimm_bits $v))>;
def : Pat<(f32 fpimm:$v), (LI_U32 (fpimm_bits $v))>;
def : Pat<(i32 (bitconvert (f32 GPR:$a))), (COPY_TO_REGCLASS GPR:$a, GPR)>;
def : Pat<(f32 (bitconvert (i32 GPR:$a))), (COPY_TO_REGCLASS GPR:$a, GPR)>;
def : Pat<(f32 (load (AddrW i32:$b, i32:$n))), (LD_W_L GPR:$b, $n)>;
def : Pat<(f32 (load (AddrFI i32:$fi, i32:$off))), (LDW_FI $fi, $off)>;
def : Pat<(store (f32 GPR:$v), (AddrW i32:$b, i32:$n)), (ST_W_L GPR:$b, $n, GPR:$v)>;
def : Pat<(store (f32 GPR:$v), (AddrFI i32:$fi, i32:$off)), (STW_FI GPR:$v, $fi, $off)>;

let Predicates = [HasFPRev1] in {
def : Pat<(fadd GPR:$a, GPR:$b), (ADD_F_RRR GPR:$a, GPR:$b)>;
def : Pat<(fsub GPR:$a, GPR:$b), (SUB_F_RRR GPR:$a, GPR:$b)>;
def : Pat<(fmul GPR:$a, GPR:$b), (MUL_F_RRR GPR:$a, GPR:$b)>;
def : Pat<(fdiv GPR:$a, GPR:$b), (DIV_F_RRR GPR:$a, GPR:$b)>;
def : Pat<(i32 (fp_to_sint (f32 GPR:$a))), (FTOI_TRUNC_RR GPR:$a)>;
def : Pat<(i32 (fp_to_uint (f32 GPR:$a))), (FTOU_TRUNC_RR GPR:$a)>;
def : Pat<(f32 (sint_to_fp (i32 GPR:$a))), (ITOF_RR GPR:$a)>;
def : Pat<(f32 (uint_to_fp (i32 GPR:$a))), (UTOF_RR GPR:$a)>;

// iff: ordered ==, < , <=, >, >= and their unordered inverses. A condition
// without o/u (fast math) takes either.
multiclass BrFCC<CondCode cc, Instruction I> {
  def : Pat<(brcc cc, (f32 GPR:$a), (f32 GPR:$b), bb:$t),
            (I GPR:$a, GPR:$b, bb:$t)>;
}
defm : BrFCC<SETOEQ, BFEQ_RR>;
defm : BrFCC<SETEQ, BFEQ_RR>;
defm : BrFCC<SETUNE, BFUNE_RR>;
defm : BrFCC<SETNE, BFUNE_RR>;
defm : BrFCC<SETOLT, BFLT_RR>;
defm : BrFCC<SETLT, BFLT_RR>;
defm : BrFCC<SETUGE, BFUGE_RR>;
defm : BrFCC<SETOLE, BFLE_RR>;
defm : BrFCC<SETLE, BFLE_RR>;
defm : BrFCC<SETUGT, BFUGT_RR>;
defm : BrFCC<SETOGT, BFGT_RR>;
defm : BrFCC<SETGT, BFGT_RR>;
defm : BrFCC<SETULE, BFULE_RR>;
defm : BrFCC<SETOGE, BFGE_RR>;
defm : BrFCC<SETGE, BFGE_RR>;
defm : BrFCC<SETULT, BFULT_RR>;
}

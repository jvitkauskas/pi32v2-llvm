//===-- PI32V2RegisterInfo.td - PI32V2 registers -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class PI32V2Reg<bits<4> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding{3-0} = Enc;
  let AltNames = alt;
  let Namespace = "PI32V2";
}

class PI32V2RegWithSubRegs<bits<4> Enc, string n, list<Register> subregs>
    : RegisterWithSubRegs<n, subregs> {
  let HWEncoding{3-0} = Enc;
  let Namespace = "PI32V2";
}

let Namespace = "PI32V2" in {
def sub_lo : SubRegIndex<32>;
def sub_hi : SubRegIndex<32, 32>;
}

// General registers r0..r15.
foreach I = 0-15 in
  def R#I : PI32V2Reg<I, "r"#I>, DwarfRegNum<[I]>;

// 64-bit pairs r(2n+1)_r(2n), encoded as n.
let SubRegIndices = [sub_lo, sub_hi], CoveredBySubRegs = 1 in
foreach I = 0-7 in
  def R#!add(!mul(I, 2), 1)#_R#!mul(I, 2)
      : PI32V2RegWithSubRegs<I, "r"#!add(!mul(I, 2), 1)#"_r"#!mul(I, 2),
                             [!cast<Register>("R"#!mul(I, 2)),
                              !cast<Register>("R"#!add(!mul(I, 2), 1))]>;

// Special registers, by number (pi32v2-isa docs/ISA.md section 2).
def RETI  : PI32V2Reg<0, "reti">;
def RETE  : PI32V2Reg<1, "rete">;
def RETX  : PI32V2Reg<2, "retx">;
def RETS  : PI32V2Reg<3, "rets">, DwarfRegNum<[17]>; // docs/ABI.md section 6.2
def SR4   : PI32V2Reg<4, "sr4">;
def PSR   : PI32V2Reg<5, "psr">;
def CNUM  : PI32V2Reg<6, "cnum">;
def USPN  : PI32V2Reg<7, "uspn">;
def SSPN  : PI32V2Reg<8, "sspn">;
def ICFGN : PI32V2Reg<9, "icfgn">;
def ICFGS : PI32V2Reg<10, "icfgs">;
def ICFG  : PI32V2Reg<11, "icfg">;
def USP   : PI32V2Reg<12, "usp">;
def SSP   : PI32V2Reg<13, "ssp">;
def SP    : PI32V2Reg<14, "sp">, DwarfRegNum<[16]>;
def PC    : PI32V2Reg<15, "pc">;

// f32 values live in the general registers too (the (f) forms work on them).
def GPR : RegisterClass<"PI32V2", [i32, f32], 32, (sequence "R%u", 0, 15)>;
def GPRLo : RegisterClass<"PI32V2", [i32], 32, (sequence "R%u", 0, 7)>;
def GPRHi : RegisterClass<"PI32V2", [i32], 32, (sequence "R%u", 8, 15)>;
def GPRPair : RegisterClass<"PI32V2", [i64], 64,
                            (add R1_R0, R3_R2, R5_R4, R7_R6, R9_R8, R11_R10,
                                 R13_R12, R15_R14)>;
def SR : RegisterClass<"PI32V2", [i32], 32,
                       (add RETI, RETE, RETX, RETS, SR4, PSR, CNUM, USPN, SSPN,
                            ICFGN, ICFGS, ICFG, USP, SSP, SP, PC)> {
  let isAllocatable = 0;
}

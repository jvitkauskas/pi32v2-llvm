//===-- PI32V2.td - Describe the PI32V2 target -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's pi32v2 (the AC79 series; the AC791N of the M-VAVE FM-1). The
// instructions are generated from the instruction table of the pi32v2-isa
// project (PI32V2InstrGen.td); see its docs/ for the meaning of everything here.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Features
//===----------------------------------------------------------------------===//

def FeatureInstRev3 : SubtargetFeature<"instrev3", "HasInstRev3", "true",
                                       "Instruction revision 3 (JieLi -minstrev3)">;
def HasInstRev3 : Predicate<"Subtarget->hasInstRev3()">,
                  AssemblerPredicate<(all_of FeatureInstRev3), "instrev3">;

def FeatureFPRev1 : SubtargetFeature<"fprev1", "HasFPRev1", "true",
                                     "Floating-point instructions, revision 1 (JieLi -mfprev1)">;
def HasFPRev1 : Predicate<"Subtarget->hasFPRev1()">,
                AssemblerPredicate<(all_of FeatureFPRev1), "fprev1">;

// A chip whose hardware does not do what some forms say (pi32v2-isa, table key
// `hw`): the compiler leaves those forms out, the assembler warns about them.
def FeatureAC791N : SubtargetFeature<"ac791n", "IsAC791N", "true",
                                     "The JieLi AC791N (hardware behaviour of the table's hw key)">;

//===----------------------------------------------------------------------===//
// Registers, instructions
//===----------------------------------------------------------------------===//

include "PI32V2RegisterInfo.td"
include "PI32V2InstrFormats.td"
include "PI32V2CallingConv.td"
include "PI32V2InstrInfo.td"

def PI32V2InstrInfo : InstrInfo;

defm : RemapAllTargetPseudoPointerOperands<GPR>;

//===----------------------------------------------------------------------===//
// Processors (JieLi's -mcpu names)
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;
def : ProcessorModel<"r3", NoSchedModel, [FeatureFPRev1]>;
def : ProcessorModel<"ac791n", NoSchedModel, [FeatureFPRev1, FeatureAC791N]>;

//===----------------------------------------------------------------------===//
// Assembly
//===----------------------------------------------------------------------===//

// The assembly parser is written by hand from tables generated with the
// instruction table's own assembler (AsmParser/PI32V2ParserGen.inc); the
// forms with no assembler syntax are in the variant "disasm".
def PI32V2DisasmVariant : AsmParserVariant {
  int Variant = 1;
  string Name = "disasm";
}

def PI32V2AsmWriter : AsmWriter {
  int PassSubtarget = 0;
}

def PI32V2 : Target {
  let InstructionSet = PI32V2InstrInfo;
  let AssemblyWriters = [PI32V2AsmWriter];
  let AllowRegisterRenaming = 1;
}

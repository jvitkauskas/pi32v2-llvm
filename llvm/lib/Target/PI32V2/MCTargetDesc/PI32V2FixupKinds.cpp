//===-- PI32V2FixupKinds.cpp - PI32V2 fixups ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2FixupKinds.h"
#include "PI32V2MCTargetDesc.h"
#include <cassert>

using namespace llvm;
using namespace llvm::PI32V2;

#define PI32V2_GEN_FIXUP_TABLE
#include "PI32V2FixupsGen.inc"

#define PI32V2_GEN_FIXUP_FOR_OPERAND
#include "PI32V2FixupsGen.inc"

const PI32V2FixupLayout &PI32V2::fixupLayout(unsigned Kind) {
  assert(Kind >= FirstTargetFixupKind && Kind < LastFixup && "not a PI32V2 fixup");
  return FixupLayouts[Kind - FirstTargetFixupKind];
}

int PI32V2::fixupKindForOperand(unsigned Opcode, unsigned OpNo) {
  int K = fixupForOperand(Opcode, OpNo);
  return K < 0 ? -1 : K + FirstTargetFixupKind;
}

//===-- PI32V2BaseInfo.h - PI32V2 operand helpers ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// How the operand kinds of the pi32v2-isa instruction table are written and
// encoded (its docs/TABLE-FORMAT.md, "Operand kinds").
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2BASEINFO_H
#define LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2BASEINFO_H

#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace PI32V2 {

/// The 32-bit value of a 12-bit modified immediate.
uint32_t modImmValue(uint64_t Raw);
/// The lowest raw 12-bit modified immediate for Value, if there is one.
std::optional<unsigned> modImmRawFor(uint32_t Value);
/// As modImmRawFor, 0 when there is none (the matcher checks first).
unsigned modImmRaw(uint32_t Value);
/// 0x%X.
std::string hexUpper(uint32_t Value);
/// rlist4: 0..3 is r3 down to r<n>, 4..15 is r<n> down to r4.
std::string rlist4Text(int64_t Raw);
/// Two names of one pair: the pair (3 bits), then which of it each name is.
std::string pairRegsText(int64_t Raw);
/// rmask: bit n is rn, highest first, runs of three or more as a range; Lead
/// (a register name, or empty) first.
std::string rmaskText(int64_t Raw, const char *Lead);
/// srmask: bit n is special register n, highest first.
std::string srmaskText(int64_t Raw, unsigned Width);
/// The name of special register N.
const char *srName(unsigned N);

} // namespace PI32V2
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;
namespace PI32V2 {
/// Put Inst in the shortest form JieLi's assembler would use for its line;
/// false if it keeps its form.
bool compressInst(MCInst &Inst, const MCInstrInfo &MII,
                  const MCRegisterInfo &MRI);

} // namespace PI32V2
} // namespace llvm

#endif

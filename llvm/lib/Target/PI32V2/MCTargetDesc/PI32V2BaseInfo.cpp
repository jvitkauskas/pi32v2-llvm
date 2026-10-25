//===-- PI32V2BaseInfo.cpp - PI32V2 operand helpers -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2BaseInfo.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

uint32_t PI32V2::modImmValue(uint64_t Raw) {
  Raw &= 0xFFF;
  if ((Raw >> 10) == 0) {
    uint32_t XY = Raw & 0xFF;
    switch ((Raw >> 8) & 3) {
    case 0:
      return XY;
    case 1:
      return XY * 0x00010001u;
    case 2:
      return XY * 0x01000100u;
    default:
      return XY * 0x01010101u;
    }
  }
  unsigned Rot = Raw >> 7;
  uint32_t Byte = 0x80 | (Raw & 0x7F);
  return (Byte >> Rot) | (Byte << ((32 - Rot) & 31));
}

std::optional<unsigned> PI32V2::modImmRawFor(uint32_t Value) {
  for (unsigned Raw = 0; Raw < 4096; ++Raw)
    if (modImmValue(Raw) == Value)
      return Raw;
  return std::nullopt;
}

unsigned PI32V2::modImmRaw(uint32_t Value) {
  return modImmRawFor(Value).value_or(0);
}

std::string PI32V2::hexUpper(uint32_t Value) {
  std::string S;
  raw_string_ostream OS(S);
  OS << format("0x%X", Value);
  return S;
}

static std::string runs(const std::vector<unsigned> &Regs) {
  std::string S;
  for (size_t K = 0; K < Regs.size();) {
    size_t J = K;
    while (J + 1 < Regs.size() && Regs[J + 1] + 1 == Regs[J])
      ++J;
    auto Add = [&](const std::string &T) {
      if (!S.empty())
        S += ", ";
      S += T;
    };
    if (J - K >= 2)
      Add("r" + std::to_string(Regs[K]) + "-r" + std::to_string(Regs[J]));
    else
      for (size_t I = K; I <= J; ++I)
        Add("r" + std::to_string(Regs[I]));
    K = J + 1;
  }
  return S;
}

std::string PI32V2::rlist4Text(int64_t Raw) {
  unsigned Hi = Raw < 4 ? 3 : Raw, Lo = Raw < 4 ? Raw : 4;
  std::vector<unsigned> Regs;
  for (int R = Hi; R >= (int)Lo; --R)
    Regs.push_back(R);
  return runs(Regs);
}

std::string PI32V2::pairRegsText(int64_t Raw) {
  unsigned Pair = 2 * (Raw >> 2);
  return "r" + std::to_string(Pair + ((Raw >> 1) & 1)) + ",r" +
         std::to_string(Pair + (Raw & 1));
}

std::string PI32V2::rmaskText(int64_t Raw, const char *Lead) {
  std::vector<unsigned> Regs;
  for (int R = 15; R >= 0; --R)
    if ((Raw >> R) & 1)
      Regs.push_back(R);
  std::string S = runs(Regs);
  if (Lead && *Lead)
    return S.empty() ? std::string(Lead) : std::string(Lead) + ", " + S;
  return S;
}

static const char *const SRNames[16] = {
    "reti", "rete", "retx",  "rets",  "sr4", "psr", "cnum", "uspn",
    "sspn", "icfgn", "icfgs", "icfg", "usp", "ssp", "sp",   "pc"};

const char *PI32V2::srName(unsigned N) { return SRNames[N & 15]; }

std::string PI32V2::srmaskText(int64_t Raw, unsigned Width) {
  std::string S;
  for (int R = Width - 1; R >= 0; --R)
    if ((Raw >> R) & 1) {
      if (!S.empty())
        S += ", ";
      S += SRNames[R & 15];
    }
  return S;
}

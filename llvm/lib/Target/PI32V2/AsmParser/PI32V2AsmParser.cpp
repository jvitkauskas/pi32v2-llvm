//===-- PI32V2AsmParser.cpp - Parse PI32V2 assembly -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's assembly is algebraic (`r1 = r2 + r3`, `[--sp] = {r3-r0}`), so the
// parser does not use the TableGen matcher. It reads a whole line, puts it in
// canonical form and matches it against the templates of PI32V2ParserGen.inc,
// which are generated from the pi32v2-isa project's assembler: the readings
// that fit are sorted as that assembler sorts them (size, alias, tie, table
// order) and the first is emitted. Branches start in their smallest form and
// grow by relaxation (PI32V2AsmBackend).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2AsmParserTables.h"
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <optional>

using namespace llvm;

namespace {

#define PI32V2_GEN_PARSER_TABLES
#include "PI32V2ParserGen.inc"

// A word character for canonical form and the spaces around numbers
// (pi32v2isa.asm.WORDCH).
static bool isWordChar(char C) {
  return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '@' ||
         C == '\\' || C == '-';
}
// A character that may continue an identifier or number ([A-Za-z0-9_.$@]).
static bool isIdentChar(char C) {
  return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '@';
}

/// Collapse white space; keep a single space only between two word characters.
static std::string canonical(StringRef Text) {
  SmallVector<StringRef, 16> Parts;
  Text.split(Parts, ' ', -1, false);
  std::string Out;
  for (StringRef P : Parts) {
    P = P.trim(" \t\r\n");
    if (P.empty())
      continue;
    SmallVector<StringRef, 4> Sub;
    P.split(Sub, '\t', -1, false);
    for (StringRef Q : Sub) {
      if (Q.empty())
        continue;
      if (!Out.empty() && isWordChar(Out.back()) && isWordChar(Q.front()))
        Out += ' ';
      Out += Q.str();
    }
  }
  return Out;
}

/// A dummy operand: the parser emits the instruction itself.
class PI32V2Operand : public MCParsedAsmOperand {
  SMLoc Loc;

public:
  explicit PI32V2Operand(SMLoc L) : Loc(L) {}
  bool isToken() const override { return true; }
  bool isImm() const override { return false; }
  bool isReg() const override { return false; }
  bool isMem() const override { return false; }
  MCRegister getReg() const override { return MCRegister(); }
  SMLoc getStartLoc() const override { return Loc; }
  SMLoc getEndLoc() const override { return Loc; }
  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    OS << "<pi32v2 instruction>";
  }
};

/// What a slot of a template matched.
struct SlotText {
  size_t Begin = 0, End = 0;
  bool Present = false;
};

class PI32V2AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
  const MCInstrInfo &MII;
  StringMap<SmallVector<unsigned, 4>> ByPrefix;
  SmallVector<size_t, 16> PrefixLengths;
  std::optional<MCInst> Pending;

  // Template matching.
  bool matchFrom(const PI32V2Template &T, StringRef Text, size_t TPos,
                 size_t Pos, SmallVectorImpl<SlotText> &Got) const;
  bool matchSlotBody(const PI32V2Template &T, unsigned Slot, StringRef Text,
                     size_t TPos, size_t Pos,
                     SmallVectorImpl<SlotText> &Got) const;

  // Operands.
  struct Value {
    bool IsReg = false, IsExpr = false;
    unsigned Reg = 0;
    int64_t Imm = 0;
    const MCExpr *Expr = nullptr;
  };
  bool readSlots(const PI32V2Template &T, StringRef Text,
                 ArrayRef<SlotText> Got, Value (&Fields)[26], bool &Fixed,
                 SMLoc Loc);
  const MCExpr *parseExpr(StringRef Text, SMLoc Loc, bool &Bad);
  bool buildInst(const PI32V2Template &T, Value (&Fields)[26], MCInst &Inst);

public:
  PI32V2AsmParser(const MCSubtargetInfo &STI, MCAsmParser &P,
                  const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(P), MII(MII) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
    for (unsigned I = 0; I < std::size(Templates); ++I) {
      StringRef T = Templates[I].Text;
      size_t N = 0;
      while (N < T.size() && (unsigned char)T[N] < 0x80 && T[N] != 1 &&
             T[N] != 2)
        ++N;
      StringRef Prefix = T.substr(0, N).rtrim(' ');
      ByPrefix[Prefix].push_back(I);
    }
    for (auto &E : ByPrefix)
      PrefixLengths.push_back(E.getKey().size());
    llvm::sort(PrefixLengths, std::greater<size_t>());
    PrefixLengths.erase(llvm::unique(PrefixLengths), PrefixLengths.end());
  }

  FeatureBitset ComputeAvailableFeatures(const FeatureBitset &FB) const {
    return FB;
  }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                     SMLoc &EndLoc) override {
    return true;
  }
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    return ParseStatus::NoMatch;
  }
  bool tokenIsStartOfStatement(AsmToken::TokenKind Kind) override {
    return Kind == AsmToken::LBrac || Kind == AsmToken::LCurly ||
           Kind == AsmToken::RCurly || Kind == AsmToken::LParen;
  }
  bool equalIsAsmAssignment() override { return false; }
  void convertToMapAndConstraints(unsigned Kind,
                                  const OperandVector &Operands) override {}

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
};

} // namespace

// ---- template matching ------------------------------------------------------

// The template continues at TPos; the text at Pos. Slots are read with the
// regular expressions of pi32v2isa.asm (_kind_regex): registers and names
// greedily, numbers and register lists as short as possible.
bool PI32V2AsmParser::matchFrom(const PI32V2Template &T, StringRef Text,
                                size_t TPos, size_t Pos,
                                SmallVectorImpl<SlotText> &Got) const {
  StringRef Tpl = T.Text;
  if (TPos == Tpl.size())
    return Pos == Text.size();
  unsigned char C = Tpl[TPos];
  if (C == 1) {
    // A space, or none when the next character does not continue a word.
    if (Pos < Text.size() && Text[Pos] == ' ' &&
        matchFrom(T, Text, TPos + 1, Pos + 1, Got))
      return true;
    if (Pos < Text.size() && isIdentChar(Text[Pos]))
      return false;
    return matchFrom(T, Text, TPos + 1, Pos, Got);
  }
  if (C == 2) {
    // A space, or none when the previous character does not end a word.
    if (Pos < Text.size() && Text[Pos] == ' ' &&
        matchFrom(T, Text, TPos + 1, Pos + 1, Got))
      return true;
    if (Pos > 0 && isIdentChar(Text[Pos - 1]))
      return false;
    return matchFrom(T, Text, TPos + 1, Pos, Got);
  }
  if (C >= 0x80) {
    unsigned Slot = C - 0x80;
    const PI32V2Slot &S = Slots[T.SlotStart + Slot];
    if (S.Backref >= 0) {
      const SlotText &Prev = Got[S.Backref];
      StringRef P = Text.substr(Prev.Begin, Prev.End - Prev.Begin);
      if (!Text.substr(Pos).starts_with(P))
        return false;
      Got[Slot] = {Pos, Pos + P.size(), true};
      return matchFrom(T, Text, TPos + 1, Pos + P.size(), Got);
    }
    if (S.Plus) {
      // (?:\+body)? : with the '+' first, then without the operand.
      if (Pos < Text.size() && Text[Pos] == '+' &&
          matchSlotBody(T, Slot, Text, TPos, Pos + 1, Got))
        return true;
      Got[Slot] = {Pos, Pos, false};
      return matchFrom(T, Text, TPos + 1, Pos, Got);
    }
    return matchSlotBody(T, Slot, Text, TPos, Pos, Got);
  }
  if (Pos < Text.size() && Text[Pos] == (char)C)
    return matchFrom(T, Text, TPos + 1, Pos + 1, Got);
  return false;
}

bool PI32V2AsmParser::matchSlotBody(const PI32V2Template &T, unsigned Slot,
                                    StringRef Text, size_t TPos, size_t Pos,
                                    SmallVectorImpl<SlotText> &Got) const {
  const PI32V2Slot &S = Slots[T.SlotStart + Slot];
  auto Try = [&](size_t End) {
    Got[Slot] = {Pos, End, true};
    return matchFrom(T, Text, TPos + 1, End, Got);
  };
  auto Digits = [&](size_t P) {
    size_t E = P;
    while (E < Text.size() && isDigit(Text[E]))
      ++E;
    return E;
  };
  switch (S.Kind) {
  case 0: // r\d+
  case 1:
  case 2: {
    if (Pos >= Text.size() || Text[Pos] != 'r')
      return false;
    size_t E = Digits(Pos + 1);
    for (size_t End = E; End > Pos + 1; --End)
      if (Try(End))
        return true;
    return false;
  }
  case 3: // r\d+_r\d+
  case 4: { // r\d+,r\d+
    char Sep = S.Kind == 3 ? '_' : ',';
    if (Pos >= Text.size() || Text[Pos] != 'r')
      return false;
    size_t E1 = Digits(Pos + 1);
    for (size_t M = E1; M > Pos + 1; --M) {
      if (M + 1 >= Text.size() || Text[M] != Sep || Text[M + 1] != 'r')
        continue;
      size_t E2 = Digits(M + 2);
      for (size_t End = E2; End > M + 2; --End)
        if (Try(End))
          return true;
    }
    return false;
  }
  case 5: { // [A-Za-z_][A-Za-z0-9_]*
    if (Pos >= Text.size() || !(isAlpha(Text[Pos]) || Text[Pos] == '_'))
      return false;
    size_t E = Pos + 1;
    while (E < Text.size() && (isAlnum(Text[E]) || Text[E] == '_'))
      ++E;
    for (size_t End = E; End > Pos; --End)
      if (Try(End))
        return true;
    return false;
  }
  case 6:
  case 7:
  case 8: // [^{}]*?
    for (size_t End = Pos; End <= Text.size(); ++End) {
      if (End > Pos && (Text[End - 1] == '{' || Text[End - 1] == '}'))
        return false;
      if (Try(End))
        return true;
    }
    return false;
  default: // .+?
    for (size_t End = Pos + 1; End <= Text.size(); ++End)
      if (Try(End))
        return true;
    return false;
  }
}

// ---- operand values ---------------------------------------------------------

static std::optional<unsigned> regNumber(StringRef T) {
  if (T.size() < 2 || T.size() > 3 || T[0] != 'r')
    return std::nullopt;
  StringRef D = T.drop_front();
  if (D.size() > 1 && D[0] == '0')
    return std::nullopt;
  unsigned N;
  if (D.getAsInteger(10, N) || N > 15)
    return std::nullopt;
  return N;
}

static std::optional<unsigned> srNumber(StringRef T) {
  for (unsigned N = 0; N < 16; ++N)
    if (T == PI32V2::srName(N))
      return N;
  if (T.starts_with("sr")) {
    unsigned N;
    if (!T.drop_front(2).getAsInteger(10, N) && N < 16)
      return N;
  }
  return std::nullopt;
}

static const unsigned PairRegs[8] = {
    PI32V2::R1_R0,   PI32V2::R3_R2,   PI32V2::R5_R4,   PI32V2::R7_R6,
    PI32V2::R9_R8,   PI32V2::R11_R10, PI32V2::R13_R12, PI32V2::R15_R14};
static const unsigned SRRegs[16] = {
    PI32V2::RETI,  PI32V2::RETE,  PI32V2::RETX,  PI32V2::RETS,
    PI32V2::SR4,   PI32V2::PSR,   PI32V2::CNUM,  PI32V2::USPN,
    PI32V2::SSPN,  PI32V2::ICFGN, PI32V2::ICFGS, PI32V2::ICFG,
    PI32V2::USP,   PI32V2::SSP,   PI32V2::SP,    PI32V2::PC};

/// 'r3-r0, rets' -> the general registers and the other names.
static bool parseRegList(StringRef T, uint32_t &Regs,
                         SmallVectorImpl<std::string> &Names) {
  T = T.trim();
  if (T.empty())
    return false;
  SmallVector<StringRef, 8> Items;
  T.split(Items, ',');
  for (StringRef Item : Items) {
    std::string S;
    for (char C : Item)
      if (C != ' ')
        S += C;
    StringRef I(S);
    auto [A, B] = I.split('-');
    auto RA = regNumber(A);
    if (RA && (B.empty() || regNumber(B))) {
      unsigned Lo = *RA, Hi = B.empty() ? *RA : *regNumber(B);
      if (Lo > Hi)
        std::swap(Lo, Hi);
      for (unsigned R = Lo; R <= Hi; ++R)
        Regs |= 1u << R;
      continue;
    }
    if (I.empty() || !(isAlpha(I[0]) || I[0] == '_' || I[0] == '.' ||
                       I[0] == '$'))
      return false;
    Names.push_back(S);
  }
  return true;
}

/// The raw field of a register list kind (pi32v2isa.asm.operand_raw).
static std::optional<int64_t> regListRaw(unsigned Kind, StringRef T,
                                         StringRef Lead, unsigned Width) {
  uint32_t Regs = 0;
  SmallVector<std::string, 4> Names;
  if (!parseRegList(T, Regs, Names))
    return std::nullopt;
  if (Kind == 6) { // rlist4
    if (!Names.empty() || !Regs)
      return std::nullopt;
    unsigned Lo = llvm::countr_zero(Regs), Hi = 31 - llvm::countl_zero(Regs);
    if (Regs != (((Hi == 31 ? 0u : (1u << (Hi + 1))) - 1) & ~((1u << Lo) - 1)))
      return std::nullopt;
    if (Hi == 3 && Lo <= 3)
      return Lo;
    if (Lo == 4 && Hi >= 4)
      return Hi;
    return std::nullopt;
  }
  if (Kind == 7) { // rmask
    if (Lead.empty() ? !Names.empty()
                     : (Names.size() != 1 || Names[0] != Lead.str()))
      return std::nullopt;
    return Regs;
  }
  if (Regs) // srmask
    return std::nullopt;
  int64_t Mask = 0;
  for (auto &N : Names) {
    auto SR = srNumber(N);
    if (!SR || *SR >= Width)
      return std::nullopt;
    Mask |= int64_t(1) << *SR;
  }
  return Mask;
}

/// The operand value (as the printer shows it) for number V in a numeric
/// field, or nothing if it does not fit (pi32v2isa.asm.fit_num, numeric_raw).
static std::optional<int64_t> fitNumber(const PI32V2NumType &N, int64_t V) {
  switch (N.Kind) {
  case 2: // bit
  case 3: { // invbit
    uint32_t X = uint32_t(V);
    if (N.Kind == 3)
      X = ~X;
    if (!X || (X & (X - 1)))
      return std::nullopt;
    return N.Kind == 3 ? int64_t(uint32_t(~X)) : int64_t(X);
  }
  case 4: { // modimm
    uint32_t X = uint32_t(V);
    if (N.Invert)
      X = ~X;
    auto Raw = PI32V2::modImmRawFor(X);
    if (!Raw)
      return std::nullopt;
    uint32_t Val = PI32V2::modImmValue(*Raw);
    return int64_t(N.Invert ? ~Val : Val);
  }
  default:
    break;
  }
  bool Wrap = N.Kind != 5;
  SmallVector<int64_t, 3> Cands = {V};
  int64_t V32 = int64_t(uint32_t(V));
  if (Wrap && V32 != V)
    Cands.push_back(V32);
  if (Wrap && V32 >= (int64_t(1) << 31))
    Cands.push_back(V32 - (int64_t(1) << 32));
  bool Unsigned = N.Kind == 0;
  for (int64_t X : Cands) {
    if (N.HasRange && !(N.Lo <= X && X <= N.Hi))
      continue;
    if (N.HasZeroAs && X == N.ZeroAs)
      return X;
    int64_t D = X - N.Add;
    if (D % N.Scale)
      continue;
    int64_t R = D / N.Scale;
    if (Unsigned) {
      if (R >= 0 && R < (int64_t(1) << N.Width) && !(N.HasZeroAs && R == 0))
        return R * N.Scale + N.Add;
    } else if (R >= -(int64_t(1) << (N.Width - 1)) &&
               R < (int64_t(1) << (N.Width - 1))) {
      return R * N.Scale + N.Add;
    }
  }
  return std::nullopt;
}

namespace {
/// Expressions as pi32v2isa.asm.ExprParser reads them.
class ExprReader {
  MCContext &Ctx;
  MCStreamer &Out;
  StringRef T;
  size_t I = 0;

public:
  bool Bad = false;
  ExprReader(MCContext &Ctx, MCStreamer &Out, StringRef T)
      : Ctx(Ctx), Out(Out), T(T) {}

  void skip() {
    while (I < T.size() && T[I] == ' ')
      ++I;
  }
  bool done() {
    skip();
    return I == T.size();
  }
  // Binary operators and their levels (asm.BIN_LEVEL).
  int level(StringRef &Op) {
    skip();
    static const struct {
      const char *Op;
      int Level;
    } Ops[] = {{"||", 1}, {"&&", 2}, {"==", 3}, {"!=", 3}, {"<>", 3},
               {"<=", 3}, {">=", 3}, {"<<", 6}, {">>", 6}, {"<", 3},
               {">", 3},  {"+", 4},  {"-", 4},  {"|", 5},  {"&", 5},
               {"^", 5},  {"!", 5},  {"*", 6},  {"/", 6},  {"%", 6}};
    for (auto &O : Ops)
      if (T.substr(I).starts_with(O.Op)) {
        Op = O.Op;
        return O.Level;
      }
    return 0;
  }
  const MCExpr *binary(int Level) {
    if (Level > 6)
      return unary();
    const MCExpr *L = binary(Level + 1);
    while (L) {
      StringRef Op;
      if (level(Op) != Level)
        return L;
      I += Op.size();
      const MCExpr *R = binary(Level + 1);
      if (!R)
        return nullptr;
      MCBinaryExpr::Opcode K;
      if (Op == "||") K = MCBinaryExpr::LOr;
      else if (Op == "&&") K = MCBinaryExpr::LAnd;
      else if (Op == "==") K = MCBinaryExpr::EQ;
      else if (Op == "!=" || Op == "<>") K = MCBinaryExpr::NE;
      else if (Op == "<=") K = MCBinaryExpr::LTE;
      else if (Op == ">=") K = MCBinaryExpr::GTE;
      else if (Op == "<<") K = MCBinaryExpr::Shl;
      else if (Op == ">>") K = MCBinaryExpr::AShr;
      else if (Op == "<") K = MCBinaryExpr::LT;
      else if (Op == ">") K = MCBinaryExpr::GT;
      else if (Op == "+") K = MCBinaryExpr::Add;
      else if (Op == "-") K = MCBinaryExpr::Sub;
      else if (Op == "|") K = MCBinaryExpr::Or;
      else if (Op == "&") K = MCBinaryExpr::And;
      else if (Op == "^") K = MCBinaryExpr::Xor;
      else if (Op == "!") K = MCBinaryExpr::OrNot;
      else if (Op == "*") K = MCBinaryExpr::Mul;
      else if (Op == "/") K = MCBinaryExpr::Div;
      else K = MCBinaryExpr::Mod;
      L = MCBinaryExpr::create(K, L, R, Ctx);
    }
    return L;
  }
  const MCExpr *unary() {
    skip();
    if (I < T.size() && (T[I] == '-' || T[I] == '+' || T[I] == '~' ||
                         T[I] == '!')) {
      char C = T[I++];
      const MCExpr *E = unary();
      if (!E)
        return nullptr;
      switch (C) {
      case '-':
        return MCUnaryExpr::createMinus(E, Ctx);
      case '+':
        return E;
      case '~':
        return MCUnaryExpr::createNot(E, Ctx);
      default:
        return MCUnaryExpr::createLNot(E, Ctx);
      }
    }
    return primary();
  }
  const MCExpr *primary() {
    skip();
    if (I >= T.size()) {
      Bad = true;
      return nullptr;
    }
    char C = T[I];
    if (C == '(') {
      ++I;
      const MCExpr *E = binary(1);
      skip();
      if (!E || I >= T.size() || T[I] != ')') {
        Bad = true;
        return nullptr;
      }
      ++I;
      return E;
    }
    if (C == '\'') {
      // 'c' or '\n'
      size_t J = I + 1;
      if (J >= T.size()) {
        Bad = true;
        return nullptr;
      }
      int64_t V;
      if (T[J] == '\\' && J + 1 < T.size()) {
        static const char Esc[] = "nntrrbbffvvaa00\\\\''\"\"";
        static const int Val[] = {10, 9, 13, 8, 12, 11, 7, 0, 92, 39, 34};
        const char *P = nullptr;
        for (unsigned K = 0; K < 11; ++K)
          if (T[J + 1] == Esc[2 * K])
            P = &Esc[2 * K], V = Val[K];
        if (!P) {
          Bad = true;
          return nullptr;
        }
        J += 2;
      } else {
        V = (unsigned char)T[J];
        J += 1;
      }
      if (J < T.size() && T[J] == '\'')
        ++J;
      I = J;
      return MCConstantExpr::create(V, Ctx);
    }
    if (isDigit(C)) {
      size_t J = I;
      while (J < T.size() && isIdentChar(T[J]))
        ++J;
      std::string N = T.substr(I, J - I).lower();
      I = J;
      StringRef S(N);
      if (S.size() >= 2 && (S.back() == 'f' || S.back() == 'b') &&
          S.drop_back().find_first_not_of("0123456789") == StringRef::npos) {
        unsigned L;
        S.drop_back().getAsInteger(10, L);
        return MCSymbolRefExpr::create(
            Ctx.getDirectionalLocalSymbol(L, S.back() == 'b'), Ctx);
      }
      uint64_t V;
      bool Err;
      if (S.starts_with("0x"))
        Err = S.drop_front(2).getAsInteger(16, V);
      else if (S.starts_with("0b"))
        Err = S.drop_front(2).getAsInteger(2, V);
      else if (S.ends_with("h"))
        Err = S.drop_back().getAsInteger(16, V);
      else if (S.size() > 1 && S[0] == '0')
        Err = S.getAsInteger(8, V);
      else
        Err = S.getAsInteger(10, V);
      if (Err) {
        Bad = true;
        return nullptr;
      }
      return MCConstantExpr::create(int64_t(V), Ctx);
    }
    if (isAlpha(C) || C == '_' || C == '.' || C == '$') {
      size_t J = I;
      while (J < T.size() && isIdentChar(T[J]))
        ++J;
      StringRef Name = T.substr(I, J - I);
      I = J;
      if (Name == ".") {
        MCSymbol *Here = Ctx.createTempSymbol();
        Out.emitLabel(Here);
        return MCSymbolRefExpr::create(Here, Ctx);
      }
      // Register names are not symbols in an instruction operand.
      StringRef Base = Name.split('.').first;
      if (regNumber(Base) || srNumber(Base) ||
          (Base.contains('_') && regNumber(Base.split('_').first))) {
        Bad = true;
        return nullptr;
      }
      if (Name.contains('@')) {
        Bad = true; // relocation variants: not yet
        return nullptr;
      }
      return MCSymbolRefExpr::create(Ctx.getOrCreateSymbol(Name), Ctx);
    }
    Bad = true;
    return nullptr;
  }
};
} // namespace

const MCExpr *PI32V2AsmParser::parseExpr(StringRef Text, SMLoc Loc,
                                         bool &Bad) {
  ExprReader R(getContext(), getParser().getStreamer(), Text);
  const MCExpr *E = R.binary(1);
  Bad = R.Bad || !E || !R.done();
  return Bad ? nullptr : E;
}

bool PI32V2AsmParser::readSlots(const PI32V2Template &T, StringRef Text,
                                ArrayRef<SlotText> Got, Value (&Fields)[26],
                                bool &Fixed, SMLoc Loc) {
  for (unsigned K = 0; K < T.NumSlots; ++K) {
    const PI32V2Slot &S = Slots[T.SlotStart + K];
    StringRef X = Text.substr(Got[K].Begin, Got[K].End - Got[K].Begin);
    Value &V = Fields[S.Field - 'a'];
    switch (S.Kind) {
    case 0:
    case 1:
    case 2: {
      auto N = regNumber(X);
      if (!N || (S.Kind == 1 && *N > 7) || (S.Kind == 2 && *N < 8))
        return false;
      V.IsReg = true;
      V.Reg = PI32V2::R0 + *N;
      break;
    }
    case 3: {
      auto [H, L] = X.split('_');
      auto RH = regNumber(H), RL = regNumber(L);
      if (!RH || !RL || *RH != *RL + 1 || (*RL & 1))
        return false;
      V.IsReg = true;
      V.Reg = PairRegs[*RL / 2];
      break;
    }
    case 4: {
      auto [A, B] = X.split(',');
      auto RA = regNumber(A.trim()), RB = regNumber(B.trim());
      if (!RA || !RB || *RA / 2 != *RB / 2)
        return false;
      V.Imm = (*RA / 2) << 2 | (*RA & 1) << 1 | (*RB & 1);
      break;
    }
    case 5: {
      auto N = srNumber(X);
      if (!N)
        return false;
      V.IsReg = true;
      V.Reg = SRRegs[*N];
      break;
    }
    case 6:
    case 7:
    case 8: {
      auto R = regListRaw(S.Kind, X, S.Lead, S.Width);
      if (!R)
        return false;
      V.Imm = *R;
      break;
    }
    default: {
      if (!Got[K].Present) { // an omitted {+n}
        V.Imm = 0;
        break;
      }
      bool Bad;
      const MCExpr *E = parseExpr(X, Loc, Bad);
      if (Bad)
        return false;
      int64_t C;
      if (E->evaluateAsAbsolute(C)) {
        V.Imm = S.Negate ? -C : C;
      } else {
        if (S.Negate)
          E = MCUnaryExpr::createMinus(E, getContext());
        V.IsExpr = true;
        V.Expr = E;
        Fixed = false;
      }
      break;
    }
    }
  }
  if (T.SameTo)
    Fields[T.SameTo - 'a'] = Fields[T.SameFrom - 'a'];
  return true;
}

bool PI32V2AsmParser::buildInst(const PI32V2Template &T, Value (&Fields)[26],
                                MCInst &Inst) {
  Inst.clear();
  Inst.setOpcode(T.Opcode);
  for (unsigned K = 0; K < T.NumOps; ++K) {
    const PI32V2Op &O = Ops[T.OpStart + K];
    Value &V = Fields[O.Field - 'a'];
    switch (O.Kind) {
    case 0:
      Inst.addOperand(MCOperand::createReg(V.Reg));
      break;
    case 1: {
      const PI32V2NumType &N = NumTypes[O.NumType];
      if (V.IsExpr) {
        // Only branch targets and 32-bit constants take a symbol.
        if (N.Kind != 5 && !N.Abs32)
          return false;
        Inst.addOperand(MCOperand::createExpr(V.Expr));
        break;
      }
      if (N.Kind == 5) { // a displacement from the end of the instruction
        if (V.Imm & 1)
          return false;
        auto R = fitNumber(N, V.Imm);
        if (!R)
          return false;
        Inst.addOperand(MCOperand::createImm(*R));
        break;
      }
      // A subtraction alias must not wrap its negated constant into a small
      // addition.
      if (T.Alias && T.Size == 2 &&
          !(-(int64_t(1) << 31) <= V.Imm && V.Imm < (int64_t(1) << 31)))
        return false;
      if (N.Abs32) {
        Inst.addOperand(MCOperand::createImm(int64_t(uint32_t(V.Imm))));
        break;
      }
      auto R = fitNumber(N, V.Imm);
      if (!R)
        return false;
      Inst.addOperand(MCOperand::createImm(*R));
      break;
    }
    default:
      Inst.addOperand(MCOperand::createImm(V.Imm));
      break;
    }
  }
  return true;
}

// ---- statements -------------------------------------------------------------

bool PI32V2AsmParser::parseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  // The whole line, from its first token to the end of the statement.
  const char *Begin = NameLoc.getPointer();
  while (!getLexer().is(AsmToken::EndOfStatement) &&
         !getLexer().is(AsmToken::Eof))
    getParser().Lex();
  const char *End = getLexer().getLoc().getPointer();
  StringRef Raw(Begin, End - Begin);
  // A comment the lexer has not removed.
  for (StringRef C : {";", "//"}) {
    size_t P = Raw.find(C);
    if (P != StringRef::npos)
      Raw = Raw.substr(0, P);
  }
  std::string Line = canonical(Raw);

  struct Reading {
    unsigned Index;
    MCInst Inst;
  };
  SmallVector<Reading, 8> Good;

  auto TryLine = [&](StringRef Text, bool OnlyWide) {
    for (size_t Len : PrefixLengths) {
      if (Len > Text.size())
        continue;
      auto It = ByPrefix.find(Text.substr(0, Len));
      if (It == ByPrefix.end())
        continue;
      for (unsigned Idx : It->second) {
        const PI32V2Template &T = Templates[Idx];
        bool Rejected = false;
        for (unsigned R = 0; R < T.NumRejects; ++R)
          if (Text == Rejects[T.RejectStart + R])
            Rejected = true;
        if (Rejected)
          continue;
        SmallVector<SlotText, 8> Got(T.NumSlots);
        if (!matchFrom(T, Text, 0, 0, Got))
          continue;
        Value Fields[26];
        bool Fixed = true;
        if (!readSlots(T, Text, Got, Fields, Fixed, NameLoc))
          continue;
        if (OnlyWide && !T.Abs32)
          continue;
        if (!Fixed && !T.Abs32 && !T.Branch)
          continue;
        MCInst Inst;
        if (!buildInst(T, Fields, Inst))
          continue;
        Good.push_back({Idx, Inst});
      }
    }
  };

  // Hexadecimal, binary and octal numbers also as decimal, for templates with
  // a number in their text.
  auto Plain = [](StringRef L) {
    std::string Out;
    for (size_t I = 0; I < L.size();) {
      bool Start = I == 0 || !isIdentChar(L[I - 1]);
      if (Start && L[I] == '0' && I + 1 < L.size()) {
        size_t J = I + 1;
        unsigned Radix = 8;
        if (L[J] == 'x' || L[J] == 'X')
          Radix = 16, ++J;
        else if (L[J] == 'b' || L[J] == 'B')
          Radix = 2, ++J;
        size_t K = J;
        while (K < L.size() && isIdentChar(L[K]))
          ++K;
        uint64_t V;
        if (K > J && !L.substr(J, K - J).getAsInteger(Radix, V)) {
          Out += std::to_string(V);
          I = K;
          continue;
        }
      }
      Out += L[I++];
    }
    return Out;
  };

  TryLine(Line, false);
  std::string P = Plain(Line);
  if (P != Line)
    TryLine(P, false);
  if (Good.empty()) {
    // Memory operands may omit +0.
    std::string N;
    StringRef L(Line);
    for (size_t I = 0; I < L.size(); ++I) {
      N += L[I];
      if (L[I] == '[' && I + 1 < L.size() && L[I + 1] == 'r') {
        size_t J = I + 2;
        while (J < L.size() && isDigit(L[J]))
          ++J;
        if (J < L.size() && L[J] == ']' && regNumber(L.substr(I + 1, J - I - 1))) {
          N += L.substr(I + 1, J - I - 1).str() + "+0";
          I = J - 1;
        }
      }
    }
    if (N != Line)
      TryLine(N, false);
  }
  if (Good.empty())
    return Error(NameLoc, "unrecognised instruction or operand: " + Line);

  // A constant that is not known when the line is read goes in the 32-bit
  // form, as a symbol does.
  bool AnyWide = false, AllFixed = true;
  for (auto &R : Good) {
    const PI32V2Template &T = Templates[R.Index];
    AnyWide |= T.Abs32;
    for (unsigned K = 0; K < R.Inst.getNumOperands(); ++K)
      if (R.Inst.getOperand(K).isExpr() && !T.Branch)
        AllFixed = false;
  }
  if (!AllFixed && AnyWide)
    Good.erase(llvm::remove_if(Good,
                               [](const Reading &R) {
                                 return !Templates[R.Index].Abs32;
                               }),
               Good.end());
  llvm::stable_sort(Good, [](const Reading &A, const Reading &B) {
    const PI32V2Template &X = Templates[A.Index], &Y = Templates[B.Index];
    return std::make_tuple(X.Size, X.Alias, X.Tie, X.Order) <
           std::make_tuple(Y.Size, Y.Alias, Y.Tie, Y.Order);
  });
  Pending = Good.front().Inst;
  Operands.push_back(std::make_unique<PI32V2Operand>(NameLoc));
  return false;
}

bool PI32V2AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  if (!Pending)
    return Error(IDLoc, "no instruction");
  MCInst Inst = *Pending;
  Pending.reset();
  Inst.setLoc(IDLoc);
  Opcode = Inst.getOpcode();
  Out.emitInstruction(Inst, getSTI());
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePI32V2AsmParser() {
  RegisterMCAsmParser<PI32V2AsmParser> X(getThePI32V2Target());
}

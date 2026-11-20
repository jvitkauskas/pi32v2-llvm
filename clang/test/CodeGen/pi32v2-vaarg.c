// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target
// va_arg reads an argument as the caller passed it: va_list points at the next word, and an argument
// passed by reference (an aggregate, a _BitInt wider than 64 bits) is a pointer to its copy there,
// read with the alignment the caller gave the copy. A transparent union is passed as its first field
// would be: inline, in that field's slots, or by reference if that field is passed so.

typedef _BitInt(128) wide;
struct S { int a, b; };
typedef union { int i; float f; } __attribute__((transparent_union)) TU;

// The caller passes a 128-bit _BitInt by reference to a copy it makes, as it does an aggregate (not
// byval, whose copy the backend would make, only 4-aligned, in the argument area).
// CHECK-LABEL: @call(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, ptr noundef nonnull align 4 dead_on_return %{{.*}}, i32 noundef 42)
int f(int n, ...);
int call(void) { return f(1, (wide)4, 42); }

// So va_arg takes one word, the pointer, and reads the value through it.
// CHECK-LABEL: @get_wide(
// CHECK: %argp.cur = load ptr, ptr %ap
// CHECK-NEXT: %argp.next = getelementptr inbounds nuw i8, ptr %argp.cur, i32 4
// CHECK-NEXT: store ptr %argp.next, ptr %ap
// CHECK-NEXT: [[P:%.*]] = load ptr, ptr %argp.cur
// CHECK-NEXT: load i128, ptr [[P]]
wide get_wide(__builtin_va_list *ap) { return __builtin_va_arg(*ap, wide); }

// CHECK-LABEL: @get_int(
// CHECK: %argp.next = getelementptr inbounds nuw i8, ptr %argp.cur, i32 4
// CHECK: load i32, ptr %argp.cur
int get_int(__builtin_va_list *ap) { return __builtin_va_arg(*ap, int); }

// CHECK-LABEL: @get_struct(
// CHECK: %argp.next = getelementptr inbounds nuw i8, ptr %argp.cur, i32 4
// CHECK: [[P:%.*]] = load ptr, ptr %argp.cur
// CHECK-NEXT: load i64, ptr [[P]]
struct S get_struct(__builtin_va_list *ap) { return __builtin_va_arg(*ap, struct S); }

// CHECK-LABEL: @get_tu(
// CHECK: %argp.next = getelementptr inbounds nuw i8, ptr %argp.cur, i32 4
// CHECK-NOT: load ptr, ptr %argp.cur
// CHECK: load i32, ptr %argp.cur
TU get_tu(__builtin_va_list *ap) { return __builtin_va_arg(*ap, TU); }

// An over-aligned transparent union is passed as its first field alone, one word (not as the padded
// union), and va_arg takes that word: the next argument follows it.
typedef union { int i; float f; } __attribute__((transparent_union, aligned(16))) ATU;
// CHECK-LABEL: @call_atu(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, i32 %{{.*}}, i32 noundef 42)
int call_atu(ATU *u) { return f(1, *u, 42); }
// CHECK-LABEL: @after_atu(
// CHECK: %argp.next = getelementptr inbounds nuw i8, ptr %argp.cur, i32 4
// CHECK: load i32, ptr %argp.next
int after_atu(__builtin_va_list *ap) {
  (void)__builtin_va_arg(*ap, ATU);
  return __builtin_va_arg(*ap, int);
}

// A transparent union whose first field is a struct is passed by reference, as the struct is.
typedef union { struct { int i; } s; int i; } __attribute__((transparent_union)) STU;
// CHECK-LABEL: @get_stu(
// CHECK: [[P:%.*]] = load ptr, ptr %argp.cur
// CHECK: load i32, ptr [[P]]
int get_stu(__builtin_va_list *ap) { return __builtin_va_arg(*ap, STU).i; }

// Alignment a typedef adds isn't the copy's: the caller copies the canonical type, 4-aligned, and
// va_arg reads it so.
typedef _BitInt(65) AWide __attribute__((aligned(32)));
// CHECK-LABEL: @call_awide(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, ptr noundef nonnull align 4 dead_on_return %{{.*}}, i32 noundef 42)
int call_awide(AWide *w) { return f(1, *w, 42); }
// CHECK-LABEL: @get_awide(
// CHECK: [[P:%.*]] = load ptr, ptr %argp.cur
// CHECK-NEXT: load i96, ptr [[P]], align 4
AWide get_awide(__builtin_va_list *ap) { return __builtin_va_arg(*ap, AWide); }

// Fetched whole, a transparent union is its first field, read from its slot, then put in a union:
// no copy of the union's 16 bytes from a 4-byte slot.
typedef union { unsigned char a; signed char b; } __attribute__((transparent_union, aligned(16))) BTU;
// CHECK-LABEL: @get_btu(
// CHECK-NOT: memcpy
// CHECK: [[B:%.*]] = load i8, ptr %argp.cur
// CHECK-NEXT: store i8 [[B]], ptr %agg.result
BTU get_btu(__builtin_va_list *ap) { return __builtin_va_arg(*ap, BTU); }

// A transparent union whose first field is an over-aligned _BitInt goes by reference to a copy the
// caller makes 32-aligned, and va_arg reads it so.
typedef _BitInt(65) W32 __attribute__((aligned(32)));
typedef union { W32 x; W32 y; } __attribute__((transparent_union)) WTU;
// CHECK-LABEL: @call_wtu(
// CHECK: alloca %union.WTU, align 32
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, ptr noundef nonnull align 32 dead_on_return %{{.*}}, i32 noundef 42)
int call_wtu(WTU *w) { return f(1, *w, 42); }
// CHECK-LABEL: @get_wtu(
// CHECK: [[P:%.*]] = load ptr, ptr %argp.cur
// CHECK-NEXT: load i96, ptr [[P]], align 32
W32 get_wtu(__builtin_va_list *ap) { return __builtin_va_arg(*ap, WTU).x; }

// A transparent union goes as its first field's in-memory type, so a value stored through another
// member loses no bit: a bool as i8, a _BitInt(33) as i64. va_arg copies those bytes.
typedef union { _Bool b; unsigned char c; } __attribute__((transparent_union)) BoolTU;
typedef union { _BitInt(33) x; unsigned long long y; } __attribute__((transparent_union)) B33TU;
// CHECK-LABEL: @call_booltu(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, i8 noundef zeroext -91, i32 noundef 42)
int call_booltu(void) { BoolTU u; u.c = 0xA5; return f(1, u, 42); }
// CHECK-LABEL: @call_b33tu(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, i64 -1, i32 noundef 42)
int call_b33tu(void) { B33TU u; u.y = ~0ull; return f(1, u, 42); }
// CHECK-LABEL: @get_booltu(
// CHECK: [[C:%.*]] = load i8, ptr %argp.cur
// CHECK-NOT: trunc
// CHECK: zext i8 [[C]] to i32
int get_booltu(__builtin_va_list *ap) { return __builtin_va_arg(*ap, BoolTU).c; }

// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target
// va_arg reads an argument as the caller passed it: va_list points at the next word, and an argument
// passed by reference (an aggregate, a _BitInt wider than 64 bits) is a pointer to its copy there,
// while a transparent union is its first field, inline.

typedef _BitInt(128) wide;
struct S { int a, b; };
typedef union { int i; float f; } __attribute__((transparent_union)) TU;

// The caller passes a 128-bit _BitInt by reference to a copy.
// CHECK-LABEL: @call(
// CHECK: call i32 (i32, ...) @f(i32 noundef 1, ptr noundef nonnull byval(i128) align 4 %{{.*}}, i32 noundef 42)
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

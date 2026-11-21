// REQUIRES: pi32v2-registered-target
// RUN: %clang_cc1 -triple pi32v2 -target-cpu r3 -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple pi32v2 -target-cpu r3 -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// A vector is passed as its lanes, each a scalar argument of its own (there are no vector registers; the backend
// splits it). va_arg reads it so: a lane narrower than a word (char, short, bool) from a word of its own, and a
// three-lane vector as three lanes. Whole-word lanes that fill the vector's size are read as it is laid out in memory.

typedef char c4 __attribute__((vector_size(4)));
typedef short s2 __attribute__((vector_size(4)));
typedef _Bool b8 __attribute__((ext_vector_type(8)));
typedef int i4 __attribute__((vector_size(16)));
typedef long long l2 __attribute__((vector_size(16)));

// Four words for four chars; the int after them is the fifth.
// CHECK-LABEL: @get_c4(
// CHECK: %argp.cur = load ptr, ptr %ap
// CHECK: [[L0:%.*]] = load i32, ptr %argp.cur, align 4
// CHECK: [[P3:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 12
// CHECK: [[PK:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 16
// CHECK: [[L3:%.*]] = load i32, ptr [[P3]], align 4
// CHECK: load i32, ptr [[PK]], align 4
// CHECK-NOT: load <4 x i8>
int get_c4(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  c4 v = __builtin_va_arg(ap, c4);
  int k = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return v[0] + v[3] + k;
}

// Two words for two shorts.
// CHECK-LABEL: @after_s2(
// CHECK: [[PK:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 8
// CHECK: load i32, ptr [[PK]], align 4
int after_s2(__builtin_va_list *ap) {
  (void)__builtin_va_arg(*ap, s2);
  return __builtin_va_arg(*ap, int);
}

// Eight words for eight bools.
// CHECK-LABEL: @after_b8(
// CHECK: [[PK:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 32
// CHECK: load i32, ptr [[PK]], align 4
int after_b8(__builtin_va_list *ap) {
  (void)__builtin_va_arg(*ap, b8);
  return __builtin_va_arg(*ap, int);
}

// Lanes of a word or two are consecutive words already: read as the vector.
// CHECK-LABEL: @get_i4(
// CHECK: load <4 x i32>, ptr %argp.cur, align 4
i4 get_i4(__builtin_va_list *ap) { return __builtin_va_arg(*ap, i4); }
// CHECK-LABEL: @get_l2(
// CHECK: load <2 x i64>, ptr %argp.cur, align 4
l2 get_l2(__builtin_va_list *ap) { return __builtin_va_arg(*ap, l2); }

// A three-lane vector is three lanes, not the four its size in memory has: the int after it is the fourth word.
typedef float f3 __attribute__((ext_vector_type(3)));
// CHECK-LABEL: @after_f3(
// CHECK: [[PK:%.*]] = getelementptr inbounds nuw i8, ptr %argp.cur, i32 12
// CHECK: load i32, ptr [[PK]], align 4
int after_f3(__builtin_va_list *ap) {
  (void)__builtin_va_arg(*ap, f3);
  return __builtin_va_arg(*ap, int);
}
// CHECK-LABEL: @get_f3(
// CHECK-DAG: load float, ptr %argp.cur, align 4
// CHECK-NOT: load <4 x float>
f3 get_f3(__builtin_va_list *ap) { return __builtin_va_arg(*ap, f3); }

// The caller and the callee in the machine code: the lanes in r1, r2, r3 and the first stack word, the int after
// them; get_c4 pushes r1-r3 below the stack arguments and reads lane 0, lane 3 and the int from there.
// ASM-LABEL: get_c4:
// ASM: [--sp] = {r3-r1}
// ASM-DAG: b[sp+4] (s)
// ASM-DAG: b[sp+16] (s)
// ASM-DAG: [sp+20]
// ASM: rts
// ASM-LABEL: call_c4:
// ASM-DAG: [sp+4] = r{{[0-9]+}}
// ASM-DAG: [sp] = r3
// ASM: call get_c4
int call_c4(c4 v) { return get_c4(0, v, 7); }

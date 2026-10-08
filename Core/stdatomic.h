#ifndef PROJECT_COMPONENT_COMMON_STDATOMIC_H_
#define PROJECT_COMPONENT_COMMON_STDATOMIC_H_

/*
 * Compatibility layer:
 * - C11/C17 with atomics support: forward to toolchain stdatomic.h
 * - C99 (or C11 without atomics): provide a GCC __atomic based fallback
 *
 * This header is intentionally placed in project include paths so user code can
 * include <stdatomic.h> uniformly across language standards.
 */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && \
    !defined(__STDC_NO_ATOMICS__)

#if defined(__GNUC__) || defined(__clang__)
#include_next <stdatomic.h>
#else
#error "C11 atomics require a compiler that supports include_next in this project."
#endif

#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum memory_order {
  memory_order_relaxed = 0,
  memory_order_consume = 1,
  memory_order_acquire = 2,
  memory_order_release = 3,
  memory_order_acq_rel = 4,
  memory_order_seq_cst = 5
} memory_order;

static inline int atomic_detail_to_gcc_mo(memory_order mo)
{
  switch (mo) {
   case memory_order_relaxed:
    return __ATOMIC_RELAXED;
   case memory_order_consume:
    return __ATOMIC_CONSUME;
   case memory_order_acquire:
    return __ATOMIC_ACQUIRE;
   case memory_order_release:
    return __ATOMIC_RELEASE;
   case memory_order_acq_rel:
    return __ATOMIC_ACQ_REL;
   case memory_order_seq_cst:
   default:
    return __ATOMIC_SEQ_CST;
  }
}

#define kill_dependency(y) (y)
#define ATOMIC_VAR_INIT(value) \
  {                             \
    (value)                     \
  }

#define ATOMIC_BOOL_LOCK_FREE 2
#define ATOMIC_CHAR_LOCK_FREE 2
#define ATOMIC_CHAR16_T_LOCK_FREE 2
#define ATOMIC_CHAR32_T_LOCK_FREE 2
#define ATOMIC_WCHAR_T_LOCK_FREE 2
#define ATOMIC_SHORT_LOCK_FREE 2
#define ATOMIC_INT_LOCK_FREE 2
#define ATOMIC_LONG_LOCK_FREE 2
#define ATOMIC_LLONG_LOCK_FREE 2
#define ATOMIC_POINTER_LOCK_FREE 2

#define ATOMIC_DETAIL_DECLARE(type, name) \
  typedef struct {                        \
    type __v;                             \
  } atomic_##name

ATOMIC_DETAIL_DECLARE(_Bool, bool);
ATOMIC_DETAIL_DECLARE(char, char);
ATOMIC_DETAIL_DECLARE(signed char, schar);
ATOMIC_DETAIL_DECLARE(unsigned char, uchar);
ATOMIC_DETAIL_DECLARE(short, short);
ATOMIC_DETAIL_DECLARE(unsigned short, ushort);
ATOMIC_DETAIL_DECLARE(int, int);
ATOMIC_DETAIL_DECLARE(unsigned int, uint);
ATOMIC_DETAIL_DECLARE(long, long);
ATOMIC_DETAIL_DECLARE(unsigned long, ulong);
ATOMIC_DETAIL_DECLARE(long long, llong);
ATOMIC_DETAIL_DECLARE(unsigned long long, ullong);
ATOMIC_DETAIL_DECLARE(uint_least16_t, char16_t);
ATOMIC_DETAIL_DECLARE(uint_least32_t, char32_t);
ATOMIC_DETAIL_DECLARE(wchar_t, wchar_t);
ATOMIC_DETAIL_DECLARE(int_least8_t, int_least8_t);
ATOMIC_DETAIL_DECLARE(uint_least8_t, uint_least8_t);
ATOMIC_DETAIL_DECLARE(int_least16_t, int_least16_t);
ATOMIC_DETAIL_DECLARE(uint_least16_t, uint_least16_t);
ATOMIC_DETAIL_DECLARE(int_least32_t, int_least32_t);
ATOMIC_DETAIL_DECLARE(uint_least32_t, uint_least32_t);
ATOMIC_DETAIL_DECLARE(int_least64_t, int_least64_t);
ATOMIC_DETAIL_DECLARE(uint_least64_t, uint_least64_t);
ATOMIC_DETAIL_DECLARE(int_fast8_t, int_fast8_t);
ATOMIC_DETAIL_DECLARE(uint_fast8_t, uint_fast8_t);
ATOMIC_DETAIL_DECLARE(int_fast16_t, int_fast16_t);
ATOMIC_DETAIL_DECLARE(uint_fast16_t, uint_fast16_t);
ATOMIC_DETAIL_DECLARE(int_fast32_t, int_fast32_t);
ATOMIC_DETAIL_DECLARE(uint_fast32_t, uint_fast32_t);
ATOMIC_DETAIL_DECLARE(int_fast64_t, int_fast64_t);
ATOMIC_DETAIL_DECLARE(uint_fast64_t, uint_fast64_t);
ATOMIC_DETAIL_DECLARE(intptr_t, intptr_t);
ATOMIC_DETAIL_DECLARE(uintptr_t, uintptr_t);
ATOMIC_DETAIL_DECLARE(size_t, size_t);
ATOMIC_DETAIL_DECLARE(ptrdiff_t, ptrdiff_t);
ATOMIC_DETAIL_DECLARE(intmax_t, intmax_t);
ATOMIC_DETAIL_DECLARE(uintmax_t, uintmax_t);

#undef ATOMIC_DETAIL_DECLARE

typedef struct {
  unsigned char __v;
} atomic_flag;

#define ATOMIC_FLAG_INIT \
  {                      \
    0                    \
  }

static inline void atomic_thread_fence(memory_order mo)
{
  __atomic_thread_fence(atomic_detail_to_gcc_mo(mo));
}

static inline void atomic_signal_fence(memory_order mo)
{
  __atomic_signal_fence(atomic_detail_to_gcc_mo(mo));
}

#define atomic_is_lock_free(obj) \
  __atomic_is_lock_free(sizeof((obj)->__v), (const void *)&((obj)->__v))

#define atomic_init(obj, desired) ((obj)->__v = (desired))

#define atomic_store_explicit(obj, desired, order)                          \
  do {                                                                       \
    __typeof__((obj)->__v) __atomic_value = (desired);                      \
    __atomic_store(&(obj)->__v, &__atomic_value,                            \
                   atomic_detail_to_gcc_mo((order)));                       \
  } while (0)

#define atomic_store(obj, desired) \
  atomic_store_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_load_explicit(obj, order)                                     \
  __extension__({                                                             \
    __typeof__((obj)->__v) __atomic_value;                                   \
    __atomic_load(&(obj)->__v, &__atomic_value,                              \
                  atomic_detail_to_gcc_mo((order)));                         \
    __atomic_value;                                                           \
  })

#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)

#define atomic_exchange_explicit(obj, desired, order)                        \
  __atomic_exchange_n(&(obj)->__v, (desired),                                \
                      atomic_detail_to_gcc_mo((order)))

#define atomic_exchange(obj, desired) \
  atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_compare_exchange_strong_explicit(obj, expected, desired, success, \
                                                failure)                          \
  __atomic_compare_exchange_n(&(obj)->__v, (expected), (desired), 0,            \
                              atomic_detail_to_gcc_mo((success)),               \
                              atomic_detail_to_gcc_mo((failure)))

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, \
                                              failure)                          \
  __atomic_compare_exchange_n(&(obj)->__v, (expected), (desired), 1,          \
                              atomic_detail_to_gcc_mo((success)),             \
                              atomic_detail_to_gcc_mo((failure)))

#define atomic_compare_exchange_strong(obj, expected, desired)                 \
  atomic_compare_exchange_strong_explicit((obj), (expected), (desired),       \
                                          memory_order_seq_cst,                \
                                          memory_order_seq_cst)

#define atomic_compare_exchange_weak(obj, expected, desired)                   \
  atomic_compare_exchange_weak_explicit((obj), (expected), (desired),         \
                                        memory_order_seq_cst,                  \
                                        memory_order_seq_cst)

#define atomic_fetch_add_explicit(obj, operand, order)                         \
  __atomic_fetch_add(&(obj)->__v, (operand), atomic_detail_to_gcc_mo((order)))
#define atomic_fetch_sub_explicit(obj, operand, order)                         \
  __atomic_fetch_sub(&(obj)->__v, (operand), atomic_detail_to_gcc_mo((order)))
#define atomic_fetch_or_explicit(obj, operand, order)                          \
  __atomic_fetch_or(&(obj)->__v, (operand), atomic_detail_to_gcc_mo((order)))
#define atomic_fetch_xor_explicit(obj, operand, order)                         \
  __atomic_fetch_xor(&(obj)->__v, (operand), atomic_detail_to_gcc_mo((order)))
#define atomic_fetch_and_explicit(obj, operand, order)                         \
  __atomic_fetch_and(&(obj)->__v, (operand), atomic_detail_to_gcc_mo((order)))

#define atomic_fetch_add(obj, operand) \
  atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_fetch_sub(obj, operand) \
  atomic_fetch_sub_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_fetch_or(obj, operand) \
  atomic_fetch_or_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_fetch_xor(obj, operand) \
  atomic_fetch_xor_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_fetch_and(obj, operand) \
  atomic_fetch_and_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_flag_test_and_set_explicit(obj, order) \
  __atomic_test_and_set(&(obj)->__v, atomic_detail_to_gcc_mo((order)))

#define atomic_flag_test_and_set(obj) \
  atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

#define atomic_flag_clear_explicit(obj, order) \
  __atomic_clear(&(obj)->__v, atomic_detail_to_gcc_mo((order)))

#define atomic_flag_clear(obj) \
  atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#ifdef __cplusplus
}
#endif

#endif /* C99 fallback */

#endif /* PROJECT_COMPONENT_COMMON_STDATOMIC_H_ */

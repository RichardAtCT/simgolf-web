// Definitions that make Ghidra's decompiler output compile as C under
// Emscripten (wasm32: 32-bit pointers, little endian, like the original x86).
// Included first by every translated file, before the module's types.h.

#ifndef SIMGOLF_PORT_GHIDRA_PRELUDE_H
#define SIMGOLF_PORT_GHIDRA_PRELUDE_H

#include <stdint.h>

#define __cdecl
#define __stdcall
#define __thiscall
#define __fastcall

typedef uint32_t pointer32;
typedef double float10;   // x87 80-bit: double is close enough
typedef uint32_t undefined3;
typedef uint64_t undefined5;
typedef uint64_t undefined6;
typedef uint64_t undefined7;
typedef long long longlong;
typedef unsigned long long ulonglong;
typedef void code;
typedef int16_t sword;
typedef int8_t sbyte;
typedef int32_t sdword;
typedef uint64_t qword;
typedef int64_t sqword;
typedef uint32_t uint3;
typedef int32_t int3;
typedef uint64_t uint5, uint6, uint7;
typedef int64_t int5, int6, int7;
typedef uint16_t wchar16_t;
typedef uint64_t uint8;
typedef uint32_t nu;
static inline uint32_t F2U(float f) { union { float f; uint32_t u; } v = {f}; return v.u; }
static inline int32_t F2I(float f) { union { float f; int32_t i; } v = {f}; return v.i; }
static inline float I2F(int32_t i) { union { int32_t i; float f; } v = {i}; return v.f; }

// Ghidra piece access X._off_size_
#define PIECE(x, off, size) (*(PIECE_T##size *)((char *)&(x) + (off)))
typedef uint8_t PIECE_T1;
typedef uint16_t PIECE_T2;
typedef uint32_t PIECE_T3;
typedef uint32_t PIECE_T4;
typedef uint64_t PIECE_T8;

// CONCATxy(a, b): a in the high x bytes, b in the low y bytes.
#define CC_MASK(y) ((y) >= 8 ? ~0ull : ((1ull << (8 * (y))) - 1))
#define CC(x, y, a, b) ((((uint64_t)(a) & CC_MASK(x)) << (8 * (y))) | ((uint64_t)(b) & CC_MASK(y)))
#define CONCAT11(a, b) ((uint16_t)CC(1, 1, a, b))
#define CONCAT12(a, b) ((uint32_t)CC(1, 2, a, b))
#define CONCAT13(a, b) ((uint32_t)CC(1, 3, a, b))
#define CONCAT14(a, b) ((uint64_t)CC(1, 4, a, b))
#define CONCAT21(a, b) ((uint32_t)CC(2, 1, a, b))
#define CONCAT22(a, b) ((uint32_t)CC(2, 2, a, b))
#define CONCAT24(a, b) ((uint64_t)CC(2, 4, a, b))
#define CONCAT26(a, b) ((uint64_t)CC(2, 6, a, b))
#define CONCAT31(a, b) ((uint32_t)CC(3, 1, a, b))
#define CONCAT34(a, b) ((uint64_t)CC(3, 4, a, b))
#define CONCAT35(a, b) ((uint64_t)CC(3, 5, a, b))
#define CONCAT42(a, b) ((uint64_t)CC(4, 2, a, b))
#define CONCAT43(a, b) ((uint64_t)CC(4, 3, a, b))
#define CONCAT44(a, b) ((uint64_t)CC(4, 4, a, b))
#define CONCAT51(a, b) ((uint64_t)CC(5, 1, a, b))
#define CONCAT52(a, b) ((uint64_t)CC(5, 2, a, b))
#define CONCAT53(a, b) ((uint64_t)CC(5, 3, a, b))
#define CONCAT62(a, b) ((uint64_t)CC(6, 2, a, b))
#define CONCAT71(a, b) ((uint64_t)CC(7, 1, a, b))

// SUBxy(v, off): y bytes of an x-byte value starting at byte off.
#define SUB21(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB41(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB42(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB81(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB82(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB84(v, o) ((uint32_t)((uint64_t)(v) >> (8 * (o))))
#define SUB31(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB32(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB43(v, o) ((uint32_t)(((uint64_t)(v) >> (8 * (o))) & 0xffffff))

#define ZEXT12(v) ((uint16_t)(uint8_t)(v))
#define ZEXT14(v) ((uint32_t)(uint8_t)(v))
#define ZEXT18(v) ((uint64_t)(uint8_t)(v))
#define ZEXT24(v) ((uint32_t)(uint16_t)(v))
#define ZEXT28(v) ((uint64_t)(uint16_t)(v))
#define ZEXT48(v) ((uint64_t)(uint32_t)(v))
#define ZEXT34(v) ((uint32_t)(v) & 0xffffff)
#define SEXT12(v) ((int16_t)(int8_t)(v))
#define SEXT14(v) ((int32_t)(int8_t)(v))
#define SEXT18(v) ((int64_t)(int8_t)(v))
#define SEXT24(v) ((int32_t)(int16_t)(v))
#define SEXT28(v) ((int64_t)(int16_t)(v))
#define SEXT48(v) ((int64_t)(int32_t)(v))

#define CARRY1(a, b) ((uint8_t)((uint8_t)(a) + (uint8_t)(b)) < (uint8_t)(a))
#define CARRY2(a, b) ((uint16_t)((uint16_t)(a) + (uint16_t)(b)) < (uint16_t)(a))
#define CARRY4(a, b) ((uint32_t)((uint32_t)(a) + (uint32_t)(b)) < (uint32_t)(a))
#define SCARRY1(a, b) (((int8_t)(a) + (int)(int8_t)(b)) != (int8_t)((int8_t)(a) + (int8_t)(b)))
#define SCARRY2(a, b) (((int16_t)(a) + (int)(int16_t)(b)) != (int16_t)((int16_t)(a) + (int16_t)(b)))
#define SCARRY4(a, b) (((int64_t)(int32_t)(a) + (int32_t)(b)) != (int32_t)((uint32_t)(a) + (uint32_t)(b)))
#define SBORROW1(a, b) (((int8_t)(a) - (int)(int8_t)(b)) != (int8_t)((int8_t)(a) - (int8_t)(b)))
#define SBORROW2(a, b) (((int16_t)(a) - (int)(int16_t)(b)) != (int16_t)((int16_t)(a) - (int16_t)(b)))
#define SBORROW4(a, b) (((int64_t)(int32_t)(a) - (int32_t)(b)) != (int32_t)((uint32_t)(a) - (uint32_t)(b)))
#define POPCOUNT(x) __builtin_popcount((uint32_t)(x))
#define LZCOUNT(x) __builtin_clz((uint32_t)(x))
#define ROUND(x) __builtin_rint(x)
#define TRUNC(x) __builtin_trunc(x)
#define SQRT(x) __builtin_sqrt(x)
#define ABS(x) __builtin_fabs(x)
#define NAN(x) __builtin_isnan(x)
// x87 transcendental instructions Ghidra prints as calls
#define fsin(x) __builtin_sin(x)
#define fcos(x) __builtin_cos(x)
#define fsqrt(x) __builtin_sqrt(x)
#define fpatan(y, x) __builtin_atan2(y, x)
#define fptan(x) __builtin_tan(x)
#define frndint(x) __builtin_rint(x)
#define LOCK()
#define UNLOCK()
#define halt_baddata() port_halt(__func__, "baddata")
#define halt_unimplemented() port_halt(__func__, "unimplemented")
#define software_interrupt(n) (port_halt(__func__, "int"), 0)
#define swi(n) (port_halt(__func__, "swi"), 0)
#ifndef true
#define true 1
#define false 0
#endif
typedef uint8_t BADSPACEBASE;

// SEH frame chain: the translated code links frames here; nothing unwinds.
extern void *ExceptionList;

// Indirect calls. Code addresses in the image (vtables, callbacks) stay as
// original addresses; icall_lookup maps them to wrappers that take eight
// 32-bit argument slots, which is how the x86 stack passed them.
#define ICALL_PARAMS uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15
typedef uint32_t (*IcallFn)(ICALL_PARAMS);
typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } IcallEntry;  // flags 1: __thiscall
uint32_t icall_this(uint32_t fn, uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
  uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
  uint32_t a13, uint32_t a14);
IcallFn icall_lookup(uint32_t addr);
extern double g_fret;     // ST0 return of a float function called indirectly
extern uint32_t g_edx;    // high half of a 64-bit return

static inline uint32_t argu_u(uint32_t x) { return x; }
static inline uint32_t argu_f(float x) { union { float f; uint32_t u; } v = {x}; return v.u; }
#define ARGU(x) _Generic((x), float: argu_f, double: argu_f, long double: argu_f, \
  default: argu_u)(_Generic((x), float: (x), double: (x), long double: (x), default: (uint32_t)(uintptr_t)(x)))
#define ICALL_FN(f) icall_lookup((uint32_t)(uintptr_t)(f))
#define ICALL0(fn_) ICALL_FN(fn_)(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL1(fn_, a) ICALL_FN(fn_)(ARGU(a), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL2(fn_, a, b) ICALL_FN(fn_)(ARGU(a), ARGU(b), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL3(fn_, a, b, c) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL4(fn_, a, b, c, d) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL5(fn_, a, b, c, d, e) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL6(fn_, a, b, c, d, e, f) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL7(fn_, a, b, c, d, e, f, g) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL8(fn_, a, b, c, d, e, f, g, h) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL9(fn_, a, b, c, d, e, f, g, h, i) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), 0, 0, 0, 0, 0, 0, 0)
#define ICALL10(fn_, a, b, c, d, e, f, g, h, i, j) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), 0, 0, 0, 0, 0, 0)
#define ICALL11(fn_, a, b, c, d, e, f, g, h, i, j, k) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), 0, 0, 0, 0, 0)
#define ICALL12(fn_, a, b, c, d, e, f, g, h, i, j, k, l) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), 0, 0, 0, 0)
#define ICALL13(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), 0, 0, 0)
#define ICALL14(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), 0, 0)
#define ICALL15(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o), 0)
#define ICALL16(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o), ARGU(p))
#define ICALLT0(fn_, obj_) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT1(fn_, obj_, a) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT2(fn_, obj_, a, b) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT3(fn_, obj_, a, b, c) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT4(fn_, obj_, a, b, c, d) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT5(fn_, obj_, a, b, c, d, e) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT6(fn_, obj_, a, b, c, d, e, f) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT7(fn_, obj_, a, b, c, d, e, f, g) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT8(fn_, obj_, a, b, c, d, e, f, g, h) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), 0, 0, 0, 0, 0, 0, 0)
#define ICALLT9(fn_, obj_, a, b, c, d, e, f, g, h, i) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), 0, 0, 0, 0, 0, 0)
#define ICALLT10(fn_, obj_, a, b, c, d, e, f, g, h, i, j) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), 0, 0, 0, 0, 0)
#define ICALLT11(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), 0, 0, 0, 0)
#define ICALLT12(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), 0, 0, 0)
#define ICALLT13(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), 0, 0)
#define ICALLT14(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m, n) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), 0)
#define ICALLT15(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o))

static inline float U2F(uint32_t u) { union { uint32_t u; float f; } v = {u}; return v.f; }
static inline double U2D(uint32_t lo, uint32_t hi) {
  union { uint64_t u; double d; } v = {((uint64_t)hi << 32) | lo}; return v.d;
}
#define U2Q(lo, hi) (((uint64_t)(hi) << 32) | (uint32_t)(lo))

void port_halt(const char *fn, const char *why);

void port_ret_log(uint32_t fn, int where, uint32_t value);
void port_trace_dump(int n);
void port_debug4(const char *what, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
#define PORT_RET(fn, where, x) ({ __typeof__(x) r_ = (x); \
  port_ret_log((fn), (where), (uint32_t)(uintptr_t)(r_)); r_; })

// Ring buffer of the last translated functions entered (port_trace_dump()).
extern uint32_t port_trace_buf[1024];
extern uint32_t port_trace_pos;
// Short loops (A B A B ...) are folded so the buffer keeps history.
#define PORT_TRACE(addr) do { uint32_t a_ = (addr); \
  if (port_trace_buf[(port_trace_pos - 1) & 1023] != a_ && port_trace_buf[(port_trace_pos - 2) & 1023] != a_) \
    port_trace_buf[port_trace_pos++ & 1023] = a_; } while (0)

#endif  // SIMGOLF_PORT_GHIDRA_PRELUDE_H

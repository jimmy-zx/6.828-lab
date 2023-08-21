#ifndef JOS_INC_MATH_H
#define JOS_INC_MATH_H

#include <inc/types.h>

/* Work derived from the musl libc.
 * COPYRIGHT: https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
 * 
 * https://git.musl-libc.org/cgit/musl/tree/include/math.h
 */

static __inline unsigned
__FLOAT_BITS(float __f)
{
	union {float __f; unsigned __i;} __u;
	__u.__f = __f;
	return __u.__i;
}
static __inline unsigned long long __DOUBLE_BITS(double __f)
{
	union {double __f; unsigned long long __i;} __u;
	__u.__f = __f;
	return __u.__i;
}


#define signbit(x) ( \
	sizeof(x) == sizeof(float) ? (int)(__FLOAT_BITS(x)>>31) : \
		(int)(__DOUBLE_BITS(x)>>63))

static __inline unsigned long long
_ipow(unsigned long long base, unsigned long long exp) {
	unsigned long long result = 1;
	for (; exp; exp >>= 1, base *= base) {
		if (exp & 1) {
			result *= base;
		}
	}
	return result;
}


#endif /* not JOS_INC_MATH_H */

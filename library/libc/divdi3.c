/*
 * 64-bit integer division and modulo helpers (__udivdi3, __umoddi3,
 * __divdi3, __moddi3) for 32-bit x86 guest programs and in-guest TCC builds.
 *
 * Implemented using bitwise shift-subtract so GCC -m32 does not emit
 * recursive calls to libgcc's __udivdi3/__umoddi3.
 */

typedef unsigned long long u64;
typedef long long s64;

static u64 udivmod64(u64 num, u64 den, u64 *rem_out)
{
	u64 quot = 0, qbit = 1;

	if (den == 0) {
		if (rem_out)
			*rem_out = 0;
		return 0;
	}

	while ((s64)den >= 0 && den < num) {
		den <<= 1;
		qbit <<= 1;
	}

	while (qbit) {
		if (den <= num) {
			num -= den;
			quot += qbit;
		}
		den >>= 1;
		qbit >>= 1;
	}

	if (rem_out)
		*rem_out = num;
	return quot;
}

u64 __udivdi3(u64 num, u64 den)
{
	return udivmod64(num, den, (u64 *)0);
}

u64 __umoddi3(u64 num, u64 den)
{
	u64 rem = 0;
	udivmod64(num, den, &rem);
	return rem;
}

s64 __divdi3(s64 a, s64 b)
{
	int neg = 0;
	u64 ua, ub, q;

	if (a < 0) {
		ua = (u64)(-a);
		neg = !neg;
	} else {
		ua = (u64)a;
	}
	if (b < 0) {
		ub = (u64)(-b);
		neg = !neg;
	} else {
		ub = (u64)b;
	}
	q = udivmod64(ua, ub, (u64 *)0);
	return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 a, s64 b)
{
	int neg = 0;
	u64 ua, ub, rem = 0;

	if (a < 0) {
		ua = (u64)(-a);
		neg = 1;
	} else {
		ua = (u64)a;
	}
	if (b < 0)
		ub = (u64)(-b);
	else
		ub = (u64)b;
	udivmod64(ua, ub, &rem);
	return neg ? -(s64)rem : (s64)rem;
}

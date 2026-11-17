#include "softfloat.h"

#define F_SIGN 0x80000000u
#define F_INF 0x7F800000u
#define F_QNAN 0x7FC00000u
#define D_SIGN 0x8000000000000000ull
#define D_INF 0x7FF0000000000000ull
#define D_QNAN 0x7FF8000000000000ull

static inline int clz32(u32 x)
{
	return x ? __builtin_clz(x) : 32;
}

static inline int clz64(u64 x)
{
	u32 hi = (u32)(x >> 32);
	return hi ? __builtin_clz(hi) : 32 + clz32((u32)x);
}

static u32 round_pack_f(u32 sign, s32 e, u32 m)
{
	if (e >= 255)
		return sign | F_INF;
	if (e <= 0)
	{
		u32 sh = (u32)(1 - e);
		m = sh >= 32 ? (m != 0) : (m >> sh) | ((m << (32 - sh)) != 0);
		e = 0;
	}
	u32 rem = m & 0xFF;
	m >>= 8;
	if (rem > 0x80 || (rem == 0x80 && (m & 1)))
		m++;
	return sign | (e ? ((u32)(e - 1) << 23) + m : m);
}

static int unpack_f(u32 a, s32 *e, u32 *m)
{
	u32 ex = a >> 23 & 0xFF, fr = a & 0x7FFFFF;
	if (ex == 0)
	{
		if (!fr)
			return 0;
		int s = clz32(fr);
		*m = fr << s;
		*e = 9 - s;
		return 1;
	}
	*m = (fr | 0x800000) << 8;
	*e = (s32)ex;
	return 1;
}

static inline int is_nan_f(u32 a)
{
	return (a & 0x7FFFFFFF) > F_INF;
}

u32 __aeabi_fadd(u32 a, u32 b)
{
	if (is_nan_f(a) || is_nan_f(b))
		return F_QNAN;
	u32 ia = a & 0x7FFFFFFF, ib = b & 0x7FFFFFFF;
	if (ia == F_INF || ib == F_INF)
	{
		if (ia == F_INF && ib == F_INF && ((a ^ b) & F_SIGN))
			return F_QNAN;
		return ia == F_INF ? a : b;
	}
	if (!ia && !ib)
		return a & b & F_SIGN;
	if (!ia)
		return b;
	if (!ib)
		return a;
	s32 ea, eb;
	u32 ma, mb;
	unpack_f(a, &ea, &ma);
	unpack_f(b, &eb, &mb);
	u32 sa = a & F_SIGN, sb = b & F_SIGN;
	if (ea < eb || (ea == eb && ma < mb))
	{
		s32 te = ea; ea = eb; eb = te;
		u32 tm = ma; ma = mb; mb = tm;
		u32 ts = sa; sa = sb; sb = ts;
	}
	u32 d = (u32)(ea - eb);
	mb = d >= 32 ? (mb != 0) : d ? (mb >> d) | ((mb << (32 - d)) != 0) : mb;
	if (sa == sb)
	{
		u32 m = ma + mb;
		if (m < ma)
			m = (m >> 1) | (m & 1) | 0x80000000u, ea++;
		return round_pack_f(sa, ea, m);
	}
	u32 m = ma - mb;
	if (!m)
		return 0;
	int s = clz32(m);
	return round_pack_f(sa, ea - s, m << s);
}

u32 __aeabi_fsub(u32 a, u32 b)
{
	return __aeabi_fadd(a, b ^ F_SIGN);
}

u32 __aeabi_frsub(u32 a, u32 b)
{
	return __aeabi_fadd(b, a ^ F_SIGN);
}

u32 __aeabi_fmul(u32 a, u32 b)
{
	u32 sign = (a ^ b) & F_SIGN;
	if (is_nan_f(a) || is_nan_f(b))
		return F_QNAN;
	u32 ia = a & 0x7FFFFFFF, ib = b & 0x7FFFFFFF;
	if (ia == F_INF || ib == F_INF)
		return (!ia || !ib) ? F_QNAN : sign | F_INF;
	if (!ia || !ib)
		return sign;
	s32 ea, eb;
	u32 ma, mb;
	unpack_f(a, &ea, &ma);
	unpack_f(b, &eb, &mb);
	u64 p = (u64)ma * mb;
	s32 e = ea + eb - 127;
	u32 hi = (u32)(p >> 32), lo = (u32)p;
	if (!(hi & 0x80000000u))
	{
		hi = hi << 1 | lo >> 31;
		lo <<= 1;
		e--;
	}
	e++;
	return round_pack_f(sign, e, hi | (lo != 0));
}

u32 __aeabi_fdiv(u32 a, u32 b)
{
	u32 sign = (a ^ b) & F_SIGN;
	if (is_nan_f(a) || is_nan_f(b))
		return F_QNAN;
	u32 ia = a & 0x7FFFFFFF, ib = b & 0x7FFFFFFF;
	if (ia == F_INF)
		return ib == F_INF ? F_QNAN : sign | F_INF;
	if (ib == F_INF)
		return sign;
	if (!ib)
		return ia ? sign | F_INF : F_QNAN;
	if (!ia)
		return sign;
	s32 ea, eb;
	u32 ma, mb;
	unpack_f(a, &ea, &ma);
	unpack_f(b, &eb, &mb);
	ma >>= 8;
	mb >>= 8;
	s32 e = ea - eb + 127;
	if (ma < mb)
		ma <<= 1, e--;
	u32 q = 0;
	for (int i = 0; i < 27; i++)
	{
		q <<= 1;
		if (ma >= mb)
			ma -= mb, q |= 1;
		ma <<= 1;
	}
	return round_pack_f(sign, e, q << 5 | (ma != 0));
}

static int cmp_f(u32 a, u32 b)
{
	if (is_nan_f(a) || is_nan_f(b))
		return 2;
	if (!((a | b) & 0x7FFFFFFF))
		return 0;
	s32 x = (s32)a, y = (s32)b;
	if (x < 0)
		x = (s32)(0x80000000u - (u32)x);
	if (y < 0)
		y = (s32)(0x80000000u - (u32)y);
	return x < y ? -1 : x > y;
}

int __aeabi_fcmpeq(u32 a, u32 b) { return cmp_f(a, b) == 0; }
int __aeabi_fcmplt(u32 a, u32 b) { return cmp_f(a, b) == -1; }
int __aeabi_fcmple(u32 a, u32 b) { int c = cmp_f(a, b); return c == -1 || c == 0; }
int __aeabi_fcmpge(u32 a, u32 b) { int c = cmp_f(a, b); return c == 1 || c == 0; }
int __aeabi_fcmpgt(u32 a, u32 b) { return cmp_f(a, b) == 1; }
int __aeabi_fcmpun(u32 a, u32 b) { return cmp_f(a, b) == 2; }

u32 __aeabi_ui2f(u32 v)
{
	if (!v)
		return 0;
	int s = clz32(v);
	return round_pack_f(0, 158 - s, v << s);
}

u32 __aeabi_i2f(s32 v)
{
	if (v >= 0)
		return __aeabi_ui2f((u32)v);
	return F_SIGN | __aeabi_ui2f(0u - (u32)v);
}

u32 __aeabi_ul2f(u64 v)
{
	if (!v)
		return 0;
	int s = clz64(v);
	v <<= s;
	return round_pack_f(0, 190 - s, (u32)(v >> 32) | ((u32)v != 0));
}

u32 __aeabi_l2f(s64 v)
{
	if (v >= 0)
		return __aeabi_ul2f((u64)v);
	return F_SIGN | __aeabi_ul2f(0ull - (u64)v);
}

static u64 f2u64(u32 a, int *neg)
{
	*neg = (int)(a >> 31);
	s32 e = (s32)(a >> 23 & 0xFF) - 127;
	if (e < 0)
		return 0;
	if (e >= 64)
		return ~0ull;
	u64 m = (a & 0x7FFFFF) | 0x800000;
	return e >= 23 ? m << (e - 23) : m >> (23 - e);
}

s32 __aeabi_f2iz(u32 a)
{
	if (is_nan_f(a))
		return 0;
	int neg;
	u64 v = f2u64(a, &neg);
	if (neg)
		return v > 0x80000000ull ? (s32)0x80000000u : (s32)(0u - (u32)v);
	return v > 0x7FFFFFFFull ? 0x7FFFFFFF : (s32)v;
}

u32 __aeabi_f2uiz(u32 a)
{
	if (is_nan_f(a))
		return 0;
	int neg;
	u64 v = f2u64(a, &neg);
	if (neg)
		return 0;
	return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)v;
}

s64 __aeabi_f2lz(u32 a)
{
	if (is_nan_f(a))
		return 0;
	int neg;
	u64 v = f2u64(a, &neg);
	if (neg)
		return v > 0x8000000000000000ull ? (s64)0x8000000000000000ull : (s64)(0ull - v);
	return v > 0x7FFFFFFFFFFFFFFFull ? 0x7FFFFFFFFFFFFFFFll : (s64)v;
}

u64 __aeabi_f2ulz(u32 a)
{
	if (is_nan_f(a))
		return 0;
	int neg;
	u64 v = f2u64(a, &neg);
	return neg ? 0 : v;
}

static u64 round_pack_d(u64 sign, s32 e, u64 m)
{
	if (e >= 2047)
		return sign | D_INF;
	if (e <= 0)
	{
		u32 sh = (u32)(1 - e);
		m = sh >= 64 ? (m != 0) : (m >> sh) | ((m << (64 - sh)) != 0);
		e = 0;
	}
	u32 rem = (u32)m & 0x7FF;
	m >>= 11;
	if (rem > 0x400 || (rem == 0x400 && (m & 1)))
		m++;
	return sign | (e ? ((u64)(e - 1) << 52) + m : m);
}

static inline int is_nan_d(u64 a)
{
	return (a & 0x7FFFFFFFFFFFFFFFull) > D_INF;
}

static int unpack_d(u64 a, s32 *e, u64 *m)
{
	u32 ex = (u32)(a >> 52) & 0x7FF;
	u64 fr = a & 0xFFFFFFFFFFFFFull;
	if (ex == 0)
	{
		if (!fr)
			return 0;
		int s = clz64(fr);
		*m = fr << s;
		*e = 12 - s;
		return 1;
	}
	*m = (fr | 0x10000000000000ull) << 11;
	*e = (s32)ex;
	return 1;
}

u64 __aeabi_dadd(u64 a, u64 b)
{
	if (is_nan_d(a) || is_nan_d(b))
		return D_QNAN;
	u64 ia = a & ~D_SIGN, ib = b & ~D_SIGN;
	if (ia == D_INF || ib == D_INF)
	{
		if (ia == D_INF && ib == D_INF && ((a ^ b) & D_SIGN))
			return D_QNAN;
		return ia == D_INF ? a : b;
	}
	if (!ia && !ib)
		return a & b & D_SIGN;
	if (!ia)
		return b;
	if (!ib)
		return a;
	s32 ea, eb;
	u64 ma, mb;
	unpack_d(a, &ea, &ma);
	unpack_d(b, &eb, &mb);
	u64 sa = a & D_SIGN, sb = b & D_SIGN;
	if (ea < eb || (ea == eb && ma < mb))
	{
		s32 te = ea; ea = eb; eb = te;
		u64 tm = ma; ma = mb; mb = tm;
		u64 ts = sa; sa = sb; sb = ts;
	}
	u32 d = (u32)(ea - eb);
	mb = d >= 64 ? (mb != 0) : d ? (mb >> d) | ((mb << (64 - d)) != 0) : mb;
	if (sa == sb)
	{
		u64 m = ma + mb;
		if (m < ma)
			m = (m >> 1) | (m & 1) | 0x8000000000000000ull, ea++;
		return round_pack_d(sa, ea, m);
	}
	u64 m = ma - mb;
	if (!m)
		return 0;
	int s = clz64(m);
	return round_pack_d(sa, ea - s, m << s);
}

u64 __aeabi_dsub(u64 a, u64 b)
{
	return __aeabi_dadd(a, b ^ D_SIGN);
}

u64 __aeabi_drsub(u64 a, u64 b)
{
	return __aeabi_dadd(b, a ^ D_SIGN);
}

u64 __aeabi_dmul(u64 a, u64 b)
{
	u64 sign = (a ^ b) & D_SIGN;
	if (is_nan_d(a) || is_nan_d(b))
		return D_QNAN;
	u64 ia = a & ~D_SIGN, ib = b & ~D_SIGN;
	if (ia == D_INF || ib == D_INF)
		return (!ia || !ib) ? D_QNAN : sign | D_INF;
	if (!ia || !ib)
		return sign;
	s32 ea, eb;
	u64 ma, mb;
	unpack_d(a, &ea, &ma);
	unpack_d(b, &eb, &mb);
	u32 a1 = (u32)(ma >> 32), a0 = (u32)ma, b1 = (u32)(mb >> 32), b0 = (u32)mb;
	u64 p00 = (u64)a0 * b0, p01 = (u64)a0 * b1, p10 = (u64)a1 * b0, p11 = (u64)a1 * b1;
	u64 mid = (p00 >> 32) + (u32)p01 + (u32)p10;
	u64 hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
	u64 lo = (mid << 32) | (u32)p00;
	s32 e = ea + eb - 1023;
	if (!(hi & 0x8000000000000000ull))
	{
		hi = hi << 1 | lo >> 63;
		lo <<= 1;
		e--;
	}
	e++;
	return round_pack_d(sign, e, hi | (lo != 0));
}

u64 __aeabi_ddiv(u64 a, u64 b)
{
	u64 sign = (a ^ b) & D_SIGN;
	if (is_nan_d(a) || is_nan_d(b))
		return D_QNAN;
	u64 ia = a & ~D_SIGN, ib = b & ~D_SIGN;
	if (ia == D_INF)
		return ib == D_INF ? D_QNAN : sign | D_INF;
	if (ib == D_INF)
		return sign;
	if (!ib)
		return ia ? sign | D_INF : D_QNAN;
	if (!ia)
		return sign;
	s32 ea, eb;
	u64 ma, mb;
	unpack_d(a, &ea, &ma);
	unpack_d(b, &eb, &mb);
	ma >>= 11;
	mb >>= 11;
	s32 e = ea - eb + 1023;
	if (ma < mb)
		ma <<= 1, e--;
	u64 q = 0;
	for (int i = 0; i < 56; i++)
	{
		q <<= 1;
		if (ma >= mb)
			ma -= mb, q |= 1;
		ma <<= 1;
	}
	return round_pack_d(sign, e, q << 8 | (ma != 0));
}

static int cmp_d(u64 a, u64 b)
{
	if (is_nan_d(a) || is_nan_d(b))
		return 2;
	if (!((a | b) & ~D_SIGN))
		return 0;
	s64 x = (s64)a, y = (s64)b;
	if (x < 0)
		x = (s64)(D_SIGN - (u64)x);
	if (y < 0)
		y = (s64)(D_SIGN - (u64)y);
	return x < y ? -1 : x > y;
}

int __aeabi_dcmpeq(u64 a, u64 b) { return cmp_d(a, b) == 0; }
int __aeabi_dcmplt(u64 a, u64 b) { return cmp_d(a, b) == -1; }
int __aeabi_dcmple(u64 a, u64 b) { int c = cmp_d(a, b); return c == -1 || c == 0; }
int __aeabi_dcmpge(u64 a, u64 b) { int c = cmp_d(a, b); return c == 1 || c == 0; }
int __aeabi_dcmpgt(u64 a, u64 b) { return cmp_d(a, b) == 1; }
int __aeabi_dcmpun(u64 a, u64 b) { return cmp_d(a, b) == 2; }

u64 __aeabi_ul2d(u64 v)
{
	if (!v)
		return 0;
	int s = clz64(v);
	return round_pack_d(0, 1086 - s, v << s);
}

u64 __aeabi_l2d(s64 v)
{
	if (v >= 0)
		return __aeabi_ul2d((u64)v);
	return D_SIGN | __aeabi_ul2d(0ull - (u64)v);
}

u64 __aeabi_ui2d(u32 v)
{
	return __aeabi_ul2d(v);
}

u64 __aeabi_i2d(s32 v)
{
	return __aeabi_l2d(v);
}

u64 __aeabi_f2d(u32 a)
{
	u64 sign = (u64)(a & F_SIGN) << 32;
	if (is_nan_f(a))
		return D_QNAN;
	u32 ia = a & 0x7FFFFFFF;
	if (ia == F_INF)
		return sign | D_INF;
	if (!ia)
		return sign;
	s32 e;
	u32 m;
	unpack_f(a, &e, &m);
	return sign | ((u64)(e + 896) << 52) + (((u64)m << 32 & 0x7FFFFFFFFFFFFFFFull) >> 11);
}

u32 __aeabi_d2f(u64 a)
{
	u32 sign = (u32)(a >> 32) & F_SIGN;
	if (is_nan_d(a))
		return F_QNAN;
	u64 ia = a & ~D_SIGN;
	if (ia == D_INF)
		return sign | F_INF;
	if (!ia)
		return sign;
	s32 e;
	u64 m;
	unpack_d(a, &e, &m);
	return round_pack_f(sign, e - 896, (u32)(m >> 32) | ((u32)m != 0));
}

static u64 d2u64(u64 a, int *neg)
{
	*neg = (int)(a >> 63);
	s32 e = (s32)(a >> 52 & 0x7FF) - 1023;
	if (e < 0)
		return 0;
	if (e >= 64)
		return ~0ull;
	u64 m = (a & 0xFFFFFFFFFFFFFull) | 0x10000000000000ull;
	return e >= 52 ? m << (e - 52) : m >> (52 - e);
}

s32 __aeabi_d2iz(u64 a)
{
	if (is_nan_d(a))
		return 0;
	int neg;
	u64 v = d2u64(a, &neg);
	if (neg)
		return v > 0x80000000ull ? (s32)0x80000000u : (s32)(0u - (u32)v);
	return v > 0x7FFFFFFFull ? 0x7FFFFFFF : (s32)v;
}

u32 __aeabi_d2uiz(u64 a)
{
	if (is_nan_d(a))
		return 0;
	int neg;
	u64 v = d2u64(a, &neg);
	if (neg)
		return 0;
	return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)v;
}

s64 __aeabi_d2lz(u64 a)
{
	if (is_nan_d(a))
		return 0;
	int neg;
	u64 v = d2u64(a, &neg);
	if (neg)
		return v > 0x8000000000000000ull ? (s64)0x8000000000000000ull : (s64)(0ull - v);
	return v > 0x7FFFFFFFFFFFFFFFull ? 0x7FFFFFFFFFFFFFFFll : (s64)v;
}

u64 __aeabi_d2ulz(u64 a)
{
	if (is_nan_d(a))
		return 0;
	int neg;
	u64 v = d2u64(a, &neg);
	return neg ? 0 : v;
}

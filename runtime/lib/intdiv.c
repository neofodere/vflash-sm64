typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef long long s64;

u32 udivmod32(u32 n, u32 d, u32 *r)
{
	if (!d)
	{
		*r = n;
		return 0xFFFFFFFFu;
	}
	if (n < d)
	{
		*r = n;
		return 0;
	}
	int sh = __builtin_clz(d) - __builtin_clz(n);
	u32 q = 0;
	d <<= sh;
	for (int i = 0; i <= sh; i++)
	{
		q <<= 1;
		if (n >= d)
		{
			n -= d;
			q |= 1;
		}
		d >>= 1;
	}
	*r = n;
	return q;
}

s32 idivmod_core(s32 n, s32 d, s32 *r)
{
	u32 ur;
	u32 un = n < 0 ? 0u - (u32)n : (u32)n;
	u32 ud = d < 0 ? 0u - (u32)d : (u32)d;
	u32 q = udivmod32(un, ud, &ur);
	*r = n < 0 ? (s32)(0u - ur) : (s32)ur;
	return (n < 0) != (d < 0) ? (s32)(0u - q) : (s32)q;
}

u32 __aeabi_uidiv(u32 n, u32 d)
{
	u32 r;
	return udivmod32(n, d, &r);
}

s32 __aeabi_idiv(s32 n, s32 d)
{
	s32 r;
	return idivmod_core(n, d, &r);
}

u64 uldivmod_core(u64 n, u64 d, u64 *r)
{
	if (!d)
	{
		*r = n;
		return ~0ull;
	}
	if (n < d)
	{
		*r = n;
		return 0;
	}
	if (!(n >> 32))
	{
		u32 r32;
		u32 q = udivmod32((u32)n, (u32)d, &r32);
		*r = r32;
		return q;
	}
	u32 hi = (u32)(n >> 32);
	int cn = hi ? __builtin_clz(hi) : 32 + __builtin_clz((u32)n);
	hi = (u32)(d >> 32);
	int cd = hi ? __builtin_clz(hi) : 32 + __builtin_clz((u32)d);
	int sh = cd - cn;
	u64 q = 0;
	d <<= sh;
	for (int i = 0; i <= sh; i++)
	{
		q <<= 1;
		if (n >= d)
		{
			n -= d;
			q |= 1;
		}
		d >>= 1;
	}
	*r = n;
	return q;
}

s64 ldivmod_core(s64 n, s64 d, s64 *r)
{
	u64 ur;
	u64 un = n < 0 ? 0ull - (u64)n : (u64)n;
	u64 ud = d < 0 ? 0ull - (u64)d : (u64)d;
	u64 q = uldivmod_core(un, ud, &ur);
	*r = n < 0 ? (s64)(0ull - ur) : (s64)ur;
	return (n < 0) != (d < 0) ? (s64)(0ull - q) : (s64)q;
}

void __aeabi_idiv0(void)
{
}

void __aeabi_ldiv0(void)
{
}

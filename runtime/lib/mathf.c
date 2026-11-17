typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;

static float f_from_bits(u32 v)
{
	union { u32 u; float f; } c = { v };
	return c.f;
}

static u32 f_bits(float f)
{
	union { u32 u; float f; } c;
	c.f = f;
	return c.u;
}

float fabsf(float x)
{
	return f_from_bits(f_bits(x) & 0x7FFFFFFFu);
}

float sqrtf(float x)
{
	u32 b = f_bits(x);
	if (!(b & 0x7FFFFFFFu))
		return x;
	if (b >> 31)
		return f_from_bits(0x7FC00000u);
	if (b >= 0x7F800000u)
		return x;
	s32 e = (s32)(b >> 23);
	u32 m = b & 0x7FFFFFu;
	if (e)
		m |= 0x800000u;
	else
	{
		int s = __builtin_clz(m) - 8;
		m <<= s;
		e = 1 - s;
	}
	e -= 127;
	u64 v = (u64)m << 23;
	if (e & 1)
		v <<= 1;
	e >>= 1;
	u64 r = 0, bit = 1ull << 46;
	while (bit > v)
		bit >>= 2;
	while (bit)
	{
		if (v >= r + bit)
		{
			v -= r + bit;
			r = (r >> 1) + bit;
		}
		else
			r >>= 1;
		bit >>= 2;
	}
	u32 q = (u32)r;
	if (v > r)
		q++;
	if (q >= 0x1000000u)
		q >>= 1, e++;
	return f_from_bits((u32)(e + 127) << 23 | (q & 0x7FFFFFu));
}

static float sin_poly(float z)
{
	float zz = z * z;
	return z + z * zz * ((-1.9515295891E-4f * zz + 8.3321608736E-3f) * zz - 1.6666654611E-1f);
}

static float cos_poly(float z)
{
	float zz = z * z;
	return 1.0f - 0.5f * zz + zz * zz * ((2.443315711809948E-5f * zz - 1.388731625493765E-3f) * zz + 4.166664568298827E-2f);
}

static float trig(float x, int cosine)
{
	int sign = 0;
	if (x < 0.0f)
	{
		x = -x;
		sign = !cosine;
	}
	if (x > 8192.0f)
		return 0.0f;
	int j = (int)(x * 1.27323954473516f);
	float y = (float)j;
	if (j & 1)
	{
		j++;
		y += 1.0f;
	}
	float z = ((x - y * 0.78515625f) - y * 2.4187564849853515625e-4f) - y * 3.77489497744594108e-8f;
	j &= 7;
	if (cosine)
	{
		if (j > 3)
			j -= 4, sign ^= 1;
		if (j > 1)
			sign ^= 1;
		float r = (j == 1 || j == 2) ? sin_poly(z) : cos_poly(z);
		return sign ? -r : r;
	}
	if (j > 3)
		sign ^= 1, j -= 4;
	float r = (j == 1 || j == 2) ? cos_poly(z) : sin_poly(z);
	return sign ? -r : r;
}

float sinf(float x)
{
	return trig(x, 0);
}

float cosf(float x)
{
	return trig(x, 1);
}

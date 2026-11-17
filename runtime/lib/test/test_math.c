#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

uint32_t udivmod32(uint32_t, uint32_t, uint32_t *);
int32_t idivmod_core(int32_t, int32_t, int32_t *);
uint64_t uldivmod_core(uint64_t, uint64_t, uint64_t *);
int64_t ldivmod_core(int64_t, int64_t, int64_t *);
float m_sqrtf(float);
float m_sinf(float);
float m_cosf(float);

static uint64_t rng = 0x9E3779B97F4A7C15ull;

static uint64_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

static long fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

static double ulp_err(float got, float want)
{
	if (got == want)
		return 0;
	float u = nextafterf(want, INFINITY) - want;
	if (u == 0)
		u = 1e-45f;
	return fabs((double)got - (double)want) / u;
}

int main(void)
{
	for (long i = 0; i < 3000000; i++)
	{
		uint32_t a = (uint32_t)rnd() >> (rnd() & 31), b = (uint32_t)rnd() >> (rnd() & 31), r;
		if (b)
		{
			CHECK(udivmod32(a, b, &r) == a / b && r == a % b, "u32 %u %u", a, b);
			int32_t sa = (int32_t)a, sb = (int32_t)b, sr;
			if (!(sa == INT32_MIN && sb == -1))
				CHECK(idivmod_core(sa, sb, &sr) == sa / sb && sr == sa % sb, "s32 %d %d", sa, sb);
		}
		uint64_t ua = rnd() >> (rnd() & 63), ub = rnd() >> (rnd() & 63), ur;
		if (ub)
		{
			CHECK(uldivmod_core(ua, ub, &ur) == ua / ub && ur == ua % ub, "u64 %llu %llu", (unsigned long long)ua, (unsigned long long)ub);
			int64_t la = (int64_t)ua, lb = (int64_t)ub, lr;
			if (!(la == INT64_MIN && lb == -1))
				CHECK(ldivmod_core(la, lb, &lr) == la / lb && lr == la % lb, "s64");
		}
	}
	double worst_sqrt = 0, worst_sin = 0, worst_cos = 0;
	for (long i = 0; i < 2000000; i++)
	{
		uint32_t bits = (uint32_t)rnd() & 0x7FFFFFFF;
		float x;
		memcpy(&x, &bits, 4);
		if (x != x || isinf(x))
			continue;
		double e = ulp_err(m_sqrtf(x), sqrtf(x));
		if (e > worst_sqrt)
			worst_sqrt = e;
		float t = (float)((double)(int32_t)rnd() / 2147483648.0 * 200.0);
		double s = fabs((double)m_sinf(t) - sin((double)t)), c = fabs((double)m_cosf(t) - cos((double)t));
		if (s > worst_sin)
			worst_sin = s;
		if (c > worst_cos)
			worst_cos = c;
	}
	printf("int div failures %ld; sqrtf max ulp %.2f; sinf max abs err %.3g; cosf max abs err %.3g\n", fails, worst_sqrt, worst_sin, worst_cos);
	return fails != 0 || worst_sqrt > 0.5 || worst_sin > 2e-6 || worst_cos > 2e-6;
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define DECL(t, n, ...) t t_##n(__VA_ARGS__);
DECL(uint32_t, fadd, uint32_t, uint32_t)
DECL(uint32_t, fsub, uint32_t, uint32_t)
DECL(uint32_t, fmul, uint32_t, uint32_t)
DECL(uint32_t, fdiv, uint32_t, uint32_t)
DECL(int, fcmpeq, uint32_t, uint32_t)
DECL(int, fcmplt, uint32_t, uint32_t)
DECL(int, fcmple, uint32_t, uint32_t)
DECL(int, fcmpge, uint32_t, uint32_t)
DECL(int, fcmpgt, uint32_t, uint32_t)
DECL(int, fcmpun, uint32_t, uint32_t)
DECL(int32_t, f2iz, uint32_t)
DECL(uint32_t, f2uiz, uint32_t)
DECL(int64_t, f2lz, uint32_t)
DECL(uint64_t, f2ulz, uint32_t)
DECL(uint32_t, i2f, int32_t)
DECL(uint32_t, ui2f, uint32_t)
DECL(uint32_t, l2f, int64_t)
DECL(uint32_t, ul2f, uint64_t)
DECL(uint64_t, f2d, uint32_t)
DECL(uint32_t, d2f, uint64_t)
DECL(uint64_t, dadd, uint64_t, uint64_t)
DECL(uint64_t, dsub, uint64_t, uint64_t)
DECL(uint64_t, dmul, uint64_t, uint64_t)
DECL(uint64_t, ddiv, uint64_t, uint64_t)
DECL(int, dcmpeq, uint64_t, uint64_t)
DECL(int, dcmplt, uint64_t, uint64_t)
DECL(int, dcmple, uint64_t, uint64_t)
DECL(int, dcmpge, uint64_t, uint64_t)
DECL(int, dcmpgt, uint64_t, uint64_t)
DECL(int, dcmpun, uint64_t, uint64_t)
DECL(int32_t, d2iz, uint64_t)
DECL(uint32_t, d2uiz, uint64_t)
DECL(int64_t, d2lz, uint64_t)
DECL(uint64_t, d2ulz, uint64_t)
DECL(uint64_t, i2d, int32_t)
DECL(uint64_t, ui2d, uint32_t)
DECL(uint64_t, l2d, int64_t)
DECL(uint64_t, ul2d, uint64_t)

static uint64_t rng = 88172645463325252ull;

static uint64_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

static float fb(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
static uint32_t bf(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static double db(uint64_t v) { double f; memcpy(&f, &v, 8); return f; }
static uint64_t bd(double f) { uint64_t v; memcpy(&v, &f, 8); return v; }

static int same_f(uint32_t a, uint32_t b)
{
	if ((a & 0x7FFFFFFF) > 0x7F800000 && (b & 0x7FFFFFFF) > 0x7F800000)
		return 1;
	return a == b;
}

static int same_d(uint64_t a, uint64_t b)
{
	if ((a & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull && (b & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull)
		return 1;
	return a == b;
}

static uint32_t rand_f(void)
{
	uint64_t r = rnd();
	uint32_t v = (uint32_t)r;
	switch (r >> 60 & 7)
	{
	case 0: v &= 0x807FFFFF; break;
	case 1: v = (v & 0x80000000) | 0x7F800000 | (v & 3); break;
	case 2: v = (v & 0x80000000) | ((0x7F - 8 + (v >> 8 & 15)) << 23) | (v & 0x7FFFFF); break;
	case 3: v = (v & 0x80000000) | ((v >> 4 & 0x3F) << 23) | (v & 0x7FFFFF); break;
	default: break;
	}
	return v;
}

static uint64_t rand_d(void)
{
	uint64_t r = rnd(), v = rnd();
	switch (r >> 60 & 7)
	{
	case 0: v &= 0x800FFFFFFFFFFFFFull; break;
	case 1: v = (v & 0x8000000000000000ull) | 0x7FF0000000000000ull | (v & 3); break;
	case 2: v = (v & 0x8000000000000000ull) | ((uint64_t)(0x3FF - 8 + (v >> 8 & 15)) << 52) | (v & 0xFFFFFFFFFFFFFull); break;
	case 3: v = (v & 0x8000000000000000ull) | ((uint64_t)(v >> 4 & 0x1FF) << 52) | (v & 0xFFFFFFFFFFFFFull); break;
	default: break;
	}
	return v;
}

static long fails;

#define CHECK(cond, ...) do { if (!(cond)) { if (fails++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

int main(void)
{
	long n = 4000000;
	for (long i = 0; i < n; i++)
	{
		uint32_t a = rand_f(), b = rand_f();
		float x = fb(a), y = fb(b);
		CHECK(same_f(t_fadd(a, b), bf(x + y)), "fadd %08x %08x -> %08x want %08x", a, b, t_fadd(a, b), bf(x + y));
		CHECK(same_f(t_fsub(a, b), bf(x - y)), "fsub %08x %08x", a, b);
		CHECK(same_f(t_fmul(a, b), bf(x * y)), "fmul %08x %08x -> %08x want %08x", a, b, t_fmul(a, b), bf(x * y));
		CHECK(same_f(t_fdiv(a, b), bf(x / y)), "fdiv %08x %08x -> %08x want %08x", a, b, t_fdiv(a, b), bf(x / y));
		CHECK(t_fcmpeq(a, b) == (x == y), "fcmpeq %08x %08x", a, b);
		CHECK(t_fcmplt(a, b) == (x < y), "fcmplt %08x %08x", a, b);
		CHECK(t_fcmple(a, b) == (x <= y), "fcmple %08x %08x", a, b);
		CHECK(t_fcmpge(a, b) == (x >= y), "fcmpge %08x %08x", a, b);
		CHECK(t_fcmpgt(a, b) == (x > y), "fcmpgt %08x %08x", a, b);
		CHECK(t_fcmpun(a, b) == (x != x || y != y), "fcmpun %08x %08x", a, b);
		if (x == x && x > -2147483648.0f && x < 2147483648.0f)
			CHECK(t_f2iz(a) == (int32_t)x, "f2iz %08x", a);
		if (x == x && x > -1.0f && x < 4294967296.0f)
			CHECK(t_f2uiz(a) == (uint32_t)x, "f2uiz %08x", a);
		if (x == x && x > -9.2e18f && x < 9.2e18f)
			CHECK(t_f2lz(a) == (int64_t)x, "f2lz %08x", a);
		if (x == x && x > -1.0f && x < 1.8e19f)
			CHECK(t_f2ulz(a) == (uint64_t)x, "f2ulz %08x", a);
		CHECK(t_f2d(a) == bd((double)x) || x != x, "f2d %08x", a);
		uint64_t da = rand_d(), dbb = rand_d();
		double dx = db(da), dy = db(dbb);
		CHECK(same_d(t_dadd(da, dbb), bd(dx + dy)), "dadd %016llx %016llx", (unsigned long long)da, (unsigned long long)dbb);
		CHECK(same_d(t_dsub(da, dbb), bd(dx - dy)), "dsub %016llx %016llx", (unsigned long long)da, (unsigned long long)dbb);
		CHECK(same_d(t_dmul(da, dbb), bd(dx * dy)), "dmul %016llx %016llx", (unsigned long long)da, (unsigned long long)dbb);
		CHECK(same_d(t_ddiv(da, dbb), bd(dx / dy)), "ddiv %016llx %016llx", (unsigned long long)da, (unsigned long long)dbb);
		CHECK(t_dcmpeq(da, dbb) == (dx == dy), "dcmpeq");
		CHECK(t_dcmplt(da, dbb) == (dx < dy), "dcmplt");
		CHECK(t_dcmple(da, dbb) == (dx <= dy), "dcmple");
		CHECK(t_dcmpge(da, dbb) == (dx >= dy), "dcmpge");
		CHECK(t_dcmpgt(da, dbb) == (dx > dy), "dcmpgt");
		CHECK(t_dcmpun(da, dbb) == (dx != dx || dy != dy), "dcmpun");
		CHECK(same_f(t_d2f(da), bf((float)dx)), "d2f %016llx -> %08x want %08x", (unsigned long long)da, t_d2f(da), bf((float)dx));
		if (dx == dx && dx > -2147483648.0 && dx < 2147483648.0)
			CHECK(t_d2iz(da) == (int32_t)dx, "d2iz %016llx", (unsigned long long)da);
		if (dx == dx && dx > -1.0 && dx < 4294967296.0)
			CHECK(t_d2uiz(da) == (uint32_t)dx, "d2uiz");
		if (dx == dx && dx > -9.2e18 && dx < 9.2e18)
			CHECK(t_d2lz(da) == (int64_t)dx, "d2lz");
		if (dx == dx && dx > -1.0 && dx < 1.8e19)
			CHECK(t_d2ulz(da) == (uint64_t)dx, "d2ulz");
		int32_t si = (int32_t)(uint32_t)rnd() >> (rnd() & 31);
		uint32_t ui = (uint32_t)rnd() >> (rnd() & 31);
		int64_t sl = (int64_t)rnd() >> (rnd() & 63);
		uint64_t ul = rnd() >> (rnd() & 63);
		CHECK(t_i2f(si) == bf((float)si), "i2f %d", si);
		CHECK(t_ui2f(ui) == bf((float)ui), "ui2f %u", ui);
		CHECK(t_l2f(sl) == bf((float)sl), "l2f %lld", (long long)sl);
		CHECK(t_ul2f(ul) == bf((float)ul), "ul2f %llu", (unsigned long long)ul);
		CHECK(t_i2d(si) == bd((double)si), "i2d");
		CHECK(t_ui2d(ui) == bd((double)ui), "ui2d");
		CHECK(t_l2d(sl) == bd((double)sl), "l2d %lld", (long long)sl);
		CHECK(t_ul2d(ul) == bd((double)ul), "ul2d %llu", (unsigned long long)ul);
	}
	printf("softfloat: %ld operations sets, %ld failures\n", n, fails);
	return fails != 0;
}

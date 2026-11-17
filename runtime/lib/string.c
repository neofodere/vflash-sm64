typedef unsigned int u32;
typedef unsigned long size_t;

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if (!(((u32)d | (u32)s) & 3))
	{
		while (n >= 16)
		{
			u32 a = ((const u32 *)s)[0], b = ((const u32 *)s)[1], c = ((const u32 *)s)[2], e = ((const u32 *)s)[3];
			((u32 *)d)[0] = a;
			((u32 *)d)[1] = b;
			((u32 *)d)[2] = c;
			((u32 *)d)[3] = e;
			d += 16;
			s += 16;
			n -= 16;
		}
		while (n >= 4)
		{
			*(u32 *)d = *(const u32 *)s;
			d += 4;
			s += 4;
			n -= 4;
		}
	}
	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if (d == s || !n)
		return dst;
	if (d < s || d >= s + n)
		return memcpy(dst, src, n);
	d += n;
	s += n;
	while (n--)
		*--d = *--s;
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	u32 v = (unsigned char)c * 0x01010101u;
	while (n && ((u32)d & 3))
	{
		*d++ = (unsigned char)c;
		n--;
	}
	while (n >= 16)
	{
		((u32 *)d)[0] = v;
		((u32 *)d)[1] = v;
		((u32 *)d)[2] = v;
		((u32 *)d)[3] = v;
		d += 16;
		n -= 16;
	}
	while (n >= 4)
	{
		*(u32 *)d = v;
		d += 4;
		n -= 4;
	}
	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++)
	{
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (!*a)
			break;
	}
	return 0;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
	char *r = d;
	for (; n && *s; n--)
		*d++ = *s++;
	while (n--)
		*d++ = 0;
	return r;
}

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strchr(const char *s, int c)
{
	for (;; s++)
	{
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return 0;
	}
}

char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++)
	{
		if (*s == (char)c)
			r = s;
		if (!*s)
			return (char *)r;
	}
}

void bcopy(const void *s, void *d, size_t n)
{
	memmove(d, s, n);
}

void bzero(void *d, size_t n)
{
	memset(d, 0, n);
}

void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset8(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }

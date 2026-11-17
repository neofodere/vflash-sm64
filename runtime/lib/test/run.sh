#!/bin/sh
set -e
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/sf_test.$$
mkdir -p "$OUT"
DEFS=$(grep -o '__aeabi_[a-z0-9_]*' ../softfloat.c | sort -u | sed 's/__aeabi_\(.*\)/-D__aeabi_\1=t_\1/' | tr '\n' ' ')
gcc -O2 -fno-builtin -w $DEFS -c ../softfloat.c -o "$OUT/sf.o"
gcc -O1 -ffp-contract=off -Wall -Wextra -o "$OUT/test_sf" test_softfloat.c "$OUT/sf.o"
"$OUT/test_sf"
gcc -O2 -fno-builtin -w -Dsqrtf=m_sqrtf -Dsinf=m_sinf -Dcosf=m_cosf -Dfabsf=m_fabsf -c ../mathf.c -o "$OUT/mf.o"
gcc -O2 -w -Dudivmod32=udivmod32 -c ../intdiv.c -o "$OUT/id.o"
gcc -O1 -ffp-contract=off -Wall -Wextra -o "$OUT/test_math" test_math.c "$OUT/mf.o" "$OUT/id.o" -lm
"$OUT/test_math"
rm -rf "$OUT"

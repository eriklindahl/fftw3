AM_CPPFLAGS = -I$(top_srcdir)/kernel
noinst_LTLIBRARIES = libsimd_support.la libsimd_sse2_nonportable.la

libsimd_support_la_SOURCES = taint.c simd-common.h simd-sse2.h sse2.c	\
x86-cpuid.h amd64-cpuid.h avx.c simd-avx.h simd-avx-128.h \
simd-avx2.h avx2.c altivec.c simd-altivec.h	\
neon.c simd-neon.h vsx.c simd-vsx.h

# sse2-nonportable.c needs SSE2_CFLAGS, but Automake does not support
# per-object CFLAGS.  Thus we build a separate library.
libsimd_sse2_nonportable_la_CFLAGS = $(SSE2_CFLAGS)
libsimd_sse2_nonportable_la_SOURCES = sse2-nonportable.c 


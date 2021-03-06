/*
 * Copyright (c) 2003, 2007-14 Matteo Frigo
 * Copyright (c) 2003, 2007-14 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include "ifftw.h"

#if HAVE_AVX2

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)

#include "amd64-cpuid.h"

int X(have_simd_avx2)(void)
{
  /*
       static int init = 0, res;

       if (!init) {
	    res = 1 && ((cpuid_ebx(7) & 0x20) == 0x20);
	    init = 1;
       }
       return res;
  */
  return 1;
}

#else /* 32-bit code */

#include "x86-cpuid.h"

int X(have_simd_avx2)(void)
{
  /*
       static int init = 0, res;

       if (!init) {
	    res =   !is_386() 
		 && has_cpuid()
  	         && ((cpuid_ebx(7) & 0x20) == 0x20);
	    init = 1;
       }
       return res;
  */
  return 1;
}
#endif

#endif

/* 	heapsort.c
	==========
	
  Based on heapsort.c	8.1 (Berkeley) 6/4/93 obtained from https://cgit.freebsd.org/src/plain/lib/libc/stdlib/heapsort.c on 12/1/2022
		https://cgit.freebsd.org/src/log/lib/libc/stdlib/heapsort.c shows last change as 2017-11-20 
		 
  Modifications by Peter Miller, 25/1/2021
    - to compile with TDM-GCC 10.3.0 under Windows
    - heapsort_b() function removed to clean up code.
    - optimise swap function and make inline function rather than a macro
    - optimise COPY function and make a function rather than a macro
    	- the above 2 changes improved the speed by 24% on average with the test program.
    	- Note heapsort is still 9* slower than qsort on average
    - avoid use of malloc() for common use cases.
    - does not now set errno (as used from qsort() which does not set errno ).
*/    
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ronnie Kon at Mindcraft Inc., Kevin Lew and Elmer Yglesias.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/*
 * Swap two areas of size number of bytes.  
 */
/* we assume pointers have correct alignment for size (es) on call to heapsort, so we can optimise swap for the common sizes */
static inline void swapfunc(char *a, char *b, size_t es)
{
	if(es==8) /* potential size of pointer (64 bits) or double */
		{
		 uint64_t t;
		 uint64_t *ap=(uint64_t *)a,*bp=(uint64_t *)b;
		 t = *ap;
		 *ap = *bp;
		 *bp = t;		
		}
	else if(es==4) /* potential size of pointer (32 bits) or float, int etc */
		{
		 uint32_t t;
		 uint32_t *ap=(uint32_t *)a,*bp=(uint32_t *)b;
		 t = *ap;
		 *ap = *bp;
		 *bp = t;			
		}		
	else
		{ /* general purpose swap for any size - do a byte at a time so can be slow if es is large */		
		 uint8_t t;
		 do {
			t = *a;
			*a++ = *b;
			*b++ = t;
	   	 } while (--es > 0);
	   }
}


/* Copy one block of size size to another. Again optimised for common sizes */
static inline void copyfunc(char *to, char *from, size_t size)
{
 if(size==8) /* potential size of pointer (64 bits) or double */
		{*(uint64_t *)to=*(uint64_t *)from;		
		}
 else if(size==4) /* potential size of pointer (32 bits) or float, int etc */
		{*(uint32_t *)to=*(uint32_t *)from;		
		}
 else memmove(to,from,size); /* general solution */		
}


/*
 * Build the list into a heap, where a heap is defined such that for
 * the records K1 ... KN, Kj/2 >= Kj for 1 <= j/2 <= j <= N.
 *
 * There two cases.  If j == nmemb, select largest of Ki and Kj.  If
 * j < nmemb, select largest of Ki, Kj and Kj+1.
 */
#define CREATE(initval, nmemb, par_i, child_i, par, child, size) { \
	for (par_i = initval; (child_i = par_i * 2) <= nmemb; \
	    par_i = child_i) { \
		child = base + child_i * size; \
		if (child_i < nmemb && compar(child, child + size) < 0) { \
			child += size; \
			++child_i; \
		} \
		par = base + par_i * size; \
		if (compar(child, par) <= 0) \
			break; \
		swapfunc(par, child, size); \
	} \
}

/*
 * Select the top of the heap and 'heapify'.  Since by far the most expensive
 * action is the call to the compar function, a considerable optimization
 * in the average case can be achieved due to the fact that k, the displaced
 * elememt, is ususally quite small, so it would be preferable to first
 * heapify, always maintaining the invariant that the larger child is copied
 * over its parent's record.
 *
 * Then, starting from the *bottom* of the heap, finding k's correct place,
 * again maintianing the invariant.  As a result of the invariant no element
 * is 'lost' when k is assigned its correct place in the heap.
 *
 * The time savings from this optimization are on the order of 15-20% for the
 * average case. See Knuth, Vol. 3, page 158, problem 18.
 *
 * XXX Don't break the #define SELECT line, below.  Reiser cpp gets upset.
 */
#define SELECT(par_i, child_i, nmemb, par, child, size, k) { \
	for (par_i = 1; (child_i = par_i * 2) <= nmemb; par_i = child_i) { \
		child = base + child_i * size; \
		if (child_i < nmemb && compar(child, child + size) < 0) { \
			child += size; \
			++child_i; \
		} \
		par = base + par_i * size; \
		copyfunc(par, child, size); \
	} \
	for (;;) { \
		child_i = par_i; \
		par_i = child_i / 2; \
		child = base + child_i * size; \
		par = base + par_i * size; \
		if (child_i == 1 || compar(k, par) < 0) { \
			copyfunc(child, k,  size); \
			break; \
		} \
		copyfunc(child, par, size); \
	} \
}

/*
 * Heapsort -- Knuth, Vol. 3, page 145.  Runs in O (N lg N), both average
 * and worst case. 
 * In comparison Quicksort is O( N lg N) on average but could be O(N^2) in the worst case.
 * On average this heapsort is ~ 9* slower than quicksort.
 */
int heapsort(void *vbase, size_t nmemb, size_t size,int (*compar)(const void *, const void *))
{
	size_t i, j, l;
	char *base, *k, *p, *t;
	uint64_t k64; /* use as 8 byte array, aligned correctly */

	if (nmemb <= 1)
		return (0);

	if (!size) {
		return (-1);
	}

	if(size<=8) k= (char *)(&k64); /* if possible avoid allocating memory with malloc [ may be multitasking which might slow down malloc() ] */
	else if ((k = malloc(size)) == NULL)
		return (-1);

	/*
	 * Items are numbered from 1 to nmemb, so offset from size bytes
	 * below the starting address.
	 */
	base = (char *)vbase - size;

	for (l = nmemb / 2 + 1; --l;)
		CREATE(l, nmemb, i, j, t, p, size);

	/*
	 * For each element of the heap, save the largest element into its
	 * final slot, save the displaced element (k), then recreate the
	 * heap.
	 */
	while (nmemb > 1) {
		copyfunc(k, base + nmemb * size,  size);
		copyfunc(base + nmemb * size, base + size,  size);
		--nmemb;
		SELECT(i, j, nmemb, t, p, size, k);
	}
	if(size>8)
		free(k); /* if k was set via malloc, free memory obtained */
	return (0);
}

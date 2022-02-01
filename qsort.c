/* 	qsort.c
	=======

  Based on qsort.c	8.1 (Berkeley) 6/4/93 obtained from https://cgit.freebsd.org/src/plain/lib/libc/stdlib/qsort.c on 12/1/2022
  	https://cgit.freebsd.org/src/log/lib/libc/stdlib/qsort.c shows last change as 2021-07-29
  	
  Modifications by Peter Miller, 25/1/2021
    - to compile with TDM-GCC 10.3.0 under Windows
    - removed I_AM_QSORT_R & I_AM_QSORT_S defines to clean up code and get rid of compiler warnings.
    - replaced goto with while(1)
    - deleted step that just used middle element as pivot when size was 7 (so now uses at least median of 3 elements as pivot)
    - added insertion sort at start with a limited number of sawpas allowed, and deleted insertion sort if no swaps as that might result in O(n^2) behaviour. 
    - in swapfunc() added special case code for items of 4 and 8 bytes (eg pointers) as these are the most likley things to be sorted with this code (given ya_sort() is faster for sorting numbers).
    - added Introsort functionality to guarantee O(n*log(n)) execution speed.   See "Introspective sorting and selection algorithms" by D.R.Musser,Software practice and experience, 8:983-993, 1997.
    - added option to multitask sort - uses all available processors. For Windows only at present. Only done when PAR_SORT is #defined.
    
*/  
// #define DEBUG /* if defined then print out when we swap to heapsort to stdout . Helps to tune INTROSORT_MULT */
#ifdef DEBUG
#include <stdio.h>
#endif

/* the parameters below allow the sort to be "tuned" - the default values should give good results using most modern PC's */ 
#define PAR_SORT /* if defined use tasks to split sort across multiple processors - only works for Windows at present */
#define USE_INSERTION_SORT 25 /* for n<USE_INSERTION_SORT we use an insertion sort rather than quicksort , as this is faster */
#define MAX_INS_MOVES 2 /* max allowed number of allowed out of place items while sticking to insertion sort - for the test program, sorting doubles, 2 is the optimum value */	 
#define USE_MED_3_3 40 /* if > USE_MED_3_3 elements use median of 3 medians of 3, otherwise use median of 3 equally spaced elements */	
#define INTROSORT_MULT 3 /* defines when we swap to heapsort 2 means only do so very rarely, 0 means "always" use heapsort. All positive integer values (including 0) will give o(n*log(n)) worse case execution time */
						 /* note heapsort is on average 9* slower than quicksort so 0 is not recommended ! */
						 
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef PAR_SORT
 #include <process.h> /* for _beginthreadex */
 #include <windows.h> /* for number of processors */
#endif

typedef int		 cmp_t(const void *, const void *);

static inline char *med3(char *a, char *b, char *c, cmp_t *cmp);
static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp, int nos_p); /* qsort function */
int heapsort(void *vbase, size_t nmemb, size_t size,cmp_t *cmp);/* in bsd-heapsort.c , used as part of introsort functionality */

#define	MIN(a, b)	((a) < (b) ? a : b)
#define P_UNUSED(x) (void)x /* a way to avoid warning unused parameter messages from the compiler */

#if defined __GNUC__

static inline int ilog2(size_t x) { return 63 - __builtin_clzll(x); }

#elif defined _WIN32  && !defined  __BORLANDC__
#include <intrin.h>
static inline int ilog2(size_t x)
{
    unsigned long i = 0;
     _BitScanReverse(&i, x);
    return i;
}

#elif defined _WIN64  && !defined  __BORLANDC__
#include <intrin.h>
static inline int ilog2(size_t x)
{
    unsigned long i = 0;
    _BitScanReverse64(&i, x);
    return i;
}
#else // version in standard C , this is slower than above optimised versions but portable
/* algorithm from https://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers */
#if (defined __BORLANDC__ && defined _WIN64) || ( defined __SIZEOF_POINTER__ &&  __SIZEOF_POINTER__ == 8)
static inline int  ilog2(size_t x) // unsigned 64 bit version 
{
  #define S(k) if (x >= (UINT64_C(1) << k)) { i += k; x >>= k; }
  int i = 0;
  S(32);
  S(16);
  S(8);
  S(4);
  S(2);
  S(1);
  return i;
  #undef S
}
#elif (defined __BORLANDC__ && defined __WIN32__) || ( defined __SIZEOF_POINTER__ && __SIZEOF_POINTER__ == 4 )
static inline int  ilog2(size_t x) // unsigned 32 bit version 
{
  #define S(k) if (x >= (UINT32_C(1) << k)) { i += k; x >>= k; }
  int i = 0;
  S(16);
  S(8);
  S(4);
  S(2);
  S(1);
  return i;
  #undef S
}
#else
#error "unknown pointer size - expected 4 (32 bit) or 8 (64 bit) "
#endif

#endif

#ifdef PAR_SORT /* helper code for Parallel version */

#define PAR_DIV_N 16 /* divisor on n (current partition size) to check size of partition about to be spawned as a new task is big enough to justify the work of creating a new task */
#define PAR_MIN_N 10000 /* min size of a partition to be spawned as a new task */
/* static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp, int nos_p); */
struct _params 
	{void *a_p;
	 size_t n_p;
	 size_t es_p;
	 cmp_t *cmp_p;
	 int nos_p_p;
	};
	
static unsigned __stdcall yasortThreadFunc( void * _Arg ) /* parallel thread that can sort a partition */
{struct _params* Arg=_Arg;
 local_qsort(Arg->a_p,Arg->n_p,Arg->es_p,Arg->cmp_p,Arg->nos_p_p); /* sort required section  */
 _endthreadex( 0 );
 return 0;
}

/* we ideally need to know the number of processors - the code below gets this for windows */
/* this is from https://docs.microsoft.com/de-de/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation with minor changes by Peter Miller */
/* see https://programmerall.com/article/52652219244/ for Linux versions */

typedef BOOL (WINAPI *LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, 
    PDWORD);


/* Helper function to count set bits in the processor mask. */
static DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
    DWORD i;
    
    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}


static int nos_procs(void) /* return the number of logical processors present. 
					   This was created by Peter Miller 15/1/2022 based on above code  */
{
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD returnLength = 0;
    DWORD logicalProcessorCount = 0;
    DWORD processorCoreCount = 0;
    DWORD byteOffset = 0; 

    while (!done) /* we need to call GetLogicalProcessorInformation() twice, the 1st time it tells us how big a buffer we need to supply */
    {
        DWORD rc = GetLogicalProcessorInformation(buffer, &returnLength);

        if (FALSE == rc) 
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
            {
                if (buffer) 
                    free(buffer);

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
                        returnLength);

                if (NULL == buffer) 
                {
                    /* Error: memoery Allocation failure */
                    return 0;
                }
            } 
            else 
            {  /* other (unexpected) error */
                return 0;
            }
        } 
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) 
    {
        if (ptr->Relationship == RelationProcessorCore) 
        {
            processorCoreCount++;
            /* A hyperthreaded core supplies more than one logical processor.*/
            logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    free(buffer);

    return (int)logicalProcessorCount; /* actual number of processor cores is processorCoreCount */
}

#endif

/*
 * Qsort routine from Bentley & McIlroy's "Engineering a Sort Function", but now with significant changes by Peter Miller (see above).
 */

/* we assume pointers have correct alignment for size (es) on call to qsort, so we can optimise swap for the common sizes */
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

#define	vecswap(a, b, n)				\
	if ((n) > 0) swapfunc(a, b, n)

#define	CMP(x, y) (cmp((x), (y)))


static inline char *med3(char *a, char *b, char *c, cmp_t *cmp)
{
	return CMP( a, b) < 0 ?
	       (CMP( b, c) < 0 ? b : (CMP( a, c) < 0 ? c : a ))
	      :(CMP( b, c) > 0 ? b : (CMP( a, c) < 0 ? a : c ));
}

/*
 * The actual qsort() implementation is static to avoid preemptible calls when
 * recursing. 
 */

static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp, int nos_p)
{
 char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
 size_t d1, d2;
 int cmp_result;
 int swap_cnt;
 if(n<=1) return; // avoid trying to take log2 of 0 - anyway an array of length 1 is already sorted  
 int itn=0;
 const int max_itn=INTROSORT_MULT*ilog2(n); // max_itn defines point we swap to mid-range pivot, then at 2*max_int we swap to ya_heapsort. if INTROSORT_MULT=0 then "always" use ya_heapsort, 3 means "almost never" use ya_heapsort
#ifdef PAR_SORT
 struct _params params;
 HANDLE th=NULL; // handle for worker thread
#else
 P_UNUSED(nos_p); // this param is not used unless PAR_SORT is defined
#endif	 
 while(1)
   {/* while(1) replaces goto loop as I think that makes structure simpler to understand and it matches yasort */
	swap_cnt = 0;
	/* for < USE_INSERTION_SORT elements to sort use insertion sort as this is faster than using quicksort */
	if (n < USE_INSERTION_SORT) 
		{
		 for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
			for (pl = pm; 
			     pl > (char *)a && CMP( pl - es, pl) > 0;
			     pl -= es)
				swapfunc(pl, pl - es, es);
		  goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete
		}
	 /* try an insertion sort first, only allow MAX_INS_MOVES items out of place before we give up and use quicksort 
	    This traps already sorted partitions, and one with a small number of elemnts out of place.
	    If more than MAX_INS_MOVES elements are out of place, this will still move things around a bit which can help break up bad patterns for qsort
	    Tried using memmove() in insertion sort like in ya_sort() and specific versions for es=8,4 but that was slightly slower than generic version below!
	 */
	for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
	 	{if(pm>(char *)a && CMP( pm-es, pm)>0 && ++swap_cnt>MAX_INS_MOVES ) goto do_qsort; /* not sorted - back to qsort */
	 	 for (pl = pm; 
		     pl > (char *)a && CMP( pl - es, pl) > 0;
		     pl -= es)
			 	swapfunc(pl, pl - es, es);
		}
	goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete
     
	do_qsort: ; /* comes here if not already sorted */	
   // if we have made too many iterations of this while loop then we need to swap to ya_heapsort 
   if(++itn>max_itn)
  		{	 
#ifdef DEBUG 	  
	  	 printf("qsort: using heapsort(%.0f)\n",(double)n);
#endif	 
		/* int heapsort(void *vbase, size_t nmemb, size_t size,cmp_t *cmp); */ 
		/* static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp, int nos_p) */
	  	 if(heapsort(a,n,es,cmp)==0) goto sortend; // if heapsort suceeded then we are done, otherwise we need to stick with quicksort.
  		}		
	/* if > USE_MED_3_3 elements use median of 3 medians of 3, otherwise use median of 3 equally spaced elements */	
	pm = (char *)a + (n / 2) * es; /* middle element of array to be sorted */
	pl = a; /* 1st element */
	pn = (char *)a + (n - 1) * es; /* last element */
	if (n > USE_MED_3_3) 
	 	{
		 size_t d = (n / 8) * es;
		 pl = med3(pl, pl + d, pl + 2 * d, cmp);/* 1st element, 1/8 and 2/8 */
		 pm = med3(pm - d, pm, pm + d, cmp);/* 3/8, 4/8 and 5/8 */ 
		 pn = med3(pn - 2 * d, pn - d, pn, cmp);/* 6/8, 7/8 and last element */
		}
	pm = med3(pl, pm, pn, cmp);

	swapfunc(a, pm, es); /* put pivot into 1st element in the array */
	pa = pb = (char *)a + es;

	pc = pd = (char *)a + (n - 1) * es;
	for (;;) 
		{
		 while (pb <= pc && (cmp_result = CMP( pb, a)) <= 0) 
		 	{
			 if (cmp_result == 0) 
			 	{
				 swapfunc(pa, pb, es);
				 pa += es;
				}
			 pb += es;
			}
		while (pb <= pc && (cmp_result = CMP( pc, a)) >= 0) 
			{
			 if (cmp_result == 0) 
			 	{
				 swapfunc(pc, pd, es);
				 pd -= es;
				}
			 pc -= es;
			}
		 if (pb > pc)
			break;
		 swapfunc(pb, pc, es);
		 pb += es;
		 pc -= es;
		}

	 pn = (char *)a + n * es;
	 d1 = MIN(pa - (char *)a, pb - pa);
	 vecswap(a, pb - d1, d1);
	 /*
	  * Cast es to preserve signedness of right-hand side of MIN()
	  * expression, to avoid sign ambiguity in the implied comparison.  es
	  * is safely within [0, SSIZE_MAX].
	  */
	 d1 = MIN(pd - pc, pn - pd - (ssize_t)es);
	 vecswap(pb, pn - d1, d1);

	 d1 = pb - pa;
	 d2 = pd - pc;
#ifdef PAR_SORT	 
	  /* if using parallel tasks check here to see if task spawned from this function has finished, if so we can spawn another one to keep it busy
	     We allow spawned tasks and recursive calls to spawn more tasks if we have enough processors (thats the (nos_p)/2 passed as a paramater)
		 This approach should scale reasonably well without needing any complex interactions betweens tasks (as they are all working on seperate portions of the arrays x & y)
		 With 1 processor everything is in main function.
		 With 2 processors we use both
		 With 4 processors we use 3 [ as (4)/2=2, then 2/2=1 , so we use main processor and spawn 2 threads]
		 With 8 (logical) processors we use 7 [ (8)/2=4, (4)/2=2, then 2/2= 1 so we use main processor + 2 threads + 4 threads = 7 in total ]
		 In terms of run time - using a processor with 8 logical cores (4 physical cores) available:
		 1 core (PAR_SORT not defined) - test program sorting largest size took 52.662 secs
		 4 cores																21.767 secs = 2.4* speedup
		 8 cores 						 										19.350 secs = 2.7* speedup (there were only 4 physical cores which may partly explain the reduced improvement)
	  */ 	
	  if(th!=NULL && n>2*PAR_MIN_N )
	  	{// if thread active and partition big enough that we might be able to use a parallel task (2* as we will at least halve the size of the partition for the parallel task) 
	  	 if( WaitForSingleObject( th, 0 )!=WAIT_TIMEOUT)
	  		{// if thread has finished
    		 CloseHandle( th );// Destroy the thread object.
			 th=NULL; // set to NULL so we can reuse it
			 // printf("Thread finished at depth %d\n",depth);
			}
		}
#endif     	 
	 if (d1 <= d2) 
	 	{
	 	 /* Recurse on left partition, then iterate on right partition */
		 if (d1 > es) 
		 	{
#ifdef PAR_SORT
			 if( th==NULL && nos_p>1 && (size_t)(d1 / es) > n/PAR_DIV_N && (size_t)(d1 / es) > PAR_MIN_N)
				{// use a worker thread last 2 tests check the overhead of thread creation is worth it.
				/*	void *a_p;
	 				size_t n_p;
	 				size_t es_p;
	 				cmp_t *cmp_p
	 				int nos_p_p;
	 			*/	
				 params.a_p=a;
				 params.n_p=d1 / es;
				 params.es_p=es;
				 params.cmp_p=cmp;
				 params.nos_p_p=(nos_p)/2; // if we still have spare processors allow more threads to be started
				 th=(HANDLE)_beginthreadex(NULL,0,yasortThreadFunc,&params,0,NULL);
				 if(th==NULL) local_qsort(a, d1 / es, es, cmp,0); // if starting thread fails then do in this process.  nos_p=0 so don't run any tasks from here
				}
			 else
				{local_qsort(a, d1 / es, es, cmp,(nos_p)/2); // recurse for smalest partition so stack depth is bounded at O(log2(n)). Allow more threads from subroutine if we still have some processors spare
				}
#else		 		
			 local_qsort(a, d1 / es, es, cmp,0);
#endif
			}
		 if (d2 > es) 
			{
			 /* Iterate rather than recurse to save stack space */
			 /* qsort(pn - d2, d2 / es, es, cmp); */
			 a = pn - d2;
			 n = d2 / es;
			}
		 else goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete
		} 
	 else 
	 	{
		 /* Recurse on right partition, then iterate on left partition */
		 if (d2 > es) 
		 	{
#ifdef PAR_SORT
			 if( th==NULL && nos_p>1 && (size_t)(d2 / es) > n/PAR_DIV_N && (size_t)(d2 / es) > PAR_MIN_N)
				{// use a worker thread last 2 tests check the overhead of thread creation is worth it.
				/*	void *a_p;
	 				size_t n_p;
	 				size_t es_p;
	 				cmp_t *cmp_p
	 				int nos_p_p;
	 			*/	
				 params.a_p=pn - d2;
				 params.n_p=d2 / es;
				 params.es_p=es;
				 params.cmp_p=cmp;
				 params.nos_p_p=(nos_p)/2; // if we still have spare processors allow more threads to be started
				 th=(HANDLE)_beginthreadex(NULL,0,yasortThreadFunc,&params,0,NULL);
				 if(th==NULL) local_qsort(pn - d2, d2 / es, es, cmp,0); // if starting thread fails then do in this process.  nos_p=0 so don't run any tasks from here
				}
			 else
				{local_qsort(pn - d2, d2 / es, es, cmp,(nos_p)/2); // recurse for smalest partition so stack depth is bounded at O(log2(n)). Allow more threads from subroutine if we still have some processors spare
				}
#else		 		
			 local_qsort(pn - d2, d2 / es, es, cmp,0);
#endif			 
			}
		 if (d1 > es) 
			{
			 /* Iterate rather than recurse to save stack space */
			 /* qsort(a, d1 / es, es, cmp); */
			 n = d1 / es;
			}
		 else goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete	
		}
   	} /* end of main while(1) loop */
 sortend: ; // need a common end point as may be using threads in which case we need to wait for them to complete	
 #ifdef PAR_SORT
 if(th!=NULL)
 	{// if a thread used need to wait for it to finish
 	 WaitForSingleObject( th, INFINITE );
    // Destroy the thread object.
     CloseHandle( th );
	}
 #endif
 return;   	
}

void qsort(void *a, size_t n, size_t es, cmp_t *cmp)
{   
 if(n<=1 || es==0) return; /* array of size 1 is sorted by definition, and elements of size 0 cannot be sorted */
#ifdef PAR_SORT	
 int nos_p=nos_procs() ;/* total number of (logical) processors available */
 local_qsort(a, n, es, cmp,nos_p);/* call main worker function */
#else
 local_qsort(a, n, es, cmp,1);
#endif
}


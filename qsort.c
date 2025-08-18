/* 	qsort.c
	=======

  Based on qsort.c	8.1 (Berkeley) 6/4/93 obtained from https://cgit.freebsd.org/src/plain/lib/libc/stdlib/qsort.c on 12/1/2022
  	https://cgit.freebsd.org/src/log/lib/libc/stdlib/qsort.c shows last change as 2021-07-29
  	
  Modifications by Peter Miller, 25/1/2021
    - to compile with TDM-GCC 10.3.0 under Windows
    - removed I_AM_QSORT_R & I_AM_QSORT_S defines to clean up code and get rid of compiler warnings.
    - replaced goto with while(1)
    - deleted step that just used middle element as pivot when size was 7 (so now uses at least median of 3 elements as pivot)
    - added insertion sort at start with a limited number of swaps allowed, and deleted insertion sort if no swaps as that might result in O(n^2) behaviour. 
    - in swapfunc() added special case code for items of 4 and 8 bytes (eg pointers) as these are the most likley things to be sorted with this code (given ya_sort() is faster for sorting numbers).
    - added Introsort functionality to guarantee O(n*log(n)) execution speed.   See "Introspective sorting and selection algorithms" by D.R.Musser,Software practice and experience, 8:983-993, 1997.
    - added option to multitask sort - uses all available processors. For Windows only at present (since changed can now use pthreads for portability). Only done when PAR_SORT is #defined.
  1v2 - no changes here - added smoothsort() to test program [ which was slower than mergesort by 22% on averages and 53% based on max timings so does not appear to offer any advantages ]
  1v3 - if only given a small number of elements to be sorted then directly use an insertion sort to avoid a lot of overhead.
  1v4 - if MEDIAN_PIVOT defined then use median of medians pivot quicksort as a backup 
  1v5 - added back final fallback of heapsort should median sort turn out to be slow on a particular input pattern.
  		MEDIAN_SORT takes twice as long as the baseline "qsort" (averages) , therefore INTROSORT_MULT should be set to 2. (see below can now be a float so set to 2.36)
  		heapsort takes 5* as long as Median sort (averages) so MEDIAN_PIVOT should be set to 5 
  1v6 - changed log2 calculations from int to float to make them a little more accurate
  	 	uses fast approximation to log2(float) based on code in expr-code.c
  		Gives almost the same execution time as 1v3 (average & max).
  1v22 - uses binary insertion sort rather than linear insertion sort for the code around MAX_INS_MOVES	where we are sorting the whole array rather than a small part of it.
  2v12 - matched structure to ya-sort() - added MAX_PIVOT_FRACTION, new threading approach allowing more parallel running, etc.
  3v0 - moved thread variables from global to local so no worries about calling qsort in a multithreaded program - but unless the PC you are using has lots of cores and cache it may be more efficient to use a mutex so
  		only 1 big parallel sort is run at a time?
  
  Note could create a complete special case for es=8 (or 4) which was much closer to ya-sort e.g the pivot could be a local variable rather a[0] ??
  
*/  
// #define DEBUG /* if defined then print out when we swap to heapsort to stdout . Helps to tune INTROSORT_MULT */
#ifdef DEBUG
 #include <stdio.h>
#else
 #define NDEBUG /* if defined then asserts become "nothing" */
#endif

	/* "configuration" for qsort - some have different values to other sorts, presumably as moving and comparing values here is more complex (extra layers of indirection, unknown types) which adds overheads */
	/* these values were set using the attached test program aiming to minimise the total test program execution time [ i.e. average speed] while checking the max execution times are also well contained */
	/* none of the values are very critical, and are expected to give reasonable results on "typical" 2024 PC's (8 cores, a few MB of cache) if you are using something very different to this you may make some (likely small) gains adjusting them   */

#define PAR_SORT /* if defined use tasks to split sort across multiple processors. WARNING: only one instance at a time is allowed to run if PAR_SORT is defined  */
#define USE_SMALL_SORT 32 		/* for n<=USE_SMALL_SORT we use an insertion sort rather than quicksort , as this is faster (an "optimal sort" is slower - see USE_YA_SMALLSORT)
									This is an integer. Typically (and at most) 32. On the test program 24 is slightly slower.  It must be >=9.
								*/
#define MAX_INS_MOVES 2 		/* max allowed number of out of place items while sticking to insertion sort of any sized array. 
								   At most MAX_INS_MOVES out of place items are allowed before swapping to a quicksort 
								   This is an integer. Typicaly 2   
								*/	 
#define INTROSORT_MULT 15.0f  	/* Point where we swap to the (very slow - but guaranteed O(n*log(n)) heapsort 
								   0 means "always" use heapsort. All positive float values (including 0) will give o(n*log(n)) worse case execution time 
								   However, typically heapsort is ~15* slower than quicksort so we don't want to use the heapsort unless its essential.
								   [ measured 14.3* based on average and 12.2* based on maximum times for the test program 1v31 running on an an 4 core/8 logical processor Intel i3-10100]
								   It is beleived this code guarantees O(n*log(n)) without the use of heapsort (by using MAX_PIVOT_FRACTION see below) so this is left as a "final backup".
								   This is a C "float". Typically 15.0f
						 	 	*/				
#define MAX_PIVOT_FRACTION 0.999f /* >-1 <1.0 : pivot_fraction is the ratio of new largest partition size / previous partition size  <=0 is an optimum partition, larger values are worse.
									 MAX value defines when we swap to a more robust (but slower to calculate) pivot calculation  (recursive median of 25 medians)					   						  								  
								    There is a "local minimum" at 0.9 (which is the region where yasort and yasort2 have their minimum), but the overhead of the recursive median is much higher here so 0.999 is significantly faster.
								    On the test program 0.999 is slightly faster in maximum times and slightly slower on average than 0.9999. As lower numbers are generally more "robust" 0.999 was selected.
								    Note that as a side effect of the recursive median of medians pivot selection the input data's order is changed which seems to make sub-partitioning simpler. 
								    This is a C "float". Typically 0.999f
								 */	
#define USE_MEDIAN25 100000		/* for n>=this value we use a median of 25 values, for smaller n we use median of 9.
								   This is an integer. Typically 100000. It must be >=25.
								*/								 
 // #define USE_YA_SMALLSORT /* if defined use smallsort() rather than insertion sort for sorting small segements of the array ( <USE_SMALL_SORT elements) - is significantly slower in the test program */
						 
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

// #define USE_PTHREADS /* normaly you should use the #ifndef below to automatically set this - but this line allows the pthreads code to be tested on Windows */
 
 
#ifndef _WIN32 
 #define USE_PTHREADS /* if defined use pthreads for multitasking - otherwise if running under windows use native windows threads. Pthreads is slightly slower than native Windows threads on Windows */
#endif
#include <signal.h> /* defines sig_atomic_t */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#ifdef PAR_SORT
 #if defined _WIN32 
  #include <process.h> /* for _beginthreadex */
  #include <windows.h> /* for number of processors */
 #elif defined __GNUC__
  #include <unistd.h> /* to get number of processors on OS's other than Windows */
 #else
  #error "Parallel sorting not supported for this complier/OS (undefine PAR_SORT to avoid this error)"
 #endif 
 #ifdef USE_PTHREADS
  #include <pthread.h>
 #endif
#endif

#ifdef DEBUG
 #define dprintf(...) printf(__VA_ARGS__) /* convert to printf when DEBUG defined */ 
#else
 #define dprintf(...) /* nothing - only prints when DEBUG defined */
#endif

typedef int		 cmp_t(const void *, const void *);

static inline char *med3(char *a, char *b, char *c, cmp_t *cmp);

/* we use heapsort from heapsort.c as a "backup" to guarantee O(n*log2(n)) runtime */
#include "heapsort.h"

#define MIN(a, b)	((a) < (b) ? (a) : (b)) /* warning a,b evaluated more than once ! */
#define MAX(a, b)	((a) > (b) ? (a) : (b)) /* warning a,b evaluated more than once ! */

#define P_UNUSED(x) (void)x /* a way to avoid warning unused parameter messages from the compiler */

static float flog2 (float x)   /* fast approximation to log2(x) based on fasterlog() in expr-code.c */
        /* assumes x>0 , as it will be when used here where x will be a relatively large integer (so no need to optimise for values around x=1) */
        /* assumes floats are stored as per the ieee f.p. standard */
        /* note ln(x)=ln(2)*log2(x) so log2(x)=ln(x)/ln(2) = ln(x)/6.93147180e-1  */ 
        /* max relative error is 3.28979% at x=2, x=4 etc */
{ union _f_andi32
        {float f;
         int32_t i;
        } f_i;
 float y;
 f_i.f=x;
 y=f_i.i;
 y *= 1.1920929e-7f; /* 2^-23 = 1.1920928955078125e-7 so value ~= ln2(x)+127) */
 return y-1.269671e+2f; /* simple (fast) approx to minimise error of approximation. Constant was tweaked to minimise relative error - best was 1.269671e+2=>3.28979% at x=2*/
}



#define PAR_MIN_N 10000 /* min size of a partition to be spawned as a new task On a moderately fast 2023 PC 100,000,000 in single processor mode takes < 10 sec, so 10,000 is < 1ms task time */
#define MAX_THREAD 32 // number of running threads for an 8 core processor 32 seems to be the optimum number, but it makes little difference 8-64

struct _params 
	{
 #ifdef USE_PTHREADS
 	 volatile pthread_t *th;// set to &thread_id when running 
 	 pthread_t thread_id;
 #else
 	 volatile HANDLE th; // handle for worker thread (Windows threads)
 #endif
	 void *x_p;
	 size_t n_p;
	 size_t es_p;
	 cmp_t *cmp_p;
	 struct T_info *pT_i_p; // forward reference
	 volatile sig_atomic_t task_fin; /* 0 => task running, 1=> task finished. This variable makes porting to pthreads simpler*/
	};

struct T_info /* shared storage needed for threads */
	{
 #ifdef USE_PTHREADS
     pthread_mutex_t ta_lock; // lock for struct T_info contents (having a single lock avoids the risk of deadlocks)
 #else
     CRITICAL_SECTION ta_lock;
 #endif		
	 struct _params Thread_arr[MAX_THREAD];
	 volatile int nos_threads; // number of threads actually running
	};

static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp,struct T_info *pT_i); /* qsort function */
#ifdef PAR_SORT /* helper code for Parallel version (note we need the definition of struct T_info even if PAR_SORT is not defined as its the last parameter for local_qsort() */
#ifdef USE_PTHREADS // void *(*start_routine)(void *)
 static void *  yasortThreadFunc( void * _Arg ) // parallel thread that can sort a partition
#else	
 static unsigned __stdcall yasortThreadFunc( void * _Arg ) // parallel thread that can sort a partition
#endif
	{struct _params* Arg=_Arg;
	 Arg->task_fin=0; // This should be set before starting the thread, waiting till now leaves a short time when the thread may appear to have finished but its actually not started... Its done here "just in case".
	 local_qsort(Arg->x_p,Arg->n_p,Arg->es_p,Arg->cmp_p,Arg->pT_i_p); /* sort required section  */
#ifdef USE_PTHREADS
 	 pthread_mutex_lock( &(Arg->pT_i_p->ta_lock)); // lock  access to nos_threads
 #else
  	 EnterCriticalSection( &(Arg->pT_i_p->ta_lock));
 #endif	
 	 Arg->task_fin=1; // indicate thread now finished - note it will actually finish when this function returns so this flag just indicates another task can now "wait" on this thread finishing  
	 --(Arg->pT_i_p->nos_threads); // actually change nos_threads 	 
 #ifdef USE_PTHREADS
 	 pthread_mutex_unlock( &(Arg->pT_i_p->ta_lock)); // unlock access to nos_threads
 #else
 	 LeaveCriticalSection(&(Arg->pT_i_p->ta_lock));
 #endif	
	 // _endthreadex( 0 ); - _endthreadex is call automatically when we return from this function - see https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/endthread-endthreadex?view=msvc-170 
	 return 0;
	}

 /* support function - assigns a thread to process a subsection of the array (or executes it inplace if no threads free) */
 // static void local_qsort(void *x, size_t n, size_t es, cmp_t *cmp)
static void new_thread(void *x, size_t n, size_t es, cmp_t *cmp,struct T_info *pT_i)
 {
 #ifdef USE_PTHREADS
  pthread_mutex_lock( &(pT_i->ta_lock)); // lock  Thread_arr 
 #else
  EnterCriticalSection( &(pT_i->ta_lock));
 #endif
 // now have exclusive access to Thread_arr
 // step through array looking for a thread that can be used, also checks if any threads have finished
 int i;
 bool found=false;
 for(i=0;i<MAX_THREAD;++i)
 	{if(pT_i->Thread_arr[i].th!=NULL && pT_i->Thread_arr[i].task_fin==1)
 		{// task has finished - clean up then reuse it
 #ifdef USE_PTHREADS
  		 pthread_join(pT_i->Thread_arr[i].thread_id,NULL);
 #else		/* use native Windows threads */
	  	 WaitForSingleObject( pT_i->Thread_arr[i].th, INFINITE ); // we know its finished from params.task_fin==1, so should not actually wait here despite "INFINITE" 
		 CloseHandle( pT_i->Thread_arr[i].th );// Destroy the thread object.	 
 #endif	 
 		 pT_i->Thread_arr[i].th=NULL; // set to NULL so we can reuse it
 		 pT_i->Thread_arr[i].task_fin=0;
		 found=true;
		 break;		
 		}
 	 else if(pT_i->Thread_arr[i].th==NULL)
 	 	{// simple case - unused thread
 	 	 found=true;
		 break;	
		}
	}

 if(found)
 	{ // we found a thread we can use/reuse
	 pT_i->Thread_arr[i].x_p=x;// setup ready to run thread
	 pT_i->Thread_arr[i].n_p=n;
	 pT_i->Thread_arr[i].es_p=es;
	 pT_i->Thread_arr[i].cmp_p=cmp;
	 pT_i->Thread_arr[i].pT_i_p=pT_i;
	 pT_i->Thread_arr[i].task_fin=0;	 
	 ++(pT_i->nos_threads); // actually change nos_threads: note another thread running	 - MUST be done before actually creating thread as otherwise there is a risk new thread will finish (and do -- nos_threads) before we inc nos_threads		 	 
 #ifdef USE_PTHREADS
	 if(pthread_create(&(pT_i->Thread_arr[i].thread_id),NULL,yasortThreadFunc,&(pT_i->Thread_arr[i]))==0)
	 	{
		 pT_i->Thread_arr[i].th=&(pT_i->Thread_arr[i].thread_id); // success 		 
	 	}
	 else
	 	{pT_i->Thread_arr[i].th=NULL; // failed to run thread
		}
 #else		/* use native Windows threads */	 
	 pT_i->Thread_arr[i].th=(HANDLE)_beginthreadex(NULL,0,yasortThreadFunc,&(pT_i->Thread_arr[i]),0,NULL);
 #endif			 
	 if(pT_i->Thread_arr[i].th==NULL) 
	 	{// if starting thread fails then do processing in this process (after we release mux!) 
		 found=false; // treat the same as if no thread was available	 
		 --(pT_i->nos_threads); // actually change nos_threads: to reflect we failed to run another thread	  		 
	 	} 
 	}
 #ifdef USE_PTHREADS
  pthread_mutex_unlock( &(pT_i->ta_lock)); // unlock Thread_arr 
 #else
  LeaveCriticalSection( &(pT_i->ta_lock));
 #endif
  if(!found)
 	{// no threads available - just run it directly in existing thread - do outside of critical section 
 	 local_qsort(x, n, es, cmp,pT_i);
 	}
 }
 
  /* support function - waits till all threads have finished */
 static void wait_all_threads(struct T_info *pT_i)
 { 
 /* don't use atomics */
 int nt=1; // initialise to any non-zero value 
 while(nt!=0)	
 	{ 
 #ifdef USE_PTHREADS
 	 pthread_mutex_lock( &(pT_i->ta_lock)); // lock  access to nos_threads
 #else
  	 EnterCriticalSection(&(pT_i->ta_lock));
 #endif	 
	 nt=pT_i->nos_threads; // actually access nos_threads	   	 
 #ifdef USE_PTHREADS
 	 pthread_mutex_unlock(&(pT_i->ta_lock)); // unlock access to nos_threads
 #else
 	 LeaveCriticalSection(&(pT_i->ta_lock));
 #endif
 	}

 // now no other tasks running, don't need to worry about locks
 // step through array looking for a thread that has not fully finished, waits for any found
 for(int i=0;i<MAX_THREAD;++i)
 	{if(pT_i->Thread_arr[i].th!=NULL )
 		{// wait for thread to finish, then clean up 
 #ifdef USE_PTHREADS
  		 pthread_join(pT_i->Thread_arr[i].thread_id,NULL);
 #else		/* use native Windows threads */
	  	 WaitForSingleObject( pT_i->Thread_arr[i].th, INFINITE ); // we know its finished from params.task_fin==1, so should not actually wait here despite "INFINITE" 
		 CloseHandle( pT_i->Thread_arr[i].th );// Destroy the thread object.		 
 #endif	 
 		 pT_i->Thread_arr[i].th=NULL; // set to NULL so we can reuse it
 		 pT_i->Thread_arr[i].task_fin=0;
		}
	}
 }
 
  /* support function - setup at start for thread handling */
 static void setup_threads(struct T_info *pT_i)
 { 
 #ifdef USE_PTHREADS
  pthread_mutex_init(&(pT_i->ta_lock),NULL); // initialise lock for Thread_arr 
 #else
  InitializeCriticalSection( &(pT_i->ta_lock));
 #endif
 // setup elements of array
  for(int i=0;i<MAX_THREAD;++i)
 	{
	 pT_i->Thread_arr[i].th=NULL; // set to NULL so we can reuse it
	 pT_i->Thread_arr[i].task_fin=0;
	}
  pT_i->nos_threads=0; // no threads currently running
 }
 
static void close_threads(struct T_info *pT_i)
  {
 #ifdef USE_PTHREADS
  pthread_mutex_destroy(&(pT_i->ta_lock)); // finished with lock for Thread_arr 
 #else
  DeleteCriticalSection( &(pT_i->ta_lock));
 #endif  
  }
  
#endif // #ifdef PAR_SORT


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
	if ((n) > 0) swapfunc((a), (b), (n))

#define	CMP(x, y) (cmp((x), (y)))


static inline char *med3(char *a, char *b, char *c, cmp_t *cmp)
{
	return CMP( a, b) < 0 ?
	       (CMP( b, c) < 0 ? b : (CMP( a, c) < 0 ? c : a ))
	      :(CMP( b, c) > 0 ? b : (CMP( a, c) < 0 ? a : c ));
}

#ifdef USE_YA_SMALLSORT
 #define __QSORT /* used by ya_smallsort.h to tell what its function is */
 #include "ya-smallsort.h"
#else
static  void small_sortq(void *a, size_t n, size_t es, cmp_t *cmp) /* insertion sort for moderate n (<= 64) */
	/* making this function inline results in a slower runtime */
{
 /* use an optimised insertion sort, based on value of es */
 if(es==8)
 	{/* special case */
 	 const size_t zes=sizeof(uint64_t); // this makes it a little faster
 	 for(char *p=(char *)a; p<(char *)a+(n-1)*zes; p+=zes) 
		{
		 uint64_t t = ((uint64_t *)p)[1];
		 char *j = p;
		 if(CMP(j,&t)>0) // if(*j>t)
		 	{// out of order 
		 	 do
		 		{j-=zes;
		 		} while(j>=(char *)a && CMP(j,&t)>0);
		 	 memmove(j+2*zes,j+zes,(p-j));// move a portion of array x right by 1 to make space for t 		 
		 	 ((uint64_t *)j)[1]=t;		
		 	}
        }	
 	}
 else if(es==4)
 	{/* special case */
 	 const size_t zes=sizeof(uint32_t);
 	 for(char *p=(char *)a; p<(char *)a+(n-1)*zes; p+=zes) 
		{
		 uint32_t t = ((uint32_t *)p)[1];
		 char *j = p;
		 if(CMP(j,&t)>0) // if(*j>t)
		 	{// out of order 
		 	 do
		 		{j-=zes;
		 		} while(j>=(char *)a && CMP(j,&t)>0);
		 	 memmove(j+2*zes,j+zes,(p-j));// move a portion of array x right by 1 to make space for t 		 
		 	 ((uint32_t *)j)[1]=t;		
		 	}
        }	
 	}		 	
 else
 	{ /* general case */
 	 for (char *pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
		for (char *pl = pm; 
		     pl > (char *)a && CMP( pl - es, pl) > 0;
		     pl -= es)
			swapfunc(pl, pl - es, es);
	}
}				

#endif
/*
* From:
* Fast median search: an ANSI C implementation
* Nicolas Devillard - ndevilla AT free DOT fr
* July 1998
*
* The following routines have been built from knowledge gathered
* around the Web. I am not aware of any copyright problem with
* them, so use it as you want.
* N. Devillard - 1998
* The functions below have been significantly edited by Peter Miller 7/2025 to fit in with the rest of the code here (so es and cmp had to be added as parameters to the functions )
*/
// #define Zv(a,b) { if ((a)>(b)) cswap((a),(b)); }
#define Zv(a,b) { if(CMP(p+(a)*es,p+(b)*es)>0) swapfunc(p+(a)*es,p+(b)*es,es); }
 

/*----------------------------------------------------------------------------
Function : opt_med9()
In : pointer to an array of 9  values
Out : median in "p[4]"
Job : optimized search of the median of 9  values
Notice : in theory, cannot go faster without assumptions on the signal.
Formula from:
XILINX XCELL magazine, vol. 23 by John L. Smith
The input array is modified in the process
The result array is guaranteed to contain the median
value in middle position "p[4]", but other elements are NOT fully sorted sorted.
---------------------------------------------------------------------------*/
static void opt_med9( void *p,  size_t es, cmp_t *cmp) /* median in "p[4]" */
{
Zv(1, 2) ; Zv(4, 5) ; Zv(7, 8) ;
Zv(0, 1) ; Zv(3, 4) ; Zv(6, 7) ;
Zv(1,2) ; Zv(4, 5) ; Zv(7, 8) ;
Zv(0, 3) ; Zv(5, 8) ; Zv(4, 7) ;
Zv(3, 6) ; Zv(1, 4) ; Zv(2, 5) ;
Zv(4, 7) ; Zv(4, 2) ; Zv(6, 4) ;
Zv(4, 2) ; 
/* result is in "p[4]" */
}

/*----------------------------------------------------------------------------
Function : opt_med25()
In : pointer to an array of 25  values
Out : median in "p[12]"
Job : optimized search of the median of 25 values
The input array is modified in the process
The result array is guaranteed to contain the median
value in middle position ("p[12]"), but other elements are NOT fully sorted sorted.
Notice : in theory, cannot go faster without assumptions on the signal.
Code taken from Graphic Gems.
---------------------------------------------------------------------------*/
static void opt_med25( void *p,  size_t es, cmp_t *cmp) /* median in "p[12]" */
{
Zv(0, 1) ; Zv(3, 4) ; Zv(2, 4) ;
Zv(2, 3) ; Zv(6, 7) ; Zv(5, 7) ;
Zv(5, 6) ; Zv(9, 10) ; Zv(8, 10) ;
Zv(8, 9) ; Zv(12, 13) ; Zv(11, 13) ;
Zv(11, 12) ; Zv(15, 16) ; Zv(14, 16) ;
Zv(14, 15) ; Zv(18, 19) ; Zv(17, 19) ;
Zv(17, 18) ; Zv(21, 22) ; Zv(20, 22) ;
Zv(20, 21) ; Zv(23, 24) ; Zv(2, 5) ;
Zv(3, 6) ; Zv(0, 6) ; Zv(0, 3) ;
Zv(4, 7) ; Zv(1, 7) ; Zv(1, 4) ;
Zv(11, 14) ; Zv(8, 14) ; Zv(8, 11) ;
Zv(12, 15) ; Zv(9, 15) ; Zv(9, 12) ;
Zv(13, 16) ; Zv(10, 16) ; Zv(10, 13) ;
Zv(20, 23) ; Zv(17, 23) ; Zv(17, 20) ;
Zv(21, 24) ; Zv(18, 24) ; Zv(18, 21) ;
Zv(19, 22) ; Zv(8, 17) ; Zv(9, 18) ;
Zv(0, 18) ; Zv(0, 9) ; Zv(10, 19) ;
Zv(1, 19) ; Zv(1, 10) ; Zv(11, 20) ;
Zv(2, 20) ; Zv(2, 11) ; Zv(12, 21) ;
Zv(3, 21) ; Zv(3, 12) ; Zv(13, 22) ;
Zv(4, 22) ; Zv(4, 13) ; Zv(14, 23) ;
Zv(5, 23) ; Zv(5, 14) ; Zv(15, 24) ;
Zv(6, 24) ; Zv(6, 15) ; Zv(7, 16) ;
Zv(7, 19) ; Zv(13, 21) ; Zv(15, 23) ;
Zv(7, 13) ; Zv(7, 15) ; Zv(1, 9) ;
Zv(3, 11) ; Zv(5, 17) ; Zv(11, 17) ;
Zv(9, 17) ; Zv(4, 10) ; Zv(6, 12) ;
Zv(7, 14) ; Zv(4, 6) ; Zv(4, 7) ;
Zv(12, 14) ; Zv(10, 14) ; Zv(6, 7) ;
Zv(10, 12) ; Zv(6, 10) ; Zv(6, 17) ;
Zv(12, 17) ; Zv(7, 17) ; Zv(7, 10) ;
Zv(12, 18) ; Zv(7, 12) ; Zv(10, 18) ;
Zv(12, 20) ; Zv(10, 20) ; Zv(10, 12) ;
/* return is in "p[12]" */
}
/* end of N. Devillard functions */


static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp,struct T_info *pT_i)
{
 char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
 size_t d1, d2;
 int cmp_result;
 int swap_cnt;
 if(n<=1) return; // avoid trying to take log2 of 0 - anyway an array of length 1 is already sorted  
 int itn=0;
 const int max_itn=((float)(INTROSORT_MULT)*flog2(n))+0.5f; // defines the point we give up with "basic" quicksort and swap to something more robust (but slower on average) to ensure we keep O(n*log2(n) runtime. End result is an int, but do calculations in floating point to get better resolution
 float pivot_fraction=0.5; // ratio of new largest partition size / previous partition size  => 0.5 is optimum, larger values are worse
 while(n>1)
   {/* n<=1 is already sorted */
	swap_cnt = 0;
	/* for < USE_SMALL_SORT elements to sort use insertion sort as this is faster than using quicksort */
	if (n < USE_SMALL_SORT) 
		{ 
#ifdef USE_YA_SMALLSORT /* use optimal sorts in ya-smallsort.h - max n is 32 - this makes the test program run ~ 9% slower !*/
 #if USE_SMALL_SORT > 32
   #error "USE_SMALL_SORT is set too high, max value for small_sortq() when USE_YA_SMALLSORT is defined is 32"
 #endif
#endif
 #if USE_SMALL_SORT <9
   #error "USE_SMALL_SORT is set too low, it must be at least 9" /* pivot selection code assumes it does not need to deal with segements of < 9 elements */
 #endif
		 small_sortq(a, n, es, cmp); // insertion sort if USE_YA_SMALLSORT not defined else "optimal sort".		
		 goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete
		}
		 /* try an insertion sort first, only allow MAX_INS_MOVES items out of place before we give up and use quicksort 
		    This traps already sorted partitions, and one with a small number of elements out of place.
		    If more than MAX_INS_MOVES elements are out of place, this will still move things around a bit which can help break up bad patterns for qsort
		    Remember, this code is only used when n >= USE_SMALL_SORT (typically 32), but swaps can still be near the start of the array, so linear insertion is still usefull.
		 */
	     /* optimised version - count items out of place */
		 if(es==8)
		 	{/* special case */
			 /* use linear insertion sort or binary insertion sort depending on how close to the start we are */
  			 for(size_t i=1;i<n;++i)
  			 	{ // at this point a[0]..a[i-1] is already in sorted (increasing) order
  			 	 uint64_t t = ((uint64_t *)a)[i]; // a[i]
  			 	 const size_t zes=sizeof(uint64_t);// might be faster than using es
  			 	 if(CMP(&t,a+(i-1)*zes)<0)
  			 	 	{if(++swap_cnt>MAX_INS_MOVES ) goto do_qsort; /* too many swaps - back to qsort */
					 // if out of order, select best method to fix, for big gaps use a binary search of the already sorted portion to find where to insert a[i]
  			 	 	 if(i==1)
  			 	 	 	{/* just needs a swap as its 1st 2 elements in array*/
  			 	 	 	 swapfunc(a,a+zes,es);
  			 	 	 	}
  			 	 	 else if(i<USE_SMALL_SORT)
  			 	 	 	{/* close to start , use linear search, already know [i] & [i-1] are out of order amd i>1 */
  						 ssize_t h;
						 for(h=(i-2);h>=0 && CMP(a+h*zes,&t)>0 ;h--);
						 h++;
		  			 	 memmove(a+(h+1)*zes,a+h*zes,(i-h)*zes);// move a portion of array x right by 1 to make space for t 		 
						 ((uint64_t *)a)[h]=t;	
  			 	 	 	}
  			 	 	 else
  			 	 	 	{/* big gap, do binary insertion */
  			 	 	 	 ssize_t j,h,l;
		  			 	 for(l=-1,h=i;h-l>1;)
		  			 	 	{/* binary search */
		  			 	 	 j=l+(h-l)/2; // (h+l)/2 without the risk of overflow 
		  			 	 	 if(CMP(&t,a+j*zes)<0) h=j; else l=j;
		  			 	 	}
		  			 	 memmove(a+(h+1)*zes,a+h*zes,(i-h)*zes);// move a portion of array x right by 1 to make space for t 		 
						 ((uint64_t *)a)[h]=t;	
						} 
			        }
				}			        
		 	}
		 else if(es==4)
		 	{/* special case */
			 /* use linear insertion sort or binary insertion sort depending on how close to the start we are */
  			 for(size_t i=1;i<n;++i)
  			 	{ // at this point a[0]..a[i-1] is already in sorted (increasing) order
  			 	 uint32_t t = ((uint32_t *)a)[i]; // a[i]
  			 	 const size_t zes=sizeof(uint32_t);// might be faster than using es
  			 	 if(CMP(&t,a+(i-1)*zes)<0)
  			 	 	{if(++swap_cnt>MAX_INS_MOVES ) goto do_qsort; /* too many swaps - back to qsort */
					 // if out of order, select best method to fix, for big gaps use a binary search of the already sorted portion to find where to insert a[i]
  			 	 	 if(i==1)
  			 	 	 	{/* just needs a swap as its 1st 2 elements in array*/
  			 	 	 	 swapfunc(a,a+zes,es);
  			 	 	 	}
  			 	 	 else if(i<USE_SMALL_SORT)
  			 	 	 	{/* close to start , use linear search, already know [i] & [i-1] are out of order amd i>1 */
  						 ssize_t h;
						 for(h=(i-2);h>=0 && CMP(a+h*zes,&t)>0 ;h--);
						 h++;
		  			 	 memmove(a+(h+1)*zes,a+h*zes,(i-h)*zes);// move a portion of array x right by 1 to make space for t 		 
						 ((uint32_t *)a)[h]=t;	
  			 	 	 	}
  			 	 	 else
  			 	 	 	{/* big gap, do binary insertion */
  			 	 	 	 ssize_t j,h,l;
		  			 	 for(l=-1,h=i;h-l>1;)
		  			 	 	{/* binary search */
		  			 	 	 j=l+(h-l)/2; // (h+l)/2 without the risk of overflow 
		  			 	 	 if(CMP(&t,a+j*zes)<0) h=j; else l=j;
		  			 	 	}
		  			 	 memmove(a+(h+1)*zes,a+h*zes,(i-h)*zes);// move a portion of array x right by 1 to make space for t 		 
						 ((uint32_t *)a)[h]=t;	
						} 
			        }
				}
		 	}		 	
		 else
		 	{ /* general case */
		 	 for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
		 	 	{if(pm>(char *)a && CMP( pm-es, pm)>0 && ++swap_cnt>MAX_INS_MOVES ) goto do_qsort; /* not sorted - back to qsort */
				 for (pl = pm; 
				     pl > (char *)a && CMP( pl - es, pl) > 0;
				     pl -= es)
					swapfunc(pl, pl - es, es);
				}
			}
	goto sortend; // need a common end point as may be using threads in which case we need to wait for them to complete
     
	do_qsort: ; /* comes here if not already sorted */	
   // if we have made too many iterations of this while loop then we need to swap to another method thats guaranteed O(n*log(n))

	if(++itn>max_itn) 
		{/* median of medians pivot approach is going very slowly - swap to heapsort which is relatively slow but has an execution time that does not significantly depend on sequence to be sorted */ 
	  	 dprintf("qsort: median pivot very slow - using heapsort(size=%.0f)\n",(double)n); 
		 /* int heapsort(void *vbase, size_t nmemb, size_t size,cmp_t *cmp); */ 
		 /* static void local_qsort(void *a, size_t n, size_t es, cmp_t *cmp, int nos_p) */
	  	 if(heapsort(a,n,es,cmp)==0) goto sortend; // if heapsort suceeded then we are done, otherwise we need to stick with quicksort with pivot selected as median of medians
	  	}
	/* select pivot into a[0] 
	   If we get here we know array has >USE_SMALL_SORT (typicaly 32) elements in it 
	   
	   If last partition "failed" (pivot_fraction > MAX_PIVOT_FRACTION) then we use recursive median of medians of 25 values which gives a good pivot in all cases
	   Otherwise we use the median of 25 elements if there are a "lot" of elements in the segment, otherwise use the median of 9 elements
	*/		  	
    if(pivot_fraction > MAX_PIVOT_FRACTION)  
 		{/* Previous partition split worse than expected (note this changes after every partition which is different to if conditions based on itn as that can only increase */	     
		 /* swap to using a good approximation to the median for the pivot, if that fails swap to heapsort (above) which takes much longer */
		 /* here we use the median of medians of 25 values recursively */
		 /* note that calculating the median of medians has the side effect of changing the order of the items to be sorted, as well as using all elements in the median of medians calculation */
		 /* Here we use a good approximation to the median as pivot - good enough to guarantee O(n*log(n)) sorting - as estimate found in O(n) */
		 /* note infinite sum n/25+n/25^2+... = n/24 = O(n) */
		 /* in the test program this is normally called very early and not repeatabily in the same segment suggesting that moving around of elements does have the side effect of removing "bad patterns" in the data */
		 /* using a similar approach with medains of medians of 9 values gives slightly slower results in the test program */
		 size_t n1=n; /* size of array we are calulating medians over */
		 char *p1=(char *)a; /* we place medians at start of the array */
#if 1    /* based on latest P2-approx-median version - keeping the number of medians created on each pass odd to improve the accuracy 
			Thus array sizes give the following splits:
			48 - final median 48 values
			49 - final median 49
			50 - final median50 
			51 - 25+13+13 then final median of 3 (will pick the "middle" of the 3 medians )
			52 - 25+14+13 - then final of 3
			74 - 25+25+24 -> 3
			75 - 25+25+9+8+8 -> 5
			76 - 25+25+9+9+8 ->5
			99 - 25+25+9+20+20 - final of 5
			100 - 25+25+25+13+12 - final median of 5
			101 - 25+25+25+13+13 - final median of 5
			624 - 24 of 25 + 24 => final 25
			625 - 24 of 25 +9+8+8 => final 27
			626 - 24 of 25 +9+9+8 => final 27
		 */
		 while(n1>50) /* We go down by dividing by 25 so we will end up with 25-50 in most cases now */
			{/* calculate median of 25 medians of 25 medians ... */
			 p1=(char *)a; /* we place medians at start of the array */
			 for(char *p2=(char *)a;p2<(char *)a+(ssize_t)((ssize_t)n1-24)*(ssize_t)es;p2+=25*es)
				{		
				 if((ssize_t)((ssize_t)(p2-(char *)a)/(ssize_t)es)>=((ssize_t)n1-49) && n1-(ssize_t)((ssize_t)(p2-(char *)a)/(ssize_t)es)!=25)
					{// 1 block off the end (there will be a partial block left if we keep going up in 25's)
					 // we know there are between 25 and 49 values left at this point - split these into 2 smaller medians (of 12-25 values) and use both of these at the next level of recursion
					 size_t nos_left=n1-(p2-(char *)a)/es,n2;
					 if((((p1-(char *)a)/es) & 1)==0 && nos_left>=11)
					 	{// we currently have created an even number of medians , create 1 more here then split the remainder to create 2 more to give us an odd number in total			 
					 	 dprintf("qsort:  Recursive-median: final block of %zu elements: currently an even number of medians created so doing median of 9 to make odd \n",nos_left);					 	
						 opt_med9(p2,es,cmp);// median of 9 adjacent values, result at element "+4"
						 swapfunc(p1, p2+4*es,es); /* put median at start of the array */	
						 p1+=es;
						 p2+=9*es;	
						 nos_left-=9;			 	 
					 	}
					 n2=nos_left/2;
					 small_sortq(p2, n2, es, cmp) ; // 1st median for 1st "half"
					 swapfunc(p1, p2+es*((n2-1)/2),es); /* put median at start of the array */			 
					 dprintf("qsort: recursive median of 25 - splitting final block of %zu elements into blocks of %zu and %zu\n",nos_left,n2,nos_left-n2);		 
				 	 p1+=es;
				 	 small_sortq(p2+n2*es, nos_left-n2, es, cmp) ;// 2nd median (note this may a 1 different in size to the 1st)
					 swapfunc(p1,p2+es*(n2+((nos_left-n2)-1)/2),es); /* put median at start of the array */
				 	 p1+=es;
					 break;			 	
					}		 	 		
				 /* for each block of 25 calculate median directly */
				 // printf("  doing median of 25\n");
				 opt_med25(p2,es,cmp);// median of 25 adjacent values, result at element "+12"
				 swapfunc(p1, p2+12*es, es); /* put median of medians at start of the array */
				 p1+=es;							 
				}
			 n1/=25;/* for the next iteration we repeat this for the medians of medians calculated in the previous loop */
			}

		 if(p1!=a) n1=(p1-(char *)a)/es;// number of medians from final pass  p1==a for n<49
		 dprintf("qsort: recursive median of 25 (%zu elements in total) - Final median of %zu elements\n",n,n1);
		 if(n1>1)
		 	{// need to do final median of < 25 elements (sort) [ if n1=1 a[0] already contains required median ]
		 	 small_sortq(a, n1, es, cmp); // medians are at start of array a
		 	 if(n1>2) swapfunc(a, a+((n1-1)/2)*es, es); /* put median of medians at start of the array as pivot must be at a[0] [ >2 as (2-1)/2=0 , we cannot do an mathematical average as we are not working with numbers ]*/
		 	}
		}
#elif 1    /* recursive median of medians of 25 - refined to improve accuracy now uses all values in the partition and tries to avoid medians of a small number of elements */
		 while(n1>50) /* was >=50 but that can lead to 2 values at the final median which is not good as we cannot average them. We go down by dividing by 25 so we will end up with 25-50 in most cases now */
			{/* calculate median of 25 medians of 25 medians ... */
			 p1=(char *)a; /* we place medians at start of the array */
			 for(char *p2=(char *)a;p2<(char *)a+(ssize_t)((ssize_t)n1-24)*(ssize_t)es;p2+=25*es)
				{		
				 if((ssize_t)((ssize_t)(p2-(char *)a)/(ssize_t)es)>=((ssize_t)n1-49) && n1-(ssize_t)((ssize_t)(p2-(char *)a)/(ssize_t)es)!=25)
					{// 1 block off the end (there will be a partial block left if we keep going up in 25's)
					 // we know there are between 25 and 49 values left at this point - split these into 2 smaller medians (of 12-25 values) and use both of these at the next level of recursion
					 size_t nos_left=n1-(p2-(char *)a)/es,n2;
					 n2=nos_left/2;
					 small_sortq(p2, n2, es, cmp) ; // 1st median for 1st "half"
					 swapfunc(p1, p2+es*((n2-1)/2),es); /* put median at start of the array */			 
					 dprintf("qsort: recursive median of 25 - splitting final block of %zu elements into blocks of %zu and %zu\n",nos_left,n2,nos_left-n2);		 
				 	 p1+=es;
				 	 small_sortq(p2+n2*es, nos_left-n2, es, cmp) ;// 2nd median (note this may a 1 different in size to the 1st)
					 swapfunc(p1,p2+es*(n2+((nos_left-n2)-1)/2),es); /* put median at start of the array */
				 	 p1+=es;
					 break;			 	
					}		 	 		
				 /* for each block of 25 calculate median directly */
				 // printf("  doing median of 25\n");
				 opt_med25(p2,es,cmp);// median of 25 adjacent values, result at element "+12"
				 swapfunc(p1, p2+12*es, es); /* put median of medians at start of the array */
				 p1+=es;							 
				}
			 n1/=25;/* for the next iteration we repeat this for the medians of medians calculated in the previous loop */
			}

		 if(p1!=a) n1=(p1-(char *)a)/es;// number of medians from final pass  p1==a for n<49
		 dprintf("qsort: recursive median of 25 (%zu elements in total) - Final median of %zu elements\n",n,n1);
		 if(n1>1)
		 	{// need to do final median of < 25 elements (sort) [ if n1=1 a[0] already contains required median ]
		 	 small_sortq(a, n1, es, cmp); // medians are at start of array a
		 	 if(n1>2) swapfunc(a, a+((n1-1)/2)*es, es); /* put median of medians at start of the array as pivot must be at a[0] [ >2 as (2-1)/2=0 , we cannot do an mathematical average as we are not working with numbers ]*/
		 	}
		}
#else /* this was used for <=3v5 */		 
		 dprintf("qsort: using recursive median of medians of blocks of 25 to calculate a good pivot (size=%.0f)\n",(double)n); 
		 // we know n is much larger than 25 here */
		 while(n1>=25)
			{/* calculate median of 25 medians of 25 medians ... */
			/* we place the medians at the start of the array a */
			 p1=(char *)a; /* we place medians at start of the array */
			 for(char *p2=(char *)a;p2<(char *)a+(n1-24)*es;p2+=25*es)
				{
				 /* for each block of 25 calculate median directly */
				 opt_med25(p2,es,cmp);// median of 25 adjacent values, result at element "+12"
				 swapfunc(p1, p2+12*es, es); /* put median of medians at start of the array */
				 p1+=es;							 
				}
			 n1/=25;/* for the next iteration we repeat this for the medians of medians calculated in the previous loop */
			}
		 if(p1!=a) n1=(p1-(char *)a)/es;// number of medians from final pass
		 if(n1>2)
		 	{// need to do final median of < 25 elements
		 	 small_sortq(a, n1, es, cmp); // medians are at start of array a
		 	 swapfunc(a, a+((n1-1)/2)*es, es); /* put median of medians at start of the array as pivot must be at a[0] */
		 	}
		}	
#endif			 		
	else
		{/* normal path to calculate pivot */	
		 /* move items we want to take medians of to start of array - final pivot must be at a[0] */
	 	 if(n>=USE_MEDIAN25  ) /*  typically n>=100000 for qsort */
		 	{/* median of 25 elements */	
#if USE_MEDIAN25<25
	#error "USE_MEDIAN25 must be >=25"
#endif			 
		 	 /* static void opt_med25( void *p,  size_t es, cmp_t *cmp)  median in "p[12]" */ 		 	 
		 	 const size_t d=((n-1)/24)*es;
		 	 for(size_t i=1;i<25;i++)
		 	 	swapfunc(a+i*es,a+i*d,es); /* put 25 elements at a[0]..a[24] - note a[0] is already in its required place */	 	 	
		 	 opt_med25(a,es,cmp);
		 	 swapfunc(a,a+12*es,es); /* move median to a[0] */	 	 
		 	}
		 else
		 	{
			 /* assume n>9, take median of 9 equally spaced items */
		 	/* note doing this in a loop (as per the case above) is significantly slower! */
			 pm = (char *)a + (n / 2) * es; /* middle element of array to be sorted */
			 pl = a; /* 1st element */
			 pn = (char *)a + (n - 1) * es; /* last element */
			 size_t d = (n / 8) * es;
			 swapfunc(pl+d,a+es,es);// 1st value is already in place at a[0], so this is a[1]
			 swapfunc(pl+2*d,a+2*es,es);
			 
			 swapfunc(pm-d,a+3*es,es);
			 swapfunc(pm,a+4*es,es);
			 swapfunc(pm+d,a+5*es,es);
			 
			 swapfunc(pn - 2 * d,a+6*es,es);
			 swapfunc(pn-d, a+7*es,es);
			 swapfunc(pn,a+8*es,es);
			 
			 // static void opt_med9( void *p,  size_t es, cmp_t *cmp) /* median in "p[4]" */
			 opt_med9(a,es,cmp);
			 swapfunc(a,a+4*es,es); /* put median into a[0] */
		 	}		 
		}
	// partition array into 3 sections < pivot, = pivot and > pivot 
	pa = pb = (char *)a + es;// start at a[1] as we collect values equal to the pivoy at the start (in the 1st while loop) and we have defined a[0] as the pivot

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
	 d2 = MIN(pd - pc, pn - pd - (ssize_t)es);
	 vecswap(pb, pn - d2, d2);
#if 0 /* WARNING - when enabled this stops with a "getchar()" when the pivot fraction is not "fixed" by this iteration - this should never happen ! */ 
   if(pivot_fraction > MAX_PIVOT_FRACTION)  
   		{ // last time was not good - see if fixed this time
   		 printf("Last pivot_fraction=%g checking if fixed this time. n=%.0f itn=%u , max_itn=%u\n",(double)pivot_fraction,(double)n,(unsigned int)itn,(unsigned int)max_itn);
   		 pivot_fraction=((float)MAX(pb-pa , pd-pc)-(float)MIN(pb-pa , pd-pc)-(float)(d1+d2))/((float)es*(float)n); // new fraction
   		 printf("  new pivot_fraction=%g \n",(double)pivot_fraction);
 #if 0   		 
   		 printf("d1=%.0f(%.0f) d2=%.0f(%.0f) pb-pa=%.0f(%.0f) pd-pc=%0.f(%.0f) es=%u\n",(double)d1,(double)d1/(double)es, (double)d2,(double)d2/(double)es,(double)(pb-pa),(double)(pb-pa)/(double)es,(double)(pd-pc),(double)(pd-pc)/(double)es,(unsigned int)es);
 #endif
   		 if(pivot_fraction > MAX_PIVOT_FRACTION) 
   		 	{printf("  BAD: press return to continue\n");getchar();}  // This is never seen in test program proving that code above works correctly 
   		 else
   		 	printf("  GOOD\n");
   		}
#endif	 
	 pivot_fraction=((float)MAX(pb-pa , pd-pc)-(float)MIN(pb-pa , pd-pc)-(float)(d1+d2))/((float)es*(float)n);// ( max("<",>") - min("<",">" - "=")/n gives range +/-1 with 1 being "bad", 0 being a perfect split "<" & ">"  and -1 being very good (all "=" so no further work needed)
#if 0	 
	 if(pivot_fraction > MAX_PIVOT_FRACTION)
	 	{printf("pivot_fraction (%g) > MAX_PIVOT_FRACTION (%g) on iteration %.0f n=%.0f\n",(double)pivot_fraction,(double)MAX_PIVOT_FRACTION,(double)itn,(double)n);
	 	 printf("d1=%.0f(%.0f) d2=%.0f(%.0f) pb-pa=%.0f(%.0f) pd-pc=%0.f(%.0f) es=%u\n",(double)d1,(double)d1/(double)es, (double)d2,(double)d2/(double)es,(double)(pb-pa),(double)(pb-pa)/(double)es,(double)(pd-pc),(double)(pd-pc)/(double)es,(unsigned int)es);
	 	}
#endif	 	
	 d1 = pb - pa;
	 d2 = pd - pc;
	 
 #ifdef PAR_SORT
 	 if (d1 <= d2) 
	 	{
	 	 /* Recurse on left partition, then iterate on right partition */
		 if (d1 > es) 
		 	{if(pivot_fraction > MAX_PIVOT_FRACTION || d1/es<PAR_MIN_N	)
			 	local_qsort(a, d1 / es, es, cmp,pT_i);
			 else new_thread(a, d1 / es, es, cmp,pT_i);
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
		 	{if(pivot_fraction > MAX_PIVOT_FRACTION || d2/es<PAR_MIN_N	)
			 	local_qsort(pn - d2, d2 / es, es, cmp,pT_i);	
			 else new_thread(pn - d2, d2 / es, es, cmp,pT_i);	 
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
 return;   	
}
 #else	 	 
	 if (d1 <= d2) 
	 	{
	 	 /* Recurse on left partition, then iterate on right partition */
		 if (d1 > es) 
		 	{
			 local_qsort(a, d1 / es, es, cmp,pT_i);
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
			 local_qsort(pn - d2, d2 / es, es, cmp,pT_i);		 
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
 return;   	
}
#endif

void qsort(void *a, size_t n, size_t es, cmp_t *cmp)
{   
 if(n<=1 || es==0) return; /* array of size 1 is sorted by definition, and elements of size 0 cannot be sorted */
 if (n < USE_SMALL_SORT) 
		{/* we are just being asked to sort a small array - just do it here to minimise unnecessary overhead (this is exactly what local_qsort() would do for sorting )*/
		 small_sortq(a,n, es,cmp);
		 return; // all done
		}
#ifdef PAR_SORT			
 if(n<PAR_MIN_N)
 		{/* if we would not use threads then we save some overhead by not setting up for threads */
		 local_qsort(a, n, es, cmp,NULL);
		 return; // all done
		}
 struct T_info T_i; /* local copy of key threading structure, we pass the adddress of this around so functions can access it as required */
 setup_threads(&T_i);
 local_qsort(a, n, es, cmp,&T_i);/* call main worker function */
 wait_all_threads(&T_i); // wait for all threads to complete 
 close_threads(&T_i); 
#else
 local_qsort(a, n, es, cmp,NULL);
#endif
}


/* heapsort.h */
/* Note this may already be defined in stdlib.h (eg on BSD based systems) so you may not need to include heapsort.h */
#ifndef __HEAPSORT_H
 #define __HEAPSORT_H
 #ifdef __cplusplus
  extern "C" {
 #endif 
	int heapsort(void *vbase, size_t nmemb, size_t size,int (*compar)(const void *, const void *));// in heapsort.c 
 #ifdef __cplusplus
    }
 #endif
#endif

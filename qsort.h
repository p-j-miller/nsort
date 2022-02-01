/* qsort.h */
/* Note: qsort is part of the standard C library where its defined in <stdlib.h>, its therefore unlikely you will need to use this header */
#ifndef __QSORT_H
 #define __QSORT_H
 #ifdef __cplusplus
  extern "C" {
 #endif 
	void qsort(void *a, size_t n, size_t es, int (*compar)(const void *, const void *));
 #ifdef __cplusplus
    }
 #endif
#endif

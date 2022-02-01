/* nsort.c 
   =======
   loosely based on sort in section 5.11 from K&R "The C programming Language" 2nd edition 
   
   sorts stdin and prints result to stdout
   if "-n" is present does numeric sort on 1st field, otherwise does a string sort
   if "-q" is present allows numbers inside double quotes and sorts based on the number. -q implies -n .
   if -n present non-numeric lines will sort first (so a csv files header should stay first)
   -u only displays unique (different) lines (so deletes duplicates).
   -h or -? print basic helptext and exit.
   
   Has limits on line length and total number of lines as it reads the whole input into RAM before sorting it.
   
   sorts into increasing order.
   
   This version (c) Peter Miller 2022.
   Uses (optionally, but by default) fast_atof from https://github.com/p-j-miller/ya-sprintf  and qsort from https://github.com/p-j-miller/yasort-and-yamedian
   
   Version 1.0 31/12/2020 - 1st version on github
   Version 1.1 1/2/2022 - swapped to use qsort.c from yasort-and-yamedian as this is always O(n*log(n)) execution speed and all available processors for sorting which can be a lot faster
   						- on a 2 processor PC the sort phase was 2.5* faster and the complete time 1.5* faster on the 1M line test file.

*/

/*----------------------------------------------------------------------------
 * Copyright (c) 2020,2022 Peter Miller
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *--------------------------------------------------------------------------*/
/* Define __USE_MINGW_ANSI_STDIO to 1 to use C99 compatible stdio functions on MinGW. see for example https://stackoverflow.com/questions/44382862/how-to-printf-a-size-t-without-warning-in-mingw-w64-gcc-7-1 */
// #define __USE_MINGW_ANSI_STDIO 1   /* if this is defined then writing to stdout > NUL is VERY slow (70 secs vs 4 secs) ! Note strtof() etc are still much slower than fast_atof() etc */
/* use mingw64 as compiler - latest TDM-GCC 10.3.0 has __USE_MINGW_ANSI_STDIO set to 1 by default so is very slow writing to NUL: and much slower writing to stdout generally  */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h> /* for bool */
#include <ctype.h>
#include <float.h>
#include <time.h>
#include <math.h>
#include <stdint.h>  /* for int64_t etc */
#include <inttypes.h> /* to print uint64_t */
#include "qsort.h" /* qsort.c used */

#define VERSION "1.1" /* using qsort.c rather than libc qsort */

#define USE_FAST_ATOF /* if defined use my fast_atol() from ya-sprintf [which should be much faster] , otherwise use strtod() from the standard library */

#if defined(USE_FAST_ATOF) 
double fast_atof(const char *string,bool * not_number)	; // converts string to double. Skips leading whitespace. sets not_number true if no number found (and returns 0) otherwise sets it false and returns number read.
double fast_strtod(const char *s,char ** endptr);// like strtod() but faster
double fast_atof_nan(const char *s);// like fast_atof, but returns NAN if the string does not start with a valid number
float fast_strtof(const char *s,char **endptr); // like strtof() but faster
extern const int fast_strtof_u; // tell rest of system we are using u64's or u32's (only useful for diagnostics/debugging)
#endif

char *strdup(const char *s); // not in gcc 5.1.0 string.h

#define nsort_num_float /* if defined do numeric sorts with float rather than double */

// following variables used internally by readline():
static const unsigned int FIRST_SIZE=256; /* initial size of line buffer start at eg 256, size is doubled if its too small */
static char *buf=NULL; /* input buffer */
static unsigned int buf_size=0; /* input buffer size, 0 means not yet allocated */

// variables used by readlines (also uses FIRST_SIZE from above)
static char **lineptr=NULL; /* pointers to lines of text */
static unsigned int lines_buf_size=0; // current size of lineptr[]
static unsigned int nlines=0; // nos lines actually in use in lineptr



bool quoted_numbers=false; /* if true allows numbers with double quotes ("123") to be sorted numerically */
bool do_uniq=false; /* set to true when -u (unique) option specified on command line */
bool verbose=false; // set to 1 if -v option present

int readlines(void);
void writelines(void);
int numcmp(const char *, const char *);


 /*  these are the compare routines for qsort() */
int mysCompare (const void * a, const void * b ) { /* compare as strings */
    const char *pa = *(const char**)a;
    const char *pb = *(const char**)b;
    return strcmp(pa,pb);
}

int mynCompare (const void * a, const void * b ) { /* compare as numbers */
    const char *pa = *(const char**)a;
    const char *pb = *(const char**)b;
    return numcmp(pa,pb);
}


/* numcmp: compare s1 and s2 numerically */
/* this version allows numbers in quotes if -q option is specified on command line (quoted_numbers=true) */
/* also treats non-numbers as very large negative numbers to the sort first [ so a csv file header wil stay at the front of the file ] */
/* if numbers are identical then sort as strings. This is needed for -u option, but defines order so seens sensible anyway */

int numcmp(const char *s1, const char  *s2)
{
#ifdef  nsort_num_float /* if defined do numeric sorts with float rather tha double */	
 float v1,v2;
 char *sret;
#else
 double v1, v2;

#ifdef USE_FAST_ATOF 	 
 bool not_number;
#else
  char *sret;
#endif   
#endif 
  while(isspace(*s1)) ++s1; /* skip initial whitespace */
 if(quoted_numbers && *s1=='"') 
 	{++s1; // skip " if allowed (no need to worry about trailing " as that will just terminate the number )
 	}
#ifdef  nsort_num_float /* if defined do numeric sorts with float rather tha double */	
 #ifdef USE_FAST_ATOF 
 v1=fast_strtof(s1,&sret);
 #else
 v1=strtof(s1,&sret);
 #endif
 if(sret==s1)  v1= -FLT_MAX; // very large negative number if no number found so sorts first	
#else
#ifdef USE_FAST_ATOF 	
 v1=fast_atof(s1,&not_number);
 if(not_number) v1= -DBL_MAX; // very large negative number if no number found so sorts first
#else
 v1=strtod(s1,&sret);
 if(sret==s1)  v1= -DBL_MAX; // very large negative number if no number found so sorts first
#endif
#endif 
 while(isspace(*s2)) ++s2; /* skip initial whitespace */
 if(quoted_numbers && *s2=='"') ++s2; // skip " if allowed  
#ifdef  nsort_num_float /* if defined do numeric sorts with float rather than double */	
 #ifdef USE_FAST_ATOF 
 v2=fast_strtof(s2,&sret);
 #else
 v2=strtof(s2,&sret);
 #endif
 if(sret==s2)  v2= -FLT_MAX; // very large negative number if no number found so sorts first
#else 
#ifdef USE_FAST_ATOF  
 v2=fast_atof(s2,&not_number);
 if(not_number) v2= -DBL_MAX; // very large negative number if no number found so sorts first
#else
 v2=strtod(s2,&sret);
 if(sret==s2)  v2= -DBL_MAX; // very large negative number if no number found so sorts first
#endif  
#endif
 if(v1==v2)
 	return strcmp(s1,s2);	
 if (v1 < v2)
	return -1;
 else
 	return 1;	
}



/* writelines: write output lines in sorted order */
/* if -u (unique) option set then only print lines that are different to previous line */
void writelines(void)
{
 unsigned int i;
 for (i = 0; i < nlines; i++)
 	{if(!do_uniq || i==0 ||  strcmp(lineptr[i-1],lineptr[i])) // always print 1st line, or if do_uniq is false. if do_uniq is true and not 1st line print lines that are different
		printf("%s\n", lineptr[i]);
	}
}


char *readline (FILE *fp)
/* read next line from input and return a pointer to it. Returns NULL on EOF or error . Deletes \n from end of line */
/* the same buffer is reused for the next input */
/* no limit on the length of the line (except available RAM) */
{int n;
 char *cp;
 int c=0;
 char *new_buf;
 unsigned int new_size;
 if(buf_size==0)
        {if((buf=(char *)malloc(FIRST_SIZE))==NULL )
                return NULL; /* oops no space at all */
         buf_size=FIRST_SIZE;
        }
 cp=buf;
 n=buf_size;
 while(1)
        {while(--n && (c=getc(fp)) != EOF) /* while fits into existing buffer */
                {if(c=='\n')
                        {*cp='\0'; /* end of string */
                         return buf;
                        }
                  *cp++=c;
                 }
         if(c==EOF)
                return cp==buf ? NULL : buf; /* if we hit EOF then return last line if any */
#if 1
		 new_size= (buf_size | 1023) + 1021; // Increase the size of the buffer a "bit", rounds up to next multiple of 1024 and adds 1020. from an idea at https://stackoverflow.com/questions/43594181/using-parallel-arrays-in-c-to-sort-out-lines-of-data
#else                
         new_size=buf_size <<1; /* double size of buffer */
#endif         
         if((new_buf=(char *)realloc(buf,new_size))==NULL)
                {/* realloc failed "eat" rest of line and return truncated line to caller */
                 while((c=getc(fp)) != EOF && c!= '\n');
                 *cp='\0';
                 return buf;
                }
           else
                { /* realloc went OK */
                 cp=new_buf+(buf_size-1); /* reposition pointer into new buffer */
                 n=buf_size+1;
                 buf=new_buf;
                 buf_size=new_size;
                }
        }
/* NOT REACHED */
}

/* readlines: read input lines */
/* returns -1 on error , >=0 if OK */
/* no limit on the number of lines that can be read (except available RAM). */
int readlines(void)
{
 char *p,*l;
 nlines = 0;
 while((l=readline(stdin))!= NULL)
 	{
	 if ((p = strdup(l)) == NULL)
		return -1; // no space for a copy of the line just read in
	 if(lines_buf_size==0)
	 	{// need to alocate initial spce for lineptr
		 lineptr=calloc(FIRST_SIZE,sizeof(char *));
		 if(lineptr==NULL) 
		 	return -1; // no space
		 lines_buf_size=FIRST_SIZE;
		}
	 else if(nlines>=lines_buf_size)	
	 	{// need to increase the space to store lines
#if 0
		 lines_buf_size= (lines_buf_size | 1023) + 1021; // Increase the size of the buffer a "bit", rounds up to next multiple of 1024 and adds 1020. from an idea at https://stackoverflow.com/questions/43594181/using-parallel-arrays-in-c-to-sort-out-lines-of-data
#else  	 	
	 	 lines_buf_size <<=1 ; // double space
#endif	 	 
		 lineptr=realloc(lineptr,lines_buf_size*sizeof(char *));
		 if(lineptr==NULL) 
		 	return -1; // no space
		}
	 lineptr[nlines++] = p; // store line just read into array
	}
 return nlines;
}


/* sort input lines */
int main(int argc, char *argv[])
{
 bool numeric = false; /* true if numeric sort */
 char c;
 clock_t start_t,end_t; 
 /* based on argument parser from K&R pp 117. allows both nsort -nq and nsort -n -q */
 while(--argc>0 && (*++argv)[0] == '-')
 	{ while( (c= *++argv[0]) ) /* yes this is an assignment operator !, extra brackets due to gcc warning. */
 		switch(tolower(c))
 			{
 			 case 'n': 	numeric=true;  break;
 			 case 'q': 	quoted_numbers=true; numeric=true; break; // -q implies -n
 			 case 'u':  do_uniq=true;  break;
 			 case 'v':  verbose=true;  break;  
 			 case '?':  // falls through to 'h' below
 			 case 'h': 	
	 					argc= -1; //cause "usage" message then exit
	 					break;
 			 default: 	fprintf(stderr,"nsort: invalid option -%c\n",c);
 				 		argc= -1;
 				 		break;
 			}
 	}
 if(argc>0) // we want 0 as only -xx arguments expected on command line
 	{fprintf(stderr,"nsort: Invalid argument \"%s\"\n",*argv);
	 argc= -1; //cause "usage" message then exit
	} 				
 if(argc<0)
 	{fprintf(stderr,"nsort version %s created at %s on %s\n sorts stdin to stdout printing the result in increasing order\n",VERSION,__TIME__,__DATE__);
 	 if(verbose) 
 		{
#if defined(USE_FAST_ATOF)  			
 #ifdef nsort_num_float /* if defined do numeric sorts with float rather than double */	 		
	 	 fprintf(stderr,"Compiled with gcc %s pointer size=%d numeric compare type=float fast_strtof uses u%d\n",__VERSION__,__SIZEOF_POINTER__,fast_strtof_u);
 #else
	 	 fprintf(stderr,"Compiled with gcc %s pointer size=%d numeric compare type=double fast_strtof uses u%d\n",__VERSION__,__SIZEOF_POINTER__,fast_strtof_u);	 
 #endif
#else
 #ifdef __USE_MINGW_ANSI_STDIO
	 	 fprintf(stderr,"Compiled with gcc %s pointer size=%d  __USE_MINGW_ANSI_STDIO defined\n",__VERSION__,__SIZEOF_POINTER__);
 #else
		 fprintf(stderr,"Compiled with gcc %s pointer size=%d  __USE_MINGW_ANSI_STDIO NOT defined\n",__VERSION__,__SIZEOF_POINTER__);
 #endif	
#endif 
		}	
 	 fprintf(stderr,"Usage: nsort [-nquv?h]\n");
	 fprintf(stderr,"-n lines are assumed to start with numbers and sorting is done on these.\n");
	 fprintf(stderr,"   if the numbers are identical the lines are sorted as strings\n");
	 fprintf(stderr,"-q sort on initial numbers in double quotes (implies -n) \n");
	 fprintf(stderr,"   otherwise sort lines as strings\n");
	 fprintf(stderr,"-u only print lines that are unique (ie deletes duplicates)\n");
	 fprintf(stderr,"-v verbose output (to stderr) - prints execution time etc\n");
	 fprintf(stderr,"-? or -h prints (this) help message then exists\n");
	 return 1;
	} 	
 if(verbose) 
 	{
#if defined(USE_FAST_ATOF) 		 		
 #ifdef nsort_num_float /* if defined do numeric sorts with float rather than double */	 		
	 fprintf(stderr,"nsort version %s compiled with gcc %s pointer size=%d numeric compare type=float fast_strtof uses u%d\n",VERSION,__VERSION__,__SIZEOF_POINTER__,fast_strtof_u);
 #else
	 fprintf(stderr,"nsort version %s compiled with gcc %s pointer size=%d numeric compare type=double fast_strtof uses u%d\n",VERSION,__VERSION__,__SIZEOF_POINTER__,fast_strtof_u);	 
 #endif	 
#else
 #ifdef __USE_MINGW_ANSI_STDIO
	 	 fprintf(stderr,"Compiled with gcc %s pointer size=%d  __USE_MINGW_ANSI_STDIO defined\n",__VERSION__,__SIZEOF_POINTER__);
 #else
		 fprintf(stderr,"Compiled with gcc %s pointer size=%d  __USE_MINGW_ANSI_STDIO NOT defined\n",__VERSION__,__SIZEOF_POINTER__);
 #endif	
#endif 
	}
 /* now do the actual sorting ... */
 start_t=clock();		
 if (readlines() >= 0) {
 	if(verbose)
 		{
 		 end_t=clock();
 		 fprintf(stderr,"nsort: read in %d lines in %.3f secs\n",nlines,(end_t-start_t)/(double)(CLOCKS_PER_SEC));
 		 start_t=clock();
 		}
    qsort(lineptr,nlines,sizeof(char *),
    	(int (*)(const void*,const void*))(numeric ? mynCompare : mysCompare)); /* actually do the sort */		
 	if(verbose)
 		{
 		 end_t=clock();
 		 fprintf(stderr,"nsort: sort took %.3f secs\n",(end_t-start_t)/(double)(CLOCKS_PER_SEC));
 		 start_t=clock();
 		}    	
	writelines(); /* write out lines in sorted order */
 	if(verbose)
 		{
 		 end_t=clock();
 		 fprintf(stderr,"nsort: output written in %.3f secs\n",(end_t-start_t)/(double)(CLOCKS_PER_SEC));
 		} 	
	return 0;
   } else {
	fprintf(stderr,"nsort: error input too big to sort\n");
	return 1;
   }
}


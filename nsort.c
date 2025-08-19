/* nsort: Sort large files by merging subfiles */
/* Peter Miller 30-6-2025
	Usage: nsort [-nquv?h] [-o ofile] [ifile]
	nsort sorts stdin or ifile if thats given on the command line.
	-n lines are assumed to start with numbers and sorting is done on these.
	   if the numbers are identical the lines are sorted as strings
	-q sort on initial numbers in double quotes (implies -n) 
	   otherwise sort lines as strings
	-u only print lines that are unique (ie deletes duplicates)
	-o save sorted output in ofile (by default its written to stdout)
	-v verbose output (to stderr) - prints execution time etc
	-? or -h prints (this) help message then exists
	If [ifile] is provided input is read from this (single) file, otherwise stdin is read
	It is OK for ofile and ifile to be the same file, but clearly the original contents of the file are lost in this case
	The major change in this version is that nsort can sort files that are larger than the available RAM (size is only limited by available(free) harddisk space).

  Note this is a rewrite of the previous version of nsort, so while 1v2 was the current released version on github internal numbering here started at 0v1.
  0v1 - 1st functioning version - but with hard limits on line lenghts etc.
  0v2 - dynamically allocates space so no hard limits on line lengths - may still run out of disk space or exceed MAXLINES*MAXSUBFILES lines
  0v3 - fixes up missing newline on last line of input
  0v4 - prints execution time
  0v5 -  swapped to my qsort.c execution time on demo1M-leading0s.csv (over 1M lines)
  	nsort 0v4	0.528 secs (standard library qsort)
  	nsort 0v5	0.430 secs (my qsort, 512 byte buffers, MAXLINES 100000)
  	nsort 0v5 	0.414 secs (my qsort, 16384 byte buffers for stdin/stdout,MAXLINES 100,000)
  	nsort 0v5 	0.283 secs (my qsort, 16384 byte buffers for stdin/stdout,MAXLINES 10,000,000)
  	nsort 1v2	0.629 secs (current version on github)
  	usort 0v6	1.571 secs (Unix sort program ported to Windows by P.Miller)
  	usort 0v6	1.222 secs with -y  (use maximum RAM)
  	
  	Note with MAXLINES 10,000,000 and MAXSUBFILES 8000 then files with 80,000,000,000 (80G lines) can be sorted
	  MAXLINES could be made significantly larger if necessary, but that seems unnecessary at present.
	  
  0v6 - new approach now "sub-merges" files to limit the total number of merge files means only limit to the size of file that can be sorted is disk space (and time!)
   	execution time on demo1M-leading0s.csv (over 1M lines)
   	MAXLINES  10,000 MAXSUBFILES 8	: 1.603 secs
   	MAXLINES  10,000 MAXSUBFILES 16	: 1.018 secs
   	MAXLINES  10,000 MAXSUBFILES 32	: 0.867 secs 
   	MAXLINES  10,000 MAXSUBFILES 64	: 0.719 secs
   	MAXLINES  10,000 MAXSUBFILES 128: 0.658 secs (no sub-merges required)
   	MAXLINES 100,000 MAXSUBFILES 8	: 0.534 secs
   	MAXLINES 100,000 MAXSUBFILES 16	: 0.431 secs (no sub-merges required)
  
  0v7 - use comp() for all comparisons (previous versions only used this for qsort , not for merging)
  0v8 - merged in full nsort functionality
  0v9 - swapped to use my strtod() from atof.c for a speedup of ~ 3.5* on numeric sorts
  		nsort -vnu <demo1M-leading0s.csv > esortout0.txt went from 2.128 secs to 0.602 secs (MAXLINES 100,000, MAXSUBFILES 16)
  		and 1.526 secs to 0.433 secs with MAXLINES 10,000,000 MAXSUBFILES 16
  1v0 - renamed nsort (as it now has all of nsorts functionality, but is faster and can sort much larger files)
  1v1 - control-C & control-break trapped under windows and tidies up tmp files before exiting.
  1v2 - more general control-c handling 
  1v3 - allows a input filename on command line rather than only using stdin and -o file to specify output filename (rather than stdout)
  	  - note that input filename and output filename must be different (this is trapped and a warning given before input file is overwritten ).
  	    [ Note the trap is a string compare of filenames which is not guaranteed to detect the fact that the files & paths don't point to the same file ]
  1v4 - allow input filename and output filename to be the same file (when specified on the command line)	
  1v5 - swapped to latest version of atof.c (from wmawk2), use only if USE_FAST_STRTOD defined
  -> released to github at 2v0 as significant changes from the previous release (1v2).
      
*/
/*----------------------------------------------------------------------------
 * Copyright (c) 2025 Peter Miller
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
#define UNIX_SIGNALS /* if defined use Unix style signals (signal.h) - this does work under Windows with minGW / winlibs */
				/* if not defined uses native Windows capabilities to trap control-C and control-break */
#define USE_FAST_STRTOD

#ifdef UNIX_SIGNALS
 #include <signal.h>
#elif defined _WIN32 /* defined when compiling for windows, either 32 or 64 bits */
 #include <windows.h> /* control-C handling  specific to windows */
#endif 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>
#include <float.h>
#include "qsort.h"

#define VERSION "2v0"
// #define DEBUG

#ifdef USE_FAST_STRTOD
 double fast_strtod(const char *s,char ** endptr); /* in atof.c */
 #define strtod fast_strtod
#endif

#define MAXLINES 10000000 /* 10000000 10,000,000 */
#define MAXSUBFILES 16 /* TMP_MAX is 2147483647 for windows 11 using ucrt, but we have an array of size MAXSUBFILES we need to keep in RAM  */
	/* note that https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setmaxstdio?view=msvc-170 says that by default a maximum of 512 files can be simultaneously open at the files level
	   with an absolute max of 8192 files which is a much smaller limit than TMP_MAX 
	   FOPEN_MAX is 20 but I assume that is a bug?
	*/

#define INIT_BUF_SIZE 256 /* initial length of line input buffer */
#define BUF_SIZE_INC 128  /* increment to length of line input buffer if current value is too small */

#define VBUF_SIZE 16384 /* specify buffer size for stdin/stdout - if not defined BUFSIZ (512 bytes with gcc 15.1.0 and ucrt) is used */

bool verbose=false; /* set to true if -v option present */

size_t buf_size = INIT_BUF_SIZE; /* current size of input line buffer, is increased automatically if necessary */
 
struct subfile
{
    FILE *f;
    char *line; /* pointer to space for the longest line in the file "f" */
    size_t line_size; /* length of line */
};

int (*comp)(const void* a,const void* b) ; /* function that is used withn the sorting code to do the compare, set to one of the functions below */
int mysCompare (const void * a, const void * b ); /* compare as strings */
int mynCompare (const void * a, const void * b ); /* compare as numbers */
int mynqCompare (const void * a, const void * b ); /* compare as numbers within double quotes */

bool make_subfile(char *[], size_t, struct subfile *, size_t);
#ifdef UNIX_SIGNALS	
static void CtrlHandler(int);
#elif defined _WIN32 
	/* Control-C handler for Windows */
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType); 
#endif

int main(int argc, char *argv[])
{	
 size_t i; 
 char c;
 bool merge_flag = false;/* set to true if we need to merge files */
 size_t nlines, nfiles = 0, min_idx;
 static char *s, *lines[MAXLINES];
 static struct subfile subfiles[MAXSUBFILES];
 clock_t start_t,end_t; 
 bool do_uniq=false; /* set to true when -u (unique) option specified on command line */
 bool need_outfile=false; /* set to true if -o filename present on command line */
 char *infilename="stdin",*outfilename="stdout";
 start_t=clock();
 comp=mysCompare; /* compare as strings by default */
 s=malloc(buf_size);

 if(s==NULL)
	{fprintf(stderr,"nsort: Error - No free RAM found at start!\n");
	 exit(1);
	}

if(_getmaxstdio()<(MAXSUBFILES+4) && _setmaxstdio(MAXSUBFILES+4)==-1) /* if necessary, try and set maximum number of open files +3 for stdin, stdout, stderr + one more for merging  */
	fprintf(stderr,"nsort: Warning - tried to set maximum number of open files to %d but failed and limit is %d\n",MAXSUBFILES,_getmaxstdio());
#ifdef UNIX_SIGNALS	
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)  /* control-C */
		signal(SIGINT, CtrlHandler);
	if (signal(SIGBREAK, SIG_IGN) != SIG_IGN) /* control-Break */
		signal(SIGBREAK, CtrlHandler);		
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, CtrlHandler);
#elif defined _WIN32 
	/* Control-C & control-break handler for Windows */
if (SetConsoleCtrlHandler(CtrlHandler, TRUE))
    { /* handler set OK */
   	}
 else
    {
     fprintf(stderr,"nsort: Warning - Could not set control-C handler\n");
     /* ignore this - but if ^C typed tmp files may be left around */
   	}
#endif

/* based on argument parser from K&R pp 117. allows both nsort -nv and nsort -n -v */
 while(--argc>0 && (*++argv)[0] == '-')
 	{ while( (c= *++argv[0]) ) /* yes this is an assignment operator !, extra brackets due to gcc warning. */
 		switch(tolower(c))
 			{
 			 case 'n': 	comp=mynCompare;  break;  // compare numbers
 			 case 'o':  need_outfile=true; break; // outputfile specified on command line
 			 case 'q': 	comp=mynCompare; break;   // compare quoted numbers 
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
 if(need_outfile)
 	{// -o given as a command line argument 
	 if(argc>0)
	 	{/* filename given on command line , just reuse stdin so no other impacts on the code*/
	 	 argc--;
	 	 /* cannot actually open output file yet - we need to check its not the same as the input file */
		 outfilename= *argv;	
#ifdef DEBUG
		 fprintf(stderr,"nsort: Debug - sort output will go to file \"%s\"\n",outfilename);
#endif
		 ++argv;
		} 
	 else
	 	{fprintf(stderr,"nsort: Error no filename for -o argument\n");
	 	 argc= -1;
	 	}
	} 	 
 if(argc>0)
 	{/* filename given on command line , just reuse stdin so no other impacts on the code*/
 	 argc--;
 	 if(freopen(*argv,"r",stdin)==NULL)
	 	{fprintf(stderr,"nsort: cannot open file \"%s\"\n",*argv);
		 argc= -1; //cause "usage" message then exit
		}
	 infilename= *argv;	
#ifdef DEBUG
	 fprintf(stderr,"nsort: Debug - sorting file \"%s\"\n",infilename);
#endif
	 ++argv;
	} 	 
 if(argc>0) // we want 0 as only -xx arguments expected on command line
 	{fprintf(stderr,"nsort: Invalid argument \"%s\"\n",*argv);
	 argc= -1; //cause "usage" message then exit
	} 
#if 0	/* not needed as we now open output file after all of input has been read */
 if(strcmp(infilename,outfilename)==0)
 	{fprintf(stderr,"nsort: Error - input and output filenames cannot be the same (both set to \"%s\")!\n",infilename);
	 argc= -1;	//cause "usage" message then exit
	}
 else if(need_outfile)
 	{/* must leave this to here otherwise if input and output filenames are the same we would erase input file when we tried to open output file */
 	 /* a string compare is not guaranteed to tell if both paths/filename actually point to the same file but should trap most cases */
	 if(freopen(outfilename,"w",stdout)==NULL)
	 	{fprintf(stderr,"nsort: cannot open file \"%s\" for output [ -o file]\n",outfilename);
		 argc= -1; //cause "usage" message then exit
		}
	}	
#endif			
 if(argc<0)
 	{fprintf(stderr,"\nnsort version %s created at %s on %s\n nsort sorts its input into increasing order\n",VERSION,__TIME__,__DATE__);
 	 if(verbose) 
 		{
 	 	 fprintf(stderr,"Compiled on %s at %s with gcc %s pointer size=%d\n",__DATE__,__TIME__,__VERSION__,__SIZEOF_POINTER__);
		}	
 	 fprintf(stderr,"\nUsage: nsort [-nquv?h] [-o ofile] [ifile]\n");
	 fprintf(stderr,"-n lines are assumed to start with numbers and sorting is done on these.\n");
	 fprintf(stderr,"   if the numbers are identical the lines are sorted as strings\n");
	 fprintf(stderr,"-q sort on initial numbers in double quotes (implies -n) \n");
	 fprintf(stderr,"   otherwise sort lines as strings\n");
	 fprintf(stderr,"-u only print lines that are unique (ie deletes duplicates)\n");
	 fprintf(stderr,"-o save sorted output in ofile (by default its written to stdout)\n");
	 fprintf(stderr,"-v verbose output (to stderr) - prints execution time etc\n");
	 fprintf(stderr,"-? or -h prints (this) help message then exists\n");
	 fprintf(stderr,"If [ifile] is provided input is read from this (single) file, otherwise stdin is read\n");
	 fprintf(stderr,"It is OK for ofile and ifile to be the same file, but clearly the original contents of the file are lost in this case\n");
	 exit(1);
	} 	
 if(verbose) 
 	{
 	 fprintf(stderr,"nsort version %s compiled on %s at %s with gcc %s pointer size=%d\n",VERSION,__DATE__,__TIME__,__VERSION__,__SIZEOF_POINTER__);
 	 fprintf(stderr,"nsort will sort \"%s\" with output to \"%s\"\n",infilename,outfilename);
	}

#ifdef VBUF_SIZE
/* set input buffer size - note merge files only use standard buffer sizes [ left to here as code above may change stdin to file] */
 setvbuf(stdin,NULL,_IOFBF,VBUF_SIZE);
#endif
 for(i=0;i<MAXSUBFILES;++i)
 	subfiles[i].line=NULL; /* we use this to tell if an element has been used */

  /* Read file - form subfiles if needed */
 for (nlines = 0; fgets(s,buf_size,stdin); ++nlines)
   	{/* first deal with long lines */
   	 while ( strchr(s, '\n') == NULL)
		{/* have not read in the whole line (which should be \n terminated, except for very last line in a file) */
		 char *new_s;
		 size_t len_s=strlen(s);
		 if(len_s < buf_size - 1 )
		 	{
#ifdef DEBUG
		 	 fprintf(stderr,"nsort: Debug - line has no \\n but fits in buffer \"%s\" - assuming last line of file and adding \\n\n",s);
		 	 fprintf(stderr," s[len_s-1]=%x s[len_s]=%x s[len_s+1]=%x\n",(int)s[len_s-1], (int)s[len_s], (int)s[len_s+1]);
#else
			 if(verbose) fprintf(stderr,"nsort: warning - last line of input has no \\n so nsort has added one\n");		 	 
#endif		 
			 s[len_s]='\n';
			 s[len_s+1]=0;
			 break;	
		 	}
#ifdef DEBUG
		 fprintf(stderr,"nsort: Debug - buf_size (%zu) too small as not all line read in - we have \"%s\" so far\n",buf_size,s);
#endif		 
		 buf_size += BUF_SIZE_INC;
		 new_s = realloc(s, buf_size);
		 if(new_s==NULL)
		 	{/* out of memory - flush existing lines to a merge file to make some space */
	         merge_flag = true;  
	         if(make_subfile(lines,nlines,subfiles,nfiles++)) nfiles=2;
	         nlines=0;
	         new_s = realloc(s, buf_size); /* should now be able to allocate space */
	        }
		 if(new_s==NULL)
		 	{/* really out of memory, even after flushing lines */
		 	 fprintf(stderr,"nsort: Error [1] - out of RAM - buf_size=%zu\n",buf_size);
		 	 exit(1);
		 	}
		 s=new_s; /* new (bigger) buffer */
		 fgets(s+buf_size-BUF_SIZE_INC-1,BUF_SIZE_INC+1,stdin); /* read more of line ,-1 for terminating 0 on existing string */
#ifdef DEBUG
		 fprintf(stderr,"nsort: Debug - buf_size is now %zu - after next fgets() we have \"%s\" so far\n",buf_size,s);
#endif		 
		}
     if (nlines == MAXLINES || (lines[nlines] = malloc(strlen(s)+1)) == NULL)
        {
         /* Sort lines to a temporary merge file */
         merge_flag = true;  
         if(make_subfile(lines,nlines,subfiles,nfiles++)) nfiles=2;
         lines[nlines = 0] = malloc(strlen(s)+1);
		 if(lines[0]==NULL)
		 	{/* really out of memory, even after flushing lines */
		 	 fprintf(stderr,"nsort: Error [2] - out of RAM - buf_size=%zu\n",buf_size);
		 	 exit(1);
		 	}         
        }
     strcpy(lines[nlines],s);
    }	
 /* at this point all the input has been read (either to RAM or to one or more merge files */
 if(need_outfile)
 	{/* must leave this to here otherwise if input and output filenames are the same we would erase input file when we tried to open output file */
 	 fclose(stdin); /* output may go to the same file as used for the input, so its safer to do close the input file first */
	 if(freopen(outfilename,"w",stdout)==NULL)
	 	{fprintf(stderr,"nsort: cannot open file \"%s\" for output [ -o file]\n",outfilename);
		 exit(1); // exit
		}
	}
#ifdef VBUF_SIZE
/* set output file buffer size [ left to here as code above may change stdout to a file] */
 setvbuf(stdout,NULL,_IOFBF,VBUF_SIZE);
#endif
 if (merge_flag)
    	{
         /* Form last merge file from remaining lines */
         if(make_subfile(lines,nlines,subfiles,nfiles++)) nfiles=2;
		 if(verbose) fprintf(stderr,"nsort: merging %zd files\n",nfiles);
         /* Prepare to read temporary files */
         for (i = 0; i < nfiles; ++i)
        	{
             FILE *f = subfiles[i].f;
             rewind(f);
             fgets(subfiles[i].line,subfiles[i].line_size,f);
        	}

         /* Do the (final) merge */
         s[0]=0;/* s was used to read input, now used to remember previous line output if do_uniq is true */
         while (nfiles)
        	{
             struct subfile *sfp;

             /* Find next output line */
             for (min_idx = 0, i = 1; i < nfiles; ++i)
                if (comp(&(subfiles[i].line),
                           &(subfiles[min_idx].line)) < 0)
                    min_idx = i;
             sfp = &subfiles[min_idx];

             /* Output the line */
             if(do_uniq)
             	{if(strcmp(s,sfp->line))
					{// if lines different print and copy new line to s
             		 fputs(sfp->line,stdout);
             		 strcpy(s,sfp->line);
             		}
             	}
             else
             	fputs(sfp->line,stdout);
             /* Get the next line from this file */
             if (fgets(sfp->line,sfp->line_size,sfp->f) == NULL)
                subfiles[min_idx] = subfiles[--nfiles];
        	}
         fflush(stdout);
         if(ferror(stdout))
         	{fprintf(stderr,"nsort: error writing to stdout [disk full?]\n");
         	 exit(1);
         	}
    	}	
     else
    	{
         /* Sort single file (no merges required) */
         qsort(lines,nlines,sizeof lines[0],comp);
         for (i = 0; i < nlines; ++i)
        	{if(!do_uniq || i==0 ||  strcmp(lines[i-1],lines[i])) // always print 1st line, or if do_uniq is false. if do_uniq is true and not 1st line print lines that are different
             	fputs(lines[i],stdout);
        	}
         fflush(stdout);
         if(ferror(stdout))
         	{fprintf(stderr,"nsort: error writing to stdout [disk full?]\n");
         	 exit(1);
         	}
    	}
#if defined DEBUG && defined _WIN32
    /* Check heap status at end (Windows only) */
    int heapstatus = _heapchk();
    switch( heapstatus )
	   {
	   case _HEAPOK:
	      fprintf(stderr,"nsort: Debug - heap is OK\n" );
	      break;
	   case _HEAPEMPTY:
	      fprintf(stderr,"nsort: Debug - heap is empty\n" );
	      break;
	   case _HEAPBADBEGIN:
	      fprintf(stderr, "nsort: Debug - ERROR - bad start of heap\n" );
	      break;
	   case _HEAPBADNODE:
	      fprintf(stderr, "nsort: Debug - ERROR - bad node in heap\n" );
	      break;
	   case _HEAPBADPTR:
	      fprintf(stderr, "nsort: Debug - ERROR - Pointer into heap isn't valid\n" );
	      break;	      
	   }
#endif  
	 end_t=clock();  
	 if(verbose) fprintf(stderr,"nsort: sort took %.3f secs\n",(end_t-start_t)/(double)(CLOCKS_PER_SEC));
     exit(0); /* good exit */
}


bool make_subfile(char *lines[],size_t nl, struct subfile subf[],size_t nf)
	/* if returns true need to reset nfiles to 2 */
{
    size_t i;
    bool sub_merge=false;
    FILE *f = tmpfile();
    if(f==NULL)
    	{fprintf(stderr,"nsort: Error trying to create a temporary file [ out of disk space?]\n");
    	 exit(1);
    	}     
    if(nf>=MAXSUBFILES)
    	{/* we are already using the maximum number of merge files, so merge these into 1, using f for output */
    	 size_t initial_nfiles=nf;
    	 size_t min_idx=0;
    	 sub_merge=true;
    	          /* Prepare to read temporary files */
         for (i = 0; i < initial_nfiles; ++i)
        	{
             FILE *f1 = subf[i].f;
             rewind(f1);
             fgets(subf[i].line,subf[i].line_size,f1);
        	}
#ifdef DEBUG
		 fprintf(stderr,"nsort: DEBUG - doing sub-merge on %zd files\n",initial_nfiles);
#endif		     	 
         while (initial_nfiles)
        	{
             struct subfile *sfp;

             /* Find next output line */
             for (min_idx = 0, i = 1; i < initial_nfiles; ++i)
                if (comp(&(subf[i].line),
                           &(subf[min_idx].line)) < 0)
                    min_idx = i;
             sfp = &subf[min_idx];

             /* Output the line */
             fputs(sfp->line,f);
             /* Get the next line from this file */
             if (fgets(sfp->line,sfp->line_size,sfp->f) == NULL)
             	{/* about to overwrite data in subfiles[min_idx], so reclaim space / file handle from it 1st */
             	 fclose(subf[min_idx].f);
             	 free(subf[min_idx].line);
             	 subf[min_idx].line=NULL; /* flag as "empty" */
                 subf[min_idx] = subf[--initial_nfiles]; /* now overwrite it */
                 subf[initial_nfiles].line=NULL; /* this has now been copied to [min_idx] so we will free it from there */
                }
        	}
         fflush(f);
         if(ferror(f))
         	{fprintf(stderr,"nsort: error writing to temp merge file [disk full?]\n");
         	 exit(1);
         	}   
		 /* now tidy up all original nfiles (any missed above) */
		 for (i = 0; i < nf; ++i)
		 	{if(subf[i].line!=NULL)
		 		{
#ifdef DEBUG
				 fprintf(stderr,"nsort: DEBUG tidying up subfiles[%zd] (min_idx=%zd)\n",i,min_idx);
#endif		 		
		 		 fclose(subf[i].f);
             	 free(subf[i].line);
             	 subf[i].line=NULL; /* flag as "empty" */
		 		}
		 	}
		 /* now setup just 1 existing merge file */
		 nf=1;
		 subf[0].f = f;
    	 subf[0].line_size=buf_size; /* space for longest line so far (i.e. any line in file f) */
    	 subf[0].line=malloc(buf_size); 
		 f = tmpfile(); /* finally need another temp file to write new sorted data into */	
		 if(f==NULL)
    		{fprintf(stderr,"nsort: Error trying to create a temporary file [ out of disk space?]\n");
    	 	 exit(1);
    		}  
#ifdef DEBUG
		 fprintf(stderr,"nsort: DEBUG - sub-merge completed\n");
#endif		 		 
    	}
   
    /* Write sorted subfile to temporary file */
    qsort(lines,nl,sizeof lines[0],comp);
    for (i = 0; i < nl; ++i)
    	{
         fputs(lines[i],f);
         free(lines[i]);
    	}
    fflush(f); /* make sure everything is actually written to the file */
    if(ferror(f))
    	{fprintf(stderr,"nsort: Error in writing a sorted subfile to temporary file [ out of disk space?]\n");
    	 exit(1);
    	}
    subf[nf].f = f;
    subf[nf].line_size=buf_size; /* space for longest line so far (i.e. any line in file f) */
    subf[nf].line=malloc(buf_size); 
 	if(subf[nf].line==NULL)
		{fprintf(stderr,"nsort: Error - No RAM found at end of make_subfile()!\n");
	 	 exit(1);
		}
 	return sub_merge;		
}

int mysCompare (const void *p1, const void *p2)
{ /* compare lines as strings */
 return strcmp(* (char **) p1, * (char **) p2);
}

int mynCompare (const void * a, const void * b ) 
{ /* compare as numbers (doubles), compares lines as strings if leading numbers are equal  */
 double v1, v2;
 char *sret;
 v1=strtod(* (char **) a,&sret);
 if(sret==* (char **) a)  v1= -DBL_MAX; // very large negative number if no number found so sorts first

 v2=strtod(* (char **) b,&sret);
 if(sret==* (char **) b)  v2= -DBL_MAX; // very large negative number if no number found so sorts first

 if(v1==v2)
 	return strcmp(* (char **) a, * (char **) b);	// compare whole lines as strings
 if (v1 < v2)
	return -1;
 else
 	return 1;
}


int mynqCompare (const void * a, const void * b ) 
{ /* compare as numbers (doubles) optionally inside double quotes , compares lines as strings if leading numbers are equal  */
 const char *s1 = *(const char**)a;
 const char *s2 = *(const char**)b;
 double v1, v2;
 char *sret;
 
 while(isspace(*s1)) ++s1; /* skip initial whitespace */
 if(*s1=='"') 
 	{++s1; // skip " if present (no need to worry about trailing " as that will just terminate the number )
 	}
 v1=strtod(s1,&sret);
 if(sret==s1)  v1= -DBL_MAX; // very large negative number if no number found so sorts first
 
 while(isspace(*s2)) ++s2; /* skip initial whitespace */
 if(*s2=='"') ++s2; // skip " if present  

 v2=strtod(s2,&sret);
 if(sret==s2)  v2= -DBL_MAX; // very large negative number if no number found so sorts first

 if(v1==v2)
 	return strcmp(* (char **) a, * (char **) b);	// compare whole lines as strings
 if (v1 < v2)
	return -1;
 else
 	return 1;
}

#ifdef UNIX_SIGNALS	
static void CtrlHandler(int)
{
 #ifdef DEBUG
 fprintf(stderr,"nsort: Debug control-C or control-break detected\n");
 #endif	
 #if defined _WIN32 
 _rmtmp(); // close and delete any temporary files created by tmpfile();
 #endif
 exit(1);
}
#elif defined _WIN32 /* defined when compiling for windows, either 32 or 64 bits */
	/* Control-C handler - see https://learn.microsoft.com/en-us/windows/console/registering-a-control-handler-function
	  and https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler
	  Note https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/signal?view=msvc-170 says: 
	   "SIGINT is not supported for any Win32 application. 
	    When a CTRL+C interrupt occurs, Win32 operating systems generate a new thread to specifically handle that interrupt. "	
	  ** this appears to be incorrect as mingw (winlibs) can (and does) handle SIGINT    
	*/
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
 if(fdwCtrlType==CTRL_C_EVENT || fdwCtrlType==CTRL_BREAK_EVENT || fdwCtrlType==CTRL_CLOSE_EVENT)
 	{// Control-C pressed , Control-Break pressed  or user has closed cmd window
#ifdef DEBUG
	 fprintf(stderr,"nsort: Debug control-C or control-break detected\n");
#endif	 	
 	 _rmtmp(); // close and delete any temporary files created by tmpfile();
 	}
 return FALSE; /* means other handlers will be called - i.e. nsort will be terminated */
}
#endif	   

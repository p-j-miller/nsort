# nsort
nsort is a simple but fast sort program that can sort in numeric or alphabetic order.
It was written as the Windows command line sort cannot sort lines in numerical order.
 
nsort is a filter (input from stdin and output to stdout) so to sort in numerical order the file demo1M.csv (supplied and which has 1 million lines) and put the result in the file sorted1M.csv use:

	nsort -nv < demo1M.csv >sorted1M.csv
  
  The file sorted1M-ok.csv is the correct result which can be checked by using (for linux use cmp rather than comp)
  
	comp sorted1M.csv  sorted1M-ok.csv

 nsort sorts lines into increasing order.

 As of version 2v0, input and output files can be directly specified on the command line.
```
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
 ```
 
  For Windows use a compiled file is supplied (nsort.exe).
  See the file INSTALL for instructions to compile the code yourself for Windows and Linux.
  
* Version 1.0 - 1st release
 
* Version 1.1 - use qsort from https://github.com/p-j-miller/yasort-and-yamedian , on Windows this uses all available processor cores to speed up the sorting. No changes in functionality.

* Version 1.2
    - bug fix - if very last line had some characters before EOF and was shorter than the previous line then part of previous line was left in the buffer
    - numeric sorts now do a 2nd level compare on whole string if numbers are identical (previously stripped whitespace before string compare)
    - changed to gcc 15.1.0 with ucrt
    - updated to use qsort 2v0 from https://github.com/p-j-miller/yasort-and-yamedian which sorts a little faster (note the total time is dominated by reading in and writing out the file rather than the actual sorting).


* Version 2.0
    - Major change - this version can sort files that are larger than the available RAM (size is only limited by available(free) harddisk space).
    - input and output files can be specified on the command line
    - Speed improvements

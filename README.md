# nsort
nsort is a simple but fast sort program that can sort in numeric or alphabetic order.
It was written as the Windows command line sort cannot sort lines in numerical order.
 
nsort is a filter (input from stdin and output to stdout) so to sort in numerical order the file demo1M.csv (supplied and which has 1 million lines) and put the result in the file sorted1M.csv use:

	nsort -nv < demo1M.csv >sorted1M.csv
  
  The file sorted1M-ok.csv is the correct result which can be checked by using (for linux use cmp rather than comp)
  
	comp sorted1M.csv  sorted1M-ok.csv

 nsort sorts lines into increasing order.

```
 Usage: nsort [-nquv?h]
  -n lines are assumed to start with numbers and sorting is done on these.
     if the numbers are identical they are sorted as strings
     non-numeric lines will sort first (so a csv files header should stay first)
  -q sort on initial numbers in double quotes (implies -n)
     otherwise (no -n or -q option given) sort lines as strings
  -u only print lines that are unique (ie deletes duplicates)
  -v verbose output (to stderr) - prints execution time etc
  -? or -h prints (this) help message then exists
 ```
 
  For Windows use a compiled file is supplied (nsort.exe).
  See the file INSTALL for instructions to compile the code yourself for Windows and Linux.
  
 Version 1.0 - 1st release
 
 Version 1.1 - use qsort from https://github.com/p-j-miller/yasort-and-yamedian , on Windows this uses all available processor cores to speed up the sorting. No changes in functionality.

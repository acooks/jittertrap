# errlib - C error handling functions

**Attribution: Advanced Programming in the UNIX Environment - W. Richard Stevens, 3rd edition.**

Write:
```c
	if (error_condition)
		err_dump("example format string: %s\n", foo);
``` 
Instead of:
```c
	if (error_condition) {
		char buf[200];
	
		sprintf(buf, "example format string: %s\n", foo);
		perror(buf);
		abort();
	}
```

# Installation

To use this library in your project simply copy err.h and
liberr.a. For example

project
	-src
		-foo.c
	-include
		-err.h
	-lib
		-liberr.a

build
	gcc foo.c -I../include -L../lib
			
	

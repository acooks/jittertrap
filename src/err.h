/*
 * Error functions defined in err.c
 *
 * Attribution: Advanced Programming in the UNIX Environment
 *         W. Richard Stevens, 3rd edition.
 *
 * Write:
 *
 * if (error_condition)
 *     err_dump(<printf format string>);
 *
 * Instead of:
 *
 * if (error_condition) {
 *     char buf[200]
 *     
 *     sprintf(buf, <printf format string>);
 *     perror(buf);
 *     abort();
 * }
 *
 * Tobin Harding
 */

#ifndef _ERR_H
#define _ERR_H

enum {
    ERRMAXLINE = 4096		/* max line length */   
};

/* Nonfatal error related to a system call */
void	err_ret(const char *fmt, ...); 

/* Fatal error related to a system call. */
void	err_sys(const char *fmt, ...) __attribute__((noreturn));

/* Fatal error related to a system call. */
void	err_dump(const char *fmt, ...) __attribute__((noreturn));

/* Nonfatal error unrelated to a system call. */
void	err_msg(const char *fmt, ...);

/* Nonfatal error unrelated to a system call. */
void	err_cont(int error, const char *fmt, ...);

/* Fatal error unrelated to a system call. */
void	err_quit(const char *fmt, ...) __attribute__((noreturn));

/* Fatal error unrelated to a system call. */
void	err_exit(int error, const char *fmt, ...) __attribute__((noreturn));

#endif	/* _ERR_H */

/*
 * Error functions defined in liberr
 *
 * Attribution: Advanced Programming in the UNIX Environment
 *         W. Richard Stevens, 3rd edition.
 *
 * Copyright Tobin Harding 2015
 *
 * This file is part of liberr.
 *
 * liberr is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liberr is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liberr.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ERR_H
#define _ERR_H

enum {
    LIBERR_MAXLINE = 4096		/* max line length */   
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

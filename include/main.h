#ifndef	_MAIN_H
#define	_MAIN_H

#if defined(SOLARIS)
#define _XOPEN_SOURCE	500	/* Single UNIX Specification, Version 2  for Solaris 9 */
#define CMSG_LEN(x)	_CMSG_DATA_ALIGN(sizeof(struct cmsghdr)+(x))
#elif !defined(BSD)
#define _XOPEN_SOURCE	600	/* Single UNIX Specification, Version 3 */
#endif

#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#define	MAXLINE	4096			/* max line length */
#define MAXURL 2048

void err_dump(const char *, ...); /* {App misc_source} */
void err_msg(const char *, ...);
void err_quit(const char *, ...);
void err_exit(int, const char *, ...);
void err_ret(const char *, ...);
void err_sys(const char *, ...);

#endif	/* _MAIN_H */
#ifndef	H_RPMIO
#define	H_RPMIO

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

typedef	/*@abstract@*/ struct _FD {
    int		fd_fd;
    void *	fd_bzd;
    void *	fd_gzd;
    void *	fd_url;
} *FD_t;

#endif H_RPMIO

#ifndef H_RPMIO_F
#define H_RPMIO_F

#ifdef __cplusplus
extern "C" {
#endif

int timedRead(FD_t fd, void * bufptr, int length);

extern /*@null@*/ FD_t fdNew(void);
extern int fdValid(FD_t fd);
extern int fdFileno(FD_t fd);

extern /*@null@*/ FD_t fdOpen(const char *pathname, int flags, mode_t mode);
extern /*@null@*/ FD_t fdDup(int fdno);

extern off_t	fdLseek(FD_t fd, off_t offset, int whence);
extern ssize_t	fdRead(FD_t fd, void * buf, size_t count);
extern ssize_t	fdWrite(FD_t fd, const void * buf, size_t count);
extern int	fdClose(/*@only@*/ FD_t fd);

extern /*@shared@*/ FILE *fdFdopen(/*@owned@*/ FD_t fd, const char *mode);

/*
 * Support for GZIP library.
 */
#ifdef	HAVE_ZLIB_H

#include <zlib.h>

extern gzFile * gzdFileno(FD_t fd);

extern /*@null@*/ FD_t gzdOpen(const char *pathname, const char *mode);

extern /*@shared@*/ FD_t gzdFdopen(FD_t fd, const char *mode);

extern ssize_t gzdRead(FD_t fd, void * buf, size_t count);

extern ssize_t gzdWrite(FD_t fd, const void * buf, size_t count);

extern off_t gzdLseek(FD_t fd, off_t offset, int whence);

extern int gzdFlush(FD_t fd);

extern char * gzdStrerror(FD_t fd);

extern int gzdClose(/*@only@*/ FD_t fd);

#endif	/* HAVE_ZLIB_H */

/*
 * Support for BZIP2 library.
 */
#ifdef	HAVE_BZLIB_H

#include <bzlib.h>

extern BZFILE * bzdFileno(FD_t fd);

extern /*@null@*/ FD_t bzdOpen(const char *pathname, const char *mode);

extern /*@shared@*/ FD_t bzdFdopen(FD_t fd, const char *mode);

extern ssize_t bzdRead(FD_t fd, void * buf, size_t count);

extern ssize_t bzdWrite(FD_t fd, const void * buf, size_t count);

extern int bzdFlush(FD_t fd);

extern char * bzdStrerror(FD_t fd);

extern int bzdClose(/*@only@*/ FD_t fd);

#endif	/* HAVE_BZLIB_H */

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMIO_F */

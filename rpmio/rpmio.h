#ifndef	H_RPMIO
#define	H_RPMIO

/** \ingroup rpmio
 * \file rpmio/rpmio.h
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/** \ingroup rpmio
 * Hide libio API lossage.
 * The libio interface changed after glibc-2.1.3 to pass the seek offset
 * argument as a pointer rather than as an off_t. The snarl below defines
 * typedefs to isolate the lossage.
 * API unchanged.
 */
/*@{*/
#if !defined(__LCLINT__) && defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ == 2
#define USE_COOKIE_SEEK_POINTER 1
typedef _IO_off64_t 	_libio_off_t;
typedef _libio_off_t *	_libio_pos_t;
#else
typedef off_t 		_libio_off_t;
typedef off_t 		_libio_pos_t;
#endif
/*@}*/

/** \ingroup rpmio
 */
typedef	/*@abstract@*/ /*@refcounted@*/ struct _FD_s * FD_t;

/** \ingroup rpmio
 */
typedef /*@observer@*/ struct FDIO_s * FDIO_t;

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmio
 * \name RPMIO Vectors.
 */
/*@{*/

/** \ingroup rpmio
 */
typedef ssize_t fdio_read_function_t (void *cookie, char *buf, size_t nbytes);

/** \ingroup rpmio
 */
typedef ssize_t fdio_write_function_t (void *cookie, const char *buf, size_t nbytes);

/** \ingroup rpmio
 */
typedef int fdio_seek_function_t (void *cookie, _libio_pos_t pos, int whence);

/** \ingroup rpmio
 */
typedef int fdio_close_function_t (void *cookie);


/** \ingroup rpmio
 */
typedef /*@only@*/ /*@null@*/ FD_t fdio_ref_function_t ( /*@only@*/ void * cookie,
		const char * msg, const char * file, unsigned line);

/** \ingroup rpmio
 */
typedef /*@only@*/ /*@null@*/ FD_t fdio_deref_function_t ( /*@only@*/ FD_t fd,
		const char * msg, const char * file, unsigned line);


/** \ingroup rpmio
 */
typedef /*@only@*/ /*@null@*/ FD_t fdio_new_function_t (const char * msg,
		const char * file, unsigned line);


/** \ingroup rpmio
 */
typedef int fdio_fileno_function_t (void * cookie);


/** \ingroup rpmio
 */
typedef FD_t fdio_open_function_t (const char * path, int flags, mode_t mode);

/** \ingroup rpmio
 */
typedef FD_t fdio_fopen_function_t (const char * path, const char * fmode);

/** \ingroup rpmio
 */
typedef void * fdio_ffileno_function_t (FD_t fd);

/** \ingroup rpmio
 */
typedef int fdio_fflush_function_t (FD_t fd);
/*@}*/


/** \ingroup rpmrpc
 * \name RPMRPC Vectors.
 */
/*@{*/
typedef int fdio_mkdir_function_t (const char * path, mode_t mode);
typedef int fdio_chdir_function_t (const char * path);
typedef int fdio_rmdir_function_t (const char * path);
typedef int fdio_rename_function_t (const char * oldpath, const char * newpath);
typedef int fdio_unlink_function_t (const char * path);
/*@-typeuse@*/
typedef int fdio_stat_function_t (const char * path, struct stat * st);
typedef int fdio_lstat_function_t (const char * path, struct stat * st);
typedef int fdio_access_function_t (const char * path, int amode);
/*@=typeuse@*/
/*@}*/


/** \ingroup rpmio
 */
struct FDIO_s {
  fdio_read_function_t *	read;
  fdio_write_function_t *	write;
  fdio_seek_function_t *	seek;
  fdio_close_function_t *	close;

  fdio_ref_function_t *		_fdref;
  fdio_deref_function_t *	_fdderef;
  fdio_new_function_t *		_fdnew;
  fdio_fileno_function_t *	_fileno;

  fdio_open_function_t *	_open;
  fdio_fopen_function_t *	_fopen;
  fdio_ffileno_function_t *	_ffileno;
  fdio_fflush_function_t *	_fflush;

  fdio_mkdir_function_t *	_mkdir;
  fdio_chdir_function_t *	_chdir;
  fdio_rmdir_function_t *	_rmdir;
  fdio_rename_function_t *	_rename;
  fdio_unlink_function_t *	_unlink;
};


/** \ingroup rpmio
 * \name RPMIO Interface.
 */
/*@{*/

/** \ingroup rpmio
 * strerror(3) clone.
 */
/*@-redecl@*/
/*@observer@*/ const char * Fstrerror(/*@null@*/ FD_t fd)
	/*@*/;
/*@=redecl@*/

/** \ingroup rpmio
 * fread(3) clone.
 */
size_t Fread(/*@out@*/ void * buf, size_t size, size_t nmemb, FD_t fd)
	/*@modifies fd, *buf, fileSystem @*/;

/** \ingroup rpmio
 * fwrite(3) clone.
 */
size_t Fwrite(const void * buf, size_t size, size_t nmemb, FD_t fd)
	/*@modifies fd, fileSystem @*/;

/** \ingroup rpmio
 * fseek(3) clone.
 */
int Fseek(FD_t fd, _libio_off_t offset, int whence)
	/*@modifies fileSystem @*/;

/** \ingroup rpmio
 * fclose(3) clone.
 */
int Fclose( /*@killref@*/ FD_t fd)
	/*@modifies fd, fileSystem @*/;

/** \ingroup rpmio
 */
/*@null@*/ FD_t	Fdopen(FD_t fd, const char * fmode)
	/*@modifies fd, fileSystem @*/;

/** \ingroup rpmio
 * fopen(3) clone.
 */
/*@null@*/ FD_t	Fopen(/*@null@*/ const char * path,
			/*@null@*/ const char * fmode)
	/*@modifies fileSystem @*/;


/** \ingroup rpmio
 * fflush(3) clone.
 */
int Fflush(/*@null@*/ FD_t fd)
	/*@modifies fd, fileSystem @*/;

/** \ingroup rpmio
 * ferror(3) clone.
 */
int Ferror(/*@null@*/ FD_t fd)
	/*@*/;

/** \ingroup rpmio
 * fileno(3) clone.
 */
int Fileno(FD_t fd)
	/*@*/;

/** \ingroup rpmio
 * fcntl(2) clone.
 */
int Fcntl(FD_t fd, int op, void *lip)
	/*@modifies fd, *lip, fileSystem @*/;

/** \ingroup rpmio
 * pread(2) clone.
 */
ssize_t Pread(FD_t fd, void * buf, size_t count, _libio_off_t offset)
	/*@modifies fd, *buf, fileSystem @*/;

/** \ingroup rpmio
 * pwrite(2) clone.
 */
ssize_t Pwrite(FD_t fd, const void * buf, size_t count, _libio_off_t offset)
	/*@modifies fd, fileSystem @*/;

/*@}*/

/** \ingroup rpmrpc
 * \name RPMRPC Interface.
 */
/*@{*/

/** \ingroup rpmrpc
 * mkdir(2) clone.
 */
int Mkdir(const char * path, mode_t mode)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * chdir(2) clone.
 */
int Chdir(const char * path)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * rmdir(2) clone.
 */
int Rmdir(const char * path)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * rename(2) clone.
 */
int Rename(const char * oldpath, const char * newpath)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * link(2) clone.
 */
int Link(const char * oldpath, const char * newpath)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * unlink(2) clone.
 */
int Unlink(const char * path)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * readlink(2) clone.
 */
int Readlink(const char * path, char * buf, size_t bufsiz)
	/*@modifies *buf, fileSystem @*/;

/** \ingroup rpmrpc
 * stat(2) clone.
 */
int Stat(const char * path, /*@out@*/ struct stat * st)
	/*@modifies *st, fileSystem @*/;

/** \ingroup rpmrpc
 * lstat(2) clone.
 */
int Lstat(const char * path, /*@out@*/ struct stat * st)
	/*@modifies *st, fileSystem @*/;

/** \ingroup rpmrpc
 * access(2) clone.
 */
int Access(const char * path, int amode)
	/*@modifies fileSystem @*/;


/** \ingroup rpmrpc
 * glob(3) clone.
 */
int Glob(const char * pattern, int flags,
		int errfunc(const char * epath, int eerrno),
		/*@out@*/ glob_t * pglob)
	/*@modifies *pglob, fileSystem @*/;

/** \ingroup rpmrpc
 * globfree(3) clone.
 */
void Globfree( /*@only@*/ glob_t * pglob)
	/*@modifies *pglob, fileSystem @*/;


/** \ingroup rpmrpc
 * opendir(3) clone.
 */
/*@null@*/ DIR * Opendir(const char * name)
	/*@modifies fileSystem @*/;

/** \ingroup rpmrpc
 * readdir(3) clone.
 */
/*@null@*/ struct dirent * Readdir(DIR * dir)
	/*@modifies *dir, fileSystem @*/;

/** \ingroup rpmrpc
 * closedir(3) clone.
 */
int	Closedir(/*@only@*/ DIR * dir)
	/*@modifies *dir, fileSystem @*/;

/*@}*/


/** \ingroup rpmio
 * \name RPMIO Utilities.
 */
/*@{*/

/** \ingroup rpmio
 */
off_t	fdSize(FD_t fd)
	/*@modifies fd, fileSystem@*/;

/** \ingroup rpmio
 */
/*@null@*/ FD_t fdDup(int fdno)
	/*@modifies fileSystem@*/;

#ifdef UNUSED
/*@null@*/ FILE *fdFdopen( /*@only@*/ void * cookie, const char * mode);
#endif

/* XXX Legacy interfaces needed by gnorpm, rpmfind et al */

/** \ingroup rpmio
 */
/*@-shadow -declundef -fcnuse@*/
int fdFileno(void * cookie)
	/*@*/;
/*@=shadow =declundef =fcnuse@*/


/*@-exportlocal@*/
/** \ingroup rpmio
 */
/*@null@*/ FD_t fdOpen(const char *path, int flags, mode_t mode)
	/*@modifies fileSystem @*/;

/** \ingroup rpmio
 */
ssize_t fdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@modifies *cookie, *buf, fileSystem @*/;

/** \ingroup rpmio
 */
ssize_t	fdWrite(void * cookie, const char * buf, size_t count)
	/*@modifies *cookie, fileSystem @*/;

/** \ingroup rpmio
 */
int fdClose( /*@only@*/ void * cookie)
	/*@modifies *cookie, fileSystem @*/;

/* XXX FD_t reference count debugging wrappers */
#define	fdLink(_fd, _msg)	fdio->_fdref(_fd, _msg, __FILE__, __LINE__)
#define	fdFree(_fd, _msg)	fdio->_fdderef(_fd, _msg, __FILE__, __LINE__)
#define	fdNew(_msg)		fdio->_fdnew(_msg, __FILE__, __LINE__)


/** \ingroup rpmio
 */
int fdWritable(FD_t fd, int secs)
	/*@modifies fd @*/;

/** \ingroup rpmio
 */
int fdReadable(FD_t fd, int secs)
	/*@modifies fd @*/;
/*@=exportlocal@*/

/** \ingroup rpmio
 * FTP and HTTP error codes.
 */
/*@-typeuse@*/
typedef enum ftperrCode_e {
    FTPERR_BAD_SERVER_RESPONSE	= -1,	/*!< Bad server response */
    FTPERR_SERVER_IO_ERROR	= -2,	/*!< Server I/O error */
    FTPERR_SERVER_TIMEOUT	= -3,	/*!< Server timeout */
    FTPERR_BAD_HOST_ADDR	= -4,	/*!< Unable to lookup server host address */
    FTPERR_BAD_HOSTNAME		= -5,	/*!< Unable to lookup server host name */
    FTPERR_FAILED_CONNECT	= -6,	/*!< Failed to connect to server */
    FTPERR_FILE_IO_ERROR	= -7,	/*!< Failed to establish data connection to server */
    FTPERR_PASSIVE_ERROR	= -8,	/*!< I/O error to local file */
    FTPERR_FAILED_DATA_CONNECT	= -9,	/*!< Error setting remote server to passive mode */
    FTPERR_FILE_NOT_FOUND	= -10,	/*!< File not found on server */
    FTPERR_NIC_ABORT_IN_PROGRESS= -11,	/*!< Abort in progress */
    FTPERR_UNKNOWN		= -100	/*!< Unknown or unexpected error */
} ftperrCode;
/*@=typeuse@*/

/** \ingroup rpmio
 */
/*@-redecl@*/
/*@observer@*/ const char *const ftpStrerror(int errorNumber)	/*@*/;
/*@=redecl@*/

/** \ingroup rpmio
 */
/*@unused@*/
/*@dependent@*/ /*@null@*/ void * ufdGetUrlinfo(FD_t fd)
	/*@modifies fd @*/;

/** \ingroup rpmio
 */
/*@-redecl@*/
/*@unused@*/
/*@observer@*/ const char * urlStrerror(const char * url)	/*@*/;
/*@=redecl@*/

/** \ingroup rpmio
 */
/*@-exportlocal@*/
int ufdCopy(FD_t sfd, FD_t tfd)
	/*@modifies sfd, tfd, fileSystem @*/;
/*@=exportlocal@*/

/** \ingroup rpmio
 */
int ufdGetFile( /*@killref@*/ FD_t sfd, FD_t tfd)
	/*@modifies sfd, tfd, fileSystem @*/;

/** \ingroup rpmio
 */
int timedRead(FD_t fd, /*@out@*/ void * bufptr, int length)
	/*@modifies fd, *bufptr, fileSystem @*/;
#define	timedRead	ufdio->read


/*@-exportlocal@*/
/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t fdio;

/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t fpio;

/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t ufdio;

/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t gzdio;

/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t bzdio;

/** \ingroup rpmio
 */
/*@observer@*/ extern FDIO_t fadio;
/*@=exportlocal@*/
/*@}*/

/*@unused@*/ static inline int xislower(int c) /*@*/ {
    return (c >= 'a' && c <= 'z');
}
/*@unused@*/ static inline int xisupper(int c) /*@*/ {
    return (c >= 'A' && c <= 'Z');
}
/*@unused@*/ static inline int xisalpha(int c) /*@*/ {
    return (xislower(c) || xisupper(c));
}
/*@unused@*/ static inline int xisdigit(int c) /*@*/ {
    return (c >= '0' && c <= '9');
}
/*@unused@*/ static inline int xisalnum(int c) /*@*/ {
    return (xisalpha(c) || xisdigit(c));
}
/*@unused@*/ static inline int xisblank(int c) /*@*/ {
    return (c == ' ' || c == '\t');
}
/*@unused@*/ static inline int xisspace(int c) /*@*/ {
    return (xisblank(c) || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

/*@unused@*/ static inline int xtolower(int c) /*@*/ {
    return ((xisupper(c)) ? (c | ('a' - 'A')) : c);
}
/*@unused@*/ static inline int xtoupper(int c) /*@*/ {
    return ((xislower(c)) ? (c & ~('a' - 'A')) : c);
}

/** \ingroup rpmio
 * Locale insensitive strcasecmp(3).
 */
int xstrcasecmp(const char * s1, const char * s2)		/*@*/;

/** \ingroup rpmio
 * Locale insensitive strncasecmp(3).
 */
int xstrncasecmp(const char *s1, const char * s2, size_t n)	/*@*/;

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMIO */

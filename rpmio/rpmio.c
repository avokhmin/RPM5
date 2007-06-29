/** \ingroup rpmio
 * \file rpmio/rpmio.c
 */

#include "system.h"
#include <stdarg.h>

#if HAVE_MACHINE_TYPES_H
# include <machine/types.h>
#endif

#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

#if defined(__LCLINT__)
struct addrinfo
{
  int ai_flags;			/* Input flags.  */
  int ai_family;		/* Protocol family for socket.  */
  int ai_socktype;		/* Socket type.  */
  int ai_protocol;		/* Protocol for socket.  */
  socklen_t ai_addrlen;		/* Length of socket address.  */
  struct sockaddr *ai_addr;	/* Socket address for socket.  */
  char *ai_canonname;		/* Canonical name for service location.  */
  struct addrinfo *ai_next;	/* Pointer to next in list.  */
};

/*@-exportheader@*/
extern int getaddrinfo (__const char *__restrict __name,
			__const char *__restrict __service,
			__const struct addrinfo *__restrict __req,
			/*@out@*/ struct addrinfo **__restrict __pai)
	/*@modifies *__pai @*/;

extern int getnameinfo (__const struct sockaddr *__restrict __sa,
			socklen_t __salen, /*@out@*/ char *__restrict __host,
			socklen_t __hostlen, /*@out@*/ char *__restrict __serv,
			socklen_t __servlen, unsigned int __flags)
	/*@modifies __host, __serv @*/;

extern void freeaddrinfo (/*@only@*/ struct addrinfo *__ai)
	/*@modifies __ai @*/;
/*@=exportheader@*/
#else
#include <netdb.h>		/* XXX getaddrinfo et al */
#endif

#include <netinet/in.h>
#include <arpa/inet.h>		/* XXX for inet_aton and HP-UX */

#if HAVE_NETINET_IN_SYSTM_H
# include <sys/types.h>
# include <netinet/in_systm.h>
#endif

#include <rpmmacro.h>		/* XXX rpmioAccess needs rpmCleanPath() */

#if HAVE_LIBIO_H && defined(_G_IO_IO_FILE_VERSION)
#define	_USE_LIBIO	1
#endif

/* XXX HP-UX w/o -D_XOPEN_SOURCE needs */
#if !defined(HAVE_HERRNO) && (defined(hpux) || defined(__hpux) || defined(__LCLINT__))
/*@unchecked@*/
extern int h_errno;
#endif

#ifndef IPPORT_FTP
#define IPPORT_FTP	21
#endif
#ifndef	IPPORT_HTTP
#define	IPPORT_HTTP	80
#endif

#if !defined(HAVE_INET_ATON)
#define inet_aton(cp,inp) rpm_inet_aton(cp,inp)
static int rpm_inet_aton(const char *cp, struct in_addr *inp)
	/*@modifies *inp @*/
{
    long addr;

    addr = inet_addr(cp);
    if (addr == ((long) -1)) return 0;

    memcpy(inp, &addr, sizeof(addr));
    return 1;
}
#endif

#if defined(USE_ALT_DNS) && USE_ALT_DNS
#include "dns.h"
#endif

#include <rpmio_internal.h>
#undef	fdFileno
#undef	fdOpen
#define	fdOpen	__fdOpen
#undef	fdRead
#define	fdRead	__fdRead
#undef	fdWrite
#define	fdWrite	__fdWrite
#undef	fdClose
#define	fdClose	__fdClose

#include <rpmdav.h>
#include "ugid.h"
#include "rpmmessages.h"

#include "debug.h"

/*@access FILE @*/	/* XXX to permit comparison/conversion with void *. */
/*@access urlinfo @*/
/*@access FDSTAT_t @*/

#define FDNREFS(fd)	(fd ? ((FD_t)fd)->nrefs : -9)
#define FDTO(fd)	(fd ? ((FD_t)fd)->rd_timeoutsecs : -99)
#define FDCPIOPOS(fd)	(fd ? ((FD_t)fd)->fd_cpioPos : -99)

#define	FDONLY(fd)	assert(fdGetIo(fd) == fdio)
#define	GZDONLY(fd)	assert(fdGetIo(fd) == gzdio)
#define	BZDONLY(fd)	assert(fdGetIo(fd) == bzdio)
#define	LZDONLY(fd)	assert(fdGetIo(fd) == lzdio)

#define	UFDONLY(fd)	/* assert(fdGetIo(fd) == ufdio) */

#define	fdGetFILE(_fd)	((FILE *)fdGetFp(_fd))

/**
 */
/*@unchecked@*/
#if _USE_LIBIO
int noLibio = 0;
#else
int noLibio = 1;
#endif

#define TIMEOUT_SECS 60

/**
 */
/*@unchecked@*/
static int ftpTimeoutSecs = TIMEOUT_SECS;

/**
 */
/*@unchecked@*/
int _rpmio_debug = 0;

/**
 */
/*@unchecked@*/
int _av_debug = 0;

/**
 */
/*@unchecked@*/
int _ftp_debug = 0;

/**
 */
/*@unchecked@*/
int _dav_debug = 0;

/* =============================================================== */

/*@-boundswrite@*/
static /*@observer@*/ const char * fdbg(/*@null@*/ FD_t fd)
	/*@*/
{
    static char buf[BUFSIZ];
    char *be = buf;
    int i;

    buf[0] = '\0';
    if (fd == NULL)
	return buf;

#ifdef DYING
    sprintf(be, "fd %p", fd);	be += strlen(be);
    if (fd->rd_timeoutsecs >= 0) {
	sprintf(be, " secs %d", fd->rd_timeoutsecs);
	be += strlen(be);
    }
#endif
    if (fd->bytesRemain != -1) {
	sprintf(be, " clen %d", (int)fd->bytesRemain);
	be += strlen(be);
    }
    if (fd->wr_chunked) {
	strcpy(be, " chunked");
	be += strlen(be);
    }
    *be++ = '\t';
    for (i = fd->nfps; i >= 0; i--) {
	FDSTACK_t * fps = &fd->fps[i];
	if (i != fd->nfps)
	    *be++ = ' ';
	*be++ = '|';
	*be++ = ' ';
	if (fps->io == fdio) {
	    sprintf(be, "FD %d fp %p", fps->fdno, fps->fp);
	} else if (fps->io == ufdio) {
	    sprintf(be, "UFD %d fp %p", fps->fdno, fps->fp);
#ifdef	HAVE_ZLIB_H
	} else if (fps->io == gzdio) {
	    sprintf(be, "GZD %p fdno %d", fps->fp, fps->fdno);
#endif
#if HAVE_BZLIB_H
	} else if (fps->io == bzdio) {
	    sprintf(be, "BZD %p fdno %d", fps->fp, fps->fdno);
#endif
	} else if (fps->io == lzdio) {
	    sprintf(be, "LZD %p fdno %d", fps->fp, fps->fdno);
	} else if (fps->io == fpio) {
	    /*@+voidabstract@*/
	    sprintf(be, "%s %p(%d) fdno %d",
		(fps->fdno < 0 ? "LIBIO" : "FP"),
		fps->fp, fileno(((FILE *)fps->fp)), fps->fdno);
	    /*@=voidabstract@*/
	} else {
	    sprintf(be, "??? io %p fp %p fdno %d ???",
		fps->io, fps->fp, fps->fdno);
	}
	be += strlen(be);
	*be = '\0';
    }
    return buf;
}
/*@=boundswrite@*/

/* =============================================================== */
off_t fdSize(FD_t fd)
{
    struct stat sb;
    off_t rc = -1;

#ifdef	NOISY
DBGIO(0, (stderr, "==>\tfdSize(%p) rc %ld\n", fd, (long)rc));
#endif
    FDSANE(fd);
    if (fd->contentLength >= 0)
	rc = fd->contentLength;
    else switch (fd->urlType) {
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	if (fstat(Fileno(fd), &sb) == 0)
	    rc = sb.st_size;
	/*@fallthrough@*/
    case URL_IS_HTTPS:
    case URL_IS_HTTP:
    case URL_IS_HKP:
    case URL_IS_FTP:
    case URL_IS_DASH:
	break;
    }
    return rc;
}

FD_t fdDup(int fdno)
{
    FD_t fd;
    int nfdno;

    if ((nfdno = dup(fdno)) < 0)
	return NULL;
    fd = fdNew("open (fdDup)");
    fdSetOpen(fd, "fdDup", nfdno, 0);	/* XXX bogus */
    fdSetFdno(fd, nfdno);
DBGIO(fd, (stderr, "==> fdDup(%d) fd %p %s\n", fdno, (fd ? fd : NULL), fdbg(fd)));
    /*@-refcounttrans@*/ return fd; /*@=refcounttrans@*/
}

static inline /*@unused@*/ int fdSeekNot(void * cookie,
		/*@unused@*/ _libio_pos_t pos,  /*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);
    FDSANE(fd);		/* XXX keep gcc quiet */
    return -2;
}

#ifdef UNUSED
FILE *fdFdopen(void * cookie, const char *fmode)
{
    FD_t fd = c2f(cookie);
    int fdno;
    FILE * fp;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    if (fdno < 0) return NULL;
    fp = fdopen(fdno, fmode);
DBGIO(fd, (stderr, "==> fdFdopen(%p,\"%s\") fdno %d -> fp %p fdno %d\n", cookie, fmode, fdno, fp, fileno(fp)));
    fd = fdFree(fd, "open (fdFdopen)");
    return fp;
}
#endif

/* =============================================================== */
/*@-mustmod@*/ /* FIX: cookie is modified */
static inline /*@null@*/ FD_t XfdLink(void * cookie, const char * msg,
		const char * file, unsigned line)
	/*@modifies *cookie @*/
{
    FD_t fd;
if (cookie == NULL)
    /*@-castexpose@*/
DBGREFS(0, (stderr, "--> fd  %p ++ %d %s at %s:%u\n", cookie, FDNREFS(cookie)+1, msg, file, line));
    /*@=castexpose@*/
    fd = c2f(cookie);
    if (fd) {
	fd->nrefs++;
DBGREFS(fd, (stderr, "--> fd  %p ++ %d %s at %s:%u %s\n", fd, fd->nrefs, msg, file, line, fdbg(fd)));
    }
    return fd;
}
/*@=mustmod@*/

static inline /*@null@*/
FD_t XfdFree( /*@killref@*/ FD_t fd, const char *msg,
		const char *file, unsigned line)
	/*@modifies fd @*/
{
	int i;

if (fd == NULL)
DBGREFS(0, (stderr, "--> fd  %p -- %d %s at %s:%u\n", fd, FDNREFS(fd), msg, file, line));
    FDSANE(fd);
    if (fd) {
DBGREFS(fd, (stderr, "--> fd  %p -- %d %s at %s:%u %s\n", fd, fd->nrefs, msg, file, line, fdbg(fd)));
	if (--fd->nrefs > 0)
	    /*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/
	fd->opath = _free(fd->opath);
	fd->stats = _free(fd->stats);
	for (i = fd->ndigests - 1; i >= 0; i--) {
	    FDDIGEST_t fddig = fd->digests + i;
	    if (fddig->hashctx == NULL)
		continue;
	    (void) rpmDigestFinal(fddig->hashctx, NULL, NULL, 0);
	    fddig->hashctx = NULL;
	}
	fd->ndigests = 0;
	/*@-refcounttrans@*/ free(fd); /*@=refcounttrans@*/
    }
    return NULL;
}

static inline /*@null@*/
FD_t XfdNew(const char * msg, const char * file, unsigned line)
	/*@globals internalState @*/
	/*@modifies internalState @*/
{
    FD_t fd = xcalloc(1, sizeof(*fd));
    if (fd == NULL) /* XXX xmalloc never returns NULL */
	return NULL;
    fd->nrefs = 0;
    fd->flags = 0;
    fd->magic = FDMAGIC;
    fd->urlType = URL_IS_UNKNOWN;

    fd->nfps = 0;
    memset(fd->fps, 0, sizeof(fd->fps));

    fd->fps[0].io = ufdio;
    fd->fps[0].fp = NULL;
    fd->fps[0].fdno = -1;

    fd->opath = NULL;
    fd->oflags = 0;
    fd->omode = 0;
    fd->url = NULL;
    fd->rd_timeoutsecs = 1;	/* XXX default value used to be -1 */
    fd->contentLength = fd->bytesRemain = -1;
    fd->wr_chunked = 0;
    fd->syserrno = 0;
    fd->errcookie = NULL;
    fd->stats = xcalloc(1, sizeof(*fd->stats));

    fd->ndigests = 0;
    memset(fd->digests, 0, sizeof(fd->digests));

    fd->ftpFileDoneNeeded = 0;
    fd->fd_cpioPos = 0;

    return XfdLink(fd, msg, file, line);
}

static ssize_t fdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals errno, fileSystem, internalState @*/
	/*@modifies buf, errno, fileSystem, internalState @*/
	/*@requires maxSet(buf) >= (count - 1) @*/
	/*@ensures maxRead(buf) == result @*/
{
    FD_t fd = c2f(cookie);
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    fdstat_enter(fd, FDSTAT_READ);
/*@-boundswrite@*/
    /* HACK: flimsy wiring for davRead */
    if (fd->req != NULL) {
	rc = davRead(fd, buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
	/* XXX Chunked davRead EOF. */
	if (rc == 0)
	    fd->bytesRemain = 0;
    } else
	rc = read(fdFileno(fd), buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
/*@=boundswrite@*/
    fdstat_exit(fd, FDSTAT_READ, rc);

    if (fd->ndigests && rc > 0) fdUpdateDigests(fd, (void *)buf, rc);

DBGIO(fd, (stderr, "==>\tfdRead(%p,%p,%ld) rc %ld %s\n", cookie, buf, (long)count, (long)rc, fdbg(fd)));

    return rc;
}

static ssize_t fdWrite(void * cookie, const char * buf, size_t count)
	/*@globals errno, fileSystem, internalState @*/
	/*@modifies errno, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno = fdFileno(fd);
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0) fdUpdateDigests(fd, (void *)buf, count);

    if (count == 0) return 0;

    fdstat_enter(fd, FDSTAT_WRITE);
/*@-boundsread@*/
    /* HACK: flimsy wiring for davWrite */
    if (fd->req != NULL)
	rc = davWrite(fd, buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
    else
	rc = write(fdno, buf, (count > fd->bytesRemain ? fd->bytesRemain : count));
/*@=boundsread@*/
    fdstat_exit(fd, FDSTAT_WRITE, rc);

DBGIO(fd, (stderr, "==>\tfdWrite(%p,%p,%ld) rc %ld %s\n", cookie, buf, (long)count, (long)rc, fdbg(fd)));

    return rc;
}

static inline int fdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
#ifdef USE_COOKIE_SEEK_POINTER
    _IO_off64_t p = *pos;
#else
    off_t p = pos;
#endif
    FD_t fd = c2f(cookie);
    off_t rc;

    assert(fd->bytesRemain == -1);	/* XXX FIXME fadio only for now */
    fdstat_enter(fd, FDSTAT_SEEK);
    rc = lseek(fdFileno(fd), p, whence);
    fdstat_exit(fd, FDSTAT_SEEK, rc);

DBGIO(fd, (stderr, "==>\tfdSeek(%p,%ld,%d) rc %lx %s\n", cookie, (long)p, whence, (unsigned long)rc, fdbg(fd)));

    return rc;
}

static int fdClose( /*@only@*/ void * cookie)
	/*@globals errno, fileSystem, systemState, internalState @*/
	/*@modifies errno, fileSystem, systemState, internalState @*/
{
    FD_t fd;
    int fdno;
    int rc;

    if (cookie == NULL) return -2;
    fd = c2f(cookie);
    fdno = fdFileno(fd);

    fdSetFdno(fd, -1);

    fdstat_enter(fd, FDSTAT_CLOSE);
    /* HACK: flimsy wiring for davClose */
/*@-branchstate@*/
    if (fd->req != NULL)
	rc = davClose(fd);
    else
	rc = ((fdno >= 0) ? close(fdno) : -2);
/*@=branchstate@*/
    fdstat_exit(fd, FDSTAT_CLOSE, rc);

DBGIO(fd, (stderr, "==>\tfdClose(%p) rc %lx %s\n", (fd ? fd : NULL), (unsigned long)rc, fdbg(fd)));

    fd = fdFree(fd, "open (fdClose)");
    return rc;
}

static /*@null@*/ FD_t fdOpen(const char *path, int flags, mode_t mode)
	/*@globals errno, fileSystem, internalState @*/
	/*@modifies errno, fileSystem, internalState @*/
{
    FD_t fd;
    int fdno;

    fdno = open(path, flags, mode);
    if (fdno < 0) return NULL;
    if (fcntl(fdno, F_SETFD, FD_CLOEXEC)) {
	(void) close(fdno);
	return NULL;
    }
    fd = fdNew("open (fdOpen)");
    fdSetOpen(fd, path, flags, mode);
    fdSetFdno(fd, fdno);
    fd->flags = flags;
DBGIO(fd, (stderr, "==>\tfdOpen(\"%s\",%x,0%o) %s\n", path, (unsigned)flags, (unsigned)mode, fdbg(fd)));
    /*@-refcounttrans@*/ return fd; /*@=refcounttrans@*/
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s fdio_s = {
  fdRead, fdWrite, fdSeek, fdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  fdOpen, NULL, fdGetFp, NULL,	mkdir, chdir, rmdir, rename, unlink
};
/*@=type@*/
FDIO_t fdio = /*@-compmempass@*/ &fdio_s /*@=compmempass@*/ ;

int fdWritable(FD_t fd, int secs)
{
    int fdno;
    int rc;
#if HAVE_POLL_H
    int msecs = (secs >= 0 ? (1000 * secs) : -1);
    struct pollfd wrfds;
#else
    struct timeval timeout, *tvp = (secs >= 0 ? &timeout : NULL);
    fd_set wrfds;
    FD_ZERO(&wrfds);
#endif
	
    /* HACK: flimsy wiring for davWrite */
    if (fd->req != NULL)
	return 1;

    if ((fdno = fdFileno(fd)) < 0)
	return -1;	/* XXX W2DO? */
	
    do {
#if HAVE_POLL_H
	wrfds.fd = fdno;
	wrfds.events = POLLOUT;
	wrfds.revents = 0;
	rc = poll(&wrfds, 1, msecs);
#else
	if (tvp) {
	    tvp->tv_sec = secs;
	    tvp->tv_usec = 0;
	}
	FD_SET(fdno, &wrfds);
/*@-compdef -nullpass@*/
	rc = select(fdno + 1, NULL, &wrfds, NULL, tvp);
/*@=compdef =nullpass@*/
#endif

	/* HACK: EBADF on PUT chunked termination from ufdClose. */
if (_rpmio_debug && !(rc == 1 && errno == 0))
fprintf(stderr, "*** fdWritable fdno %d rc %d %s\n", fdno, rc, strerror(errno));
	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		return rc;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
	}
	return rc;
    } while (1);
    /*@notreached@*/
}

int fdReadable(FD_t fd, int secs)
{
    int fdno;
    int rc;
#if HAVE_POLL_H
    int msecs = (secs >= 0 ? (1000 * secs) : -1);
    struct pollfd rdfds;
#else
    struct timeval timeout, *tvp = (secs >= 0 ? &timeout : NULL);
    fd_set rdfds;
    FD_ZERO(&rdfds);
#endif

    /* HACK: flimsy wiring for davRead */
    if (fd->req != NULL)
	return 1;

    if ((fdno = fdFileno(fd)) < 0)
	return -1;	/* XXX W2DO? */
	
    do {
#if HAVE_POLL_H
	rdfds.fd = fdno;
	rdfds.events = POLLIN;
	rdfds.revents = 0;
	rc = poll(&rdfds, 1, msecs);
#else
	if (tvp) {
	    tvp->tv_sec = secs;
	    tvp->tv_usec = 0;
	}
	FD_SET(fdno, &rdfds);
	/*@-compdef -nullpass@*/
	rc = select(fdno + 1, &rdfds, NULL, NULL, tvp);
	/*@=compdef =nullpass@*/
#endif

	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		return rc;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
	}
	return rc;
    } while (1);
    /*@notreached@*/
}

/*@-boundswrite@*/
int fdFgets(FD_t fd, char * buf, size_t len)
{
    int fdno;
    int secs = fd->rd_timeoutsecs;
    size_t nb = 0;
    int ec = 0;
    char lastchar = '\0';

    if ((fdno = fdFileno(fd)) < 0)
	return 0;	/* XXX W2DO? */
	
    do {
	int rc;

	/* Is there data to read? */
	rc = fdReadable(fd, secs);

	switch (rc) {
	case -1:	/* error */
	    ec = -1;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case  0:	/* timeout */
	    ec = -1;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to read */
	    /*@switchbreak@*/ break;
	}

	errno = 0;
#ifdef	NOISY
	rc = fdRead(fd, buf + nb, 1);
#else
	rc = read(fdFileno(fd), buf + nb, 1);
#endif
	if (rc < 0) {
	    fd->syserrno = errno;
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** read: fd %p rc %d errno %d %s \"%s\"\n", fd, rc, errno, strerror(errno), buf);
	    ec = -1;
	    break;
	} else if (rc == 0) {
if (_rpmio_debug)
fprintf(stderr, "*** read: fd %p rc %d EOF errno %d %s \"%s\"\n", fd, rc, errno, strerror(errno), buf);
	    break;
	} else {
	    nb += rc;
	    buf[nb] = '\0';
	    lastchar = buf[nb - 1];
	}
    } while (ec == 0 && nb < len && lastchar != '\n');

    return (ec >= 0 ? nb : ec);
}
/*@=boundswrite@*/

/* =============================================================== */
/* Support for FTP/HTTP I/O.
 */
const char * ftpStrerror(int errorNumber)
{
    switch (errorNumber) {
    case 0:
	return _("Success");

    /* HACK error impediance match, coalesce and rename. */
    case FTPERR_NE_ERROR:
	return ("NE_ERROR: Generic error.");
    case FTPERR_NE_LOOKUP:
	return ("NE_LOOKUP: Hostname lookup failed.");
    case FTPERR_NE_AUTH:
	return ("NE_AUTH: Server authentication failed.");
    case FTPERR_NE_PROXYAUTH:
	return ("NE_PROXYAUTH: Proxy authentication failed.");
    case FTPERR_NE_CONNECT:
	return ("NE_CONNECT: Could not connect to server.");
    case FTPERR_NE_TIMEOUT:
	return ("NE_TIMEOUT: Connection timed out.");
    case FTPERR_NE_FAILED:
	return ("NE_FAILED: The precondition failed.");
    case FTPERR_NE_RETRY:
	return ("NE_RETRY: Retry request.");
    case FTPERR_NE_REDIRECT:
	return ("NE_REDIRECT: Redirect received.");

    case FTPERR_BAD_SERVER_RESPONSE:
	return _("Bad server response");
    case FTPERR_SERVER_IO_ERROR:
	return _("Server I/O error");
    case FTPERR_SERVER_TIMEOUT:
	return _("Server timeout");
    case FTPERR_BAD_HOST_ADDR:
	return _("Unable to lookup server host address");
    case FTPERR_BAD_HOSTNAME:
	return _("Unable to lookup server host name");
    case FTPERR_FAILED_CONNECT:
	return _("Failed to connect to server");
    case FTPERR_FAILED_DATA_CONNECT:
	return _("Failed to establish data connection to server");
    case FTPERR_FILE_IO_ERROR:
	return _("I/O error to local file");
    case FTPERR_PASSIVE_ERROR:
	return _("Error setting remote server to passive mode");
    case FTPERR_FILE_NOT_FOUND:
	return _("File not found on server");
    case FTPERR_NIC_ABORT_IN_PROGRESS:
	return _("Abort in progress");

    case FTPERR_UNKNOWN:
    default:
	return _("Unknown or unexpected error");
    }
}

const char *urlStrerror(const char *url)
{
    const char *retstr;
    /*@-branchstate@*/
    switch (urlIsURL(url)) {
    case URL_IS_HTTPS:
    case URL_IS_HTTP:
    case URL_IS_HKP:
    case URL_IS_FTP:
    {	urlinfo u;
/* XXX This only works for httpReq/ftpLogin/ftpReq failures */
	if (urlSplit(url, &u) == 0)
	    retstr = ftpStrerror(u->openError);
	else
	    retstr = _("Malformed URL");
    }	break;
    default:
	retstr = strerror(errno);
	break;
    }
    /*@=branchstate@*/
    return retstr;
}

#if !defined(HAVE_GETADDRINFO)
#if !defined(USE_ALT_DNS) || !USE_ALT_DNS
static int mygethostbyname(const char * host,
		/*@out@*/ struct in_addr * address)
	/*@globals h_errno @*/
	/*@modifies *address @*/
{
    struct hostent * hostinfo;

    /*@-multithreaded @*/
    hostinfo = gethostbyname(host);
    /*@=multithreaded @*/
    if (!hostinfo) return 1;

/*@-boundswrite@*/
    memcpy(address, hostinfo->h_addr_list[0], sizeof(*address));
/*@=boundswrite@*/
    return 0;
}
#endif

/*@-boundsread@*/
/*@-compdef@*/	/* FIX: address->s_addr undefined. */
static int getHostAddress(const char * host, /*@out@*/ struct in_addr * address)
	/*@globals errno, h_errno @*/
	/*@modifies *address, errno @*/
{
#if 0	/* XXX workaround nss_foo module hand-off using valgrind. */
    if (!strcmp(host, "localhost")) {
	/*@-moduncon @*/
	if (!inet_aton("127.0.0.1", address))
	    return FTPERR_BAD_HOST_ADDR;
	/*@=moduncon @*/
    } else
#endif
    if (xisdigit(host[0])) {
	/*@-moduncon @*/
	if (!inet_aton(host, address))
	    return FTPERR_BAD_HOST_ADDR;
	/*@=moduncon @*/
    } else {
	if (mygethostbyname(host, address)) {
	    errno = h_errno;
	    return FTPERR_BAD_HOSTNAME;
	}
    }

    return 0;
}
/*@=compdef@*/
/*@=boundsread@*/
#endif	/* HAVE_GETADDRINFO */

static int tcpConnect(FD_t ctrl, const char * host, int port)
	/*@globals fileSystem, internalState @*/
	/*@modifies ctrl, fileSystem, internalState @*/
{
    int fdno = -1;
    int rc;
#ifdef	HAVE_GETADDRINFO
/*@-unrecog@*/
    struct addrinfo hints, *res, *res0;
    char pbuf[NI_MAXSERV];
    int xx;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    sprintf(pbuf, "%d", port);
    pbuf[sizeof(pbuf)-1] = '\0';
    rc = FTPERR_FAILED_CONNECT;
    if (getaddrinfo(host, pbuf, &hints, &res0) == 0) {
	for (res = res0; res != NULL; res = res->ai_next) {
	    if ((fdno = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
		continue;
	    if (connect(fdno, res->ai_addr, res->ai_addrlen) < 0) {
		xx = close(fdno);
		continue;
	    }
	    /* success */
	    rc = 0;
	    if (_ftp_debug) {
		char hbuf[NI_MAXHOST];
		hbuf[0] = '\0';
		xx = getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof(hbuf),
				NULL, 0, NI_NUMERICHOST);
		fprintf(stderr,"++ connect [%s]:%d on fdno %d\n",
				/*@-unrecog@*/ hbuf /*@=unrecog@*/, port, fdno);
	    }
	    break;
	}
	freeaddrinfo(res0);
    }
    if (rc < 0)
	goto errxit;
/*@=unrecog@*/
#else	/* HAVE_GETADDRINFO */
    struct sockaddr_in sin;

/*@-boundswrite@*/
    memset(&sin, 0, sizeof(sin));
/*@=boundswrite@*/
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

  do {
    if ((rc = getHostAddress(host, &sin.sin_addr)) < 0)
	break;

    if ((fdno = socket(sin.sin_family, SOCK_STREAM, IPPROTO_IP)) < 0) {
	rc = FTPERR_FAILED_CONNECT;
	break;
    }

    /*@-internalglobs@*/
    if (connect(fdno, (struct sockaddr *) &sin, sizeof(sin))) {
	rc = FTPERR_FAILED_CONNECT;
	break;
    }
    /*@=internalglobs@*/
  } while (0);

    if (rc < 0)
	goto errxit;

if (_ftp_debug)
fprintf(stderr,"++ connect %s:%d on fdno %d\n",
/*@-unrecog -moduncon -evalorderuncon @*/
inet_ntoa(sin.sin_addr)
/*@=unrecog =moduncon =evalorderuncon @*/ ,
(int)ntohs(sin.sin_port), fdno);
#endif	/* HAVE_GETADDRINFO */

    fdSetFdno(ctrl, (fdno >= 0 ? fdno : -1));
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
    if (fdno >= 0)
	(void) close(fdno);
    return rc;
}

/*@-boundswrite@*/
static int checkResponse(void * uu, FD_t ctrl,
		/*@out@*/ int *ecp, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies ctrl, *ecp, *str, fileSystem @*/
{
    urlinfo u = uu;
    char *buf;
    size_t bufAlloced;
    int bufLength = 0;
    const char *s;
    char *se;
    int ec = 0;
    int moretodo = 1;
    char errorCode[4];

    URLSANE(u);
    if (u->bufAlloced == 0 || u->buf == NULL) {
	u->bufAlloced = _url_iobuf_size;
	u->buf = xcalloc(u->bufAlloced, sizeof(u->buf[0]));
    }
    buf = u->buf;
    bufAlloced = u->bufAlloced;
    *buf = '\0';

    errorCode[0] = '\0';

    do {
	int rc;

	/*
	 * Read next line from server.
	 */
	se = buf + bufLength;
	*se = '\0';
	rc = fdFgets(ctrl, se, (bufAlloced - bufLength));
	if (rc < 0) {
	    ec = FTPERR_BAD_SERVER_RESPONSE;
	    continue;
	} else if (rc == 0 || fdWritable(ctrl, 0) < 1)
	    moretodo = 0;

	/*
	 * Process next line from server.
	 */
	for (s = se; *s != '\0'; s = se) {
		const char *e;

		while (*se && *se != '\n') se++;

		if (se > s && se[-1] == '\r')
		   se[-1] = '\0';
		if (*se == '\0')
		    /*@innerbreak@*/ break;

if (_ftp_debug)
fprintf(stderr, "<- %s\n", s);

		/* HTTP: header termination on empty line */
		if (*s == '\0') {
		    moretodo = 0;
		    /*@innerbreak@*/ break;
		}
		*se++ = '\0';

		/* HTTP: look for "HTTP/1.1 123 ..." */
		if (!strncmp(s, "HTTP", sizeof("HTTP")-1)) {
		    ctrl->contentLength = -1;
		    if ((e = strchr(s, '.')) != NULL) {
			e++;
			u->httpVersion = *e - '0';
			if (u->httpVersion < 1 || u->httpVersion > 2)
			    ctrl->persist = u->httpVersion = 0;
			else
			    ctrl->persist = 1;
		    }
		    if ((e = strchr(s, ' ')) != NULL) {
			e++;
			if (strchr("0123456789", *e))
			    strncpy(errorCode, e, 3);
			errorCode[3] = '\0';
		    }
		    /*@innercontinue@*/ continue;
		}

		/* HTTP: look for "token: ..." */
		for (e = s; *e && !(*e == ' ' || *e == ':'); e++)
		    {};
		if (e > s && *e++ == ':') {
		    size_t ne = (e - s);
		    while (*e && *e == ' ') e++;
#if 0
		    if (!strncmp(s, "Date:", ne)) {
		    } else
		    if (!strncmp(s, "Server:", ne)) {
		    } else
		    if (!strncmp(s, "Last-Modified:", ne)) {
		    } else
		    if (!strncmp(s, "ETag:", ne)) {
		    } else
#endif
		    if (!strncmp(s, "Accept-Ranges:", ne)) {
			if (!strcmp(e, "bytes"))
			    u->allow |= RPMURL_SERVER_HASRANGE;
			if (!strcmp(e, "none"))
			    u->allow &= ~RPMURL_SERVER_HASRANGE;
		    } else
		    if (!strncmp(s, "Content-Length:", ne)) {
			if (strchr("0123456789", *e))
			    ctrl->contentLength = atol(e);
		    } else
		    if (!strncmp(s, "Connection:", ne)) {
			if (!strcmp(e, "close"))
			    ctrl->persist = 0;
		    }
#if 0
		    else
		    if (!strncmp(s, "Content-Type:", ne)) {
		    } else
		    if (!strncmp(s, "Transfer-Encoding:", ne)) {
			if (!strcmp(e, "chunked"))
			    ctrl->wr_chunked = 1;
			else
			    ctrl->wr_chunked = 0;
		    } else
		    if (!strncmp(s, "Allow:", ne)) {
		    }
#endif
		    /*@innercontinue@*/ continue;
		}

		/* HTTP: look for "<TITLE>501 ... </TITLE>" */
		if (!strncmp(s, "<TITLE>", sizeof("<TITLE>")-1))
		    s += sizeof("<TITLE>") - 1;

		/* FTP: look for "123-" and/or "123 " */
		if (strchr("0123456789", *s)) {
		    if (errorCode[0] != '\0') {
			if (!strncmp(s, errorCode, sizeof("123")-1) && s[3] == ' ')
			    moretodo = 0;
		    } else {
			strncpy(errorCode, s, sizeof("123")-1);
			errorCode[3] = '\0';
			if (s[3] != '-')
			    moretodo = 0;
		    }
		}
	}

	if (moretodo && se > s) {
	    bufLength = se - s - 1;
	    if (s != buf)
		memmove(buf, s, bufLength);
	} else {
	    bufLength = 0;
	}
    } while (moretodo && ec == 0);

    if (str)	*str = buf;
    if (ecp)	*ecp = atoi(errorCode);

    return ec;
}
/*@=boundswrite@*/

static int ftpCheckResponse(urlinfo u, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies u, *str, fileSystem @*/
{
    int ec = 0;
    int rc;

    URLSANE(u);
    rc = checkResponse(u, u->ctrl, &ec, str);

    switch (ec) {
    case 550:
	return FTPERR_FILE_NOT_FOUND;
	/*@notreached@*/ break;
    case 552:
	return FTPERR_NIC_ABORT_IN_PROGRESS;
	/*@notreached@*/ break;
    default:
	if (ec >= 400 && ec <= 599) {
	    return FTPERR_BAD_SERVER_RESPONSE;
	}
	break;
    }
    return rc;
}

static int ftpCommand(urlinfo u, char ** str, ...)
	/*@globals fileSystem, internalState @*/
	/*@modifies u, *str, fileSystem, internalState @*/
{
    va_list ap;
    int len = 0;
    const char * s, * t;
    char * te;
    int rc;

    URLSANE(u);
    va_start(ap, str);
    while ((s = va_arg(ap, const char *)) != NULL) {
	if (len) len++;
	len += strlen(s);
    }
    len += sizeof("\r\n")-1;
    va_end(ap);

/*@-boundswrite@*/
    t = te = alloca(len + 1);

    va_start(ap, str);
    while ((s = va_arg(ap, const char *)) != NULL) {
	if (te > t) *te++ = ' ';
	te = stpcpy(te, s);
    }
    te = stpcpy(te, "\r\n");
    va_end(ap);
/*@=boundswrite@*/

if (_ftp_debug)
fprintf(stderr, "-> %s", t);
    if (fdWrite(u->ctrl, t, (te-t)) != (te-t))
	return FTPERR_SERVER_IO_ERROR;

    rc = ftpCheckResponse(u, str);
    return rc;
}

static int ftpLogin(urlinfo u)
	/*@globals fileSystem, internalState @*/
	/*@modifies u, fileSystem, internalState @*/
{
    const char * host;
    const char * user;
    const char * password;
    int port;
    int rc;

    URLSANE(u);
    u->ctrl = fdLink(u->ctrl, "open ctrl");

    if (((host = (u->proxyh ? u->proxyh : u->host)) == NULL)) {
	rc = FTPERR_BAD_HOSTNAME;
	goto errxit;
    }

    if ((port = (u->proxyp > 0 ? u->proxyp : u->port)) < 0) port = IPPORT_FTP;

    /*@-branchstate@*/
    if ((user = (u->proxyu ? u->proxyu : u->user)) == NULL)
	user = "anonymous";
    /*@=branchstate@*/

    /*@-branchstate@*/
    if ((password = u->password) == NULL) {
 	uid_t uid = getuid();
	struct passwd * pw;
	if (uid && (pw = getpwuid(uid)) != NULL) {
/*@-boundswrite@*/
	    char *myp = alloca(strlen(pw->pw_name) + sizeof("@"));
	    strcpy(myp, pw->pw_name);
	    strcat(myp, "@");
/*@=boundswrite@*/
	    password = myp;
	} else {
	    password = "root@";
	}
    }
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (fdFileno(u->ctrl) >= 0 && fdWritable(u->ctrl, 0) < 1)
	/*@-refcounttrans@*/ (void) fdClose(u->ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/

/*@-usereleased@*/
    if (fdFileno(u->ctrl) < 0) {
	rc = tcpConnect(u->ctrl, host, port);
	if (rc < 0)
	    goto errxit2;
    }

    if ((rc = ftpCheckResponse(u, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "USER", user, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "PASS", password, NULL)))
	goto errxit;

    if ((rc = ftpCommand(u, NULL, "TYPE", "I", NULL)))
	goto errxit;

    /*@-compdef@*/
    return 0;
    /*@=compdef@*/

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(u->ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
errxit2:
    /*@-branchstate@*/
    if (fdFileno(u->ctrl) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(u->ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/
    /*@-compdef@*/
    return rc;
    /*@=compdef@*/
/*@=usereleased@*/
}

int ftpReq(FD_t data, const char * ftpCmd, const char * ftpArg)
{
    urlinfo u = data->url;
#if !defined(HAVE_GETADDRINFO)
    struct sockaddr_in dataAddress;
#endif	/* HAVE_GETADDRINFO */
    char remoteIP[NI_MAXHOST];
    char * cmd;
    int cmdlen;
    char * passReply;
    char * chptr;
    int rc;
    int epsv;
    int port;

    remoteIP[0] = '\0';
/*@-boundswrite@*/
    URLSANE(u);
    if (ftpCmd == NULL)
	return FTPERR_UNKNOWN;	/* XXX W2DO? */

    cmdlen = strlen(ftpCmd) + (ftpArg ? 1+strlen(ftpArg) : 0) + sizeof("\r\n");
    chptr = cmd = alloca(cmdlen);
    chptr = stpcpy(chptr, ftpCmd);
    if (ftpArg) {
	*chptr++ = ' ';
	chptr = stpcpy(chptr, ftpArg);
    }
    chptr = stpcpy(chptr, "\r\n");
    cmdlen = chptr - cmd;

/*
 * Get the ftp version of the Content-Length.
 */
    if (!strncmp(cmd, "RETR", 4)) {
	unsigned cl;

	passReply = NULL;
	rc = ftpCommand(u, &passReply, "SIZE", ftpArg, NULL);
	if (rc)
	    goto errxit;
	if (sscanf(passReply, "%d %u", &rc, &cl) != 2) {
	    rc = FTPERR_BAD_SERVER_RESPONSE;
	    goto errxit;
	}
	rc = 0;
	data->contentLength = cl;
    }

    epsv = 0;
    passReply = NULL;
#ifdef HAVE_GETNAMEINFO
    rc = ftpCommand(u, &passReply, "EPSV", NULL);
    if (rc == 0) {
#ifdef HAVE_GETADDRINFO
	struct sockaddr_storage ss;
#else /* HAVE_GETADDRINFO */
	struct sockaddr_in ss;
#endif /* HAVE_GETADDRINFO */
	socklen_t sslen = sizeof(ss);

	/* we need to know IP of remote host */
	if ((getpeername(fdFileno(c2f(u->ctrl)), (struct sockaddr *)&ss, &sslen) == 0)
	 && (getnameinfo((struct sockaddr *)&ss, sslen,
			remoteIP, sizeof(remoteIP),
			NULL, 0, NI_NUMERICHOST) == 0))
	{
		epsv++;
	} else {
		/* abort EPSV and fall back to PASV */
		rc = ftpCommand(u, &passReply, "ABOR", NULL);
		if (rc) {
		    rc = FTPERR_PASSIVE_ERROR;
		    goto errxit;
		}
	}
    }
   if (epsv == 0)
#endif /* HAVE_GETNAMEINFO */
	rc = ftpCommand(u, &passReply, "PASV", NULL);
    if (rc) {
	rc = FTPERR_PASSIVE_ERROR;
	goto errxit;
    }

    chptr = passReply;
    while (*chptr && *chptr != '(') chptr++;
    if (*chptr != '(') return FTPERR_PASSIVE_ERROR;
    chptr++;
    passReply = chptr;
    while (*chptr && *chptr != ')') chptr++;
    if (*chptr != ')') return FTPERR_PASSIVE_ERROR;
    *chptr-- = '\0';

  if (epsv) {
	int i;
	if(sscanf(passReply,"%*c%*c%*c%d%*c",&i) != 1) {
	   rc = FTPERR_PASSIVE_ERROR;
	   goto errxit;
	}
	port = i;
  } else {

    while (*chptr && *chptr != ',') chptr--;
    if (*chptr != ',') return FTPERR_PASSIVE_ERROR;
    chptr--;
    while (*chptr && *chptr != ',') chptr--;
    if (*chptr != ',') return FTPERR_PASSIVE_ERROR;
    *chptr++ = '\0';

    /* now passReply points to the IP portion, and chptr points to the
       port number portion */

    {	int i, j;
	if (sscanf(chptr, "%d,%d", &i, &j) != 2) {
	    rc = FTPERR_PASSIVE_ERROR;
	    goto errxit;
	}
	port = (((unsigned)i) << 8) + j;
    }

    chptr = passReply;
    while (*chptr++ != '\0') {
	if (*chptr == ',') *chptr = '.';
    }
/*@=boundswrite@*/
    sprintf(remoteIP, "%s", passReply);
  } /* if (epsv) */

#ifdef HAVE_GETADDRINFO
/*@-unrecog@*/
    {
	struct addrinfo hints, *res, *res0;
	char pbuf[NI_MAXSERV];
	int xx;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
#if defined(AI_IDN)
	hints.ai_flags |= AI_IDN;
#endif
	sprintf(pbuf, "%d", port);
	pbuf[sizeof(pbuf)-1] = '\0';
	if (getaddrinfo(remoteIP, pbuf, &hints, &res0)) {
	    rc = FTPERR_PASSIVE_ERROR;
	    goto errxit;
	}

	for (res = res0; res != NULL; res = res->ai_next) {
	    rc = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	    fdSetFdno(data, (rc >= 0 ? rc : -1));
	    if (rc < 0) {
	        if (res->ai_next)
		    continue;
		else {
		    rc = FTPERR_FAILED_CONNECT;
		    freeaddrinfo(res0);
		    goto errxit;
		}
	    }
	    data = fdLink(data, "open data (ftpReq)");

	    /* XXX setsockopt SO_LINGER */
	    /* XXX setsockopt SO_KEEPALIVE */
	    /* XXX setsockopt SO_TOS IPTOS_THROUGHPUT */

	    {
		int criterr = 0;
	        while (connect(fdFileno(data), res->ai_addr, res->ai_addrlen) < 0) {
	            if (errno == EINTR)
		        /*@innercontinue@*/ continue;
		    criterr++;
		}
		if (criterr) {
		    if (res->ai_addr) {
/*@-refcounttrans@*/
		        xx = fdClose(data);
/*@=refcounttrans@*/
		        continue;
		    } else {
		        rc = FTPERR_PASSIVE_ERROR;
		        freeaddrinfo(res0);
		        goto errxit;
		    }
		}
	    }
	    /* success */
	    rc = 0;
	    break;
	}
	freeaddrinfo(res0);
    }
/*@=unrecog@*/
#else /* HAVE_GETADDRINFO */
    memset(&dataAddress, 0, sizeof(dataAddress));
    dataAddress.sin_family = AF_INET;
    dataAddress.sin_port = htons(port);

    /*@-moduncon@*/
    if (!inet_aton(remoteIP, &dataAddress.sin_addr)) {
	rc = FTPERR_PASSIVE_ERROR;
	goto errxit;
    }
    /*@=moduncon@*/

    rc = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    fdSetFdno(data, (rc >= 0 ? rc : -1));
    if (rc < 0) {
	rc = FTPERR_FAILED_CONNECT;
	goto errxit;
    }
    data = fdLink(data, "open data (ftpReq)");

    /* XXX setsockopt SO_LINGER */
    /* XXX setsockopt SO_KEEPALIVE */
    /* XXX setsockopt SO_TOS IPTOS_THROUGHPUT */

    /*@-internalglobs@*/
    while (connect(fdFileno(data), (struct sockaddr *) &dataAddress,
	        sizeof(dataAddress)) < 0)
    {
	if (errno == EINTR)
	    continue;
	rc = FTPERR_FAILED_DATA_CONNECT;
	goto errxit;
    }
    /*@=internalglobs@*/
#endif /* HAVE_GETADDRINFO */

if (_ftp_debug)
fprintf(stderr, "-> %s", cmd);
    if (fdWrite(u->ctrl, cmd, cmdlen) != cmdlen) {
	rc = FTPERR_SERVER_IO_ERROR;
	goto errxit;
    }

    if ((rc = ftpCheckResponse(u, NULL))) {
	goto errxit;
    }

    data->ftpFileDoneNeeded = 1;
    u->ctrl = fdLink(u->ctrl, "grab data (ftpReq)");
    u->ctrl = fdLink(u->ctrl, "open data (ftpReq)");
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(u->ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
    /*@-branchstate@*/
    if (fdFileno(data) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(data); /*@=refcounttrans@*/
    /*@=branchstate@*/
    return rc;
}

/*@unchecked@*/ /*@null@*/
static rpmCallbackFunction	urlNotify = NULL;

/*@unchecked@*/ /*@null@*/
static void *			urlNotifyData = NULL;

/*@unchecked@*/
static int			urlNotifyCount = -1;

void urlSetCallback(rpmCallbackFunction notify, void *notifyData, int notifyCount) {
    urlNotify = notify;
    urlNotifyData = notifyData;
    urlNotifyCount = (notifyCount >= 0) ? notifyCount : 4096;
}

int ufdCopy(FD_t sfd, FD_t tfd)
{
    char buf[BUFSIZ];
    int itemsRead;
    int itemsCopied = 0;
    int rc = 0;
    int notifier = -1;

    if (urlNotify) {
/*@-boundsread@*/
	/*@-noeffectuncon @*/ /* FIX: check rc */
	(void)(*urlNotify) (NULL, RPMCALLBACK_INST_OPEN_FILE,
		0, 0, NULL, urlNotifyData);
	/*@=noeffectuncon @*/
/*@=boundsread@*/
    }

    while (1) {
	rc = Fread(buf, sizeof(buf[0]), sizeof(buf), sfd);
	if (rc < 0)
	    break;
	else if (rc == 0) {
	    rc = itemsCopied;
	    break;
	}
	itemsRead = rc;
	rc = Fwrite(buf, sizeof(buf[0]), itemsRead, tfd);
	if (rc < 0)
	    break;
 	if (rc != itemsRead) {
	    rc = FTPERR_FILE_IO_ERROR;
	    break;
	}

	itemsCopied += itemsRead;
	if (urlNotify && urlNotifyCount > 0) {
	    int n = itemsCopied/urlNotifyCount;
	    if (n != notifier) {
/*@-boundsread@*/
		/*@-noeffectuncon @*/ /* FIX: check rc */
		(void)(*urlNotify) (NULL, RPMCALLBACK_INST_PROGRESS,
			itemsCopied, 0, NULL, urlNotifyData);
		/*@=noeffectuncon @*/
/*@=boundsread@*/
		notifier = n;
	    }
	}
    }

    DBGIO(sfd, (stderr, "++ copied %d bytes: %s\n", itemsCopied,
	ftpStrerror(rc)));

    if (urlNotify) {
/*@-boundsread@*/
	/*@-noeffectuncon @*/ /* FIX: check rc */
	(void)(*urlNotify) (NULL, RPMCALLBACK_INST_OPEN_FILE,
		itemsCopied, itemsCopied, NULL, urlNotifyData);
	/*@=noeffectuncon @*/
/*@=boundsread@*/
    }

    return rc;
}

static int urlConnect(const char * url, /*@out@*/ urlinfo * uret)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies *uret, fileSystem, internalState @*/
{
    urlinfo u;
    int rc = 0;

    if (urlSplit(url, &u) < 0)
	return -1;

    if (u->urltype == URL_IS_FTP) {
	FD_t fd;

	if ((fd = u->ctrl) == NULL) {
	    fd = u->ctrl = fdNew("persist ctrl (urlConnect FTP)");
	    fdSetOpen(u->ctrl, url, 0, 0);
	    fdSetIo(u->ctrl, ufdio);
	}
	
	fd->rd_timeoutsecs = ftpTimeoutSecs;
	fd->contentLength = fd->bytesRemain = -1;
	fd->url = NULL;		/* XXX FTP ctrl has not */
	fd->ftpFileDoneNeeded = 0;
	fd = fdLink(fd, "grab ctrl (urlConnect FTP)");

	if (fdFileno(u->ctrl) < 0) {
	    rpmMessage(RPMMESS_DEBUG, _("logging into %s as %s, pw %s\n"),
			u->host ? u->host : "???",
			u->user ? u->user : "ftp",
			u->password ? u->password : "(username)");

	    if ((rc = ftpLogin(u)) < 0) {	/* XXX save ftpLogin error */
		u->ctrl = fdFree(fd, "grab ctrl (urlConnect FTP)");
		u->openError = rc;
	    }
	}
    }

/*@-boundswrite@*/
    if (uret != NULL)
	*uret = urlLink(u, "urlConnect");
/*@=boundswrite@*/
    u = urlFree(u, "urlSplit (urlConnect)");	

    return rc;
}

int ufdGetFile(FD_t sfd, FD_t tfd)
{
    int rc;

    FDSANE(sfd);
    FDSANE(tfd);
    rc = ufdCopy(sfd, tfd);
    (void) Fclose(sfd);
    if (rc > 0)		/* XXX ufdCopy now returns no. bytes copied */
	rc = 0;
    return rc;
}

int ftpCmd(const char * cmd, const char * url, const char * arg2)
{
    urlinfo u;
    int rc;
    const char * path;

    if (urlConnect(url, &u) < 0)
	return -1;

    (void) urlPath(url, &path);

    rc = ftpCommand(u, NULL, cmd, path, arg2, NULL);
    u->ctrl = fdFree(u->ctrl, "grab ctrl (ftpCmd)");
    return rc;
}

/* XXX these aren't worth the pain of including correctly */
#if !defined(IAC)
#define	IAC	255		/* interpret as command: */
#endif
#if !defined(IP)
#define	IP	244		/* interrupt process--permanently */
#endif
#if !defined(DM)
#define	DM	242		/* data mark--for connect. cleaning */
#endif
#if !defined(SHUT_RDWR)
#define	SHUT_RDWR	1+1
#endif

static int ftpAbort(urlinfo u, FD_t data)
	/*@globals fileSystem, internalState @*/
	/*@modifies u, data, fileSystem, internalState @*/
{
    static unsigned char ipbuf[3] = { IAC, IP, IAC };
    FD_t ctrl;
    int rc;
    int tosecs;

    URLSANE(u);

    if (data != NULL) {
	data->ftpFileDoneNeeded = 0;
	if (fdFileno(data) >= 0)
	    u->ctrl = fdFree(u->ctrl, "open data (ftpAbort)");
	u->ctrl = fdFree(u->ctrl, "grab data (ftpAbort)");
    }
    ctrl = u->ctrl;

    DBGIO(0, (stderr, "-> ABOR\n"));

/*@-usereleased -compdef@*/
    if (send(fdFileno(ctrl), ipbuf, sizeof(ipbuf), MSG_OOB) != sizeof(ipbuf)) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
	return FTPERR_SERVER_IO_ERROR;
    }

    sprintf(u->buf, "%cABOR\r\n",(char) DM);
    if (fdWrite(ctrl, u->buf, 7) != 7) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
	return FTPERR_SERVER_IO_ERROR;
    }

    if (data && fdFileno(data) >= 0) {
	/* XXX shorten data drain time wait */
	tosecs = data->rd_timeoutsecs;
	data->rd_timeoutsecs = 10;
	if (fdReadable(data, data->rd_timeoutsecs) > 0) {
/*@-boundswrite@*/
	    while (timedRead(data, u->buf, u->bufAlloced) > 0)
		u->buf[0] = '\0';
/*@=boundswrite@*/
	}
	data->rd_timeoutsecs = tosecs;
	/* XXX ftp abort needs to close the data channel to receive status */
	(void) shutdown(fdFileno(data), SHUT_RDWR);
	(void) close(fdFileno(data));
	data->fps[0].fdno = -1;	/* XXX WRONG but expedient */
    }

    /* XXX shorten ctrl drain time wait */
    tosecs = u->ctrl->rd_timeoutsecs;
    u->ctrl->rd_timeoutsecs = 10;
    if ((rc = ftpCheckResponse(u, NULL)) == FTPERR_NIC_ABORT_IN_PROGRESS) {
	rc = ftpCheckResponse(u, NULL);
    }
    rc = ftpCheckResponse(u, NULL);
    u->ctrl->rd_timeoutsecs = tosecs;

    return rc;
/*@=usereleased =compdef@*/
}

static int ftpFileDone(urlinfo u, FD_t data)
	/*@globals fileSystem @*/
	/*@modifies u, data, fileSystem @*/
{
    int rc = 0;

    URLSANE(u);
    assert(data->ftpFileDoneNeeded);

    if (data->ftpFileDoneNeeded) {
	data->ftpFileDoneNeeded = 0;
	u->ctrl = fdFree(u->ctrl, "open data (ftpFileDone)");
	u->ctrl = fdFree(u->ctrl, "grab data (ftpFileDone)");
	rc = ftpCheckResponse(u, NULL);
    }
    return rc;
}

#ifdef DEAD
static int httpResp(urlinfo u, FD_t ctrl, /*@out@*/ char ** str)
	/*@globals fileSystem @*/
	/*@modifies ctrl, *str, fileSystem @*/
{
    int ec = 0;
    int rc;

    URLSANE(u);
    rc = checkResponse(u, ctrl, &ec, str);

if (_ftp_debug && !(rc == 0 && (ec == 200 || ec == 201)))
fprintf(stderr, "*** httpResp: rc %d ec %d\n", rc, ec);

    switch (ec) {
    case 200:
    case 201:			/* 201 Created. */
	break;
    case 204:			/* HACK: if overwriting, 204 No Content. */
    case 403:			/* 403 Forbidden. */
	ctrl->syserrno = EACCES;	/* HACK */
	rc = FTPERR_UNKNOWN;
	break;
    default:
	rc = FTPERR_FILE_NOT_FOUND;
	break;
    }
    return rc;
}

static int httpReq(FD_t ctrl, const char * httpCmd, const char * httpArg)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies ctrl, fileSystem, internalState @*/
{
    urlinfo u;
    const char * host;
    const char * path;
    char hthost[NI_MAXHOST];
    int port;
    int rc;
    char * req;
    size_t len;
    int retrying = 0;

assert(ctrl != NULL);
    u = ctrl->url;
    URLSANE(u);

    if (((host = (u->proxyh ? u->proxyh : u->host)) == NULL))
	return FTPERR_BAD_HOSTNAME;
    if (strchr(host, ':'))
	sprintf(hthost, "[%s]", host);
    else
	strcpy(hthost, host);

    if ((port = (u->proxyp > 0 ? u->proxyp : u->port)) < 0) port = 80;
    path = (u->proxyh || u->proxyp > 0) ? u->url : httpArg;
    /*@-branchstate@*/
    if (path == NULL) path = "";
    /*@=branchstate@*/

reopen:
    /*@-branchstate@*/
    if (fdFileno(ctrl) >= 0 && (rc = fdWritable(ctrl, 0)) < 1) {
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
    }
    /*@=branchstate@*/

/*@-usereleased@*/
    if (fdFileno(ctrl) < 0) {
	rc = tcpConnect(ctrl, host, port);
	if (rc < 0)
	    goto errxit2;
	ctrl = fdLink(ctrl, "open ctrl (httpReq)");
    }

    len = sizeof("\
req x HTTP/1.0\r\n\
User-Agent: rpm/3.0.4\r\n\
Host: y:z\r\n\
Accept: text/plain\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
") + strlen(httpCmd) + strlen(path) + sizeof(VERSION) + strlen(hthost) + 20;

/*@-boundswrite@*/
    req = alloca(len);
    *req = '\0';

  if (!strcmp(httpCmd, "PUT")) {
    sprintf(req, "\
%s %s HTTP/1.%d\r\n\
User-Agent: rpm/%s\r\n\
Host: %s:%d\r\n\
Accept: text/plain\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
",	httpCmd, path, (u->httpVersion ? 1 : 0), VERSION, hthost, port);
} else {
    sprintf(req, "\
%s %s HTTP/1.%d\r\n\
User-Agent: rpm/%s\r\n\
Host: %s:%d\r\n\
Accept: text/plain\r\n\
\r\n\
",	httpCmd, path, (u->httpVersion ? 1 : 0), VERSION, hthost, port);
}
/*@=boundswrite@*/

if (_ftp_debug)
fprintf(stderr, "-> %s", req);

    len = strlen(req);
    if (fdWrite(ctrl, req, len) != len) {
	rc = FTPERR_SERVER_IO_ERROR;
	goto errxit;
    }

    /*@-branchstate@*/
    if (!strcmp(httpCmd, "PUT")) {
	ctrl->wr_chunked = 1;
    } else {

	rc = httpResp(u, ctrl, NULL);

	if (rc) {
	    if (!retrying) {	/* not HTTP_OK */
		retrying = 1;
		/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
		goto reopen;
	    }
	    goto errxit;
	}
    }
    /*@=branchstate@*/

    ctrl = fdLink(ctrl, "open data (httpReq)");
    return 0;

errxit:
    /*@-observertrans@*/
    fdSetSyserrno(ctrl, errno, ftpStrerror(rc));
    /*@=observertrans@*/
errxit2:
    /*@-branchstate@*/
    if (fdFileno(ctrl) >= 0)
	/*@-refcounttrans@*/ (void) fdClose(ctrl); /*@=refcounttrans@*/
    /*@=branchstate@*/
    return rc;
/*@=usereleased@*/
}
#endif

/* XXX DYING: unused */
void * ufdGetUrlinfo(FD_t fd)
{
    FDSANE(fd);
    if (fd->url == NULL)
	return NULL;
    return urlLink(fd->url, "ufdGetUrlinfo");
}

/* =============================================================== */
static ssize_t ufdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies buf, fileSystem, internalState @*/
	/*@requires maxSet(buf) >= (count - 1) @*/
	/*@ensures maxRead(buf) == result @*/
{
    FD_t fd = c2f(cookie);
    int bytesRead;
    int total;

    /* XXX preserve timedRead() behavior */
    if (fdGetIo(fd) == fdio) {
	struct stat sb;
	int fdno = fdFileno(fd);
	(void) fstat(fdno, &sb);
	if (S_ISREG(sb.st_mode))
	    return fdRead(fd, buf, count);
    }

    UFDONLY(fd);
    assert(fd->rd_timeoutsecs >= 0);

    for (total = 0; total < count; total += bytesRead) {

	int rc;

	bytesRead = 0;

	/* Is there data to read? */
	if (fd->bytesRemain == 0) return total;	/* XXX simulate EOF */
	rc = fdReadable(fd, fd->rd_timeoutsecs);

	switch (rc) {
	case -1:	/* error */
	case  0:	/* timeout */
	    return total;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to read */
	    /*@switchbreak@*/ break;
	}

/*@-boundswrite@*/
	rc = fdRead(fd, buf + total, count - total);
/*@=boundswrite@*/

	if (rc < 0) {
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** read: rc %d errno %d %s \"%s\"\n", rc, errno, strerror(errno), buf);
	    return rc;
	    /*@notreached@*/ break;
	} else if (rc == 0) {
	    return total;
	    /*@notreached@*/ break;
	}
	bytesRead = rc;
    }

    return count;
}

static ssize_t ufdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int bytesWritten;
    int total = 0;

#ifdef	NOTYET
    if (fdGetIo(fd) == fdio) {
	struct stat sb;
	(void) fstat(fdGetFdno(fd), &sb);
	if (S_ISREG(sb.st_mode))
	    return fdWrite(fd, buf, count);
    }
#endif

    UFDONLY(fd);

    for (total = 0; total < count; total += bytesWritten) {

	int rc;

	bytesWritten = 0;

	/* Is there room to write data? */
	if (fd->bytesRemain == 0) {
fprintf(stderr, "*** ufdWrite fd %p WRITE PAST END OF CONTENT\n", fd);
	    return total;	/* XXX simulate EOF */
	}
	rc = fdWritable(fd, 2);		/* XXX configurable? */

	switch (rc) {
	case -1:	/* error */
	case  0:	/* timeout */
	    return total;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:	/* data to write */
	    /*@switchbreak@*/ break;
	}

	rc = fdWrite(fd, buf + total, count - total);

	if (rc < 0) {
	    switch (errno) {
	    case EWOULDBLOCK:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
if (_rpmio_debug)
fprintf(stderr, "*** write: rc %d errno %d %s \"%s\"\n", rc, errno, strerror(errno), buf);
	    return rc;
	    /*@notreached@*/ break;
	} else if (rc == 0) {
	    return total;
	    /*@notreached@*/ break;
	}
	bytesWritten = rc;
    }

    return count;
}

static inline int ufdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);

    switch (fd->urlType) {
    case URL_IS_UNKNOWN:
    case URL_IS_PATH:
	break;
    case URL_IS_HTTPS:
    case URL_IS_HTTP:
    case URL_IS_HKP:
    case URL_IS_FTP:
    case URL_IS_DASH:
    default:
	return -2;
	/*@notreached@*/ break;
    }
    return fdSeek(cookie, pos, whence);
}

/*@-branchstate@*/
/*@-usereleased@*/	/* LCL: fd handling is tricky here. */
int ufdClose( /*@only@*/ void * cookie)
{
    FD_t fd = c2f(cookie);

    UFDONLY(fd);

    /*@-branchstate@*/
    if (fd->url) {
	urlinfo u = fd->url;

	if (fd == u->data)
		fd = u->data = fdFree(fd, "grab data (ufdClose persist)");
	else
		fd = fdFree(fd, "grab data (ufdClose)");
	(void) urlFree(fd->url, "url (ufdClose)");
	fd->url = NULL;
	u->ctrl = fdFree(u->ctrl, "grab ctrl (ufdClose)");

	if (u->urltype == URL_IS_FTP) {

	    /* XXX if not using libio, lose the fp from fpio */
	    {   FILE * fp;
		/*@+voidabstract -nullpass@*/
		fp = fdGetFILE(fd);
		if (noLibio && fp)
		    fdSetFp(fd, NULL);
		/*@=voidabstract =nullpass@*/
	    }

	    /*
	     * Non-error FTP has 4 refs on the data fd:
	     *	"persist data (ufdOpen FTP)"		rpmio.c:888
	     *	"grab data (ufdOpen FTP)"		rpmio.c:892
	     *	"open data (ftpReq)"			ftp.c:633
	     *	"fopencookie"				rpmio.c:1507
	     *
	     * Non-error FTP has 5 refs on the ctrl fd:
	     *	"persist ctrl"				url.c:176
	     *	"grab ctrl (urlConnect FTP)"		rpmio.c:404
	     *	"open ctrl"				ftp.c:504
	     *	"grab data (ftpReq)"			ftp.c:661
	     *	"open data (ftpReq)"			ftp.c:662
	     */
	    if (fd->bytesRemain > 0) {
		if (fd->ftpFileDoneNeeded) {
		    if (fdReadable(u->ctrl, 0) > 0)
			(void) ftpFileDone(u, fd);
		    else
			(void) ftpAbort(u, fd);
		}
	    } else {
		int rc;
		/* XXX STOR et al require close before ftpFileDone */
		/*@-refcounttrans@*/
		rc = fdClose(fd);
		/*@=refcounttrans@*/
#if 0	/* XXX error exit from ufdOpen does not have this set */
		assert(fd->ftpFileDoneNeeded != 0);
#endif
		/*@-compdef@*/ /* FIX: u->data undefined */
		if (fd->ftpFileDoneNeeded)
		    (void) ftpFileDone(u, fd);
		/*@=compdef@*/
		return rc;
	    }
	}

	/* XXX Why not (u->urltype == URL_IS_HTTP) ??? */
	/* XXX Why not (u->urltype == URL_IS_HTTPS) ??? */
	/* XXX Why not (u->urltype == URL_IS_HKP) ??? */
	if (u->scheme != NULL
	 && (!strncmp(u->scheme, "http", sizeof("http")-1) || !strncmp(u->scheme, "hkp", sizeof("hkp")-1)))
	{
	    /*
	     * HTTP has 4 (or 5 if persistent malloc) refs on the fd:
	     *	"persist ctrl"				url.c:177
	     *	"grab ctrl (ufdOpen HTTP)"		rpmio.c:924
	     *	"grab data (ufdOpen HTTP)"		rpmio.c:928
	     *	"open ctrl (httpReq)"			ftp.c:382
	     *	"open data (httpReq)"			ftp.c:435
	     */

	    if (fd == u->ctrl)
		fd = u->ctrl = fdFree(fd, "open data (ufdClose HTTP persist ctrl)");
	    else if (fd == u->data)
		fd = u->data = fdFree(fd, "open data (ufdClose HTTP persist data)");
	    else
		fd = fdFree(fd, "open data (ufdClose HTTP)");

	    /* XXX if not using libio, lose the fp from fpio */
	    {   FILE * fp;
		/*@+voidabstract -nullpass@*/
		fp = fdGetFILE(fd);
		if (noLibio && fp)
		    fdSetFp(fd, NULL);
		/*@=voidabstract =nullpass@*/
	    }

	    /* If content remains, then don't persist. */
	    if (fd->bytesRemain > 0)
		fd->persist = 0;
	    fd->contentLength = fd->bytesRemain = -1;

	    /* If persisting, then Fclose will juggle refcounts. */
	    if (fd->persist && (fd == u->ctrl || fd == u->data))
		return 0;
	}
    }
    return fdClose(fd);
}
/*@=usereleased@*/
/*@=branchstate@*/

/*@-nullstate@*/	/* FIX: u->{ctrl,data}->url undef after XurlLink. */
/*@null@*/ FD_t ftpOpen(const char *url, /*@unused@*/ int flags,
		/*@unused@*/ mode_t mode, /*@out@*/ urlinfo *uret)
	/*@modifies *uret @*/
{
    urlinfo u = NULL;
    FD_t fd = NULL;

#if 0	/* XXX makeTempFile() heartburn */
    assert(!(flags & O_RDWR));
#endif
    if (urlConnect(url, &u) < 0)
	goto exit;

    if (u->data == NULL)
	u->data = fdNew("persist data (ftpOpen)");

    if (u->data->url == NULL)
	fd = fdLink(u->data, "grab data (ftpOpen persist data)");
    else
	fd = fdNew("grab data (ftpOpen)");

    if (fd) {
	fdSetOpen(fd, url, flags, mode);
	fdSetIo(fd, ufdio);
	fd->ftpFileDoneNeeded = 0;
	fd->rd_timeoutsecs = ftpTimeoutSecs;
	fd->contentLength = fd->bytesRemain = -1;
	fd->url = urlLink(u, "url (ufdOpen FTP)");
	fd->urlType = URL_IS_FTP;
    }

exit:
/*@-boundswrite@*/
    if (uret)
	*uret = u;
/*@=boundswrite@*/
    /*@-refcounttrans@*/
    return fd;
    /*@=refcounttrans@*/
}
/*@=nullstate@*/

static /*@null@*/ FD_t ufdOpen(const char * url, int flags, mode_t mode)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = NULL;
    const char * cmd;
    urlinfo u;
    const char * path;
    urltype urlType = urlPath(url, &path);

if (_rpmio_debug)
fprintf(stderr, "*** ufdOpen(%s,0x%x,0%o)\n", url, (unsigned)flags, (unsigned)mode);

    /*@-branchstate@*/
    switch (urlType) {
    case URL_IS_FTP:
	fd = ftpOpen(url, flags, mode, &u);
	if (fd == NULL || u == NULL)
	    break;

	/* XXX W2DO? use STOU rather than STOR to prevent clobbering */
	cmd = ((flags & O_WRONLY)
		?  ((flags & O_APPEND) ? "APPE" :
		   ((flags & O_CREAT) ? "STOR" : "STOR"))
		:  ((flags & O_CREAT) ? "STOR" : "RETR"));
	u->openError = ftpReq(fd, cmd, path);
	if (u->openError < 0) {
	    /* XXX make sure that we can exit through ufdClose */
	    fd = fdLink(fd, "error data (ufdOpen FTP)");
	} else {
	    fd->bytesRemain = ((!strcmp(cmd, "RETR"))
		?  fd->contentLength : -1);
	    fd->wr_chunked = 0;
	}
	break;
    case URL_IS_HTTPS:
    case URL_IS_HTTP:
    case URL_IS_HKP:
	fd = davOpen(url, flags, mode, &u);
	if (fd == NULL || u == NULL)
	    break;

	cmd = ((flags & O_WRONLY)
		?  ((flags & O_APPEND) ? "PUT" :
		   ((flags & O_CREAT) ? "PUT" : "PUT"))
		: "GET");
	u->openError = davReq(fd, cmd, path);
	if (u->openError < 0) {
	    /* XXX make sure that we can exit through ufdClose */
	    fd = fdLink(fd, "error ctrl (ufdOpen HTTP)");
	    fd = fdLink(fd, "error data (ufdOpen HTTP)");
	} else {
	    fd->bytesRemain = ((!strcmp(cmd, "GET"))
		?  fd->contentLength : -1);
	    fd->wr_chunked = ((!strcmp(cmd, "PUT"))
		?  fd->wr_chunked : 0);
	}
	break;
    case URL_IS_DASH:
	assert(!(flags & O_RDWR));
	fd = fdDup( ((flags & O_WRONLY) ? STDOUT_FILENO : STDIN_FILENO) );
	if (fd) {
	    fdSetOpen(fd, url, flags, mode);
	    fdSetIo(fd, ufdio);
	    fd->rd_timeoutsecs = 600;	/* XXX W2DO? 10 mins? */
	    fd->contentLength = fd->bytesRemain = -1;
	}
	break;
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
    default:
	fd = fdOpen(path, flags, mode);
	if (fd) {
	    fdSetIo(fd, ufdio);
	    fd->rd_timeoutsecs = 1;
	    fd->contentLength = fd->bytesRemain = -1;
	}
	break;
    }
    /*@=branchstate@*/

    if (fd == NULL) return NULL;
    fd->urlType = urlType;
    if (Fileno(fd) < 0) {
	(void) ufdClose(fd);
	return NULL;
    }
DBGIO(fd, (stderr, "==>\tufdOpen(\"%s\",%x,0%o) %s\n", url, (unsigned)flags, (unsigned)mode, fdbg(fd)));
    return fd;
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s ufdio_s = {
  ufdRead, ufdWrite, ufdSeek, ufdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  ufdOpen, NULL, fdGetFp, NULL,	Mkdir, Chdir, Rmdir, Rename, Unlink
};
/*@=type@*/
FDIO_t ufdio = /*@-compmempass@*/ &ufdio_s /*@=compmempass@*/ ;

/* =============================================================== */
/* Support for GZIP library.
 */
#ifdef	HAVE_ZLIB_H
/*@-moduncon@*/

/*@-noparams@*/
#include <zlib.h>
/*@=noparams@*/

static inline /*@dependent@*/ /*@null@*/ void * gzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
/*@-boundsread@*/
	FDSTACK_t * fps = &fd->fps[i];
/*@=boundsread@*/
	if (fps->io != gzdio)
	    continue;
	rc = fps->fp;
	break;
    }

    return rc;
}

static /*@null@*/
FD_t gzdOpen(const char * path, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd;
    gzFile gzfile;
    if ((gzfile = gzopen(path, fmode)) == NULL)
	return NULL;
    fd = fdNew("open (gzdOpen)");
    fdPop(fd); fdPush(fd, gzdio, gzfile, -1);

DBGIO(fd, (stderr, "==>\tgzdOpen(\"%s\", \"%s\") fd %p %s\n", path, fmode, (fd ? fd : NULL), fdbg(fd)));
    return fdLink(fd, "gzdOpen");
}

static /*@null@*/ FD_t gzdFdopen(void * cookie, const char *fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    gzFile gzfile;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);		/* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    gzfile = gzdopen(fdno, fmode);
    if (gzfile == NULL) return NULL;

    fdPush(fd, gzdio, gzfile, fdno);		/* Push gzdio onto stack */

    return fdLink(fd, "gzdFdopen");
}

static int gzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    gzFile gzfile;
    gzfile = gzdFileno(fd);
    if (gzfile == NULL) return -2;
    return gzflush(gzfile, Z_SYNC_FLUSH);	/* XXX W2DO? */
}

/* =============================================================== */
static ssize_t gzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    gzFile gzfile;
    ssize_t rc;

    if (fd == NULL || fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    gzfile = gzdFileno(fd);
    if (gzfile == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_READ);
    rc = gzread(gzfile, buf, count);
DBGIO(fd, (stderr, "==>\tgzdRead(%p,%p,%u) rc %lx %s\n", cookie, buf, (unsigned)count, (unsigned long)rc, fdbg(fd)));
    if (rc < 0) {
	int zerror = 0;
	fd->errcookie = gzerror(gzfile, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	if (fd->ndigests && rc > 0) fdUpdateDigests(fd, (void *)buf, rc);
    }
    return rc;
}

static ssize_t gzdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    gzFile gzfile;
    ssize_t rc;

    if (fd == NULL || fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0) fdUpdateDigests(fd, (void *)buf, count);

    gzfile = gzdFileno(fd);
    if (gzfile == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_WRITE);
    rc = gzwrite(gzfile, (void *)buf, count);
DBGIO(fd, (stderr, "==>\tgzdWrite(%p,%p,%u) rc %lx %s\n", cookie, buf, (unsigned)count, (unsigned long)rc, fdbg(fd)));
    if (rc < 0) {
	int zerror = 0;
	fd->errcookie = gzerror(gzfile, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    } else if (rc > 0) {
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    }
    return rc;
}

/* XXX zlib-1.0.4 has not */
static inline int gzdSeek(void * cookie, _libio_pos_t pos, int whence)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
#ifdef USE_COOKIE_SEEK_POINTER
    _IO_off64_t p = *pos;
#else
    off_t p = pos;
#endif
    int rc;
#if HAVE_GZSEEK
    FD_t fd = c2f(cookie);
    gzFile gzfile;

    if (fd == NULL) return -2;
    assert(fd->bytesRemain == -1);	/* XXX FIXME */

    gzfile = gzdFileno(fd);
    if (gzfile == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_SEEK);
    rc = gzseek(gzfile, p, whence);
DBGIO(fd, (stderr, "==>\tgzdSeek(%p,%ld,%d) rc %lx %s\n", cookie, (long)p, whence, (unsigned long)rc, fdbg(fd)));
    if (rc < 0) {
	int zerror = 0;
	fd->errcookie = gzerror(gzfile, &zerror);
	if (zerror == Z_ERRNO) {
	    fd->syserrno = errno;
	    fd->errcookie = strerror(fd->syserrno);
	}
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_SEEK, rc);
    }
#else
    rc = -2;
#endif
    return rc;
}

static int gzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    gzFile gzfile;
    int rc;

    gzfile = gzdFileno(fd);
    if (gzfile == NULL) return -2;	/* XXX can't happen */

    fdstat_enter(fd, FDSTAT_CLOSE);
    /*@-dependenttrans@*/
    rc = gzclose(gzfile);
    /*@=dependenttrans@*/

    /* XXX TODO: preserve fd if errors */

    if (fd) {
DBGIO(fd, (stderr, "==>\tgzdClose(%p) zerror %d %s\n", cookie, rc, fdbg(fd)));
	if (rc < 0) {
	    fd->errcookie = "gzclose error";
	    if (rc == Z_ERRNO) {
		fd->syserrno = errno;
		fd->errcookie = strerror(fd->syserrno);
	    }
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

DBGIO(fd, (stderr, "==>\tgzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "GZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (gzdClose)");
    /*@=branchstate@*/
    return rc;
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s gzdio_s = {
  gzdRead, gzdWrite, gzdSeek, gzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, gzdOpen, gzdFileno, gzdFlush,	NULL, NULL, NULL, NULL, NULL
};
/*@=type@*/
FDIO_t gzdio = /*@-compmempass@*/ &gzdio_s /*@=compmempass@*/ ;

/*@=moduncon@*/
#endif	/* HAVE_ZLIB_H */

/* =============================================================== */
/* Support for BZIP2 library.
 */
#if HAVE_BZLIB_H
/*@-moduncon@*/

#include <bzlib.h>

static inline /*@dependent@*/ void * bzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
/*@-boundsread@*/
	FDSTACK_t * fps = &fd->fps[i];
/*@=boundsread@*/
	if (fps->io != bzdio)
	    continue;
	rc = fps->fp;
	break;
    }

    return rc;
}

/*@-globuse@*/
static /*@null@*/ FD_t bzdOpen(const char * path, const char * mode)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FD_t fd;
    BZFILE *bzfile;;
    if ((bzfile = BZ2_bzopen(path, mode)) == NULL)
	return NULL;
    fd = fdNew("open (bzdOpen)");
    fdPop(fd); fdPush(fd, bzdio, bzfile, -1);
    return fdLink(fd, "bzdOpen");
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t bzdFdopen(void * cookie, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;
    BZFILE *bzfile;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);		/* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    bzfile = BZ2_bzdopen(fdno, fmode);
    if (bzfile == NULL) return NULL;

    fdPush(fd, bzdio, bzfile, fdno);		/* Push bzdio onto stack */

    return fdLink(fd, "bzdFdopen");
}
/*@=globuse@*/

/*@-globuse@*/
static int bzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    return BZ2_bzflush(bzdFileno(fd));
}
/*@=globuse@*/

/* =============================================================== */
/*@-globuse@*/
/*@-mustmod@*/		/* LCL: *buf is modified */
static ssize_t bzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies *buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    ssize_t rc = 0;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */
    bzfile = bzdFileno(fd);
    fdstat_enter(fd, FDSTAT_READ);
    if (bzfile)
	/*@-compdef@*/
	rc = BZ2_bzread(bzfile, buf, count);
	/*@=compdef@*/
    if (rc == -1) {
	int zerror = 0;
	if (bzfile)
	    fd->errcookie = BZ2_bzerror(bzfile, &zerror);
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	/*@-compdef@*/
	if (fd->ndigests && rc > 0) fdUpdateDigests(fd, (void *)buf, rc);
	/*@=compdef@*/
    }
    return rc;
}
/*@=mustmod@*/
/*@=globuse@*/

/*@-globuse@*/
static ssize_t bzdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0) fdUpdateDigests(fd, (void *)buf, count);

    bzfile = bzdFileno(fd);
    fdstat_enter(fd, FDSTAT_WRITE);
    rc = BZ2_bzwrite(bzfile, (void *)buf, count);
    if (rc == -1) {
	int zerror = 0;
	fd->errcookie = BZ2_bzerror(bzfile, &zerror);
    } else if (rc > 0) {
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    }
    return rc;
}
/*@=globuse@*/

static inline int bzdSeek(void * cookie, /*@unused@*/ _libio_pos_t pos,
			/*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);

    BZDONLY(fd);
    return -2;
}

static int bzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    BZFILE *bzfile;
    int rc;

    bzfile = bzdFileno(fd);

    if (bzfile == NULL) return -2;
    fdstat_enter(fd, FDSTAT_CLOSE);
    /*@-noeffectuncon@*/ /* FIX: check rc */
    BZ2_bzclose(bzfile);
    /*@=noeffectuncon@*/
    rc = 0;	/* XXX FIXME */

    /* XXX TODO: preserve fd if errors */

    if (fd) {
	if (rc == -1) {
	    int zerror = 0;
	    fd->errcookie = BZ2_bzerror(bzfile, &zerror);
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

DBGIO(fd, (stderr, "==>\tbzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "BZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (bzdClose)");
    /*@=branchstate@*/
    return rc;
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s bzdio_s = {
  bzdRead, bzdWrite, bzdSeek, bzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, bzdOpen, bzdFileno, bzdFlush,	NULL, NULL, NULL, NULL, NULL
};
/*@=type@*/
FDIO_t bzdio = /*@-compmempass@*/ &bzdio_s /*@=compmempass@*/ ;

/*@=moduncon@*/
#endif	/* HAVE_BZLIB_H */

/* =============================================================== */
/* Support for LZMA library.
 */
#include "LzmaDecode.h"

#define kInBufferSize (1 << 15)
typedef struct _CBuffer {
  ILzmaInCallback InCallback;
/*@dependent@*/
  FILE *File;
  unsigned char Buffer[kInBufferSize];
} CBuffer;

typedef struct lzfile {
    CBuffer g_InBuffer;
    CLzmaDecoderState state;  /* it's about 24-80 bytes structure, if int is 32-bit */
    unsigned char properties[LZMA_PROPERTIES_SIZE];

#if 0
    FILE *file;
#endif
    int pid;
} LZFILE;

static size_t MyReadFile(FILE *file, void *data, size_t size)
	/*@globals fileSystem @*/
	/*@modifies *file, *data, fileSystem @*/
{ 
    if (size == 0) return 0;
    return fread(data, 1, size, file); 
}

static int MyReadFileAndCheck(FILE *file, void *data, size_t size)
	/*@globals fileSystem @*/
	/*@modifies *file, *data, fileSystem @*/
{
    return (MyReadFile(file, data, size) == size);
}

static int LzmaReadCompressed(void *object, const unsigned char **buffer, SizeT *size)
	/*@globals fileSystem @*/
	/*@modifies *buffer, *size, fileSystem @*/
{
    CBuffer *b = object;
    *buffer = b->Buffer;
    *size = MyReadFile(b->File, b->Buffer, kInBufferSize);
    return LZMA_RESULT_OK;
}

static inline /*@dependent@*/ void * lzdFileno(FD_t fd)
	/*@*/
{
    void * rc = NULL;
    int i;

    FDSANE(fd);
    for (i = fd->nfps; i >= 0; i--) {
/*@-boundsread@*/
	    FDSTACK_t * fps = &fd->fps[i];
/*@=boundsread@*/
	    if (fps->io != lzdio)
	        continue;
	    rc = fps->fp;
    	break;
    }
    
    return rc;
}

static FD_t lzdWriteOpen(int fdno, int fopen)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    int pid;
    int p[2];
    int xx;
    const char *lzma;

    if (fdno < 0) return NULL;
    if (pipe(p) < 0) {
        xx = close(fdno);
        return NULL;
    }
    pid = fork();
    if (pid < 0) {
        xx = close(fdno);
        return NULL;
    }
    if (pid) {
        FD_t fd;
        LZFILE * lzfile = xcalloc(1, sizeof(*lzfile));

        xx = close(fdno);
        xx = close(p[0]);
        lzfile->pid = pid;
        lzfile->g_InBuffer.File = fdopen(p[1], "wb");
        if (lzfile->g_InBuffer.File == NULL) {
            xx = close(p[1]);
            lzfile = _free(lzfile);
            return NULL;
        }
        fd = fdNew("open (lzdOpen write)");
        if (fopen) fdPop(fd);
        fdPush(fd, lzdio, lzfile, -1);
        return fdLink(fd, "lzdOpen");
    } else {
        int i;
        /* lzma */
        xx = close(p[1]);
        xx = dup2(p[0], 0);
        xx = dup2(fdno, 1);
        for (i = 3; i < 1024; i++)
	    xx = close(i);
        lzma = rpmGetPath("%{?__lzma}%{!?__lzma:/usr/bin/lzma}", NULL);
        if (execl(lzma, "lzma", "e", "-si", "-so", NULL))
            _exit(1);
        lzma = _free(lzma);
    }
    return NULL; /* warning */
}

static FD_t lzdReadOpen(int fdno, int fopen)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    LZFILE *lzfile;
    unsigned char ff[8];
    FD_t fd;
    size_t nb;

    if (fdno < 0) return NULL;
    lzfile = xcalloc(1, sizeof(*lzfile));
    if (lzfile == NULL) return NULL;
    lzfile->g_InBuffer.File = fdopen(fdno, "rb");
    if (lzfile->g_InBuffer.File == NULL) goto error2;

    if (!MyReadFileAndCheck(lzfile->g_InBuffer.File, lzfile->properties, sizeof(lzfile->properties)))
            goto error;

    memset(ff, 0, sizeof(ff));
    if (!MyReadFileAndCheck(lzfile->g_InBuffer.File, ff, 8)) goto error;
    if (LzmaDecodeProperties(&lzfile->state.Properties, lzfile->properties, LZMA_PROPERTIES_SIZE) != LZMA_RESULT_OK)
        goto error;
    nb = LzmaGetNumProbs(&lzfile->state.Properties) * sizeof(*lzfile->state.Probs);
    lzfile->state.Probs = xmalloc(nb);
    if (lzfile->state.Probs == NULL) goto error;

    if (lzfile->state.Properties.DictionarySize == 0)
        lzfile->state.Dictionary = 0;
    else {
        lzfile->state.Dictionary = xmalloc(lzfile->state.Properties.DictionarySize);
        if (lzfile->state.Dictionary == NULL) {
            lzfile->state.Probs = _free(lzfile->state.Probs);
            goto error;
        }
    }
    lzfile->g_InBuffer.InCallback.Read = LzmaReadCompressed;
    LzmaDecoderInit(&lzfile->state);

    fd = fdNew("open (lzdOpen read)");
    if (fopen) fdPop(fd);
    fdPush(fd, lzdio, lzfile, -1);
    return fdLink(fd, "lzdOpen");

error:
    (void) fclose(lzfile->g_InBuffer.File);
error2:
    lzfile = _free(lzfile);
    return NULL;
}

/*@-globuse@*/
static /*@null@*/ FD_t lzdOpen(const char * path, const char * mode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    if (mode == NULL)
        return NULL;
    if (mode[0] == 'w') {
        int fdno = open(path, O_WRONLY);

        if (fdno < 0) return NULL;
        return lzdWriteOpen(fdno, 1);
    } else {
        int fdno = open(path, O_RDONLY);

        if (fdno < 0) return NULL;
        return lzdReadOpen(fdno, 1);
    }
}
/*@=globuse@*/

/*@-globuse@*/
static /*@null@*/ FD_t lzdFdopen(void * cookie, const char * fmode)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    int fdno;

    if (fmode == NULL) return NULL;
    fdno = fdFileno(fd);
    fdSetFdno(fd, -1);		/* XXX skip the fdio close */
    if (fdno < 0) return NULL;
    if (fmode[0] == 'w') {
        return lzdWriteOpen(fdno, 0);
    } else {
        return lzdReadOpen(fdno, 0);
    }
}
/*@=globuse@*/

/*@-globuse@*/
static int lzdFlush(FD_t fd)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    LZFILE *lzfile = lzdFileno(fd);

    if (lzfile == NULL || lzfile->g_InBuffer.File == NULL) return -2;
    return fflush(lzfile->g_InBuffer.File);
}
/*@=globuse@*/

/* =============================================================== */
/*@-globuse@*/
/*@-mustmod@*/		/* LCL: *buf is modified */
static ssize_t lzdRead(void * cookie, /*@out@*/ char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies buf, fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE *lzfile;
    ssize_t rc = 0;
    int res = 0;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */
    lzfile = lzdFileno(fd);
    fdstat_enter(fd, FDSTAT_READ);
    if (lzfile->g_InBuffer.File)
/*@-compdef@*/
	res = LzmaDecode(&lzfile->state, &lzfile->g_InBuffer.InCallback, buf, count, &rc);
/*@=compdef@*/
    if (res) {
	if (lzfile)
	    fd->errcookie = "Lzma: decoding error";
    } else if (rc >= 0) {
	fdstat_exit(fd, FDSTAT_READ, rc);
	/*@-compdef@*/
	if (fd->ndigests && rc > 0) fdUpdateDigests(fd, (void *)buf, rc);
	/*@=compdef@*/
    }
    return rc;
}
/*@=mustmod@*/
/*@=globuse@*/

/*@-globuse@*/
static ssize_t lzdWrite(void * cookie, const char * buf, size_t count)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE *lzfile;
    ssize_t rc;

    if (fd->bytesRemain == 0) return 0;	/* XXX simulate EOF */

    if (fd->ndigests && count > 0) fdUpdateDigests(fd, (void *)buf, count);

    lzfile = lzdFileno(fd);
    fdstat_enter(fd, FDSTAT_WRITE);
    rc = fwrite((void *)buf, 1, count, lzfile->g_InBuffer.File);
    if (rc == -1) {
	fd->errcookie = strerror(ferror(lzfile->g_InBuffer.File));
    } else if (rc > 0) {
	fdstat_exit(fd, FDSTAT_WRITE, rc);
    }
    return rc;
}
/*@=globuse@*/

static inline int lzdSeek(void * cookie, /*@unused@*/ _libio_pos_t pos,
			/*@unused@*/ int whence)
	/*@*/
{
    FD_t fd = c2f(cookie);

    LZDONLY(fd);
    return -2;
}

static int lzdClose( /*@only@*/ void * cookie)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    FD_t fd = c2f(cookie);
    LZFILE * lzfile = lzdFileno(fd);
    int rc;

    if (lzfile == NULL) return -2;
    fdstat_enter(fd, FDSTAT_CLOSE);
/*@-noeffectuncon@*/ /* FIX: check rc */
    rc = fclose(lzfile->g_InBuffer.File);
    if (lzfile->pid)
	rc = wait4(lzfile->pid, NULL, 0, NULL);
    else { /* reading */
        lzfile->state.Probs = _free(lzfile->state.Probs);
        lzfile->state.Dictionary = _free(lzfile->state.Dictionary);
    }
/*@-dependenttrans@*/
    lzfile = _free(lzfile);
/*@=dependenttrans@*/
/*@=noeffectuncon@*/
    rc = 0;	/* XXX FIXME */

    /* XXX TODO: preserve fd if errors */

    if (fd) {
	if (rc == -1) {
	    fd->errcookie = strerror(ferror(lzfile->g_InBuffer.File));
	} else if (rc >= 0) {
	    fdstat_exit(fd, FDSTAT_CLOSE, rc);
	}
    }

DBGIO(fd, (stderr, "==>\tlzdClose(%p) rc %lx %s\n", cookie, (unsigned long)rc, fdbg(fd)));

    if (_rpmio_debug || rpmIsDebug()) fdstat_print(fd, "LZDIO", stderr);
    /*@-branchstate@*/
    if (rc == 0)
	fd = fdFree(fd, "open (lzdClose)");
    /*@=branchstate@*/
    return rc;
}

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s lzdio_s = {
  lzdRead, lzdWrite, lzdSeek, lzdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  NULL, lzdOpen, lzdFileno, lzdFlush,	NULL, NULL, NULL, NULL, NULL
};
/*@=type@*/
FDIO_t lzdio = /*@-compmempass@*/ &lzdio_s /*@=compmempass@*/ ;

/* =============================================================== */
/*@observer@*/
static const char * getFdErrstr (FD_t fd)
	/*@*/
{
    const char *errstr = NULL;

#ifdef	HAVE_ZLIB_H
    if (fdGetIo(fd) == gzdio) {
	errstr = fd->errcookie;
    } else
#endif	/* HAVE_ZLIB_H */

#ifdef	HAVE_BZLIB_H
    if (fdGetIo(fd) == bzdio) {
	errstr = fd->errcookie;
    } else
#endif	/* HAVE_BZLIB_H */
    if (fdGetIo(fd) == lzdio) {
    errstr = fd->errcookie;
    } else 
    {
	errstr = (fd->syserrno ? strerror(fd->syserrno) : "");
    }

    return errstr;
}

/* =============================================================== */

const char *Fstrerror(FD_t fd)
{
    if (fd == NULL)
	return (errno ? strerror(errno) : "");
    FDSANE(fd);
    return getFdErrstr(fd);
}

#define	FDIOVEC(_fd, _vec)	\
  ((fdGetIo(_fd) && fdGetIo(_fd)->_vec) ? fdGetIo(_fd)->_vec : NULL)

size_t Fread(void *buf, size_t size, size_t nmemb, FD_t fd) {
    fdio_read_function_t _read;
    int rc;

    FDSANE(fd);
DBGIO(fd, (stderr, "==> Fread(%p,%u,%u,%p) %s\n", buf, (unsigned)size, (unsigned)nmemb, (fd ? fd : NULL), fdbg(fd)));

    if (fdGetIo(fd) == fpio) {
	/*@+voidabstract -nullpass@*/
	rc = fread(buf, size, nmemb, fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/
	return rc;
    }

    /*@-nullderef@*/
    _read = FDIOVEC(fd, read);
    /*@=nullderef@*/

    rc = (_read ? (*_read) (fd, buf, size * nmemb) : -2);
    return rc;
}

size_t Fwrite(const void *buf, size_t size, size_t nmemb, FD_t fd)
{
    fdio_write_function_t _write;
    int rc;

    FDSANE(fd);
DBGIO(fd, (stderr, "==> Fwrite(%p,%u,%u,%p) %s\n", buf, (unsigned)size, (unsigned)nmemb, (fd ? fd : NULL), fdbg(fd)));

    if (fdGetIo(fd) == fpio) {
/*@-boundsread@*/
	/*@+voidabstract -nullpass@*/
	rc = fwrite(buf, size, nmemb, fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/
/*@=boundsread@*/
	return rc;
    }

    /*@-nullderef@*/
    _write = FDIOVEC(fd, write);
    /*@=nullderef@*/

    rc = (_write ? _write(fd, buf, size * nmemb) : -2);
    return rc;
}

int Fseek(FD_t fd, _libio_off_t offset, int whence) {
    fdio_seek_function_t _seek;
#ifdef USE_COOKIE_SEEK_POINTER
    _IO_off64_t o64 = offset;
    _libio_pos_t pos = &o64;
#else
    _libio_pos_t pos = offset;
#endif

    long int rc;

    FDSANE(fd);
DBGIO(fd, (stderr, "==> Fseek(%p,%ld,%d) %s\n", fd, (long)offset, whence, fdbg(fd)));

    if (fdGetIo(fd) == fpio) {
	FILE *fp;

	/*@+voidabstract -nullpass@*/
	fp = fdGetFILE(fd);
	rc = fseek(fp, offset, whence);
	/*@=voidabstract =nullpass@*/
	return rc;
    }

    /*@-nullderef@*/
    _seek = FDIOVEC(fd, seek);
    /*@=nullderef@*/

    rc = (_seek ? _seek(fd, pos, whence) : -2);
    return rc;
}

int Fclose(FD_t fd)
{
    int rc = 0, ec = 0;

    FDSANE(fd);
DBGIO(fd, (stderr, "==> Fclose(%p) %s\n", (fd ? fd : NULL), fdbg(fd)));

    fd = fdLink(fd, "Fclose");
    /*@-branchstate@*/
    while (fd->nfps >= 0) {
/*@-boundsread@*/
	FDSTACK_t * fps = &fd->fps[fd->nfps];
/*@=boundsread@*/
	
	if (fps->io == fpio) {
	    FILE *fp;
	    int fpno;

	    /*@+voidabstract -nullpass@*/
	    fp = fdGetFILE(fd);
	    fpno = fileno(fp);
	    /*@=voidabstract =nullpass@*/
	/* XXX persistent HTTP/1.1 returns the previously opened fp */
	    if (fd->nfps > 0 && fpno == -1 &&
		fd->fps[fd->nfps-1].io == ufdio &&
		fd->fps[fd->nfps-1].fp == fp &&
		(fd->fps[fd->nfps-1].fdno >= 0 || fd->req != NULL))
	    {
		int hadreqpersist = (fd->req != NULL);

		if (fp)
		    rc = fflush(fp);
		fd->nfps--;
		/*@-refcounttrans@*/
		rc = ufdClose(fd);
		/*@=refcounttrans@*/
/*@-usereleased@*/
		if (fdGetFdno(fd) >= 0)
		    break;
		if (!fd->persist)
		    hadreqpersist = 0;
		fdSetFp(fd, NULL);
		fd->nfps++;
		if (fp) {
		    /* HACK: flimsy Keepalive wiring. */
		    if (hadreqpersist) {
			fd->nfps--;
/*@-exposetrans@*/
			fdSetFp(fd, fp);
/*@=exposetrans@*/
/*@-refcounttrans@*/
			(void) fdClose(fd);
/*@=refcounttrans@*/
			fdSetFp(fd, NULL);
			fd->nfps++;
/*@-refcounttrans@*/
			(void) fdClose(fd);
/*@=refcounttrans@*/
		    } else
			rc = fclose(fp);
		}
		fdPop(fd);
		if (noLibio)
		    fdSetFp(fd, NULL);
	    } else {
		if (fp)
		    rc = fclose(fp);
		if (fpno == -1) {
		    fd = fdFree(fd, "fopencookie (Fclose)");
		    fdPop(fd);
		}
	    }
	} else {
	    /*@-nullderef@*/
	    fdio_close_function_t _close = FDIOVEC(fd, close);
	    /*@=nullderef@*/
	    rc = _close(fd);
	}
	if (fd->nfps == 0)
	    break;
	if (ec == 0 && rc)
	    ec = rc;
	fdPop(fd);
    }
    /*@=branchstate@*/
    fd = fdFree(fd, "Fclose");
    return ec;
/*@=usereleased@*/
}

/**
 * Convert stdio fmode to open(2) mode, filtering out zlib/bzlib flags.
 *	returns stdio[0] = NUL on error.
 *
 * @todo glibc also supports ",ccs="
 *
 * - glibc:	m use mmap'd input
 * - glibc:	c no cancel
 * - gzopen:	[0-9] is compession level
 * - gzopen:	'f' is filtered (Z_FILTERED)
 * - gzopen:	'h' is Huffman encoding (Z_HUFFMAN_ONLY)
 * - bzopen:	[1-9] is block size (modulo 100K)
 * - bzopen:	's' is smallmode
 * - HACK:	'.' terminates, rest is type of I/O
 */
/*@-boundswrite@*/
static inline void cvtfmode (const char *m,
				/*@out@*/ char *stdio, size_t nstdio,
				/*@out@*/ char *other, size_t nother,
				/*@out@*/ const char **end, /*@out@*/ int * f)
	/*@modifies *stdio, *other, *end, *f @*/
{
    int flags = 0;
    char c;

    switch (*m) {
    case 'a':
	flags |= O_WRONLY | O_CREAT | O_APPEND;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    case 'w':
	flags |= O_WRONLY | O_CREAT | O_TRUNC;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    case 'r':
	flags |= O_RDONLY;
	if (--nstdio > 0) *stdio++ = *m;
	break;
    default:
	*stdio = '\0';
	return;
	/*@notreached@*/ break;
    }
    m++;

    while ((c = *m++) != '\0') {
	switch (c) {
	case '.':
	    /*@switchbreak@*/ break;
	case '+':
	    flags &= ~(O_RDONLY|O_WRONLY);
	    flags |= O_RDWR;
	    if (--nstdio > 0) *stdio++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case 'x':	/* glibc: open file exclusively. */
	    flags |= O_EXCL;
	    /*@fallthrough@*/
	case 'm':	/* glibc: mmap'd reads */
	case 'c':	/* glibc: no cancel */
#if defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ >= 3
	    if (--nstdio > 0) *stdio++ = c;
#endif
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case 'b':
	    if (--nstdio > 0) *stdio++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:
	    if (--nother > 0) *other++ = c;
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	}
	break;
    }

    *stdio = *other = '\0';
    if (end != NULL)
	*end = (*m != '\0' ? m : NULL);
    if (f != NULL)
	*f = flags;
}
/*@=boundswrite@*/

#if _USE_LIBIO
#if defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ == 0
/* XXX retrofit glibc-2.1.x typedef on glibc-2.0.x systems */
typedef _IO_cookie_io_functions_t cookie_io_functions_t;
#endif
#endif

/*@-boundswrite@*/
FD_t Fdopen(FD_t ofd, const char *fmode)
{
    char stdio[20], other[20], zstdio[20];
    const char *end = NULL;
    FDIO_t iof = NULL;
    FD_t fd = ofd;

if (_rpmio_debug)
fprintf(stderr, "*** Fdopen(%p,%s) %s\n", fd, fmode, fdbg(fd));
    FDSANE(fd);

    if (fmode == NULL)
	return NULL;

    cvtfmode(fmode, stdio, sizeof(stdio), other, sizeof(other), &end, NULL);
    if (stdio[0] == '\0')
	return NULL;
    zstdio[0] = '\0';
    strncat(zstdio, stdio, sizeof(zstdio) - strlen(zstdio));
    strncat(zstdio, other, sizeof(zstdio) - strlen(zstdio));

    if (end == NULL && other[0] == '\0')
	/*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/

    /*@-branchstate@*/
    if (end && *end) {
	if (!strcmp(end, "fdio")) {
	    iof = fdio;
#if HAVE_ZLIB_H
	} else if (!strcmp(end, "gzdio")) {
	    iof = gzdio;
	    /*@-internalglobs@*/
	    fd = gzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
#endif
#if HAVE_BZLIB_H
	} else if (!strcmp(end, "bzdio")) {
	    iof = bzdio;
	    /*@-internalglobs@*/
	    fd = bzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
#endif
    } else if (!strcmp(end, "lzdio")) {
        iof = lzdio;
        fd = lzdFdopen(fd, zstdio);
	} else if (!strcmp(end, "ufdio")) {
	    iof = ufdio;
	} else if (!strcmp(end, "fpio")) {
	    iof = fpio;
	    if (noLibio) {
		int fdno = Fileno(fd);
		FILE * fp = fdopen(fdno, stdio);
/*@+voidabstract -nullpass@*/
if (_rpmio_debug)
fprintf(stderr, "*** Fdopen fpio fp %p\n", (void *)fp);
/*@=voidabstract =nullpass@*/
		if (fp == NULL)
		    return NULL;
		/* XXX gzdio/bzdio use fp for private data */
		/*@+voidabstract@*/
		if (fdGetFp(fd) == NULL)
		    fdSetFp(fd, fp);
		fdPush(fd, fpio, fp, fdno);	/* Push fpio onto stack */
		/*@=voidabstract@*/
	    }
	}
    } else if (other[0] != '\0') {
	for (end = other; *end && strchr("0123456789fh", *end); end++)
	    {};
	if (*end == '\0') {
#if HAVE_ZLIB_H
	    iof = gzdio;
	    /*@-internalglobs@*/
	    fd = gzdFdopen(fd, zstdio);
	    /*@=internalglobs@*/
#endif
	}
    }
    /*@=branchstate@*/
    if (iof == NULL)
	/*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/

    if (!noLibio) {
	FILE * fp = NULL;

#if _USE_LIBIO
	{   cookie_io_functions_t ciof;
	    ciof.read = iof->read;
	    ciof.write = iof->write;
	    ciof.seek = iof->seek;
	    ciof.close = iof->close;
	    fp = fopencookie(fd, stdio, ciof);
DBGIO(fd, (stderr, "==> fopencookie(%p,\"%s\",*%p) returns fp %p\n", fd, stdio, iof, fp));
	}
#endif

	/*@-branchstate@*/
	if (fp) {
	    /* XXX gzdio/bzdio use fp for private data */
	    /*@+voidabstract -nullpass@*/
	    if (fdGetFp(fd) == NULL)
		fdSetFp(fd, fp);
	    fdPush(fd, fpio, fp, fileno(fp));	/* Push fpio onto stack */
	    /*@=voidabstract =nullpass@*/
	    fd = fdLink(fd, "fopencookie");
	}
	/*@=branchstate@*/
    }

DBGIO(fd, (stderr, "==> Fdopen(%p,\"%s\") returns fd %p %s\n", ofd, fmode, (fd ? fd : NULL), fdbg(fd)));
    /*@-refcounttrans -retalias@*/ return fd; /*@=refcounttrans =retalias@*/
}
/*@=boundswrite@*/

FD_t Fopen(const char *path, const char *fmode)
{
    char stdio[20], other[20];
    const char *end = NULL;
    mode_t perms = 0666;
    int flags = 0;
    FD_t fd;

    if (path == NULL || fmode == NULL)
	return NULL;

    stdio[0] = '\0';
    cvtfmode(fmode, stdio, sizeof(stdio), other, sizeof(other), &end, &flags);
    if (stdio[0] == '\0')
	return NULL;

    /*@-branchstate@*/
    if (end == NULL || !strcmp(end, "fdio")) {
if (_rpmio_debug)
fprintf(stderr, "*** Fopen fdio path %s fmode %s\n", path, fmode);
	fd = fdOpen(path, flags, perms);
	if (fdFileno(fd) < 0) {
	    if (fd) (void) fdClose(fd);
	    return NULL;
	}
    } else {
	FILE *fp;
	int fdno;
	int isHTTP = 0;

	/* XXX gzdio and bzdio here too */

	switch (urlIsURL(path)) {
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_HKP:
	    isHTTP = 1;
	    /*@fallthrough@*/
	case URL_IS_PATH:
	case URL_IS_DASH:
	case URL_IS_FTP:
	case URL_IS_UNKNOWN:
if (_rpmio_debug)
fprintf(stderr, "*** Fopen ufdio path %s fmode %s\n", path, fmode);
	    fd = ufdOpen(path, flags, perms);
	    if (fd == NULL || !(fdFileno(fd) >= 0 || fd->req != NULL))
		return fd;
	    break;
	default:
if (_rpmio_debug)
fprintf(stderr, "*** Fopen WTFO path %s fmode %s\n", path, fmode);
	    return NULL;
	    /*@notreached@*/ break;
	}

	/* XXX persistent HTTP/1.1 returns the previously opened fp */
	if (isHTTP && ((fp = fdGetFp(fd)) != NULL) && ((fdno = fdGetFdno(fd)) >= 0 || fd->req != NULL))
	{
	    /*@+voidabstract@*/
	    fdPush(fd, fpio, fp, fileno(fp));	/* Push fpio onto stack */
	    /*@=voidabstract@*/
	    return fd;
	}
    }
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (fd)
	fd = Fdopen(fd, fmode);
    /*@=branchstate@*/
    return fd;
}

int Fflush(FD_t fd)
{
    void * vh;
    if (fd == NULL) return -1;
    if (fdGetIo(fd) == fpio)
	/*@+voidabstract -nullpass@*/
	return fflush(fdGetFILE(fd));
	/*@=voidabstract =nullpass@*/

    vh = fdGetFp(fd);
#if HAVE_ZLIB_H
    if (vh && fdGetIo(fd) == gzdio)
	return gzdFlush(vh);
#endif
#if HAVE_BZLIB_H
    if (vh && fdGetIo(fd) == bzdio)
	return bzdFlush(vh);
#endif

    return 0;
}

int Ferror(FD_t fd)
{
    int i, rc = 0;

    if (fd == NULL) return -1;
    if (fd->req != NULL) {
	/* HACK: flimsy wiring for neon errors. */
	rc = (fd->syserrno  || fd->errcookie != NULL) ? -1 : 0;
    } else
    for (i = fd->nfps; rc == 0 && i >= 0; i--) {
/*@-boundsread@*/
	FDSTACK_t * fps = &fd->fps[i];
/*@=boundsread@*/
	int ec;
	
	if (fps->io == fpio) {
	    /*@+voidabstract -nullpass@*/
	    ec = ferror(fdGetFILE(fd));
	    /*@=voidabstract =nullpass@*/
#if HAVE_ZLIB_H
	} else if (fps->io == gzdio) {
	    ec = (fd->syserrno || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under gzdio always has fdno == -1 */
#endif
#if HAVE_BZLIB_H
	} else if (fps->io == bzdio) {
	    ec = (fd->syserrno  || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under bzdio always has fdno == -1 */
#endif
    } else if (fps->io == lzdio) {
	    ec = (fd->syserrno  || fd->errcookie != NULL) ? -1 : 0;
	    i--;	/* XXX fdio under lzdio always has fdno == -1 */
	} else {
	/* XXX need to check ufdio/gzdio/bzdio/fdio errors correctly. */
	    ec = (fdFileno(fd) < 0 ? -1 : 0);
	}

	if (rc == 0 && ec)
	    rc = ec;
    }
DBGIO(fd, (stderr, "==> Ferror(%p) rc %d %s\n", fd, rc, fdbg(fd)));
    return rc;
}

int Fileno(FD_t fd)
{
    int i, rc = -1;

    if (fd == NULL)
        return -1;
    if (fd->req != NULL)
	rc = 123456789;	/* HACK: https has no steenkin fileno. */
    else
    for (i = fd->nfps ; rc == -1 && i >= 0; i--) {
/*@-boundsread@*/
	rc = fd->fps[i].fdno;
/*@=boundsread@*/
    }

DBGIO(fd, (stderr, "==> Fileno(%p) rc %d %s\n", (fd ? fd : NULL), rc, fdbg(fd)));
    return rc;
}

/* XXX this is naive */
int Fcntl(FD_t fd, int op, void *lip)
{
    return fcntl(Fileno(fd), op, lip);
}

/* =============================================================== */
/* Helper routines that may be generally useful.
 */
/*@-bounds@*/
int rpmioMkpath(const char * path, mode_t mode, uid_t uid, gid_t gid)
{
    char * d, * de;
    int created = 0;
    int rc;

    if (path == NULL)
	return -1;
    d = alloca(strlen(path)+2);
    de = stpcpy(d, path);
    de[1] = '\0';
    for (de = d; *de != '\0'; de++) {
	struct stat st;
	char savec;

	while (*de && *de != '/') de++;
	savec = de[1];
	de[1] = '\0';

	rc = Stat(d, &st);
	if (rc) {
	    switch(errno) {
	    default:
		return errno;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    case ENOENT:
		/*@switchbreak@*/ break;
	    }
	    rc = Mkdir(d, mode);
	    if (rc)
		return errno;
	    created = 1;
	    if (!(uid == (uid_t) -1 && gid == (gid_t) -1)) {
		rc = chown(d, uid, gid);
		if (rc)
		    return errno;
	    }
	} else if (!S_ISDIR(st.st_mode)) {
	    return ENOTDIR;
	}
	de[1] = savec;
    }
    rc = 0;
    if (created)
	rpmMessage(RPMMESS_DEBUG, "created directory(s) %s mode 0%o\n",
			path, mode);
    return rc;
}
/*@=bounds@*/


#define	_PATH	"/bin:/usr/bin:/sbin:/usr/sbin"
/*@unchecked@*/ /*@observer@*/
static const char *_path = _PATH;

#define alloca_strdup(_s)       strcpy(alloca(strlen(_s)+1), (_s))

int rpmioAccess(const char * FN, const char * path, int mode)
{
    char fn[4096];
    char * bn;
    char * r, * re;
    char * t, * te;
    int negate = 0;
    int rc = 0;

    /* Empty paths are always accessible. */
    if (FN == NULL || *FN == '\0')
	return 0;

    if (mode == 0)
	mode = X_OK;

    /* Strip filename out of its name space wrapper. */
    bn = alloca_strdup(FN);
    for (t = bn; t && *t; t++) {
	if (*t != '(')
	    continue;
	*t++ = '\0';

	/* Permit negation on name space tests. */
	if (*bn == '!') {
	    negate = 1;
	    bn++;
	}

	/* Set access flags from name space marker. */
	if (strlen(bn) == 3
	 && strchr("Rr_", bn[0]) != NULL
	 && strchr("Ww_", bn[1]) != NULL
	 && strchr("Xx_", bn[2]) != NULL) {
	    mode = 0;
	    if (strchr("Rr", bn[0]) != NULL)
		mode |= R_OK;
	    if (strchr("Ww", bn[1]) != NULL)
		mode |= W_OK;
	    if (strchr("Xx", bn[2]) != NULL)
		mode |= X_OK;
	    if (mode == 0)
		mode = F_OK;
	} else if (!strcmp(bn, "exists"))
	    mode = F_OK;
	else if (!strcmp(bn, "executable"))
	    mode = X_OK;
	else if (!strcmp(bn, "readable"))
	    mode = R_OK;
	else if (!strcmp(bn, "writable"))
	    mode = W_OK;

	bn = t;
	te = bn + strlen(t) - 1;
	if (*te != ')')		/* XXX syntax error, never exists */
	    return 1;
	*te = '\0';
	break;
    }

    /* Empty paths are always accessible. */
    if (*bn == '\0')
	goto exit;

    /* Check absolute path for access. */
    if (*bn == '/') {
	rc = (Access(bn, mode) != 0 ? 1 : 0);
if (_rpmio_debug)
fprintf(stderr, "*** rpmioAccess(\"%s\", 0x%x) rc %d\n", bn, mode, rc);
	goto exit;
    }

    /* Find path to search. */
    if (path == NULL)
	path = getenv("PATH");
    if (path == NULL)
	path = _path;
    if (path == NULL) {
	rc = 1;
	goto exit;
    }

    /* Look for relative basename on PATH. */
/*@-branchstate@*/
    for (r = alloca_strdup(path); r != NULL && *r != '\0'; r = re) {

	/* Find next element, terminate current element. */
	for (re = r; (re = strchr(re, ':')) != NULL; re++) {
	    if (!(re[1] == '/' && re[2] == '/'))
		/*@innerbreak@*/ break;
	}
	if (re && *re == ':')
	    *re++ = '\0';
	else
	    re = r + strlen(r);

	/* Expand ~/ to $HOME/ */
	fn[0] = '\0';
	t = fn;
	*t = '\0';	/* XXX redundant. */
	if (r[0] == '~' && r[1] == '/') {
	    const char * home = getenv("HOME");
	    if (home == NULL)	/* XXX No HOME? */
		continue;
	    if (strlen(home) > (sizeof(fn) - strlen(r))) /* XXX too big */
		continue;
	    t = stpcpy(t, home);
	    r++;	/* skip ~ */
	}
	t = stpcpy(t, r);
	if (t[-1] != '/' && *bn != '/')
	    *t++ = '/';
	t = stpcpy(t, bn);
	t = rpmCleanPath(fn);
	if (t == NULL)	/* XXX can't happen */
	    continue;

	/* Check absolute path for access. */
	rc = (Access(t, mode) != 0 ? 1 : 0);
if (_rpmio_debug)
fprintf(stderr, "*** rpmioAccess(\"%s\", 0x%x) rc %d\n", t, mode, rc);
	if (rc == 0)
	    goto exit;
    }
/*@=branchstate@*/

    rc = 1;

exit:
    if (negate)
	rc ^= 1;
    return rc;
}

/*@-boundswrite@*/
int rpmioSlurp(const char * fn, const byte ** bp, ssize_t * blenp)
{
    static ssize_t blenmax = (32 * BUFSIZ);
    ssize_t blen = 0;
    byte * b = NULL;
    ssize_t size;
    FD_t fd;
    int rc = 0;

    fd = Fopen(fn, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rc = 2;
	goto exit;
    }

    size = fdSize(fd);
    blen = (size >= 0 ? size : blenmax);
    /*@-branchstate@*/
    if (blen) {
	int nb;
	b = xmalloc(blen+1);
	b[0] = '\0';
	nb = Fread(b, sizeof(*b), blen, fd);
	if (Ferror(fd) || (size > 0 && nb != blen)) {
	    rc = 1;
	    goto exit;
	}
	if (blen == blenmax && nb < blen) {
	    blen = nb;
	    b = xrealloc(b, blen+1);
	}
	b[blen] = '\0';
    }
    /*@=branchstate@*/

exit:
    if (fd) (void) Fclose(fd);
	
    if (rc) {
	if (b) free(b);
	b = NULL;
	blen = 0;
    }

    if (bp) *bp = b;
    else if (b) free(b);

    if (blenp) *blenp = blen;

    return rc;
}
/*@=boundswrite@*/

/*@-type@*/ /* LCL: function typedefs */
static struct FDIO_s fpio_s = {
  ufdRead, ufdWrite, fdSeek, ufdClose, XfdLink, XfdFree, XfdNew, fdFileno,
  ufdOpen, NULL, fdGetFp, NULL,	Mkdir, Chdir, Rmdir, Rename, Unlink
};
/*@=type@*/
FDIO_t fpio = /*@-compmempass@*/ &fpio_s /*@=compmempass@*/ ;

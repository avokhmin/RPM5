#ifndef	_H_BUILDIO_
#define	_H_BUILDIO_

/** \ingroup rpmbuild
 * \file build/buildio.h
 * Routines to read and write packages.
 * @deprecated this information will move elsewhere eventually.
 * @todo Eliminate, merge into rpmlib.
 */

#include "cpio.h"

/**
 */
typedef struct cpioSourceArchive {
    unsigned int cpioArchiveSize;
    FD_t	cpioFdIn;
/*@dependent@*/ struct cpioFileMapping *cpioList;
    int		cpioCount;
    struct rpmlead *lead;	/* XXX FIXME: exorcize lead/arch/os */
} CSA_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read rpm package components from file.
 * @param filename	file name of package (or NULL to use stdin)
 * @retval specp	spec structure to carry package header (or NULL)
 * @retval lead		package lead
 * @retval sigs		package signature
 * @param csa
 * @return		0 on success
 */
int readRPM(const char *fileName, /*@out@*/ Spec *specp, /*@out@*/ struct rpmlead *lead,
		/*@out@*/ Header *sigs, CSA_t *csa);

/**
 * Write rpm package to file.
 *
 * @warning The first header argument is now passed by reference in order to
 * return a reloaded contiguous header to the caller.
 *
 * @retval hdrp		header to write (final header is returned).
 * @param filename	file name of package
 * @param type		RPMLEAD_SOURCE/RPMLEAD_BINARY
 * @param csa
 * @param passPhrase
 * @retval cookie	generated cookie (i.e build host/time)
 * @return		0 on success
 */
int writeRPM(Header *hdrp, const char *fileName, int type,
		CSA_t *csa, char *passPhrase, /*@out@*/ const char **cookie);

#ifdef __cplusplus
}
#endif

#endif	/* _H_BUILDIO_ */

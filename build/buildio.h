#ifndef	_H_BUILDIO_
#define	_H_BUILDIO_

/** \ingroup rpmbuild
 * \file build/buildio.h
 * Routines to read and write packages.
 * @deprecated this information will move elsewhere eventually.
 * @todo Eliminate, merge into rpmlib.
 */

#include "rpmbuild.h"

/**
 */
typedef /*@abstract@*/ struct cpioSourceArchive_s {
    rpmuint32_t	cpioArchiveSize;
/*@relnull@*/
    FD_t	cpioFdIn;
/*@refcounted@*/ /*@relnull@*/
    rpmfi	fi;
/*@only@*/
    struct rpmlead * lead;	/* XXX FIXME: exorcize lead/arch/os */
} * CSA_t;

#ifdef __cplusplus
extern "C" {
#endif

#if defined(DEAD)
/**
 * Read rpm package components from file.
 * @param fileName	file name of package (or NULL to use stdin)
 * @retval specp	spec structure to carry package header (or NULL)
 * @retval lead		package lead
 * @retval sigs		package signature
 * @param csa
 * @return		0 on success
 */
/*@unused@*/ int readRPM(/*@null@*/ const char * fileName,
		/*@out@*/ Spec * specp,
		/*@out@*/ void * l,
		/*@out@*/ Header * sigs,
		CSA_t csa)
	/*@globals rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *specp, *lead, *sigs, csa, csa->cpioFdIn,
		rpmGlobalMacroContext, fileSystem, internalState @*/;
#endif

/**
 * Write rpm package to file.
 *
 * @warning The first argument (header) is now passed by reference in order to
 * return a reloaded contiguous header to the caller.
 *
 * @retval *hdrp	header to write (final header is returned).
 * @retval *pkgidp	header+payload MD5 of package (NULL to disable).
 * @param fileName	file name of package
 * @param csa
 * @param passPhrase
 * @retval cookie	generated cookie (i.e build host/time)
 * @param _dig		DSA keypair for auto-signing (or NULL)
 * @return		RPMRC_OK on success
 */
rpmRC writeRPM(Header * hdrp, /*@null@*/ unsigned char ** pkgidp,
		const char * fileName,
		CSA_t csa,
		/*@null@*/ char * passPhrase,
		/*@out@*/ const char ** cookie, void * _dig)
	/*@globals rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *hdrp, *pkgidp, *cookie, csa, csa->cpioArchiveSize,
		rpmGlobalMacroContext, fileSystem, internalState @*/;

#ifdef __cplusplus
}
#endif

#endif	/* _H_BUILDIO_ */

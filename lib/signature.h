#ifndef H_SIGNATURE
#define	H_SIGNATURE

/** \ingroup signature
 * \file lib/signature.h
 * Generate and verify signatures.
 */

#include <header.h>

/** \ingroup signature
 * Signature types stored in rpm lead.
 */
typedef	enum sigType_e {
    RPMSIGTYPE_HEADERSIG= 5	/*!< Header style signature */
} sigType;

/** \ingroup signature
 * Identify PGP versions.
 * @note Greater than 0 is a valid PGP version.
 */
typedef enum pgpVersion_e {
    PGP_NOTDETECTED	= -1,
    PGP_UNKNOWN		= 0,
    PGP_2		= 2,
    PGP_5		= 5
} pgpVersion;

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup signature
 * Return new, empty (signature) header instance.
 * @return		signature header
 */
Header rpmNewSignature(void)
	/*@*/;

/** \ingroup signature
 * Read (and verify header+payload size) signature header.
 * If an old-style signature is found, we emulate a new style one.
 * @param fd		file handle
 * @retval sighp	address of (signature) header (or NULL)
 * @param sig_type	type of signature header to read (from lead)
 * @retval msg		failure msg
 * @return		rpmRC return code
 */
rpmRC rpmReadSignature(FD_t fd, /*@null@*/ /*@out@*/ Header *sighp,
		sigType sig_type, /*@null@*/ /*@out@*/ const char ** msg)
	/*@globals fileSystem @*/
	/*@modifies fd, *sighp, *msg, fileSystem @*/;

/** \ingroup signature
 * Write signature header.
 * @param fd		file handle
 * @param h		(signature) header
 * @return		0 on success, 1 on error
 */
int rpmWriteSignature(FD_t fd, Header sigh)
	/*@globals fileSystem @*/
	/*@modifies fd, sigh, fileSystem @*/;

/** \ingroup signature
 * Generate signature(s) from a header+payload file, save in signature header.
 * @param sigh		signature header
 * @param file		header+payload file name
 * @param sigTag	type of signature(s) to add
 * @param passPhrase	private key pass phrase
 * @return		0 on success, -1 on failure
 */
int rpmAddSignature(Header sigh, const char * file,
		    int_32 sigTag, /*@null@*/ const char * passPhrase)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies sigh, sigTag, rpmGlobalMacroContext, fileSystem, internalState @*/;

/******************************************************************/

/**
 *  Possible actions for rpmLookupSignatureType()
 */
#define RPMLOOKUPSIG_QUERY	0	/* Lookup type in effect          */
#define RPMLOOKUPSIG_DISABLE	1	/* Disable (--sign was not given) */
#define RPMLOOKUPSIG_ENABLE	2	/* Re-enable %_signature          */

/** \ingroup signature
 * Return type of signature needed for signing/building.
 * @param action	enable/disable/query action
 * @return		sigTag to use, 0 if none, -1 on error
 */
int rpmLookupSignatureType(int action)
	/*@globals rpmGlobalMacroContext, h_errno, internalState @*/
	/*@modifies rpmGlobalMacroContext, internalState @*/;

/** \ingroup signature
 * Read a pass phrase using getpass(3), confirm with gpg/pgp helper binaries.
 * @param prompt	user prompt
 * @param sigTag	signature type/tag
 * @return		pass phrase
 */
/*@dependent@*/ /*@null@*/
char * rpmGetPassPhrase(/*@null@*/ const char * prompt,
		const int sigTag)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/;

/** \ingroup signature
 * Return path to pgp executable of given type, or NULL when not found.
 * @retval pgpVer	pgp version
 * @return		path to pgp executable
 */
/*@-exportlocal -redecl@*/
/*@null@*/ const char * rpmDetectPGPVersion(
			/*@null@*/ /*@out@*/ pgpVersion * pgpVer)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *pgpVer, rpmGlobalMacroContext @*/;
/*@=exportlocal =redecl@*/

#ifdef __cplusplus
}
#endif

#endif	/* H_SIGNATURE */

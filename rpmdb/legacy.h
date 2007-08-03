#ifndef H_LEGACY
#define H_LEGACY

/**
 * \file rpmdb/legacy.h
 *
 */

/**
 */
/*@-redecl@*/
/*@unchecked@*/
extern int _noDirTokens;
/*@=redecl@*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return digest and size of a file.
 * @param digestalgo	digest algorithm to use
 * @param fn		file name
 * @retval digest	address of md5sum
 * @param asAscii	return md5sum as ascii string?
 * @retval *fsizep	file size pointer (or NULL)
 * @return		0 on success, 1 on error
 */
int dodigest(int digestalgo, const char * fn, /*@out@*/ unsigned char * digest,
		int asAscii, /*@null@*/ /*@out@*/ size_t *fsizep)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies digest, *fsizep, fileSystem, internalState @*/;

/**
 * Return MD5 digest and size of a file.
 * @param fn		file name
 * @retval digest	address of md5sum
 * @param asAscii	return md5sum as ascii string?
 * @retval *fsizep	file size pointer (or NULL)
 * @return		0 on success, 1 on error
 */
int domd5(const char * fn, /*@out@*/ unsigned char * digest, int asAscii,
		/*@null@*/ /*@out@*/ size_t *fsizep)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies digest, *fsizep, fileSystem, internalState @*/;

#ifdef __cplusplus
}
#endif

#endif	/* H_LEGACY */

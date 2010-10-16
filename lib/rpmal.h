#ifndef H_RPMAL
#define H_RPMAL

/** \ingroup rpmdep rpmtrans
 * \file lib/rpmal.h
 * Structures used for managing added/available package lists.
 */

#include <rpmds.h>
#include <rpmfi.h>

/*@-exportlocal@*/
/*@unchecked@*/
extern int _rpmal_debug;
/*@=exportlocal@*/

#define	RPMAL_NOMATCH	((alKey)-1L)

/** \ingroup rpmts
 * An added/available package retrieval index.
 */
/*@-mutrep@*/
typedef /*@abstract@*/ int alNum;
/*@=mutrep@*/

/**
 */
typedef /*@abstract@*/ struct rpmal_s *		rpmal;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize available packckages, items, and directory list.
 * @param delta		no. of entries to add on each realloc
 * @return al		new available list
 */
/*@-exportlocal@*/
/*@only@*/
rpmal rpmalCreate(int delta)
	/*@*/;
/*@=exportlocal@*/

/**
 * Free available packages, items, and directory members.
 * @param al		available list
 * @return		NULL always
 */
/*@null@*/
rpmal rpmalFree(/*@only@*/ /*@null@*/ rpmal al)
	/*@globals fileSystem @*/
	/*@modifies al, fileSystem @*/;

/**
 * Delete package from available list.
 * @param al		available list
 * @param pkgKey	package key
 */
/*@-exportlocal@*/
void rpmalDel(/*@null@*/ rpmal al, /*@null@*/ alKey pkgKey)
	/*@globals fileSystem @*/
	/*@modifies al, fileSystem @*/;
/*@=exportlocal@*/

/**
 * Add package to available list.
 * @param alistp	address of available list
 * @param pkgKey	package key, RPMAL_NOMATCH to force an append
 * @param key		associated file name/python object
 * @param provides	provides dependency set
 * @param fi		file info set
 * @param tscolor	transaction color bits
 * @return		available package index
 */
alKey rpmalAdd(rpmal * alistp,
		/*@dependent@*/ /*@null@*/ alKey pkgKey,
		/*@dependent@*/ /*@null@*/ fnpyKey key,
		/*@null@*/ rpmds provides, /*@null@*/ rpmfi fi,
		rpmuint32_t tscolor)
	/*@globals fileSystem @*/
	/*@modifies *alistp, provides, fi, fileSystem @*/;

/**
 * Add package provides to available list index.
 * @param al		available list
 * @param pkgKey	package key
 * @param provides	added package provides
 * @param tscolor	transaction color bits
 */
/*@-exportlocal@*/
void rpmalAddProvides(rpmal al,
		/*@dependent@*/ /*@null@*/ alKey pkgKey,
		/*@null@*/ rpmds provides, rpmuint32_t tscolor)
	/*@modifies al, provides @*/;
/*@=exportlocal@*/

/**
 * Generate index for available list.
 * @param al		available list
 */
void rpmalMakeIndex(/*@null@*/ rpmal al)
	/*@modifies al @*/;

/**
 * Check added package file lists for package(s) that provide a file.
 * @param al		available list
 * @param ds		dependency set
 * @retval keyp		added package key pointer (or NULL)
 * @return		associated package key(s), NULL if none
 */
/*@-exportlocal@*/
/*@only@*/ /*@null@*/
fnpyKey * rpmalAllFileSatisfiesDepend(/*@null@*/ const rpmal al,
		/*@null@*/ const rpmds ds, /*@null@*/ alKey * keyp)
	/*@globals fileSystem, internalState @*/
	/*@modifies al, *keyp, fileSystem, internalState @*/;
/*@=exportlocal@*/

/**
 * Check added package file lists for package(s) that have a provide.
 * @param al		available list
 * @param ds		dependency set
 * @retval keyp		added package key pointer (or NULL)
 * @return		associated package key(s), NULL if none
 */
/*@only@*/ /*@null@*/
fnpyKey * rpmalAllSatisfiesDepend(const rpmal al, const rpmds ds,
		/*@null@*/ alKey * keyp)
	/*@globals fileSystem, internalState @*/
	/*@modifies al, *keyp, fileSystem, internalState @*/;

/**
 * Check added package file lists for first package that has a provide.
 * @todo Eliminate.
 * @param al		available list
 * @param ds		dependency set
 * @retval keyp		added package key pointer (or NULL)
 * @return		associated package key, NULL if none
 */
/*@null@*/
fnpyKey rpmalSatisfiesDepend(const rpmal al, const rpmds ds,
		/*@null@*/ alKey * keyp)
	/*@globals fileSystem, internalState @*/
	/*@modifies al, *keyp, fileSystem, internalState @*/;

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMAL */

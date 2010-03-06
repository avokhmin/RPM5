#ifndef H_FINGERPRINT
#define H_FINGERPRINT

/** \ingroup rpmtrans
 * \file rpmdb/fprint.h
 * Identify a file name path by a unique "finger print".
 */

#include "rpmhash.h"

/**
 */
typedef /*@abstract@*/ struct fprintCache_s * fingerPrintCache;

/**
 * @todo Convert to pointer and make abstract.
 */
typedef struct fingerPrint_s fingerPrint;

/**
 * Finger print cache entry.
 * This is really a directory and symlink cache. We don't differentiate between
 * the two. We can prepopulate it, which allows us to easily conduct "fake"
 * installs of a system w/o actually mounting filesystems.
 */
struct fprintCacheEntry_s {
    const char * dirName;		/*!< path to existing directory */
    dev_t dev;				/*!< stat(2) device number */
    ino_t ino;				/*!< stat(2) inode number */
};

/**
 * Finger print cache.
 */
struct fprintCache_s {
    hashTable ht;			/*!< hashed by dirName */
};

#if defined(_FPRINT_INTERNAL)
struct rpmffi_s {
    rpmte p;
    int fileno;
};
#endif	/* _FPRINT_INTERNAL */

/**
 * Associates a trailing sub-directory and final base name with an existing
 * directory finger print.
 */
struct fingerPrint_s {
/*! directory finger print entry (the directory path is stat(2)-able */
    const struct fprintCacheEntry_s * entry;
/*! trailing sub-directory path (directories that are not stat(2)-able */
/*@owned@*/ /*@relnull@*/
    const char * subDir;
/*@dependent@*/
    const char * baseName;	/*!< file base name */
};

/**
 */
#define	FP_ENTRY_EQUAL(a, b) (((a)->dev == (b)->dev) && ((a)->ino == (b)->ino))

/**
*/
#define FP_EQUAL(a, b) ( \
	FP_ENTRY_EQUAL((a).entry, (b).entry) && \
	!strcmp((a).baseName, (b).baseName) && ( \
	    ((a).subDir == (b).subDir) || \
	    ((a).subDir && (b).subDir && !strcmp((a).subDir, (b).subDir)) \
	) \
    )

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create finger print cache.
 * @param sizeHint	number of elements expected
 * @return pointer to initialized fingerprint cache
 */
/*@only@*/ fingerPrintCache fpCacheCreate(int sizeHint)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/;

/**
 * Destroy finger print cache.
 * @param cache		pointer to fingerprint cache
 * @return		NULL always
 */
/*@null@*/
fingerPrintCache fpCacheFree(/*@only@*/ fingerPrintCache cache)
	/*@globals fileSystem @*/
	/*@modifies cache, fileSystem @*/;

/**
 * Return finger print of a file path.
 * @param cache		pointer to fingerprint cache
 * @param dirName	leading directory name of file path
 * @param baseName	base name of file path
 * @param scareMem
 * @return pointer to the finger print associated with a file path.
 */
fingerPrint fpLookup(fingerPrintCache cache, const char * dirName, 
			const char * baseName, int scareMem)
	/*@globals fileSystem, internalState @*/
	/*@modifies cache, fileSystem, internalState @*/;

/**
 * Return hash value for a finger print.
 * Hash based on dev and inode only!
 * @param h		hash initial value
 * @param *data		finger print entry
 * @param size		size of fingerprint entry
 * @return		hash value
 */
uint32_t fpHashFunction(uint32_t h, const void * data, size_t size)
	/*@*/;

/**
 * Compare two finger print entries.
 * This routine is exactly equivalent to the FP_EQUAL macro.
 * @param key1		finger print 1
 * @param key2		finger print 2
 * @return result of comparing key1 and key2
 */
int fpEqual(const void * key1, const void * key2)
	/*@*/;

/**
 * Return finger prints of an array of file paths.
 * @warning: scareMem is assumed!
 * @param cache		pointer to fingerprint cache
 * @param dirNames	directory names
 * @param baseNames	file base names
 * @param dirIndexes	index into dirNames for each baseNames
 * @param fileCount	number of file entries
 * @retval *fpList	array of finger prints
 */
void fpLookupList(fingerPrintCache cache, const char ** dirNames, 
		  const char ** baseNames, const rpmuint32_t * dirIndexes, 
		  rpmuint32_t fileCount, fingerPrint * fpList)
	/*@globals fileSystem, internalState @*/
	/*@modifies cache, *fpList, fileSystem, internalState @*/;

/**
 * Check file for to be installed symlinks in their path,
 *  correct their fingerprint and add it to newht.
 * @param ht		hash table containing all files fingerprints
 * @param newht		hash table to add the corrected fingerprints
 * @param fpc		fingerprint cache
 * @param _p		transaction element
 * @param filenr	the number of the file we are dealing with
 */
void fpLookupSubdir(hashTable symlinks, hashTable fphash, fingerPrintCache fpc,
		void * _p, int filenr)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif

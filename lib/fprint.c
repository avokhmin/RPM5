/**
 * \file lib/fprint.c
 */

#include "system.h"
#include <rpmlib.h>
#include <rpmmacro.h>	/* XXX for rpmCleanPath */

#include "fprint.h"
#include "debug.h"

fingerPrintCache fpCacheCreate(int sizeHint)
{
    fingerPrintCache fpc;

    fpc = xmalloc(sizeof(*fpc));
    fpc->ht = htCreate(sizeHint * 2, 0, 1, hashFunctionString,
		       hashEqualityString);
    return fpc;
}

void fpCacheFree(fingerPrintCache cache)
{
    htFree(cache->ht);
    free(cache);
}

/**
 * Find directory name entry in cache.
 * @param cache		pointer to fingerprint cache
 * @param dirName	string to locate in cache
 * @return pointer to directory name entry (or NULL if not found).
 */
static const struct fprintCacheEntry_s * cacheContainsDirectory(
			    fingerPrintCache cache,
			    const char * dirName)
{
    const void ** data;

    if (htGetEntry(cache->ht, dirName, &data, NULL, NULL))
	return NULL;
    return data[0];
}

/**
 * Return finger print of a file path.
 * @param cache		pointer to fingerprint cache
 * @param dirName	leading directory name of path
 * @param baseName	file name of path
 * @param scareMemory
 * @return pointer to the finger print associated with a file path.
 */
static fingerPrint doLookup(fingerPrintCache cache,
	const char * dirName, const char * baseName, int scareMemory)
{
    char dir[PATH_MAX];
    const char * cleanDirName;
    size_t cdnl;
    char * end;		    /* points to the '\0' at the end of "buf" */
    fingerPrint fp;
    struct stat sb;
    char * buf;
    const struct fprintCacheEntry_s * cacheHit;

    /* assert(*dirName == '/' || !scareMemory); */

    /* XXX WATCHOUT: fp.subDir is set below from relocated dirName arg */
    cleanDirName = dirName;
    cdnl = strlen(cleanDirName);

    if (*cleanDirName == '/') {
	if (!scareMemory)
	    cleanDirName =
		rpmCleanPath(strcpy(alloca(cdnl+1), dirName));
    } else {
	scareMemory = 0;	/* XXX causes memory leak */

	/* Using realpath on the arg isn't correct if the arg is a symlink,
	 * especially if the symlink is a dangling link.  What we 
	 * do instead is use realpath() on `.' and then append arg to
	 * the result.
	 */

	/* if the current directory doesn't exist, we might fail. 
	   oh well. likewise if it's too long.  */
	dir[0] = '\0';
	if ( /*@-unrecog@*/ realpath(".", dir) /*@=unrecog@*/ != NULL) {
	    end = dir + strlen(dir);
	    if (end[-1] != '/')	*end++ = '/';
	    end = stpncpy(end, cleanDirName, sizeof(dir) - (end - dir));
	    *end = '\0';
	    (void)rpmCleanPath(dir); /* XXX possible /../ from concatenation */
	    end = dir + strlen(dir);
	    if (end[-1] != '/')	*end++ = '/';
	    *end = '\0';
	    cleanDirName = dir;
	    cdnl = end - dir;
	}
    }

    buf = strcpy(alloca(cdnl + 1), cleanDirName);
    end = buf + cdnl;

    /* no need to pay attention to that extra little / at the end of dirName */
    if (buf[1] && end[-1] == '/') {
	end--;
	*end = '\0';
    }

    fp.entry = NULL;
    fp.subDir = NULL;
    fp.baseName = NULL;
    while (1) {

	/* as we're stating paths here, we want to follow symlinks */

	cacheHit = cacheContainsDirectory(cache, (*buf != '\0' ? buf : "/"));
	if (cacheHit != NULL) {
	    fp.entry = cacheHit;
	} else if (!stat((*buf != '\0' ? buf : "/"), &sb)) {
	    size_t nb = sizeof(*fp.entry) + (*buf != '\0' ? (end-buf) : 1) + 1;
	    char * dn = xmalloc(nb);
	    struct fprintCacheEntry_s * newEntry = (void *)dn;

	    dn += sizeof(*newEntry);
	    strcpy(dn, (*buf != '\0' ? buf : "/"));
	    newEntry->ino = sb.st_ino;
	    newEntry->dev = sb.st_dev;
	    newEntry->isFake = 0;
	    newEntry->dirName = dn;
	    fp.entry = newEntry;

	    /*@-kepttrans@*/
	    htAddEntry(cache->ht, dn, fp.entry);
	    /*@@kepttrans@*/
	}

        if (fp.entry) {
	    fp.subDir = cleanDirName + (end - buf);
	    if (fp.subDir[0] == '/' && fp.subDir[1] != '\0')
		fp.subDir++;
	    if (fp.subDir[0] == '\0' ||
	    /* XXX don't bother saving '/' as subdir */
	       (fp.subDir[0] == '/' && fp.subDir[1] == '\0'))
		fp.subDir = NULL;
	    fp.baseName = baseName;
	    if (!scareMemory && fp.subDir != NULL)
		fp.subDir = xstrdup(fp.subDir);
	    return fp;
	}

        /* stat of '/' just failed! */
	if (end == buf + 1)
	    abort();

	end--;
	while ((end > buf) && *end != '/') end--;
	if (end == buf)	    /* back to stat'ing just '/' */
	    end++;

	*end = '\0';
    }

    /*@notreached@*/

    return fp;
}

fingerPrint fpLookup(fingerPrintCache cache, const char * dirName, 
			const char * baseName, int scareMemory)
{
    return doLookup(cache, dirName, baseName, scareMemory);
}

unsigned int fpHashFunction(const void * key)
{
    const fingerPrint * fp = key;
    unsigned int hash = 0;
    char ch;
    const char * chptr;

    ch = 0;
    chptr = fp->baseName;
    while (*chptr != '\0') ch ^= *chptr++;

    hash |= ((unsigned)ch) << 24;
    hash |= (((((unsigned)fp->entry->dev) >> 8) ^ fp->entry->dev) & 0xFF) << 16;
    hash |= fp->entry->ino & 0xFFFF;
    
    return hash;
}

int fpEqual(const void * key1, const void * key2)
{
    const fingerPrint *k1 = key1;
    const fingerPrint *k2 = key2;

    /* If the addresses are the same, so are the values. */
    if (k1 == k2)
	return 0;

    /* Otherwise, compare fingerprints by value. */
    if (FP_EQUAL(*k1, *k2))
	return 0;
    return 1;

}

void fpLookupList(fingerPrintCache cache, const char ** dirNames, 
		  const char ** baseNames, const int * dirIndexes, 
		  int fileCount, fingerPrint * fpList)
{
    int i;

    for (i = 0; i < fileCount; i++) {
	/* If this is in the same directory as the last file, don't bother
	   redoing all of this work */
	if (i > 0 && dirIndexes[i - 1] == dirIndexes[i]) {
	    fpList[i].entry = fpList[i - 1].entry;
	    fpList[i].subDir = fpList[i - 1].subDir;
	    fpList[i].baseName = baseNames[i];
	} else {
	    fpList[i] = doLookup(cache, dirNames[dirIndexes[i]], baseNames[i],
				 1);
	}
    }
}

void fpLookupHeader(fingerPrintCache cache, Header h, fingerPrint * fpList)
{
    int fileCount;
    const char ** baseNames, ** dirNames;
    int_32 * dirIndexes;

    if (!headerGetEntryMinMemory(h, RPMTAG_BASENAMES, NULL, 
			(const void **) &baseNames, &fileCount)) return;

    headerGetEntryMinMemory(h, RPMTAG_DIRNAMES, NULL,
			(const void **) &dirNames, NULL);
    headerGetEntryMinMemory(h, RPMTAG_DIRINDEXES, NULL,
			(const void **) &dirIndexes, NULL);
    fpLookupList(cache, dirNames, baseNames, dirIndexes, fileCount, fpList);
    free(dirNames);
    free(baseNames);
}

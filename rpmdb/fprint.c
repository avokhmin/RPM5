/**
 * \file rpmdb/fprint.c
 */

#include "system.h"

#include <rpmio.h>	/* XXX rpmRC codes. */
#include <rpmmacro.h>	/* XXX for rpmCleanPath */
#include <rpmdb.h>

#include "fprint.h"
#include "debug.h"

fingerPrintCache fpCacheCreate(int sizeHint)
{
    fingerPrintCache fpc;

    fpc = xmalloc(sizeof(*fpc));
    fpc->ht = htCreate(sizeHint * 2, 0, 1, NULL, NULL);
    return fpc;
}

fingerPrintCache fpCacheFree(fingerPrintCache cache)
{
    cache->ht = htFree(cache->ht);
    free(cache);
    return NULL;
}

/**
 * Find directory name entry in cache.
 * @param cache		pointer to fingerprint cache
 * @param dirName	string to locate in cache
 * @return pointer to directory name entry (or NULL if not found).
 */
static /*@null@*/ const struct fprintCacheEntry_s * cacheContainsDirectory(
			    fingerPrintCache cache,
			    const char * dirName)
	/*@*/
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
 * @param scareMem
 * @return pointer to the finger print associated with a file path.
 */
static fingerPrint doLookup(fingerPrintCache cache,
		const char * dirName, const char * baseName, int scareMem)
	/*@modifies cache @*/
{
    char dir[PATH_MAX];
    const char * cleanDirName;
    size_t cdnl;
    char * end;		    /* points to the '\0' at the end of "buf" */
    fingerPrint fp;
    struct stat sb;
    char * buf;
    const struct fprintCacheEntry_s * cacheHit;

    /* assert(*dirName == '/' || !scareMem); */

    /* XXX WATCHOUT: fp.subDir is set below from relocated dirName arg */
    cleanDirName = dirName;
    cdnl = strlen(cleanDirName);

    if (*cleanDirName == '/') {
	if (!scareMem)
	    cleanDirName =
		rpmCleanPath(strcpy(alloca(cdnl+1), dirName));
    } else {
	scareMem = 0;	/* XXX causes memory leak */

	/* Using realpath on the arg isn't correct if the arg is a symlink,
	 * especially if the symlink is a dangling link.  What we 
	 * do instead is use realpath() on `.' and then append arg to
	 * the result.
	 */

	/* if the current directory doesn't exist, we might fail. 
	   oh well. likewise if it's too long.  */
	dir[0] = '\0';
	if (realpath(".", dir) != NULL) {
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
    fp.entry = NULL;
    fp.subDir = NULL;
    fp.baseName = NULL;
    /*@-nullret@*/
    if (cleanDirName == NULL) return fp;	/* XXX can't happen */
    /*@=nullret@*/

    buf = strcpy(alloca(cdnl + 1), cleanDirName);
    end = buf + cdnl;

    /* no need to pay attention to that extra little / at the end of dirName */
    if (buf[1] && end[-1] == '/') {
	end--;
	*end = '\0';
    }

    while (1) {

	/* as we're stating paths here, we want to follow symlinks */

	cacheHit = cacheContainsDirectory(cache, (*buf != '\0' ? buf : "/"));
	if (cacheHit != NULL) {
	    fp.entry = cacheHit;
	} else if (!stat((*buf != '\0' ? buf : "/"), &sb)) {
	    size_t nb = sizeof(*fp.entry) + (*buf != '\0' ? (end-buf) : 1) + 1;
	    char * dn = xmalloc(nb);
	    struct fprintCacheEntry_s * newEntry = (void *)dn;

	    /*@-usereleased@*/	/* LCL: contiguous malloc confusion */
	    dn += sizeof(*newEntry);
	    strcpy(dn, (*buf != '\0' ? buf : "/"));
	    newEntry->ino = sb.st_ino;
	    newEntry->dev = sb.st_dev;
	    newEntry->dirName = dn;
	    fp.entry = newEntry;

	    /*@-kepttrans -dependenttrans @*/
	    htAddEntry(cache->ht, dn, fp.entry);
	    /*@=kepttrans =dependenttrans @*/
	    /*@=usereleased@*/
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
	    if (!scareMem && fp.subDir != NULL)
		fp.subDir = xstrdup(fp.subDir);
	/*@-compdef@*/ /* FIX: fp.entry.{dirName,dev,ino} undef @*/
	    return fp;
	/*@=compdef@*/
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

    /*@-compdef@*/ /* FIX: fp.entry.{dirName,dev,ino} undef @*/
    /*@-nullret@*/ return fp; /*@=nullret@*/	/* LCL: can't happen. */
    /*@=compdef@*/
}

fingerPrint fpLookup(fingerPrintCache cache, const char * dirName, 
			const char * baseName, int scareMem)
{
    return doLookup(cache, dirName, baseName, scareMem);
}

uint32_t fpHashFunction(uint32_t h, const void * data,
		/*@unused@*/ size_t size)
{
    const fingerPrint * fp = data;
    const char * chptr = fp->baseName;
    unsigned char ch = '\0';

    while (*chptr != '\0') ch ^= *chptr++;

    h |= ((unsigned)ch) << 24;
    h |= (((((unsigned)fp->entry->dev) >> 8) ^ fp->entry->dev) & 0xFF) << 16;
    h |= fp->entry->ino & 0xFFFF;
    
    return h;
}

int fpEqual(const void * key1, const void * key2)
{
    const fingerPrint *k1 = key1;
    const fingerPrint *k2 = key2;

    /* If the addresses are the same, so are the values. */
    if (k1 == k2)
	return 0;

    /* Otherwise, compare fingerprints by value. */
    /*@-nullpass@*/	/* LCL: whines about (*k2).subdir */
    if (FP_EQUAL(*k1, *k2))
	return 0;
    /*@=nullpass@*/
    return 1;

}

void fpLookupList(fingerPrintCache cache, const char ** dirNames, 
		  const char ** baseNames, const uint32_t * dirIndexes, 
		  uint32_t fileCount, fingerPrint * fpList)
{
    unsigned i;

    for (i = 0; i < (unsigned) fileCount; i++) {
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

#ifdef	NOTUSED
/**
 * Return finger prints of all file names in header.
 * @warning: scareMem is assumed!
 * @param cache		pointer to fingerprint cache
 * @param h		package header
 * @retval fpList	pointer to array of finger prints
 */
static
void fpLookupHeader(fingerPrintCache cache, Header h, fingerPrint * fpList)
	/*@modifies h, cache, *fpList @*/
{
    rpmTagData he_p = { .ptr = NULL };
    HE_s he_s = { .tag = 0, .t = 0, .p = &he_p, .c = 0, .freeData = 0 };
    HE_t he = &he_s;
    const char ** baseNames;
    const char ** dirNames;
    uint32_t * dirIndexes;
    rpmTagCount fileCount;
    int xx;

    he->tag = RPMTAG_BASENAMES;
    xx = headerGet(h, he->tag, &he->t, he->p, &he->c);
    baseNames = he_p.argv;
    fileCount = he->c;
    if (!xx)
	return;

    he->tag = RPMTAG_DIRNAMES;
    xx = headerGet(h, he, 0);
    dirNames = he_p.argv;
    he->tag = RPMTAG_DIRINDEXES;
    xx = headerGet(h, he, 0);
    dirIndexes = he_p.ui32p;

    fpLookupList(cache, dirNames, baseNames, dirIndexes, fileCount, fpList);

    dirIndexes = _free(dirIndexes);
    dirNames = _free(dirNames);
    baseNames = _free(baseNames);
}
#endif
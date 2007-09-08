/** \ingroup header
 * \file lib/package.c
 */

#include "system.h"

#include <netinet/in.h>

#include <rpmio_internal.h>
#include <rpmlib.h>

#include "rpmts.h"

#include "misc.h"		/* XXX stripTrailingChar() */
#include <pkgio.h>		/* XXX readLead */

#include "header_internal.h"	/* XXX headerCheck */
#include "signature.h"
#include "debug.h"

#define	alloca_strdup(_s)	strcpy(alloca(strlen(_s)+1), (_s))

/*@access pgpDig @*/
/*@access pgpDigParams @*/
/*@access Header @*/		/* XXX compared with NULL */
/*@access entryInfo @*/		/* XXX headerCheck */
/*@access indexEntry @*/	/* XXX headerCheck */
/*@access FD_t @*/		/* XXX stealing digests */

/*@unchecked@*/
static int _print_pkts = 0;

/*@unchecked@*/
static unsigned int nkeyids_max = 256;
/*@unchecked@*/
static unsigned int nkeyids = 0;
/*@unchecked@*/
static unsigned int nextkeyid  = 0;
/*@unchecked@*/ /*@only@*/ /*@null@*/
static unsigned int * keyids;

extern int _nolead;
extern int _nosigh;

/**
 * Alignment needs (and sizeof scalars types) for internal rpm data types.
 */
/*@observer@*/ /*@unchecked@*/
static int typeAlign[16] =  {
    1,	/*!< RPM_NULL_TYPE */
    1,	/*!< RPM_CHAR_TYPE */
    1,	/*!< RPM_INT8_TYPE */
    2,	/*!< RPM_INT16_TYPE */
    4,	/*!< RPM_INT32_TYPE */
    8,	/*!< RPM_INT64_TYPE */
    1,	/*!< RPM_STRING_TYPE */
    1,	/*!< RPM_BIN_TYPE */
    1,	/*!< RPM_STRING_ARRAY_TYPE */
    1,	/*!< RPM_I18NSTRING_TYPE */
    0,
    0,
    0,
    0,
    0,
    0
};

/**
 * Sanity check on no. of tags.
 * This check imposes a limit of 65K tags, more than enough.
 */
#define hdrchkTags(_ntags)      ((_ntags) & 0xffff0000)

/**
 * Sanity check on type values.
 */
#define hdrchkType(_type) ((_type) < RPM_MIN_TYPE || (_type) > RPM_MAX_TYPE)

/**
 * Sanity check on data size and/or offset and/or count.
 * This check imposes a limit of 16 MB, more than enough.
 */
#define hdrchkData(_nbytes) ((_nbytes) & 0xff000000)

/**
 * Sanity check on data alignment for data type.
 */
#define hdrchkAlign(_type, _off)	((_off) & (typeAlign[_type]-1))

/**
 * Sanity check on range of data offset.
 */
#define hdrchkRange(_dl, _off)		((_off) < 0 || (_off) > (_dl))

void headerMergeLegacySigs(Header h, const Header sigh)
{
    HFD_t hfd = (HFD_t) headerFreeData;
    HeaderIterator hi;
    int_32 tag, type, count;
    const void * ptr;
    int xx;

    if (h == NULL || sigh == NULL)
	return;

    for (hi = headerInitIterator(sigh);
        headerNextIterator(hi, &tag, &type, &ptr, &count);
        ptr = hfd(ptr, type))
    {
	switch (tag) {
	/* XXX Translate legacy signature tag values. */
	case RPMSIGTAG_SIZE:
	    tag = RPMTAG_SIGSIZE;
	    /*@switchbreak@*/ break;
#if defined(SUPPORT_RPMV3_BROKEN)
	case RPMSIGTAG_LEMD5_1:
	    tag = RPMTAG_SIGLEMD5_1;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_LEMD5_2:
	    tag = RPMTAG_SIGLEMD5_2;
	    /*@switchbreak@*/ break;
#endif
#if defined(SUPPORT_RPMV3_VERIFY_RSA)
	case RPMSIGTAG_PGP:
	    tag = RPMTAG_SIGPGP;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_PGP5:
	    tag = RPMTAG_SIGPGP5;
	    /*@switchbreak@*/ break;
#endif
	case RPMSIGTAG_MD5:
	    tag = RPMTAG_SIGMD5;
	    /*@switchbreak@*/ break;
#if defined(SUPPORT_RPMV3_VERIFY_DSA)
	case RPMSIGTAG_GPG:
	    tag = RPMTAG_SIGGPG;
	    /*@switchbreak@*/ break;
#endif
	case RPMSIGTAG_PAYLOADSIZE:
	    tag = RPMTAG_ARCHIVESIZE;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_SHA1:
	case RPMSIGTAG_DSA:
	case RPMSIGTAG_RSA:
	default:
	    if (!(tag >= HEADER_SIGBASE && tag < HEADER_TAGBASE))
		continue;
	    /*@switchbreak@*/ break;
	}
	if (ptr == NULL) continue;	/* XXX can't happen */
	if (!headerIsEntry(h, tag)) {
	    if (hdrchkType(type))
		continue;
	    if (count < 0 || hdrchkData(count))
		continue;
	    switch(type) {
	    case RPM_NULL_TYPE:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    case RPM_CHAR_TYPE:
	    case RPM_INT8_TYPE:
	    case RPM_INT16_TYPE:
	    case RPM_INT32_TYPE:
		if (count != 1)
		    continue;
		/*@switchbreak@*/ break;
	    case RPM_STRING_TYPE:
	    case RPM_BIN_TYPE:
		if (count >= 16*1024)
		    continue;
		/*@switchbreak@*/ break;
	    case RPM_STRING_ARRAY_TYPE:
	    case RPM_I18NSTRING_TYPE:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
 	    xx = headerAddEntry(h, tag, type, ptr, count);
	}
    }
    hi = headerFreeIterator(hi);
}

Header headerRegenSigHeader(const Header h, int noArchiveSize)
{
    HFD_t hfd = (HFD_t) headerFreeData;
    Header sigh = rpmNewSignature();
    HeaderIterator hi;
    int_32 tag, stag, type, count;
    const void * ptr;
    int xx;

    for (hi = headerInitIterator(h);
        headerNextIterator(hi, &tag, &type, &ptr, &count);
        ptr = hfd(ptr, type))
    {
	switch (tag) {
	/* XXX Translate legacy signature tag values. */
	case RPMTAG_SIGSIZE:
	    stag = RPMSIGTAG_SIZE;
	    /*@switchbreak@*/ break;
#if defined(SUPPORT_RPMV3_BROKEN)
	case RPMTAG_SIGLEMD5_1:
	    stag = RPMSIGTAG_LEMD5_1;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGLEMD5_2:
	    stag = RPMSIGTAG_LEMD5_2;
	    /*@switchbreak@*/ break;
#endif
#if defined(SUPPORT_RPMV3_VERIFY_RSA)
	case RPMTAG_SIGPGP:
	    stag = RPMSIGTAG_PGP;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGPGP5:
	    stag = RPMSIGTAG_PGP5;
	    /*@switchbreak@*/ break;
#endif
	case RPMTAG_SIGMD5:
	    stag = RPMSIGTAG_MD5;
	    /*@switchbreak@*/ break;
#if defined(SUPPORT_RPMV3_VERIFY_DSA)
	case RPMTAG_SIGGPG:
	    stag = RPMSIGTAG_GPG;
	    /*@switchbreak@*/ break;
#endif
	case RPMTAG_ARCHIVESIZE:
	    /* XXX rpm-4.1 and later has archive size in signature header. */
	    if (noArchiveSize)
		continue;
	    stag = RPMSIGTAG_PAYLOADSIZE;
	    /*@switchbreak@*/ break;
	case RPMTAG_SHA1HEADER:
	case RPMTAG_DSAHEADER:
	case RPMTAG_RSAHEADER:
	default:
	    if (!(tag >= HEADER_SIGBASE && tag < HEADER_TAGBASE))
		continue;
	    stag = tag;
	    /*@switchbreak@*/ break;
	}
	if (ptr == NULL) continue;	/* XXX can't happen */
	if (!headerIsEntry(sigh, stag))
	    xx = headerAddEntry(sigh, stag, type, ptr, count);
    }
    hi = headerFreeIterator(hi);
    return sigh;
}

/**
 * Remember current key id.
 * @param ts		transaction set
 * @return		0 if new keyid, otherwise 1
 */
static int rpmtsStashKeyid(rpmts ts)
	/*@globals nextkeyid, nkeyids, keyids @*/
	/*@modifies nextkeyid, nkeyids, keyids @*/
{
    const void * sig = rpmtsSig(ts);
    pgpDig dig = rpmtsDig(ts);
    pgpDigParams sigp = rpmtsSignature(ts);
    unsigned int keyid;
    int i;

    if (sig == NULL || dig == NULL || sigp == NULL)
	return 0;

    keyid = pgpGrab(sigp->signid+4, 4);
    if (keyid == 0)
	return 0;

    if (keyids != NULL)
    for (i = 0; i < nkeyids; i++) {
/*@-boundsread@*/
	if (keyid == keyids[i])
	    return 1;
/*@=boundsread@*/
    }

    if (nkeyids < nkeyids_max) {
	nkeyids++;
	keyids = xrealloc(keyids, nkeyids * sizeof(*keyids));
    }
/*@-boundswrite@*/
    if (keyids)		/* XXX can't happen */
	keyids[nextkeyid] = keyid;
/*@=boundswrite@*/
    nextkeyid++;
    nextkeyid %= nkeyids_max;

    return 0;
}

int headerVerifyInfo(int il, int dl, const void * pev, void * iv, int negate)
{
/*@-castexpose@*/
    entryInfo pe = (entryInfo) pev;
/*@=castexpose@*/
    entryInfo info = iv;
    int i;

/*@-boundsread@*/
    for (i = 0; i < il; i++) {
	info->tag = ntohl(pe[i].tag);
	info->type = ntohl(pe[i].type);
	info->offset = ntohl(pe[i].offset);
	if (negate)
	    info->offset = -info->offset;
	info->count = ntohl(pe[i].count);

	if (hdrchkType(info->type))
	    return i;
	if (hdrchkAlign(info->type, info->offset))
	    return i;
	if (!negate && hdrchkRange(dl, info->offset))
	    return i;
	if (hdrchkData(info->count))
	    return i;

    }
/*@=boundsread@*/
    return -1;
}

/**
 * Check header consistency, performing headerGetEntry() the hard way.
 *
 * Sanity checks on the header are performed while looking for a
 * header-only digest or signature to verify the blob. If found,
 * the digest or signature is verified.
 *
 * @param ts		transaction set
 * @param uh		unloaded header blob
 * @param uc		no. of bytes in blob (or 0 to disable)
 * @retval *msg		signature verification msg
 * @return		RPMRC_OK/RPMRC_NOTFOUND/RPMRC_FAIL
 */
rpmRC headerCheck(rpmts ts, const void * uh, size_t uc, const char ** msg)
{
    pgpDig dig;
    char buf[8*BUFSIZ];
    int_32 * ei = (int_32 *) uh;
/*@-boundsread@*/
    int_32 il = ntohl(ei[0]);
    int_32 dl = ntohl(ei[1]);
/*@-castexpose@*/
    entryInfo pe = (entryInfo) &ei[2];
/*@=castexpose@*/
/*@=boundsread@*/
    int_32 ildl[2];
    int_32 pvlen = sizeof(ildl) + (il * sizeof(*pe)) + dl;
    unsigned char * dataStart = (unsigned char *) (pe + il);
    indexEntry entry = memset(alloca(sizeof(*entry)), 0, sizeof(*entry));
    entryInfo info = memset(alloca(sizeof(*info)), 0, sizeof(*info));
    const void * sig = NULL;
    unsigned char * b;
    rpmVSFlags vsflags = rpmtsVSFlags(ts);
    int siglen = 0;
    int blen;
    size_t nb;
    int_32 ril = 0;
    unsigned char * regionEnd = NULL;
    rpmRC rc = RPMRC_FAIL;	/* assume failure */
    int xx;
    int i;
    static int hclvl;

    hclvl++;
/*@-boundswrite@*/
    buf[0] = '\0';
/*@=boundswrite@*/

    /* Is the blob the right size? */
    if (uc > 0 && pvlen != uc) {
	(void) snprintf(buf, sizeof(buf),
		_("blob size(%d): BAD, 8 + 16 * il(%d) + dl(%d)\n"),
		(int)uc, (int)il, (int)dl);
	goto exit;
    }

    /* Check (and convert) the 1st tag element. */
    xx = headerVerifyInfo(1, dl, pe, &entry->info, 0);
    if (xx != -1) {
	(void) snprintf(buf, sizeof(buf),
		_("tag[%d]: BAD, tag %d type %d offset %d count %d\n"),
		0, entry->info.tag, entry->info.type,
		entry->info.offset, entry->info.count);
	goto exit;
    }

    /* Is there an immutable header region tag? */
/*@-sizeoftype@*/
    if (!(entry->info.tag == RPMTAG_HEADERIMMUTABLE
       && entry->info.type == RPM_BIN_TYPE
       && entry->info.count == REGION_TAG_COUNT))
    {
	rc = RPMRC_NOTFOUND;
	goto exit;
    }
/*@=sizeoftype@*/

    /* Is the offset within the data area? */
    if (entry->info.offset >= dl) {
	(void) snprintf(buf, sizeof(buf),
		_("region offset: BAD, tag %d type %d offset %d count %d\n"),
		entry->info.tag, entry->info.type,
		entry->info.offset, entry->info.count);
	goto exit;
    }

    /* Is there an immutable header region tag trailer? */
    regionEnd = dataStart + entry->info.offset;
/*@-sizeoftype@*/
/*@-bounds@*/
    (void) memcpy(info, regionEnd, REGION_TAG_COUNT);
/*@=bounds@*/
    regionEnd += REGION_TAG_COUNT;

    xx = headerVerifyInfo(1, dl, info, &entry->info, 1);
    if (xx != -1 ||
	!(entry->info.tag == RPMTAG_HEADERIMMUTABLE
       && entry->info.type == RPM_BIN_TYPE
       && entry->info.count == REGION_TAG_COUNT))
    {
	(void) snprintf(buf, sizeof(buf),
		_("region trailer: BAD, tag %d type %d offset %d count %d\n"),
		entry->info.tag, entry->info.type,
		entry->info.offset, entry->info.count);
	goto exit;
    }
/*@=sizeoftype@*/
/*@-boundswrite@*/
    memset(info, 0, sizeof(*info));
/*@=boundswrite@*/

    /* Is the no. of tags in the region less than the total no. of tags? */
    ril = entry->info.offset/sizeof(*pe);
    if ((entry->info.offset % sizeof(*pe)) || ril > il) {
	(void) snprintf(buf, sizeof(buf),
		_("region size: BAD, ril(%d) > il(%d)\n"), ril, il);
	goto exit;
    }

    /* Find a header-only digest/signature tag. */
    for (i = ril; i < il; i++) {
	xx = headerVerifyInfo(1, dl, pe+i, &entry->info, 0);
	if (xx != -1) {
	    (void) snprintf(buf, sizeof(buf),
		_("tag[%d]: BAD, tag %d type %d offset %d count %d\n"),
		i, entry->info.tag, entry->info.type,
		entry->info.offset, entry->info.count);
	    goto exit;
	}

	switch (entry->info.tag) {
	case RPMTAG_SHA1HEADER:
	    if (vsflags & RPMVSF_NOSHA1HEADER)
		/*@switchbreak@*/ break;
	    blen = 0;
/*@-boundsread@*/
	    for (b = dataStart + entry->info.offset; *b != '\0'; b++) {
		if (strchr("0123456789abcdefABCDEF", *b) == NULL)
		    /*@innerbreak@*/ break;
		blen++;
	    }
	    if (entry->info.type != RPM_STRING_TYPE || *b != '\0' || blen != 40)
	    {
		(void) snprintf(buf, sizeof(buf), _("hdr SHA1: BAD, not hex\n"));
		goto exit;
	    }
/*@=boundsread@*/
	    if (info->tag == 0) {
/*@-boundswrite@*/
		*info = entry->info;	/* structure assignment */
/*@=boundswrite@*/
		siglen = blen + 1;
	    }
	    /*@switchbreak@*/ break;
	case RPMTAG_RSAHEADER:
	    if (vsflags & RPMVSF_NORSAHEADER)
		/*@switchbreak@*/ break;
	    if (entry->info.type != RPM_BIN_TYPE) {
		(void) snprintf(buf, sizeof(buf), _("hdr RSA: BAD, not binary\n"));
		goto exit;
	    }
/*@-boundswrite@*/
	    *info = entry->info;	/* structure assignment */
/*@=boundswrite@*/
	    siglen = info->count;
	    /*@switchbreak@*/ break;
	case RPMTAG_DSAHEADER:
	    if (vsflags & RPMVSF_NODSAHEADER)
		/*@switchbreak@*/ break;
	    if (entry->info.type != RPM_BIN_TYPE) {
		(void) snprintf(buf, sizeof(buf), _("hdr DSA: BAD, not binary\n"));
		goto exit;
	    }
/*@-boundswrite@*/
	    *info = entry->info;	/* structure assignment */
/*@=boundswrite@*/
	    siglen = info->count;
	    /*@switchbreak@*/ break;
	default:
	    /*@switchbreak@*/ break;
	}
    }
    rc = RPMRC_NOTFOUND;

exit:
    /* Return determined RPMRC_OK/RPMRC_FAIL conditions. */
    if (rc != RPMRC_NOTFOUND) {
/*@-boundswrite@*/
	buf[sizeof(buf)-1] = '\0';
	if (msg) *msg = xstrdup(buf);
/*@=boundswrite@*/
	hclvl--;
	return rc;
    }

    /* If no header-only digest/signature, then do simple sanity check. */
    if (info->tag == 0) {
verifyinfo_exit:
	xx = headerVerifyInfo(ril-1, dl, pe+1, &entry->info, 0);
	if (xx != -1) {
	    (void) snprintf(buf, sizeof(buf),
		_("tag[%d]: BAD, tag %d type %d offset %d count %d\n"),
		xx+1, entry->info.tag, entry->info.type,
		entry->info.offset, entry->info.count);
	    rc = RPMRC_FAIL;
	} else {
	    (void) snprintf(buf, sizeof(buf), "Header sanity check: OK\n");
	    rc = RPMRC_OK;
	}
/*@-boundswrite@*/
	buf[sizeof(buf)-1] = '\0';
	if (msg) *msg = xstrdup(buf);
/*@=boundswrite@*/
	hclvl--;
	return rc;
    }

    /* Verify header-only digest/signature. */
    dig = rpmtsDig(ts);
    if (dig == NULL)
	goto verifyinfo_exit;
    dig->nbytes = 0;

/*@-boundsread@*/
    sig = memcpy(xmalloc(siglen), dataStart + info->offset, siglen);
/*@=boundsread@*/
    (void) rpmtsSetSig(ts, info->tag, info->type, sig, info->count);

    switch (info->tag) {
    case RPMTAG_RSAHEADER:
	/* Parse the parameters from the OpenPGP packets that will be needed. */
	xx = pgpPrtPkts(sig, info->count, dig, (_print_pkts & rpmIsDebug()));
	if (dig->signature.version != 3 && dig->signature.version != 4) {
	    rpmMessage(RPMMESS_ERROR,
		_("skipping header with unverifiable V%u signature\n"),
		dig->signature.version);
	    rpmtsCleanDig(ts);
	    rc = RPMRC_FAIL;
	    goto exit;
	}

	ildl[0] = htonl(ril);
	ildl[1] = (regionEnd - dataStart);
	ildl[1] = htonl(ildl[1]);

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DIGEST), 0);
	dig->hdrmd5ctx = rpmDigestInit(dig->signature.hash_algo, RPMDIGEST_NONE);

	b = NULL; nb = 0;
	(void) headerGetMagic(NULL, &b, &nb);
	if (b && nb > 0) {
	    (void) rpmDigestUpdate(dig->hdrmd5ctx, b, nb);
	    dig->nbytes += nb;
	}

	b = (unsigned char *) ildl;
	nb = sizeof(ildl);
        (void) rpmDigestUpdate(dig->hdrmd5ctx, b, nb);
        dig->nbytes += nb;

	b = (unsigned char *) pe;
	nb = (htonl(ildl[0]) * sizeof(*pe));
        (void) rpmDigestUpdate(dig->hdrmd5ctx, b, nb);
        dig->nbytes += nb;

	b = (unsigned char *) dataStart;
	nb = htonl(ildl[1]);
        (void) rpmDigestUpdate(dig->hdrmd5ctx, b, nb);
        dig->nbytes += nb;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DIGEST), dig->nbytes);

	break;
    case RPMTAG_DSAHEADER:
	/* Parse the parameters from the OpenPGP packets that will be needed. */
	xx = pgpPrtPkts(sig, info->count, dig, (_print_pkts & rpmIsDebug()));
	if (dig->signature.version != 3 && dig->signature.version != 4) {
	    rpmMessage(RPMMESS_ERROR,
		_("skipping header with unverifiable V%u signature\n"),
		dig->signature.version);
	    rpmtsCleanDig(ts);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	/*@fallthrough@*/
    case RPMTAG_SHA1HEADER:
/*@-boundswrite@*/
	ildl[0] = htonl(ril);
	ildl[1] = (regionEnd - dataStart);
	ildl[1] = htonl(ildl[1]);
/*@=boundswrite@*/

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DIGEST), 0);
	dig->hdrsha1ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);

	b = NULL; nb = 0;
	(void) headerGetMagic(NULL, &b, &nb);
	if (b && nb > 0) {
	    (void) rpmDigestUpdate(dig->hdrsha1ctx, b, nb);
	    dig->nbytes += nb;
	}

	b = (unsigned char *) ildl;
	nb = sizeof(ildl);
        (void) rpmDigestUpdate(dig->hdrsha1ctx, b, nb);
        dig->nbytes += nb;

	b = (unsigned char *) pe;
	nb = (htonl(ildl[0]) * sizeof(*pe));
        (void) rpmDigestUpdate(dig->hdrsha1ctx, b, nb);
        dig->nbytes += nb;

	b = (unsigned char *) dataStart;
	nb = htonl(ildl[1]);
        (void) rpmDigestUpdate(dig->hdrsha1ctx, b, nb);
        dig->nbytes += nb;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DIGEST), dig->nbytes);

	break;
    default:
	sig = _free(sig);
	break;
    }

/*@-boundswrite@*/
    buf[0] = '\0';
/*@=boundswrite@*/
    rc = rpmVerifySignature(ts, buf);

/*@-boundswrite@*/
    buf[sizeof(buf)-1] = '\0';
    if (msg) *msg = xstrdup(buf);
/*@=boundswrite@*/

    /* XXX headerCheck can recurse, free info only at top level. */
    if (hclvl == 1)
	rpmtsCleanDig(ts);
    if (info->tag == RPMTAG_SHA1HEADER)
	sig = _free(sig);
    hclvl--;
    return rc;
}

rpmRC rpmReadHeader(rpmts ts, void * _fd, Header *hdrp, const char ** msg)
{
    FD_t fd = _fd;
    char buf[BUFSIZ];
    int_32 block[4];
    int_32 il;
    int_32 dl;
    int_32 * ei = NULL;
    size_t uc;
    unsigned char * b;
    size_t nb;
    Header h = NULL;
    const char * origin = NULL;
    rpmRC rc = RPMRC_FAIL;		/* assume failure */
    int xx;

/*@-boundswrite@*/
    buf[0] = '\0';

    if (hdrp)
	*hdrp = NULL;
    if (msg)
	*msg = NULL;
/*@=boundswrite@*/

    memset(block, 0, sizeof(block));
    if ((xx = timedRead(fd, (char *)block, sizeof(block))) != sizeof(block)) {
	(void) snprintf(buf, sizeof(buf),
		_("hdr size(%d): BAD, read returned %d\n"), (int)sizeof(block), xx);
	goto exit;
    }

    b = NULL;
    nb = 0;
    (void) headerGetMagic(NULL, &b, &nb);
    if (memcmp(block, b, nb)) {
	(void) snprintf(buf, sizeof(buf), _("hdr magic: BAD\n"));
	goto exit;
    }

/*@-boundsread@*/
    il = ntohl(block[2]);
/*@=boundsread@*/
    if (hdrchkTags(il)) {
	(void) snprintf(buf, sizeof(buf),
		_("hdr tags: BAD, no. of tags(%d) out of range\n"), il);

	goto exit;
    }
/*@-boundsread@*/
    dl = ntohl(block[3]);
/*@=boundsread@*/
    if (hdrchkData(dl)) {
	(void) snprintf(buf, sizeof(buf),
		_("hdr data: BAD, no. of bytes(%d) out of range\n"), dl);
	goto exit;
    }

/*@-sizeoftype@*/
    nb = (il * sizeof(struct entryInfo_s)) + dl;
/*@=sizeoftype@*/
    uc = sizeof(il) + sizeof(dl) + nb;
    ei = xmalloc(uc);
/*@-bounds@*/
    ei[0] = block[2];
    ei[1] = block[3];
    if ((xx = timedRead(fd, (char *)&ei[2], nb)) != nb) {
	(void) snprintf(buf, sizeof(buf),
		_("hdr blob(%u): BAD, read returned %d\n"), (unsigned)nb, xx);
	goto exit;
    }
/*@=bounds@*/

    /* Sanity check header tags */
    rc = headerCheck(ts, ei, uc, msg);
    if (rc != RPMRC_OK)
	goto exit;

    /* OK, blob looks sane, load the header. */
    h = headerLoad(ei);
    if (h == NULL) {
	(void) snprintf(buf, sizeof(buf), _("hdr load: BAD\n"));
        goto exit;
    }
    h->flags |= HEADERFLAG_ALLOCATED;
    ei = NULL;	/* XXX will be freed with header */

    /* Save the opened path as the header origin. */
    origin = fdGetOPath(fd);
    if (origin != NULL)
	(void) headerSetOrigin(h, origin);
    
exit:
/*@-boundswrite@*/
    if (hdrp && h && rc == RPMRC_OK)
	*hdrp = headerLink(h);
/*@=boundswrite@*/
    ei = _free(ei);
    h = headerFree(h);

/*@-boundswrite@*/
    if (msg != NULL && *msg == NULL && buf[0] != '\0') {
	buf[sizeof(buf)-1] = '\0';
	*msg = xstrdup(buf);
    }
/*@=boundswrite@*/

    return rc;
}

/*@-bounds@*/	/* LCL: segfault */
rpmRC rpmReadPackageFile(rpmts ts, void * _fd, const char * fn, Header * hdrp)
{
    FD_t fd = _fd;
    pgpDig dig;
    char buf[8*BUFSIZ];
    ssize_t count;
    Header sigh = NULL;
    int_32 sigtag;
    int_32 sigtype;
    const void * sig;
    int_32 siglen;
    rpmtsOpX opx;
    size_t nb;
    Header h = NULL;
    const char * msg = NULL;
    rpmVSFlags vsflags;
    rpmRC rc = RPMRC_FAIL;	/* assume failure */
    rpmop opsave = memset(alloca(sizeof(*opsave)), 0, sizeof(*opsave));
    int xx;
    int i;

    if (hdrp) *hdrp = NULL;

#ifdef	DYING
    {	struct stat st;
/*@-boundswrite@*/
	memset(&st, 0, sizeof(st));
/*@=boundswrite@*/
	(void) fstat(Fileno(fd), &st);
	/* if fd points to a socket, pipe, etc, st.st_size is *always* zero */
	if (S_ISREG(st.st_mode) && st.st_size < sizeof(*l)) {
	    rc = RPMRC_NOTFOUND;
	    goto exit;
	}
    }
#endif

    /* Snapshot current I/O counters (cached persistent I/O reuses counters) */
    (void) rpmswAdd(opsave, fdstat_op(fd, FDSTAT_READ));

if (!_nolead) {
    const char item[] = "Lead";
    rc = rpmpkgRead(item, fd, NULL, &msg);
    switch (rc) {
    default:
	rpmError(RPMERR_READLEAD, "%s: %s\n", fn, msg);
	/*@fallthrough@*/
    case RPMRC_NOTFOUND:
	msg = _free(msg);
	goto exit;
	/*@notreached@*/ break;
    case RPMRC_OK:
	break;
    }
    msg = _free(msg);
}

if (!_nosigh) {
    /* Read the signature header. */
    msg = NULL;
    rc = rpmReadSignature(ts, fd, &sigh, &msg);
    switch (rc) {
    default:
	rpmError(RPMERR_SIGGEN, _("%s: rpmReadSignature failed: %s"), fn,
		(msg && *msg ? msg : "\n"));
	msg = _free(msg);
	goto exit;
	/*@notreached@*/ break;
    case RPMRC_OK:
	if (sigh == NULL) {
	    rpmError(RPMERR_SIGGEN, _("%s: No signature available\n"), fn);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	break;
    }
    msg = _free(msg);
}

#define	_chk(_mask)	(sigtag == 0 && !(vsflags & (_mask)))

    /*
     * Figger the most effective available signature.
     * Prefer signatures over digests, then header-only over header+payload.
     * DSA will be preferred over RSA if both exist because tested first.
     * Note that NEEDPAYLOAD prevents header+payload signatures and digests.
     */
    sigtag = 0;
    opx = 0;
    vsflags = rpmtsVSFlags(ts);
    if (_chk(RPMVSF_NODSAHEADER) && headerIsEntry(sigh, RPMSIGTAG_DSA)) {
	sigtag = RPMSIGTAG_DSA;
    } else
    if (_chk(RPMVSF_NORSAHEADER) && headerIsEntry(sigh, RPMSIGTAG_RSA)) {
	sigtag = RPMSIGTAG_RSA;
    } else
#if defined(SUPPORT_RPMV3_VERIFY_DSA)
    if (_chk(RPMVSF_NODSA|RPMVSF_NEEDPAYLOAD) &&
	headerIsEntry(sigh, RPMSIGTAG_GPG))
    {
	sigtag = RPMSIGTAG_GPG;
	fdInitDigest(fd, PGPHASHALGO_SHA1, 0);
	opx = RPMTS_OP_SIGNATURE;
    } else
#endif
#if defined(SUPPORT_RPMV3_VERIFY_RSA)
    if (_chk(RPMVSF_NORSA|RPMVSF_NEEDPAYLOAD) &&
	headerIsEntry(sigh, RPMSIGTAG_PGP))
    {
	sigtag = RPMSIGTAG_PGP;
	fdInitDigest(fd, PGPHASHALGO_MD5, 0);
	opx = RPMTS_OP_SIGNATURE;
    } else
#endif
    if (_chk(RPMVSF_NOSHA1HEADER) && headerIsEntry(sigh, RPMSIGTAG_SHA1)) {
	sigtag = RPMSIGTAG_SHA1;
    } else
    if (_chk(RPMVSF_NOMD5|RPMVSF_NEEDPAYLOAD) &&
	headerIsEntry(sigh, RPMSIGTAG_MD5))
    {
	sigtag = RPMSIGTAG_MD5;
	fdInitDigest(fd, PGPHASHALGO_MD5, 0);
	opx = RPMTS_OP_DIGEST;
    }

    /* Read the metadata, computing digest(s) on the fly. */
    h = NULL;
    msg = NULL;

    /* XXX stats will include header i/o and setup overhead. */
    /* XXX repackaged packages have appended tags, legacy dig/sig check fails */
    if (opx > 0)
	(void) rpmswEnter(rpmtsOp(ts, opx), 0);
/*@-type@*/	/* XXX arrow access of non-pointer (FDSTAT_t) */
    nb = -fd->stats->ops[FDSTAT_READ].bytes;
    rc = rpmReadHeader(ts, fd, &h, &msg);
    nb += fd->stats->ops[FDSTAT_READ].bytes;
/*@=type@*/
    if (opx > 0)
	(void) rpmswExit(rpmtsOp(ts, opx), nb);

    if (rc != RPMRC_OK || h == NULL) {
	rpmError(RPMERR_FREAD, _("%s: headerRead failed: %s"), fn,
		(msg && *msg ? msg : "\n"));
	msg = _free(msg);
	goto exit;
    }
    msg = _free(msg);

    /* Any digests or signatures to check? */
    if (sigtag == 0) {
	rc = RPMRC_OK;
	goto exit;
    }

    dig = rpmtsDig(ts);
    if (dig == NULL) {
	rc = RPMRC_FAIL;
	goto exit;
    }
    dig->nbytes = 0;

    /* Retrieve the tag parameters from the signature header. */
    sig = NULL;
    xx = headerGetEntry(sigh, sigtag, &sigtype, (void **) &sig, &siglen);
    if (sig == NULL) {
	rc = RPMRC_FAIL;
	goto exit;
    }
    (void) rpmtsSetSig(ts, sigtag, sigtype, sig, siglen);

    switch (sigtag) {
    case RPMSIGTAG_RSA:
	/* Parse the parameters from the OpenPGP packets that will be needed. */
	xx = pgpPrtPkts(sig, siglen, dig, (_print_pkts & rpmIsDebug()));
	if (dig->signature.version != 3 && dig->signature.version != 4) {
	    rpmMessage(RPMMESS_ERROR,
		_("skipping package %s with unverifiable V%u signature\n"),
		fn, dig->signature.version);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
    {	void * uh = NULL;
	int_32 uht;
	int_32 uhc;
	unsigned char * hmagic = NULL;
	size_t nmagic = 0;

	if (!headerGetEntry(h, RPMTAG_HEADERIMMUTABLE, &uht, &uh, &uhc))
	    break;
	(void) headerGetMagic(NULL, &hmagic, &nmagic);
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DIGEST), 0);
	dig->hdrmd5ctx = rpmDigestInit(dig->signature.hash_algo, RPMDIGEST_NONE);
	if (hmagic && nmagic > 0) {
	    (void) rpmDigestUpdate(dig->hdrmd5ctx, hmagic, nmagic);
	    dig->nbytes += nmagic;
	}
	(void) rpmDigestUpdate(dig->hdrmd5ctx, uh, uhc);
	dig->nbytes += uhc;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DIGEST), dig->nbytes);
	rpmtsOp(ts, RPMTS_OP_DIGEST)->count--;	/* XXX one too many */
	uh = headerFreeData(uh, uht);
    }	break;
    case RPMSIGTAG_DSA:
	/* Parse the parameters from the OpenPGP packets that will be needed. */
	xx = pgpPrtPkts(sig, siglen, dig, (_print_pkts & rpmIsDebug()));
	if (dig->signature.version != 3 && dig->signature.version != 4) {
	    rpmMessage(RPMMESS_ERROR,
		_("skipping package %s with unverifiable V%u signature\n"), 
		fn, dig->signature.version);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	/*@fallthrough@*/
    case RPMSIGTAG_SHA1:
    {	void * uh = NULL;
	int_32 uht;
	int_32 uhc;
	unsigned char * hmagic = NULL;
	size_t nmagic = 0;

	if (!headerGetEntry(h, RPMTAG_HEADERIMMUTABLE, &uht, &uh, &uhc))
	    break;
	(void) headerGetMagic(NULL, &hmagic, &nmagic);
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DIGEST), 0);
	dig->hdrsha1ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);
	if (hmagic && nmagic > 0) {
	    (void) rpmDigestUpdate(dig->hdrsha1ctx, hmagic, nmagic);
	    dig->nbytes += nmagic;
	}
	(void) rpmDigestUpdate(dig->hdrsha1ctx, uh, uhc);
	dig->nbytes += uhc;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DIGEST), dig->nbytes);
	if (sigtag == RPMSIGTAG_SHA1)
	    rpmtsOp(ts, RPMTS_OP_DIGEST)->count--;	/* XXX one too many */
	uh = headerFreeData(uh, uht);
    }	break;
#if defined(SUPPORT_RPMV3_VERIFY_DSA) || defined(SUPPORT_RPMV3_VERIFY_RSA)
#if defined(SUPPORT_RPMV3_VERIFY_DSA)
    case RPMSIGTAG_GPG:
#endif
#if defined(SUPPORT_RPMV3_VERIFY_RSA)
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
#endif
	/* Parse the parameters from the OpenPGP packets that will be needed. */
	xx = pgpPrtPkts(sig, siglen, dig, (_print_pkts & rpmIsDebug()));

	if (dig->signature.version != 3 && dig->signature.version != 4) {
	    rpmMessage(RPMMESS_ERROR,
		_("skipping package %s with unverifiable V%u signature\n"),
		fn, dig->signature.version);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	/*@fallthrough@*/
#endif
    case RPMSIGTAG_MD5:
	/* Legacy signatures need the compressed payload in the digest too. */
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DIGEST), 0);
	while ((count = Fread(buf, sizeof(buf[0]), sizeof(buf), fd)) > 0)
	    dig->nbytes += count;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DIGEST), dig->nbytes);
	rpmtsOp(ts, RPMTS_OP_DIGEST)->count--;	/* XXX one too many */
	dig->nbytes += nb;	/* XXX include size of header blob. */
	if (count < 0) {
	    rpmError(RPMERR_FREAD, _("%s: Fread failed: %s\n"),
					fn, Fstrerror(fd));
	    rc = RPMRC_FAIL;
	    goto exit;
	}

	/* XXX Steal the digest-in-progress from the file handle. */
	for (i = fd->ndigests - 1; i >= 0; i--) {
	    FDDIGEST_t fddig = fd->digests + i;
	    if (fddig->hashctx != NULL)
	    switch (fddig->hashalgo) {
	    case PGPHASHALGO_MD5:
		dig->md5ctx = fddig->hashctx;
		fddig->hashctx = NULL;
		/*@switchbreak@*/ break;
	    case PGPHASHALGO_SHA1:
	    case PGPHASHALGO_RIPEMD160:
#if defined(HAVE_BEECRYPT_API_H)
	    case PGPHASHALGO_SHA256:
	    case PGPHASHALGO_SHA384:
	    case PGPHASHALGO_SHA512:
#endif
		dig->sha1ctx = fddig->hashctx;
		fddig->hashctx = NULL;
		/*@switchbreak@*/ break;
	    default:
		/*@switchbreak@*/ break;
	    }
	}
	break;
    }

/** @todo Implement disable/enable/warn/error/anal policy. */

/*@-boundswrite@*/
    buf[0] = '\0';
/*@=boundswrite@*/
    rc = rpmVerifySignature(ts, buf);
    switch (rc) {
    case RPMRC_OK:		/* Signature is OK. */
	rpmMessage(RPMMESS_DEBUG, "%s: %s", fn, buf);
	break;
    case RPMRC_NOTTRUSTED:	/* Signature is OK, but key is not trusted. */
    case RPMRC_NOKEY:		/* Public key is unavailable. */
	/* XXX Print NOKEY/NOTTRUSTED warning only once. */
    {	int lvl = (rpmtsStashKeyid(ts) ? RPMMESS_DEBUG : RPMMESS_WARNING);
	rpmMessage(lvl, "%s: %s", fn, buf);
    }	break;
    case RPMRC_NOTFOUND:	/* Signature is unknown type. */
	rpmMessage(RPMMESS_WARNING, "%s: %s", fn, buf);
	break;
    default:
    case RPMRC_FAIL:		/* Signature does not verify. */
	rpmMessage(RPMMESS_ERROR, "%s: %s", fn, buf);
	break;
    }

exit:
    if (rc != RPMRC_FAIL && h != NULL && hdrp != NULL) {

	/* Append (and remap) signature tags to the metadata. */
	headerMergeLegacySigs(h, sigh);

	/* Bump reference count for return. */
/*@-boundswrite@*/
	*hdrp = headerLink(h);
/*@=boundswrite@*/
    }
    h = headerFree(h);

    /* Accumulate time reading package header. */
    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_READHDR),
		fdstat_op(fd, FDSTAT_READ));
    (void) rpmswSub(rpmtsOp(ts, RPMTS_OP_READHDR),
		opsave);

    rpmtsCleanDig(ts);
    sigh = rpmFreeSignature(sigh);
    return rc;
}
/*@=bounds@*/

/** \ingroup rpmbuild
 * \file build/pack.c
 *  Assemble components of an RPM package.
 */

#include "system.h"

#include <rpmio_internal.h>	/* XXX fdGetFp, fdInitDigest, fdFiniDigest */
#define	_RPMFI_INTERNAL		/* XXX fi->fsm */
#define	_RPMEVR_INTERNAL	/* XXX RPMSENSE_ANY */
#define _RPMTAG_INTERNAL
#include <rpmbuild.h>
#include "signature.h"		/* XXX rpmTempFile */

#include "cpio.h"
#include "fsm.h"

#include "buildio.h"

#include "signature.h"
#include <pkgio.h>
#include "debug.h"

/*@access rpmts @*/
/*@access rpmfi @*/	/* compared with NULL */
/*@access Header @*/	/* compared with NULL */
/*@access FD_t @*/	/* compared with NULL */
/*@access StringBuf @*/	/* compared with NULL */
/*@access CSA_t @*/

/**
 */
static inline int genSourceRpmName(Spec spec)
	/*@modifies spec->sourceRpmName, spec->packages->header @*/
{
    if (spec->sourceRpmName == NULL) {
	const char *N, *V, *R;
	char fileName[BUFSIZ];

	(void) headerNEVRA(spec->packages->header, &N, NULL, &V, &R, NULL);
	(void) snprintf(fileName, sizeof(fileName), "%s-%s-%s.%ssrc.rpm",
			N, V, R, spec->noSource ? "no" : "");
	fileName[sizeof(fileName)-1] = '\0';
	N = _free(N);
	V = _free(V);
	R = _free(R);
	spec->sourceRpmName = xstrdup(fileName);
    }

    return 0;
}

/**
 * @todo Create transaction set *much* earlier.
 */
static rpmRC cpio_doio(FD_t fdo, /*@unused@*/ Header h, CSA_t csa,
		const char * payload_format, const char * fmodeMacro)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies fdo, csa, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    rpmts ts = rpmtsCreate();
    rpmfi fi = csa->cpioList;
    const char *failedFile = NULL;
    FD_t cfd;
    rpmRC rc = RPMRC_OK;
    int xx;

    {	const char *fmode = rpmExpand(fmodeMacro, NULL);
	if (!(fmode && fmode[0] == 'w'))
	    fmode = xstrdup("w9.gzdio");
	/*@-nullpass@*/
	(void) Fflush(fdo);
	cfd = Fdopen(fdDup(Fileno(fdo)), fmode);
	/*@=nullpass@*/
	fmode = _free(fmode);
    }
    if (cfd == NULL)
	return RPMRC_FAIL;

    xx = fsmSetup(fi->fsm, FSM_PKGBUILD, payload_format, ts, fi, cfd,
		&csa->cpioArchiveSize, &failedFile);
    if (xx)
	rc = RPMRC_FAIL;
    (void) Fclose(cfd);
    xx = fsmTeardown(fi->fsm);
    if (rc == RPMRC_OK && xx) rc = RPMRC_FAIL;

    if (rc) {
	if (failedFile)
	    rpmlog(RPMLOG_ERR, _("create archive failed on file %s: %s\n"),
		failedFile, cpioStrerror(rc));
	else
	    rpmlog(RPMLOG_ERR, _("create archive failed: %s\n"),
		cpioStrerror(rc));
      rc = RPMRC_FAIL;
    }

    failedFile = _free(failedFile);
    ts = rpmtsFree(ts);

    return rc;
}

/**
 */
static rpmRC cpio_copy(FD_t fdo, CSA_t csa)
	/*@globals fileSystem, internalState @*/
	/*@modifies fdo, csa, fileSystem, internalState @*/
{
    char buf[BUFSIZ];
    size_t nb;

    while((nb = Fread(buf, sizeof(buf[0]), sizeof(buf), csa->cpioFdIn)) > 0) {
	if (Fwrite(buf, sizeof(buf[0]), nb, fdo) != nb) {
	    rpmlog(RPMLOG_ERR, _("cpio_copy write failed: %s\n"),
			Fstrerror(fdo));
	    return RPMRC_FAIL;
	}
	csa->cpioArchiveSize += nb;
    }
    if (Ferror(csa->cpioFdIn)) {
	rpmlog(RPMLOG_ERR, _("cpio_copy read failed: %s\n"),
		Fstrerror(csa->cpioFdIn));
	return RPMRC_FAIL;
    }
    return RPMRC_OK;
}

/**
 */
static /*@only@*/ /*@null@*/ StringBuf addFileToTagAux(Spec spec,
		const char * file, /*@only@*/ StringBuf sb)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
    char buf[BUFSIZ];
    const char * fn = buf;
    FILE * f;
    FD_t fd;

    fn = rpmGetPath("%{_builddir}/%{?buildsubdir:%{buildsubdir}/}", file, NULL);

    fd = Fopen(fn, "r.fdio");
    if (fn != buf) fn = _free(fn);
    if (fd == NULL || Ferror(fd)) {
	sb = freeStringBuf(sb);
	return NULL;
    }
    /*@-type@*/ /* FIX: cast? */
    if ((f = fdGetFp(fd)) != NULL)
    /*@=type@*/
    while (fgets(buf, sizeof(buf), f)) {
	/* XXX display fn in error msg */
	if (expandMacros(spec, spec->macros, buf, sizeof(buf))) {
	    rpmlog(RPMLOG_ERR, _("line: %s\n"), buf);
	    sb = freeStringBuf(sb);
	    break;
	}
	appendStringBuf(sb, buf);
    }
    (void) Fclose(fd);

    return sb;
}

/**
 */
static int addFileToTag(Spec spec, const char * file, Header h, rpmTag tag)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    StringBuf sb = newStringBuf();
    int xx;

    he->tag = tag;
    xx = headerGet(h, he, 0);
    if (xx) {
	appendLineStringBuf(sb, he->p.str);
	xx = headerDel(h, he, 0);
    }
    he->p.ptr = _free(he->p.ptr);

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;
    
    he->tag = tag;
    he->t = RPM_STRING_TYPE;
    he->p.str = getStringBuf(sb);
    he->c = 1;
    xx = headerPut(h, he, 0);

    sb = freeStringBuf(sb);
    return 0;
}

/**
 */
static int addFileToArrayTag(Spec spec, const char *file, Header h, int tag)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem, internalState  @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    StringBuf sb = newStringBuf();
    const char *s;
    int xx;

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;

    s = getStringBuf(sb);

    he->tag = tag;
    he->t = RPM_STRING_ARRAY_TYPE;
    he->p.argv = &s;
    he->c = 1;
    he->append = 1;
    xx = headerPut(h, he, 0);
    he->append = 0;

    sb = freeStringBuf(sb);
    return 0;
}

rpmRC processScriptFiles(Spec spec, Package pkg)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies pkg->header, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    struct TriggerFileEntry *p;
    int xx;
    
    if (pkg->preInFile) {
	if (addFileToTag(spec, pkg->preInFile, pkg->header, RPMTAG_PREIN)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PreIn file: %s\n"), pkg->preInFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->preUnFile) {
	if (addFileToTag(spec, pkg->preUnFile, pkg->header, RPMTAG_PREUN)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PreUn file: %s\n"), pkg->preUnFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->preTransFile) {
	if (addFileToTag(spec, pkg->preTransFile, pkg->header, RPMTAG_PRETRANS)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PreIn file: %s\n"), pkg->preTransFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->postInFile) {
	if (addFileToTag(spec, pkg->postInFile, pkg->header, RPMTAG_POSTIN)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PostIn file: %s\n"), pkg->postInFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->postUnFile) {
	if (addFileToTag(spec, pkg->postUnFile, pkg->header, RPMTAG_POSTUN)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PostUn file: %s\n"), pkg->postUnFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->postTransFile) {
	if (addFileToTag(spec, pkg->postTransFile, pkg->header, RPMTAG_POSTTRANS)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open PostUn file: %s\n"), pkg->postTransFile);
	    return RPMRC_FAIL;
	}
    }
    if (pkg->verifyFile) {
	if (addFileToTag(spec, pkg->verifyFile, pkg->header,
			 RPMTAG_VERIFYSCRIPT)) {
	    rpmlog(RPMLOG_ERR,
		     _("Could not open VerifyScript file: %s\n"), pkg->verifyFile);
	    return RPMRC_FAIL;
	}
    }

    if (pkg->sanityCheckFile) {
        if (addFileToTag(spec, pkg->sanityCheckFile, pkg->header, RPMTAG_SANITYCHECK)) {
            rpmlog(RPMLOG_ERR, _("Could not open Test file: %s\n"), pkg->sanityCheckFile);
            return RPMRC_FAIL;
        }
    }

    for (p = pkg->triggerFiles; p != NULL; p = p->next) {
	he->tag = RPMTAG_TRIGGERSCRIPTPROG;
	he->t = RPM_STRING_ARRAY_TYPE;
	he->p.argv = (const char **)&p->prog;	/* XXX NOCAST */
	he->c = 1;
	he->append = 1;
	xx = headerPut(pkg->header, he, 0);
	he->append = 0;
	if (p->script) {
	    he->tag = RPMTAG_TRIGGERSCRIPTS;
	    he->t = RPM_STRING_ARRAY_TYPE;
	    he->p.argv = (const char **)&p->script;	/* XXX NOCAST */
	    he->c = 1;
	    he->append = 1;
	    xx = headerPut(pkg->header, he, 0);
	    he->append = 0;
	} else if (p->fileName) {
	    if (addFileToArrayTag(spec, p->fileName, pkg->header,
				  RPMTAG_TRIGGERSCRIPTS)) {
		rpmlog(RPMLOG_ERR,
			 _("Could not open Trigger script file: %s\n"),
			 p->fileName);
		return RPMRC_FAIL;
	    }
	} else {
	    static const char *bull = "";
	    he->tag = RPMTAG_TRIGGERSCRIPTS;
	    he->t = RPM_STRING_ARRAY_TYPE;
	    he->p.argv = &bull;
	    he->c = 1;
	    he->append = 1;
	    xx = headerPut(pkg->header, he, 0);
	    he->append = 0;
	}
    }

    return RPMRC_OK;
}

#if defined(DEAD)
int readRPM(const char *fileName, Spec *specp, void * l,
		Header *sigs, CSA_t csa)
{
    const char * msg = "";
    FD_t fdi;
    Spec spec;
    rpmRC rc;

    fdi = (fileName != NULL)
	? Fopen(fileName, "r.fdio")
	: fdDup(STDIN_FILENO);

    if (fdi == NULL || Ferror(fdi)) {
	rpmlog(RPMLOG_ERR, _("readRPM: open %s: %s\n"),
		(fileName ? fileName : "<stdin>"),
		Fstrerror(fdi));
	if (fdi) (void) Fclose(fdi);
	return RPMRC_FAIL;
    }

    {	const char item[] = "Lead";
	size_t nl = rpmpkgSizeof(item, NULL);

	if (nl == 0) {
	    rc = RPMRC_FAIL;
	    msg = xstrdup("item size is zero");
	} else {
	    l = xcalloc(1, nl);		/* XXX memory leak */
	    msg = NULL;
	    rc = rpmpkgRead(item, fdi, l, &msg);
	}
    }

    if (rc != RPMRC_OK) {
	rpmlog(RPMLOG_ERR, _("readRPM: read %s: %s\n"),
		(fileName ? fileName : "<stdin>"), msg);
	msg = _free(msg);
	return RPMRC_FAIL;
    }
    msg = _free(msg);
    /*@=sizeoftype@*/

    /* XXX FIXME: EPIPE on <stdin> */
    if (Fseek(fdi, 0, SEEK_SET) == -1) {
	rpmlog(RPMLOG_ERR, _("%s: Fseek failed: %s\n"),
			(fileName ? fileName : "<stdin>"), Fstrerror(fdi));
	return RPMRC_FAIL;
    }

    /* Reallocate build data structures */
    spec = newSpec();
    spec->packages = newPackage(spec);

    /* XXX the header just allocated will be allocated again */
    spec->packages->header = headerFree(spec->packages->header);

    /* Read the rpm lead, signatures, and header */
    {	rpmts ts = rpmtsCreate();

	/* XXX W2DO? pass fileName? */
	/*@-mustmod@*/      /* LCL: segfault */
	rc = rpmReadPackageFile(ts, fdi, "readRPM",
			 &spec->packages->header);
	/*@=mustmod@*/

	ts = rpmtsFree(ts);

	if (sigs) *sigs = NULL;			/* XXX HACK */
    }

    switch (rc) {
    case RPMRC_OK:
    case RPMRC_NOKEY:
    case RPMRC_NOTTRUSTED:
	break;
    case RPMRC_NOTFOUND:
	rpmlog(RPMLOG_ERR, _("readRPM: %s is not an RPM package\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMRC_FAIL;
    case RPMRC_FAIL:
    default:
	rpmlog(RPMLOG_ERR, _("readRPM: reading header from %s\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMRC_FAIL;
	/*@notreached@*/ break;
    }

    if (specp)
	*specp = spec;
    else
	spec = freeSpec(spec);

    if (csa != NULL)
	csa->cpioFdIn = fdi;
    else
	(void) Fclose(fdi);

    return 0;
}
#endif

#if defined(DEAD)
#define	RPMPKGVERSION_MIN	30004
#define	RPMPKGVERSION_MAX	40003
/*@unchecked@*/
static int rpmpkg_version = -1;

static int rpmLeadVersion(void)
	/*@globals rpmpkg_version, rpmGlobalMacroContext, h_errno @*/
	/*@modifies rpmpkg_version, rpmGlobalMacroContext @*/
{
    int rpmlead_version;

    /* Intitialize packaging version from macro configuration. */
    if (rpmpkg_version < 0) {
	rpmpkg_version = rpmExpandNumeric("%{_package_version}");
	if (rpmpkg_version < RPMPKGVERSION_MIN)
	    rpmpkg_version = RPMPKGVERSION_MIN;
	if (rpmpkg_version > RPMPKGVERSION_MAX)
	    rpmpkg_version = RPMPKGVERSION_MAX;
    }

    rpmlead_version = rpmpkg_version / 10000;
    /* XXX silly sanity check. */
    if (rpmlead_version < 3 || rpmlead_version > 4)
	rpmlead_version = 3;
    return rpmlead_version;
}
#endif

void providePackageNVR(Header h)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    const char *N, *V, *R;
    uint32_t E;
    int gotE;
    const char *pEVR;
    char *p;
    uint32_t pFlags = RPMSENSE_EQUAL;
    const char ** provides = NULL;
    const char ** providesEVR = NULL;
    uint32_t * provideFlags = NULL;
    int providesCount;
    int i, xx;
    int bingo = 1;

    /* Generate provides for this package N-V-R. */
    xx = headerNEVRA(h, &N, NULL, &V, &R, NULL);
    if (!(N && V && R))
	return;
    pEVR = p = alloca(21 + strlen(V) + 1 + strlen(R) + 1);
    *p = '\0';
    he->tag = RPMTAG_EPOCH;
    gotE = headerGet(h, he, 0);
    E = (he->p.ui32p ? he->p.ui32p[0] : 0);
    he->p.ptr = _free(he->p.ptr);
    if (gotE) {
	sprintf(p, "%d:", E);
	p += strlen(p);
    }
    (void) stpcpy( stpcpy( stpcpy(p, V) , "-") , R);
    V = _free(V);
    R = _free(R);

    /*
     * Rpm prior to 3.0.3 does not have versioned provides.
     * If no provides at all are available, we can just add.
     */
    he->tag = RPMTAG_PROVIDENAME;
    xx = headerGet(h, he, 0);
    provides = he->p.argv;
    providesCount = he->c;
    if (!xx)
	goto exit;

    /*
     * Otherwise, fill in entries on legacy packages.
     */
    he->tag = RPMTAG_PROVIDEVERSION;
    xx = headerGet(h, he, 0);
    providesEVR = he->p.argv;
    if (!xx) {
	for (i = 0; i < providesCount; i++) {
	    static const char * vdummy = "";
	    static rpmsenseFlags fdummy = RPMSENSE_ANY;

	    he->tag = RPMTAG_PROVIDEVERSION;
	    he->t = RPM_STRING_ARRAY_TYPE;
	    he->p.argv = &vdummy;
	    he->c = 1;
	    he->append = 1;
	    xx = headerPut(h, he, 0);
	    he->append = 0;

	    he->tag = RPMTAG_PROVIDEFLAGS;
	    he->t = RPM_UINT32_TYPE;
	    he->p.ui32p = (uint32_t *) &fdummy;
	    he->c = 1;
	    he->append = 1;
	    xx = headerPut(h, he, 0);
	    he->append = 0;
	}
	goto exit;
    }

    he->tag = RPMTAG_PROVIDEFLAGS;
    xx = headerGet(h, he, 0);
    provideFlags = he->p.ui32p;

    /*@-nullderef@*/	/* LCL: providesEVR is not NULL */
    if (provides && providesEVR && provideFlags)
    for (i = 0; i < providesCount; i++) {
        if (!(provides[i] && providesEVR[i]))
            continue;
	if (!(provideFlags[i] == RPMSENSE_EQUAL &&
	    !strcmp(N, provides[i]) && !strcmp(pEVR, providesEVR[i])))
	    continue;
	bingo = 0;
	break;
    }
    /*@=nullderef@*/

exit:
    provides = _free(provides);
    providesEVR = _free(providesEVR);
    provideFlags = _free(provideFlags);

    if (bingo) {
	he->tag = RPMTAG_PROVIDENAME;
	he->t = RPM_STRING_ARRAY_TYPE;
	he->p.argv = &N;
	he->c = 1;
	he->append = 1;
	xx = headerPut(h, he, 0);
	he->append = 0;

	he->tag = RPMTAG_PROVIDEVERSION;
	he->t = RPM_STRING_ARRAY_TYPE;
	he->p.argv = &pEVR;
	he->c = 1;
	he->append = 1;
	xx = headerPut(h, he, 0);
	he->append = 0;

	he->tag = RPMTAG_PROVIDEFLAGS;
	he->t = RPM_UINT32_TYPE;
	he->p.ui32p = &pFlags;
	he->c = 1;
	he->append = 1;
	xx = headerPut(h, he, 0);
	he->append = 0;
    }
    N = _free(N);
}

rpmRC writeRPM(Header *hdrp, unsigned char ** pkgidp, const char *fileName,
		CSA_t csa, char *passPhrase, const char **cookie)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    FD_t fd = NULL;
    FD_t ifd = NULL;
    uint32_t count;
    uint32_t sigtag;
    const char * sigtarget;
    const char * rpmio_flags = NULL;
    const char * payload_format = NULL;
    const char * SHA1 = NULL;
    const char * msg = NULL;
    char *s;
    char buf[BUFSIZ];
    Header h;
    Header sigh = NULL;
    int addsig = 0;
    int isSource;
    rpmRC rc = RPMRC_OK;
    int xx;

    /* Transfer header reference form *hdrp to h. */
    h = headerLink(*hdrp);
    *hdrp = headerFree(*hdrp);

    if (pkgidp)
	*pkgidp = NULL;

    /* Save payload information */
    isSource =
	(headerIsEntry(h, RPMTAG_SOURCERPM) == 0 &&
	 headerIsEntry(h, RPMTAG_ARCH) != 0);
    if (isSource) {
	payload_format = rpmExpand("%{?_source_payload_format}", NULL);
	rpmio_flags = rpmExpand("%{?_source_payload}", NULL);
    } else {
	payload_format = rpmExpand("%{?_binary_payload_format}", NULL);
	rpmio_flags = rpmExpand("%{?_binary_payload}", NULL);
    }

    if (!(payload_format && *payload_format)) {
	payload_format = _free(payload_format);
	payload_format = xstrdup("cpio");
    }
    if (!(rpmio_flags && *rpmio_flags)) {
	rpmio_flags = _free(rpmio_flags);
	rpmio_flags = xstrdup("w9.gzdio");
    }
    s = strchr(rpmio_flags, '.');
    if (s) {

	if (payload_format) {
	    if (!strcmp(payload_format, "tar")
	     || !strcmp(payload_format, "ustar")) {
		/* XXX addition to header is too late to be displayed/sorted. */
		/* Add prereq on rpm version that understands tar payloads */
		(void) rpmlibNeedsFeature(h, "PayloadIsUstar", "4.4.4-1");
	    }

	    he->tag = RPMTAG_PAYLOADFORMAT;
	    he->t = RPM_STRING_TYPE;
	    he->p.str = payload_format;
	    he->c = 1;
	    xx = headerPut(h, he, 0);
	}

	/* XXX addition to header is too late to be displayed/sorted. */
	if (s[1] == 'g' && s[2] == 'z') {
	    he->tag = RPMTAG_PAYLOADCOMPRESSOR;
	    he->t = RPM_STRING_TYPE;
	    he->p.str = "gzip";
	    he->c = 1;
	    xx = headerPut(h, he, 0);
	} else if (s[1] == 'b' && s[2] == 'z') {
	    he->tag = RPMTAG_PAYLOADCOMPRESSOR;
	    he->t = RPM_STRING_TYPE;
	    he->p.str = "bzip2";
	    he->c = 1;
	    xx = headerPut(h, he, 0);
	} else if (s[1] == 'l' && s[2] == 'z') {
	    he->tag = RPMTAG_PAYLOADCOMPRESSOR;
	    he->t = RPM_STRING_TYPE;
	    he->p.str = "lzma";
	    he->c = 1;
	    xx = headerPut(h, he, 0);
	    (void) rpmlibNeedsFeature(h, "PayloadIsLzma", "4.4.6-1");
	}
	strcpy(buf, rpmio_flags);
	buf[s - rpmio_flags] = '\0';

	he->tag = RPMTAG_PAYLOADFLAGS;
	he->t = RPM_STRING_TYPE;
	he->p.str = buf+1;
	he->c = 1;
	xx = headerPut(h, he, 0);
    }

    /* Create and add the cookie */
    if (cookie) {
	sprintf(buf, "%s %u", buildHost(), (unsigned) (*getBuildTime()));
	*cookie = xstrdup(buf);		/* XXX memory leak */
	he->tag = RPMTAG_COOKIE;
	he->t = RPM_STRING_TYPE;
	he->p.str = *cookie;
	he->c = 1;
	xx = headerPut(h, he, 0);
    }
    
    /* Reallocate the header into one contiguous region. */
    h = headerReload(h, RPMTAG_HEADERIMMUTABLE);
    if (h == NULL) {	/* XXX can't happen */
	rpmlog(RPMLOG_ERR, _("Unable to create immutable header region.\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }
    /* Re-reference reallocated header. */
    *hdrp = headerLink(h);

    /*
     * Write the header+archive into a temp file so that the size of
     * archive (after compression) can be added to the header.
     */
    sigtarget = NULL;
    if (rpmTempFile(NULL, &sigtarget, &fd)) {
	rpmlog(RPMLOG_ERR, _("Unable to open temp file.\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Write the header to a temp file, computing header SHA1 on the fly. */
    fdInitDigest(fd, PGPHASHALGO_SHA1, 0);
    {	const char item[] = "Header";
	msg = NULL;
	rc = rpmpkgWrite(item, fd, h, &msg);
	if (rc != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, "%s: %s: %s\n", sigtarget, item,
		(msg && *msg ? msg : "write failed\n"));
	    msg = _free(msg);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	msg = _free(msg);
	(void) Fflush(fd);
    }
    fdFiniDigest(fd, PGPHASHALGO_SHA1, &SHA1, NULL, 1);

    /* Append the payload to the temp file. */
    if (csa->cpioList != NULL)
	rc = cpio_doio(fd, h, csa, payload_format, rpmio_flags);
    else if (Fileno(csa->cpioFdIn) >= 0)
	rc = cpio_copy(fd, csa);
    else
assert(0);

    rpmio_flags = _free(rpmio_flags);
    payload_format = _free(payload_format);
    if (rc != RPMRC_OK)
	goto exit;

    (void) Fclose(fd);
    fd = NULL;
    (void) Unlink(fileName);

    /* Generate the signature */
    (void) fflush(stdout);
    sigh = headerNew();
    (void) rpmAddSignature(sigh, sigtarget, RPMSIGTAG_SIZE, passPhrase);
    (void) rpmAddSignature(sigh, sigtarget, RPMSIGTAG_MD5, passPhrase);

    sigtag = RPMSIGTAG_GPG;
    addsig = (passPhrase && passPhrase[0]);

    if (addsig) {
	rpmlog(RPMLOG_NOTICE, _("Generating signature: %d\n"), sigtag);
	(void) rpmAddSignature(sigh, sigtarget, sigtag, passPhrase);
    }
    
    if (SHA1) {
	he->tag = (rpmTag) RPMSIGTAG_SHA1;
	he->t = RPM_STRING_TYPE;
	he->p.str = SHA1;
	he->c = 1;
	xx = headerPut(sigh, he, 0);
	SHA1 = _free(SHA1);
    }

    {	uint32_t payloadSize = csa->cpioArchiveSize;
	he->tag = (rpmTag) RPMSIGTAG_PAYLOADSIZE;
	he->t = RPM_UINT32_TYPE;
	he->p.ui32p = &payloadSize;
	he->c = 1;
	xx = headerPut(sigh, he, 0);
    }

    /* Reallocate the signature into one contiguous region. */
    sigh = headerReload(sigh, RPMTAG_HEADERSIGNATURES);
    if (sigh == NULL) {	/* XXX can't happen */
	rpmlog(RPMLOG_ERR, _("Unable to reload signature header.\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Open the output file */
    fd = Fopen(fileName, "w.fdio");
    if (fd == NULL || Ferror(fd)) {
	rpmlog(RPMLOG_ERR, _("Could not open %s: %s\n"),
		fileName, Fstrerror(fd));
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Write the lead section into the package. */
    {	const char item[] = "Lead";
	size_t nl = rpmpkgSizeof(item, NULL);

	msg = NULL;
	if (nl == 0)
	    rc = RPMRC_FAIL;
	else {
	    void * l = memset(alloca(nl), 0, nl);
	    const char *N, *V, *R;
	    (void) headerNEVRA(h, &N, NULL, &V, &R, NULL);
	    sprintf(buf, "%s-%s-%s", N, V, R);
	    N = _free(N);
	    V = _free(V);
	    R = _free(R);
	    msg = buf;
	    rc = rpmpkgWrite(item, fd, l, &msg);
	}

	if (rc != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, _("Unable to write package: %s\n"),
		 Fstrerror(fd));
	    rc = RPMRC_FAIL;
	    goto exit;
	}
    }

    /* Write the signature section into the package. */
    {	const char item[] = "Signature";

	msg = NULL;
	rc = rpmpkgWrite(item, fd, sigh, &msg);
	if (rc != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, "%s: %s: %s\n", fileName, item,
                (msg && *msg ? msg : "write failed\n"));
	    msg = _free(msg);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	msg = _free(msg);
    }

    /* Append the header and archive */
    ifd = Fopen(sigtarget, "r.fdio");
    if (ifd == NULL || Ferror(ifd)) {
	rpmlog(RPMLOG_ERR, _("Unable to open sigtarget %s: %s\n"),
		sigtarget, Fstrerror(ifd));
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Add signatures to header, and write header into the package. */
    {	const char item[] = "Header";
	Header nh = NULL;

	msg = NULL;
	rc = rpmpkgRead(item, ifd, &nh, &msg);
	if (rc != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, "%s: %s: %s\n", sigtarget, item,
                (msg && *msg ? msg : "read failed\n"));
	    msg = _free(msg);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	msg = _free(msg);

#ifdef	NOTYET
	(void) headerMergeLegacySigs(nh, sigh);
#endif

	msg = NULL;
	rc = rpmpkgWrite(item, fd, nh, &msg);
	nh = headerFree(nh);
	if (rc != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, "%s: %s: %s\n", fileName, item,
                (msg && *msg ? msg : "write failed\n"));
	    msg = _free(msg);
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	msg = _free(msg);
    }
	
    /* Write the payload into the package. */
    while ((xx = Fread(buf, sizeof(buf[0]), sizeof(buf), ifd)) > 0) {
	if (xx <= -1 || Ferror(ifd)) {
	    rpmlog(RPMLOG_ERR, _("Unable to read payload from %s: %s\n"),
		     sigtarget, Fstrerror(ifd));
	    rc = RPMRC_FAIL;
	    goto exit;
	}
	count = (uint32_t) xx;
	xx = Fwrite(buf, sizeof(buf[0]), count, fd);
	if ((uint32_t)xx != count || Ferror(fd)) {
	    rpmlog(RPMLOG_ERR, _("Unable to write payload to %s: %s\n"),
		     fileName, Fstrerror(fd));
	    rc = RPMRC_FAIL;
	    goto exit;
	}
    }
    rc = RPMRC_OK;

exit:
    SHA1 = _free(SHA1);
    h = headerFree(h);

    /* XXX Fish the pkgid out of the signature header. */
    if (sigh != NULL && pkgidp != NULL) {
	he->tag = (rpmTag) RPMSIGTAG_MD5;
	xx = headerGet(sigh, he, 0);
	if (he->t == RPM_BIN_TYPE && he->p.ptr != NULL && he->c == 16)
	    *pkgidp = he->p.ui8p;		/* XXX memory leak */
    }

    sigh = headerFree(sigh);
    if (ifd) {
	(void) Fclose(ifd);
	ifd = NULL;
    }
    if (fd) {
	(void) Fclose(fd);
	fd = NULL;
    }
    if (sigtarget) {
	(void) Unlink(sigtarget);
	sigtarget = _free(sigtarget);
    }

    if (rc == RPMRC_OK)
	rpmlog(RPMLOG_NOTICE, _("Wrote: %s\n"), fileName);
    else
	(void) Unlink(fileName);

    return rc;
}

static int rpmlibMarkers(Header h)
	/*@modifies h @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    uint32_t val;
    int xx;

    he->tag = RPMTAG_RPMVERSION;
    he->t = RPM_STRING_TYPE;
    he->p.str = VERSION;
    he->c = 1;
    xx = headerPut(h, he, 0);

if (!(_rpmbuildFlags & 4)) {
    val = rpmlibTimestamp();
    he->tag = RPMTAG_RPMLIBTIMESTAMP;
    he->t = RPM_UINT32_TYPE;
    he->p.ui32p = &val;
    he->c = 1;
    xx = headerPut(h, he, 0);

    val = rpmlibVendor();
    he->tag = RPMTAG_RPMLIBVENDOR;
    he->t = RPM_UINT32_TYPE;
    he->p.ui32p = &val;
    he->c = 1;
    xx = headerPut(h, he, 0);

    val = rpmlibVersion();
    he->tag = RPMTAG_RPMLIBVERSION;
    he->t = RPM_UINT32_TYPE;
    he->p.ui32p = &val;
    he->c = 1;
    xx = headerPut(h, he, 0);
}

    he->tag = RPMTAG_BUILDHOST;
    he->t = RPM_STRING_TYPE;
    he->p.str = buildHost();
    he->c = 1;
    xx = headerPut(h, he, 0);

    he->tag = RPMTAG_BUILDTIME;
    he->t = RPM_UINT32_TYPE;
    he->p.ui32p = getBuildTime();
    he->c = 1;
    xx = headerPut(h, he, 0);

    return 0;
}

/*@unchecked@*/
static uint32_t copyTags[] = {
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    0
};

rpmRC packageBinaries(Spec spec)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    const char *errorString;
    Package pkg;
    rpmRC rc;
    int xx;

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	const char *fn;

	if (pkg->fileList == NULL)
	    continue;

	if (spec->cookie) {
	    he->tag = RPMTAG_COOKIE;
	    he->t = RPM_STRING_TYPE;
	    he->p.str = spec->cookie;
	    he->c = 1;
	    xx = headerPut(pkg->header, he, 0);
	}

	/* Copy changelog from src rpm */
	headerCopyTags(spec->packages->header, pkg->header, copyTags);

	/* Add rpmlib markers for tracking. */
	(void) rpmlibMarkers(pkg->header);
	
	he->tag = RPMTAG_OPTFLAGS;
	he->t = RPM_STRING_TYPE;
	he->p.str = rpmExpand("%{optflags}", NULL);
	he->c = 1;
	xx = headerPut(pkg->header, he, 0);
	he->p.ptr = _free(he->p.ptr);

	(void) genSourceRpmName(spec);
	he->tag = RPMTAG_SOURCERPM;
	he->t = RPM_STRING_TYPE;
	he->p.str = spec->sourceRpmName;
	he->c = 1;
	xx = headerPut(pkg->header, he, 0);

if (!(_rpmbuildFlags & 4)) {
	if (spec->sourcePkgId != NULL) {
	    he->tag = RPMTAG_SOURCEPKGID;
	    he->t = RPM_BIN_TYPE;
	    he->p.ptr = spec->sourcePkgId;
	    he->c = 16;
	    xx = headerPut(pkg->header, he, 0);
	}
}
	
	{   const char *binFormat = rpmGetPath("%{_rpmfilename}", NULL);
	    char *binRpm, *binDir;
	    binRpm = headerSprintf(pkg->header, binFormat, NULL,
			       rpmHeaderFormats, &errorString);
	    binFormat = _free(binFormat);
	    if (binRpm == NULL) {
		he->tag = RPMTAG_NVRA;
		xx = headerGet(pkg->header, he, 0);
		rpmlog(RPMLOG_ERR, _("Could not generate output "
		     "filename for package %s: %s\n"), he->p.str, errorString);
		he->p.ptr = _free(he->p.ptr);
		return RPMRC_FAIL;
	    }
	    fn = rpmGetPath("%{_rpmdir}/", binRpm, NULL);
	    if ((binDir = strchr(binRpm, '/')) != NULL) {
		struct stat st;
		const char *dn;
		*binDir = '\0';
		dn = rpmGetPath("%{_rpmdir}/", binRpm, NULL);
		if (Stat(dn, &st) < 0) {
		    switch(errno) {
		    case  ENOENT:
			if (Mkdir(dn, 0755) == 0)
			    /*@switchbreak@*/ break;
			/*@fallthrough@*/
		    default:
			rpmlog(RPMLOG_ERR,_("cannot create %s: %s\n"),
			    dn, strerror(errno));
			/*@switchbreak@*/ break;
		    }
		}
		dn = _free(dn);
	    }
	    binRpm = _free(binRpm);
	}

	memset(csa, 0, sizeof(*csa));
	csa->cpioArchiveSize = 0;
	/*@-type@*/ /* LCL: function typedefs */
	csa->cpioFdIn = fdNew("init (packageBinaries)");
/*@-assignexpose -newreftrans@*/
	csa->cpioList = rpmfiLink(pkg->cpioList, "packageBinaries");
/*@=assignexpose =newreftrans@*/
assert(csa->cpioList != NULL);

	rc = writeRPM(&pkg->header, NULL, fn,
		    csa, spec->passPhrase, NULL);

/*@-onlytrans@*/
	csa->cpioList->te = _free(csa->cpioList->te);	/* XXX memory leak */
/*@=onlytrans@*/
	csa->cpioList = rpmfiFree(csa->cpioList);
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageBinaries)");
	/*@=type@*/
	fn = _free(fn);
	if (rc)
	    return rc;
    }
    
    return RPMRC_OK;
}

rpmRC packageSources(Spec spec)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    rpmRC rc;
    int xx;
#if defined(RPM_VENDOR_OPENPKG) /* backward-compat-rpmtag-sourcepackage */
    uint32_t val;
#endif

    /* Add rpmlib markers for tracking. */
    (void) rpmlibMarkers(spec->sourceHeader);

#if defined(RPM_VENDOR_OPENPKG) /* backward-compat-rpmtag-sourcepackage */
    /* Mark package as a SRPM for backward compatibility with RPM < 4.4.6 */
    he->tag = RPMTAG_SOURCEPACKAGE;
    he->t = RPM_UINT32_TYPE;
    val = 1;
    he->p.ui32p = &val;
    he->c = 1;
    xx = headerPut(spec->sourceHeader, he, 0);
#endif
	
    (void) genSourceRpmName(spec);

    {	const char ** av = NULL;
	(void)rpmGetMacroEntries(NULL, NULL, 1, &av);
	if (av != NULL && av[0] != NULL) {
	    he->tag = RPMTAG_BUILDMACROS;
	    he->t = RPM_STRING_ARRAY_TYPE;
	    he->p.argv = av;
	    he->c = argvCount(av);
	    xx = headerPut(spec->sourceHeader, he, 0);
	}
	av = argvFree(av);
    }

    spec->cookie = _free(spec->cookie);
    
    /* XXX this should be %_srpmdir */
    {	const char *fn = rpmGetPath("%{_srcrpmdir}/", spec->sourceRpmName,NULL);

	memset(csa, 0, sizeof(*csa));
	csa->cpioArchiveSize = 0;
	/*@-type@*/ /* LCL: function typedefs */
	csa->cpioFdIn = fdNew("init (packageSources)");
/*@-assignexpose -newreftrans@*/
	csa->cpioList = rpmfiLink(spec->sourceCpioList, "packageSources");
/*@=assignexpose =newreftrans@*/
assert(csa->cpioList != NULL);

	spec->sourcePkgId = NULL;
	rc = writeRPM(&spec->sourceHeader, &spec->sourcePkgId, fn,
		csa, spec->passPhrase, &(spec->cookie));

/*@-onlytrans@*/
	csa->cpioList->te = _free(csa->cpioList->te);	/* XXX memory leak */
/*@=onlytrans@*/
	csa->cpioList = rpmfiFree(csa->cpioList);
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageSources)");
	/*@=type@*/
	fn = _free(fn);
    }

    return (rc ? RPMRC_FAIL : RPMRC_OK);
}

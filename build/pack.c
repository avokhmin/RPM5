/** \ingroup rpmbuild
 * \file build/pack.c
 *  Assemble components of an RPM package.
 */

#include "system.h"

#include <rpmio_internal.h>
#define	_RPMEVR_INTERNAL	/* XXX RPMSENSE_ANY */
#include <rpmbuild.h>

#include "rpmps.h"

#include "cpio.h"
#include "fsm.h"
#include "psm.h"

#define	_RPMFI_INTERNAL		/* XXX fi->fsm */
#include "rpmfi.h"
#include "rpmts.h"

#include "buildio.h"

#include "signature.h"
#include "rpmlead.h"
#include "debug.h"

/*@access rpmts @*/
/*@access rpmfi @*/	/* compared with NULL */
/*@access Header @*/	/* compared with NULL */
/*@access FD_t @*/	/* compared with NULL */
/*@access StringBuf @*/	/* compared with NULL */
/*@access CSA_t @*/

extern int _nolead;	/* disable writing lead. */
extern int _nosigh;	/* disable writing signature header. */

/**
 */
static inline int genSourceRpmName(Spec spec)
	/*@modifies spec->sourceRpmName @*/
{
    if (spec->sourceRpmName == NULL) {
	const char *name, *version, *release;
	char fileName[BUFSIZ];

	(void) headerNVR(spec->packages->header, &name, &version, &release);
	sprintf(fileName, "%s-%s-%s.%ssrc.rpm", name, version, release,
	    spec->noSource ? "no" : "");
	spec->sourceRpmName = xstrdup(fileName);
    }

    return 0;
}

/**
 * @todo Create transaction set *much* earlier.
 */
static int cpio_doio(FD_t fdo, /*@unused@*/ Header h, CSA_t csa,
		const char * payload_format, const char * fmodeMacro)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies fdo, csa, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    rpmts ts = rpmtsCreate();
    rpmfi fi = csa->cpioList;
    const char *failedFile = NULL;
    FD_t cfd;
    int rc, ec;

/*@-boundsread@*/
    {	const char *fmode = rpmExpand(fmodeMacro, NULL);
	if (!(fmode && fmode[0] == 'w'))
	    fmode = xstrdup("w9.gzdio");
	/*@-nullpass@*/
	(void) Fflush(fdo);
	cfd = Fdopen(fdDup(Fileno(fdo)), fmode);
	/*@=nullpass@*/
	fmode = _free(fmode);
    }
/*@=boundsread@*/
    if (cfd == NULL)
	return 1;

    rc = fsmSetup(fi->fsm, FSM_PKGBUILD, payload_format, ts, fi, cfd,
		&csa->cpioArchiveSize, &failedFile);
    (void) Fclose(cfd);
    ec = fsmTeardown(fi->fsm);
    if (!rc) rc = ec;

    if (rc) {
	if (failedFile)
	    rpmError(RPMERR_CPIO, _("create archive failed on file %s: %s\n"),
		failedFile, cpioStrerror(rc));
	else
	    rpmError(RPMERR_CPIO, _("create archive failed: %s\n"),
		cpioStrerror(rc));
      rc = 1;
    }

    failedFile = _free(failedFile);
    ts = rpmtsFree(ts);

    return rc;
}

/**
 */
static int cpio_copy(FD_t fdo, CSA_t csa)
	/*@globals fileSystem, internalState @*/
	/*@modifies fdo, csa, fileSystem, internalState @*/
{
    char buf[BUFSIZ];
    size_t nb;

    while((nb = Fread(buf, sizeof(buf[0]), sizeof(buf), csa->cpioFdIn)) > 0) {
	if (Fwrite(buf, sizeof(buf[0]), nb, fdo) != nb) {
	    rpmError(RPMERR_CPIO, _("cpio_copy write failed: %s\n"),
			Fstrerror(fdo));
	    return 1;
	}
	csa->cpioArchiveSize += nb;
    }
    if (Ferror(csa->cpioFdIn)) {
	rpmError(RPMERR_CPIO, _("cpio_copy read failed: %s\n"),
		Fstrerror(csa->cpioFdIn));
	return 1;
    }
    return 0;
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
	    rpmError(RPMERR_BADSPEC, _("line: %s\n"), buf);
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
static int addFileToTag(Spec spec, const char * file, Header h, int tag)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    StringBuf sb = newStringBuf();
    char *s;

    if (hge(h, tag, NULL, &s, NULL)) {
	appendLineStringBuf(sb, s);
	(void) headerRemoveEntry(h, tag);
    }

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;
    
    (void) headerAddEntry(h, tag, RPM_STRING_TYPE, getStringBuf(sb), 1);

    sb = freeStringBuf(sb);
    return 0;
}

/**
 */
static int addFileToArrayTag(Spec spec, const char *file, Header h, int tag)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem, internalState  @*/
{
    StringBuf sb = newStringBuf();
    char *s;

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;

    s = getStringBuf(sb);
    (void) headerAddOrAppendEntry(h, tag, RPM_STRING_ARRAY_TYPE, &s, 1);

    sb = freeStringBuf(sb);
    return 0;
}

int processScriptFiles(Spec spec, Package pkg)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies pkg->header, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    struct TriggerFileEntry *p;
    
    if (pkg->preInFile) {
	if (addFileToTag(spec, pkg->preInFile, pkg->header, RPMTAG_PREIN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PreIn file: %s\n"), pkg->preInFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->preUnFile) {
	if (addFileToTag(spec, pkg->preUnFile, pkg->header, RPMTAG_PREUN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PreUn file: %s\n"), pkg->preUnFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->preTransFile) {
	if (addFileToTag(spec, pkg->preTransFile, pkg->header, RPMTAG_PRETRANS)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PreIn file: %s\n"), pkg->preTransFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->postInFile) {
	if (addFileToTag(spec, pkg->postInFile, pkg->header, RPMTAG_POSTIN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PostIn file: %s\n"), pkg->postInFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->postUnFile) {
	if (addFileToTag(spec, pkg->postUnFile, pkg->header, RPMTAG_POSTUN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PostUn file: %s\n"), pkg->postUnFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->postTransFile) {
	if (addFileToTag(spec, pkg->postTransFile, pkg->header, RPMTAG_POSTTRANS)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PostUn file: %s\n"), pkg->postTransFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->verifyFile) {
	if (addFileToTag(spec, pkg->verifyFile, pkg->header,
			 RPMTAG_VERIFYSCRIPT)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open VerifyScript file: %s\n"), pkg->verifyFile);
	    return RPMERR_BADFILENAME;
	}
    }

    for (p = pkg->triggerFiles; p != NULL; p = p->next) {
	(void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTPROG,
			       RPM_STRING_ARRAY_TYPE, &(p->prog), 1);
	if (p->script) {
	    (void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTS,
				   RPM_STRING_ARRAY_TYPE, &(p->script), 1);
	} else if (p->fileName) {
	    if (addFileToArrayTag(spec, p->fileName, pkg->header,
				  RPMTAG_TRIGGERSCRIPTS)) {
		rpmError(RPMERR_BADFILENAME,
			 _("Could not open Trigger script file: %s\n"),
			 p->fileName);
		return RPMERR_BADFILENAME;
	    }
	} else {
	    /* This is dumb.  When the header supports NULL string */
	    /* this will go away.                                  */
	    char *bull = "";
	    (void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTS,
				   RPM_STRING_ARRAY_TYPE, &bull, 1);
	}
    }

    return 0;
}

/*@-boundswrite@*/
int readRPM(const char *fileName, Spec *specp, struct rpmlead *lead,
		Header *sigs, CSA_t csa)
{
    FD_t fdi;
    Spec spec;
    rpmRC rc;

    fdi = (fileName != NULL)
	? Fopen(fileName, "r.fdio")
	: fdDup(STDIN_FILENO);

    if (fdi == NULL || Ferror(fdi)) {
	rpmError(RPMERR_BADMAGIC, _("readRPM: open %s: %s\n"),
		(fileName ? fileName : "<stdin>"),
		Fstrerror(fdi));
	if (fdi) (void) Fclose(fdi);
	return RPMERR_BADMAGIC;
    }

    /* Get copy of lead */
    /*@-sizeoftype@*/
    if ((rc = Fread(lead, sizeof(char), sizeof(*lead), fdi)) != sizeof(*lead)) {
	rpmError(RPMERR_BADMAGIC, _("readRPM: read %s: %s\n"),
		(fileName ? fileName : "<stdin>"),
		Fstrerror(fdi));
	return RPMERR_BADMAGIC;
    }
    /*@=sizeoftype@*/

    /* XXX FIXME: EPIPE on <stdin> */
    if (Fseek(fdi, 0, SEEK_SET) == -1) {
	rpmError(RPMERR_FSEEK, _("%s: Fseek failed: %s\n"),
			(fileName ? fileName : "<stdin>"), Fstrerror(fdi));
	return RPMERR_FSEEK;
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
	rpmError(RPMERR_BADMAGIC, _("readRPM: %s is not an RPM package\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMERR_BADMAGIC;
    case RPMRC_FAIL:
    default:
	rpmError(RPMERR_BADMAGIC, _("readRPM: reading header from %s\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMERR_BADMAGIC;
	/*@notreached@*/ break;
    }

    /*@-branchstate@*/
    if (specp)
	*specp = spec;
    else
	spec = freeSpec(spec);
    /*@=branchstate@*/

    if (csa != NULL)
	csa->cpioFdIn = fdi;
    else
	(void) Fclose(fdi);

    return 0;
}
/*@=boundswrite@*/

#ifdef	DYING
/*@unchecked@*/
static unsigned char header_magic[8] = {
        0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};
#endif

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

void providePackageNVR(Header h)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char *name, *version, *release;
    int_32 * epoch;
    const char *pEVR;
    char *p;
    int_32 pFlags = RPMSENSE_EQUAL;
    const char ** provides = NULL;
    const char ** providesEVR = NULL;
    rpmTagType pnt, pvt;
    int_32 * provideFlags = NULL;
    int providesCount;
    int i, xx;
    int bingo = 1;

    /* Generate provides for this package name-version-release. */
    xx = headerNVR(h, &name, &version, &release);
    if (!(name && version && release))
	return;
    pEVR = p = alloca(21 + strlen(version) + 1 + strlen(release) + 1);
    *p = '\0';
    if (hge(h, RPMTAG_EPOCH, NULL, &epoch, NULL)) {
	sprintf(p, "%d:", *epoch);
	while (*p != '\0')
	    p++;
    }
    (void) stpcpy( stpcpy( stpcpy(p, version) , "-") , release);

    /*
     * Rpm prior to 3.0.3 does not have versioned provides.
     * If no provides at all are available, we can just add.
     */
    if (!hge(h, RPMTAG_PROVIDENAME, &pnt, &provides, &providesCount))
	goto exit;

    /*
     * Otherwise, fill in entries on legacy packages.
     */
    if (!hge(h, RPMTAG_PROVIDEVERSION, &pvt, &providesEVR, NULL)) {
	for (i = 0; i < providesCount; i++) {
	    char * vdummy = "";
	    int_32 fdummy = RPMSENSE_ANY;
	    xx = headerAddOrAppendEntry(h, RPMTAG_PROVIDEVERSION, RPM_STRING_ARRAY_TYPE,
			&vdummy, 1);
	    xx = headerAddOrAppendEntry(h, RPMTAG_PROVIDEFLAGS, RPM_INT32_TYPE,
			&fdummy, 1);
	}
	goto exit;
    }

    xx = hge(h, RPMTAG_PROVIDEFLAGS, NULL, &provideFlags, NULL);

    /*@-nullderef@*/	/* LCL: providesEVR is not NULL */
    if (provides && providesEVR && provideFlags)
    for (i = 0; i < providesCount; i++) {
        if (!(provides[i] && providesEVR[i]))
            continue;
	if (!(provideFlags[i] == RPMSENSE_EQUAL &&
	    !strcmp(name, provides[i]) && !strcmp(pEVR, providesEVR[i])))
	    continue;
	bingo = 0;
	break;
    }
    /*@=nullderef@*/

exit:
    provides = hfd(provides, pnt);
    providesEVR = hfd(providesEVR, pvt);

    if (bingo) {
	xx = headerAddOrAppendEntry(h, RPMTAG_PROVIDENAME, RPM_STRING_ARRAY_TYPE,
		&name, 1);
	xx = headerAddOrAppendEntry(h, RPMTAG_PROVIDEFLAGS, RPM_INT32_TYPE,
		&pFlags, 1);
	xx = headerAddOrAppendEntry(h, RPMTAG_PROVIDEVERSION, RPM_STRING_ARRAY_TYPE,
		&pEVR, 1);
    }
}

/*@-boundswrite@*/
int writeRPM(Header *hdrp, unsigned char ** pkgidp, const char *fileName,
		int type, CSA_t csa, char *passPhrase, const char **cookie)
{
    FD_t fd = NULL;
    FD_t ifd = NULL;
    int_32 count, sigtag;
    const char * sigtarget;
    const char * rpmio_flags = NULL;
    const char * payload_format = NULL;
    const char * SHA1 = NULL;
    char *s;
    char buf[BUFSIZ];
    Header h;
    Header sig = NULL;
    int rc = 0;

    /* Transfer header reference form *hdrp to h. */
    h = headerLink(*hdrp);
    *hdrp = headerFree(*hdrp);

    if (pkgidp)
	*pkgidp = NULL;

#ifdef	DYING
    if (Fileno(csa->cpioFdIn) < 0) {
	csa->cpioArchiveSize = 0;
	/* Add a bogus archive size to the Header */
	(void) headerAddEntry(h, RPMTAG_ARCHIVESIZE, RPM_INT32_TYPE,
		&csa->cpioArchiveSize, 1);
    }
#endif

    /* Save payload information */
    /*@-branchstate@*/
    switch(type) {
    case RPMLEAD_SOURCE:
	payload_format = rpmExpand("%{?_source_payload_format}", NULL);
	rpmio_flags = rpmExpand("%{?_source_payload}", NULL);
	break;
    case RPMLEAD_BINARY:
	payload_format = rpmExpand("%{?_binary_payload_format}", NULL);
	rpmio_flags = rpmExpand("%{?_binary_payload}", NULL);
	break;
    }
    /*@=branchstate@*/
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

	    (void) headerAddEntry(h, RPMTAG_PAYLOADFORMAT, RPM_STRING_TYPE,
			payload_format, 1);
	}

	/* XXX addition to header is too late to be displayed/sorted. */
	if (s[1] == 'g' && s[2] == 'z')
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"gzip", 1);
	else if (s[1] == 'b' && s[2] == 'z')
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"bzip2", 1);
	else if (s[1] == 'l' && s[2] == 'z') {
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"lzma", 1);
	    (void) rpmlibNeedsFeature(h, "PayloadIsLzma", "4.4.6-1");
	}
	strcpy(buf, rpmio_flags);
	buf[s - rpmio_flags] = '\0';
	(void) headerAddEntry(h, RPMTAG_PAYLOADFLAGS, RPM_STRING_TYPE, buf+1, 1);
    }

    /* Create and add the cookie */
    if (cookie) {
	sprintf(buf, "%s %d", buildHost(), (int) (*getBuildTime()));
	*cookie = xstrdup(buf);
	(void) headerAddEntry(h, RPMTAG_COOKIE, RPM_STRING_TYPE, *cookie, 1);
    }
    
    /* Reallocate the header into one contiguous region. */
    h = headerReload(h, RPMTAG_HEADERIMMUTABLE);
    if (h == NULL) {	/* XXX can't happen */
	rc = RPMERR_RELOAD;
	rpmError(RPMERR_RELOAD, _("Unable to create immutable header region.\n"));
	goto exit;
    }
    /* Re-reference reallocated header. */
    *hdrp = headerLink(h);

    /*
     * Write the header+archive into a temp file so that the size of
     * archive (after compression) can be added to the header.
     */
    sigtarget = NULL;
    if (makeTempFile(NULL, &sigtarget, &fd)) {
	rc = RPMERR_CREATE;
	rpmError(RPMERR_CREATE, _("Unable to open temp file.\n"));
	goto exit;
    }

    fdInitDigest(fd, PGPHASHALGO_SHA1, 0);
    if (headerWrite(fd, h)) {
	rc = RPMERR_NOSPACE;
	rpmError(RPMERR_NOSPACE, _("Unable to write temp header\n"));
    } else { /* Write the archive and get the size */
	(void) Fflush(fd);
	fdFiniDigest(fd, PGPHASHALGO_SHA1, &SHA1, NULL, 1);
	if (csa->cpioList != NULL) {
	    rc = cpio_doio(fd, h, csa, payload_format, rpmio_flags);
	} else if (Fileno(csa->cpioFdIn) >= 0) {
	    rc = cpio_copy(fd, csa);
	} else {
	    rc = RPMERR_BADARG;
	    rpmError(RPMERR_BADARG, _("Bad CSA data\n"));
	}
    }
    rpmio_flags = _free(rpmio_flags);
    payload_format = _free(payload_format);

    if (rc)
	goto exit;

    (void) Fclose(fd);
    fd = NULL;
    (void) Unlink(fileName);

    if (rc)
	goto exit;

    /* Generate the signature */
    (void) fflush(stdout);
    sig = rpmNewSignature();
    (void) rpmAddSignature(sig, sigtarget, RPMSIGTAG_SIZE, passPhrase);
    (void) rpmAddSignature(sig, sigtarget, RPMSIGTAG_MD5, passPhrase);

#if defined(SUPPORT_PGP_SIGNING)
    sigtag = rpmLookupSignatureType(RPMLOOKUPSIG_QUERY);
#else
    sigtag = 0;
#endif

    if (sigtag > 0) {
	rpmMessage(RPMMESS_NORMAL, _("Generating signature: %d\n"), sigtag);
	(void) rpmAddSignature(sig, sigtarget, sigtag, passPhrase);
    }
    
    if (SHA1) {
	(void) headerAddEntry(sig, RPMSIGTAG_SHA1, RPM_STRING_TYPE, SHA1, 1);
	SHA1 = _free(SHA1);
    }

    {	int_32 payloadSize = csa->cpioArchiveSize;
	(void) headerAddEntry(sig, RPMSIGTAG_PAYLOADSIZE, RPM_INT32_TYPE,
			&payloadSize, 1);
    }

    /* Reallocate the signature into one contiguous region. */
    sig = headerReload(sig, RPMTAG_HEADERSIGNATURES);
    if (sig == NULL) {	/* XXX can't happen */
	rc = RPMERR_RELOAD;
	rpmError(RPMERR_RELOAD, _("Unable to reload signature header.\n"));
	goto exit;
    }

    /* Open the output file */
    fd = Fopen(fileName, "w.fdio");
    if (fd == NULL || Ferror(fd)) {
	rc = RPMERR_CREATE;
	rpmError(RPMERR_CREATE, _("Could not open %s: %s\n"),
		fileName, Fstrerror(fd));
	goto exit;
    }

    /* Write the lead section into the package. */
if (!_nolead)
    {	int archnum = -1;
	int osnum = -1;
	struct rpmlead lead;

	if (Fileno(csa->cpioFdIn) < 0) {
	    /* XXX DIEDIEDIE: legacy values were not 0. */
	    archnum = 0;
	    osnum = 0;
	} else if (csa->lead != NULL) {
	    archnum = csa->lead->archnum;
	    osnum = csa->lead->osnum;
	}

	memset(&lead, 0, sizeof(lead));
	lead.major = rpmLeadVersion();
	lead.minor = 0;
	lead.type = type;
	lead.archnum = archnum;
	lead.osnum = osnum;
	lead.signature_type = RPMSIGTYPE_HEADERSIG;

	{   const char *name, *version, *release;
	    (void) headerNVR(h, &name, &version, &release);
	    sprintf(buf, "%s-%s-%s", name, version, release);
	    strncpy(lead.name, buf, sizeof(lead.name));
	}

	if (writeLead(fd, &lead) != RPMRC_OK) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write package: %s\n"),
		 Fstrerror(fd));
	    goto exit;
	}
    }

    /* Write the signature section into the package. */
if (!_nosigh)
    {
	rc = rpmWriteSignature(fd, sig);
	if (rc)
	    goto exit;
    }

    /* Append the header and archive */
    ifd = Fopen(sigtarget, "r.fdio");
    if (ifd == NULL || Ferror(ifd)) {
	rc = RPMERR_READ;
	rpmError(RPMERR_READ, _("Unable to open sigtarget %s: %s\n"),
		sigtarget, Fstrerror(ifd));
	goto exit;
    }

    /* Add signatures to header, and write header into the package. */
    /* XXX header+payload digests/signatures might be checked again here. */
    {	Header nh = headerRead(ifd);

	if (nh == NULL) {
	    rc = RPMERR_READ;
	    rpmError(RPMERR_READ, _("Unable to read header from %s: %s\n"),
			sigtarget, Fstrerror(ifd));
	    goto exit;
	}

#ifdef	NOTYET
	(void) headerMergeLegacySigs(nh, sig);
#endif

	rc = headerWrite(fd, nh);
	nh = headerFree(nh);

	if (rc) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write header to %s: %s\n"),
			fileName, Fstrerror(fd));
	    goto exit;
	}
    }
	
    /* Write the payload into the package. */
    while ((count = Fread(buf, sizeof(buf[0]), sizeof(buf), ifd)) > 0) {
	if (count == -1) {
	    rc = RPMERR_READ;
	    rpmError(RPMERR_READ, _("Unable to read payload from %s: %s\n"),
		     sigtarget, Fstrerror(ifd));
	    goto exit;
	}
	if (Fwrite(buf, sizeof(buf[0]), count, fd) != count) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write payload to %s: %s\n"),
		     fileName, Fstrerror(fd));
	    goto exit;
	}
    }
    rc = 0;

exit:
    SHA1 = _free(SHA1);
    h = headerFree(h);

    /* XXX Fish the pkgid out of the signature header. */
    if (sig != NULL && pkgidp != NULL) {
	int_32 tagType;
	unsigned char * MD5 = NULL;
	int_32 c;
	int xx;
	xx = headerGetEntry(sig, RPMSIGTAG_MD5, &tagType, &MD5, &c);
	if (tagType == RPM_BIN_TYPE && MD5 != NULL && c == 16)
	    *pkgidp = MD5;
    }

    sig = rpmFreeSignature(sig);
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

    if (rc == 0)
	rpmMessage(RPMMESS_NORMAL, _("Wrote: %s\n"), fileName);
    else
	(void) Unlink(fileName);

    return rc;
}
/*@=boundswrite@*/

/*@unchecked@*/
static int_32 copyTags[] = {
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    0
};

/*@-boundswrite@*/
int packageBinaries(Spec spec)
{
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    int rc;
    const char *errorString;
    Package pkg;

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	const char *fn;

	if (pkg->fileList == NULL)
	    continue;

	if (spec->cookie) {
	    (void) headerAddEntry(pkg->header, RPMTAG_COOKIE,
			   RPM_STRING_TYPE, spec->cookie, 1);
	}

	/* Copy changelog from src rpm */
	headerCopyTags(spec->packages->header, pkg->header, copyTags);
	
	(void) headerAddEntry(pkg->header, RPMTAG_RPMVERSION,
		       RPM_STRING_TYPE, VERSION, 1);
	(void) headerAddEntry(pkg->header, RPMTAG_BUILDHOST,
		       RPM_STRING_TYPE, buildHost(), 1);
	(void) headerAddEntry(pkg->header, RPMTAG_BUILDTIME,
		       RPM_INT32_TYPE, getBuildTime(), 1);

    {	const char * optflags = rpmExpand("%{optflags}", NULL);
	(void) headerAddEntry(pkg->header, RPMTAG_OPTFLAGS, RPM_STRING_TYPE,
			optflags, 1);
	optflags = _free(optflags);
    }

	(void) genSourceRpmName(spec);
	(void) headerAddEntry(pkg->header, RPMTAG_SOURCERPM, RPM_STRING_TYPE,
		       spec->sourceRpmName, 1);
	if (spec->sourcePkgId != NULL) {
	(void) headerAddEntry(pkg->header, RPMTAG_SOURCEPKGID, RPM_BIN_TYPE,
		       spec->sourcePkgId, 16);
	}
	
	{   const char *binFormat = rpmGetPath("%{_rpmfilename}", NULL);
	    char *binRpm, *binDir;
	    binRpm = headerSprintf(pkg->header, binFormat, rpmTagTable,
			       rpmHeaderFormats, &errorString);
	    binFormat = _free(binFormat);
	    if (binRpm == NULL) {
		const char *name;
		(void) headerNVR(pkg->header, &name, NULL, NULL);
		rpmError(RPMERR_BADFILENAME, _("Could not generate output "
		     "filename for package %s: %s\n"), name, errorString);
		return RPMERR_BADFILENAME;
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
			rpmError(RPMERR_BADFILENAME,_("cannot create %s: %s\n"),
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

	rc = writeRPM(&pkg->header, NULL, fn, RPMLEAD_BINARY,
		    csa, spec->passPhrase, NULL);

	csa->cpioList->te = _free(csa->cpioList->te);	/* XXX memory leak */
	csa->cpioList = rpmfiFree(csa->cpioList);
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageBinaries)");
	/*@=type@*/
	fn = _free(fn);
	if (rc)
	    return rc;
    }
    
    return 0;
}
/*@=boundswrite@*/

/*@-boundswrite@*/
int packageSources(Spec spec)
{
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    int rc;

    /* Add some cruft */
    (void) headerAddEntry(spec->sourceHeader, RPMTAG_RPMVERSION,
		   RPM_STRING_TYPE, VERSION, 1);
    (void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDHOST,
		   RPM_STRING_TYPE, buildHost(), 1);
    (void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDTIME,
		   RPM_INT32_TYPE, getBuildTime(), 1);

    (void) genSourceRpmName(spec);

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

	spec->sourcePkgId = NULL;
	rc = writeRPM(&spec->sourceHeader, &spec->sourcePkgId, fn, RPMLEAD_SOURCE,
		csa, spec->passPhrase, &(spec->cookie));

	csa->cpioList->te = _free(csa->cpioList->te);	/* XXX memory leak */
	csa->cpioList = rpmfiFree(csa->cpioList);
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageSources)");
	/*@=type@*/
	fn = _free(fn);
    }
    return rc;
}
/*@=boundswrite@*/

/** \ingroup signature
 * \file rpmdb/signature.c
 */

#include "system.h"
#if defined(HAVE_KEYUTILS_H)
#include <keyutils.h>
#endif

#include <rpmio.h>
#include <rpmurl.h>
#include <rpmcb.h>	/* XXX rpmIsVerbose() */
#define	_RPMPGP_INTERNAL
#include <rpmpgp.h>
#include <rpmtag.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath() */
#include "rpmdb.h"

#include "legacy.h"	/* XXX for mdbinfile() */
#include <pkgio.h>
#include "signature.h"
#include "debug.h"

/*@access FD_t@*/		/* XXX ufdio->read arg1 is void ptr */
/*@access Header@*/		/* XXX compared with NULL */
/*@access DIGEST_CTX@*/		/* XXX compared with NULL */
/*@access pgpDig@*/
/*@access pgpDigParams@*/

int rpmTempFile(const char * prefix, const char ** fnptr, void * fdptr)
{
    const char * tpmacro = "%{?_tmppath}%{!?_tmppath:/var/tmp/}";
    const char * tempfn = NULL;
    const char * tfn = NULL;
    static int _initialized = 0;
    int temput;
    FD_t fd = NULL;
    unsigned int ran;

    if (!prefix) prefix = "";

    /* Create the temp directory if it doesn't already exist. */
    if (!_initialized) {
	_initialized = 1;
	tempfn = rpmGenPath(prefix, tpmacro, NULL);
	if (rpmioMkpath(tempfn, 0755, (uid_t) -1, (gid_t) -1))
	    goto errxit;
    }

    /* XXX should probably use mkstemp here */
    ran = (unsigned) time(NULL);
    srand(ran);
    ran = rand() % 100000;

    /* maybe this should use link/stat? */

    do {
	char tfnbuf[64];
#ifndef	NOTYET
	sprintf(tfnbuf, "rpm-tmp.%u", ran++);
	tempfn = _free(tempfn);
	tempfn = rpmGenPath(prefix, tpmacro, tfnbuf);
#else
	strcpy(tfnbuf, "rpm-tmp.XXXXXX");
	tempfn = _free(tempfn);
	tempfn = rpmGenPath(prefix, tpmacro, mktemp(tfnbuf));
#endif

	temput = urlPath(tempfn, &tfn);
	if (*tfn == '\0') goto errxit;

	switch (temput) {
	case URL_IS_DASH:
	case URL_IS_HKP:
	    goto errxit;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_FTP:
	default:
	    /*@switchbreak@*/ break;
	}

	fd = Fopen(tempfn, "w+x.fdio");
	/* XXX FIXME: errno may not be correct for ufdio */
    } while ((fd == NULL || Ferror(fd)) && errno == EEXIST);

    if (fd == NULL || Ferror(fd)) {
	rpmlog(RPMLOG_ERR, _("error creating temporary file %s\n"), tempfn);
	goto errxit;
    }

    switch(temput) {
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
      {	struct stat sb, sb2;
	if (!stat(tfn, &sb) && S_ISLNK(sb.st_mode)) {
	    rpmlog(RPMLOG_ERR, _("error creating temporary file %s\n"), tfn);
	    goto errxit;
	}

	if (sb.st_nlink != 1) {
	    rpmlog(RPMLOG_ERR, _("error creating temporary file %s\n"), tfn);
	    goto errxit;
	}

	if (fstat(Fileno(fd), &sb2) == 0) {
	    if (sb2.st_ino != sb.st_ino || sb2.st_dev != sb.st_dev) {
		rpmlog(RPMLOG_ERR, _("error creating temporary file %s\n"), tfn);
		goto errxit;
	    }
	}
      }	break;
    default:
	break;
    }

    if (fnptr)
	*fnptr = tempfn;
    else 
	tempfn = _free(tempfn);
    if (fdptr)
	*(FD_t *)fdptr = fd;

    return 0;

errxit:
    tempfn = _free(tempfn);
    if (fnptr)
	*fnptr = NULL;
    /*@-usereleased@*/
    if (fd != NULL) (void) Fclose(fd);
    /*@=usereleased@*/
    return 1;
}


/**
 * Generate GPG signature(s) for a header+payload file.
 * @param file		header+payload file name
 * @retval *sigTagp	signature tag
 * @retval *pktp	signature packet(s)
 * @retval *pktlenp	signature packet(s) length
 * @param passPhrase	private key pass phrase
 * @return		0 on success, 1 on failure
 */
static int makeGPGSignature(const char * file, rpmSigTag * sigTagp,
		/*@out@*/ uint8_t ** pktp, /*@out@*/ uint32_t * pktlenp,
		/*@null@*/ const char * passPhrase)
	/*@globals rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *pktp, *pktlenp, *sigTagp, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    char * sigfile = alloca(strlen(file)+sizeof(".sig"));
    pid_t pid;
    int status;
    int inpipe[2];
    FILE * fpipe;
    struct stat st;
    const char * cmd;
    char *const *av;
    pgpDig dig = NULL;
    pgpDigParams sigp = NULL;
    const char * pw = NULL;
    int rc;

    (void) stpcpy( stpcpy(sigfile, file), ".sig");

    addMacro(NULL, "__plaintext_filename", NULL, file, -1);
    addMacro(NULL, "__signature_filename", NULL, sigfile, -1);

    inpipe[0] = inpipe[1] = 0;
    (void) pipe(inpipe);

    if (!(pid = fork())) {
	const char *gpg_path = rpmExpand("%{?_gpg_path}", NULL);

	(void) dup2(inpipe[0], 3);
	(void) close(inpipe[1]);

	if (gpg_path && *gpg_path != '\0')
	    (void) setenv("GNUPGHOME", gpg_path, 1);

	unsetenv("MALLOC_CHECK_");
	cmd = rpmExpand("%{?__gpg_sign_cmd}", NULL);
	rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
	if (!rc)
	    rc = execve(av[0], av+1, environ);

	rpmlog(RPMLOG_ERR, _("Could not exec %s: %s\n"), "gpg",
			strerror(errno));
	_exit(EXIT_FAILURE);
    }

    delMacro(NULL, "__plaintext_filename");
    delMacro(NULL, "__signature_filename");

#if defined(HAVE_KEYUTILS_H)
    if (passPhrase && !strcmp(passPhrase, "@u user rpm:passwd")) {
	key_serial_t keyring = KEY_SPEC_PROCESS_KEYRING;
	long key;
	int xx;

/*@-moduncon@*/
	key = keyctl_search(keyring, "user", "rpm:passwd", 0);
	pw = NULL;
	if ((xx = keyctl_read_alloc(key, (void **)&pw)) < 0) {
	    rpmlog(RPMLOG_ERR, _("Failed %s(%d) key(0x%lx): %s\n"),
			"keyctl_read_alloc of key", xx, key, strerror(errno));
	    return 1;
	}
/*@=moduncon@*/
    } else
#endif
	pw = passPhrase;

    fpipe = fdopen(inpipe[1], "w");
    (void) close(inpipe[0]);
    if (fpipe) {
	fprintf(fpipe, "%s\n", (pw ? pw : ""));
	(void) fclose(fpipe);
    }
/*@-mods@*/
    if (pw && pw != passPhrase) {
	(void) memset((void *)pw, 0, strlen(pw));
/*@-temptrans@*/	/* XXX mixed use in variable */
	pw = _free(pw);
/*@=temptrans@*/
    }
/*@=mods@*/

    (void) waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmlog(RPMLOG_ERR, _("gpg exec failed (%d)\n"), WEXITSTATUS(status));
	return 1;
    }

    if (Stat(sigfile, &st)) {
	/* GPG failed to write signature */
	if (sigfile) (void) Unlink(sigfile);  /* Just in case */
	rpmlog(RPMLOG_ERR, _("gpg failed to write signature\n"));
	return 1;
    }

    *pktlenp = st.st_size;
    rpmlog(RPMLOG_DEBUG, D_("GPG sig size: %u\n"), (unsigned)*pktlenp);
    *pktp = xmalloc(*pktlenp);

    {	FD_t fd;

	rc = 0;
	fd = Fopen(sigfile, "r.ufdio");
	if (fd != NULL && !Ferror(fd)) {
	    rc = (int) Fread(*pktp, sizeof((*pktp)[0]), *pktlenp, fd);
	    if (sigfile) (void) Unlink(sigfile);
	    (void) Fclose(fd);
	}
	if ((uint32_t)rc != *pktlenp) {
	    *pktp = _free(*pktp);
	    rpmlog(RPMLOG_ERR, _("unable to read the signature\n"));
	    return 1;
	}
    }

    rpmlog(RPMLOG_DEBUG, D_("Got %u bytes of GPG sig\n"), (unsigned)*pktlenp);

    /* Parse the signature, change signature tag as appropriate. */
    dig = pgpDigNew(0);

    (void) pgpPrtPkts(*pktp, *pktlenp, dig, 0);
    sigp = pgpGetSignature(dig);

    /* Identify the type of signature being returned. */
    switch (*sigTagp) {
    default:
assert(0);	/* XXX never happens. */
	/*@notreached@*/ break;
    case RPMSIGTAG_SIZE:
    case RPMSIGTAG_MD5:
    case RPMSIGTAG_SHA1:
	break;
    case RPMSIGTAG_DSA:
	/* XXX check hash algorithm too? */
	if (sigp->pubkey_algo == PGPPUBKEYALGO_RSA)
	    *sigTagp = RPMSIGTAG_RSA;
	break;
    case RPMSIGTAG_RSA:
	if (sigp->pubkey_algo == PGPPUBKEYALGO_DSA)
	    *sigTagp = RPMSIGTAG_DSA;
	break;
    }

    dig = pgpDigFree(dig);

    return 0;
}

/**
 * Generate header only signature(s) from a header+payload file.
 * @param sigh		signature header
 * @param file		header+payload file name
 * @param sigTag	type of signature(s) to add
 * @param passPhrase	private key pass phrase
 * @return		0 on success, -1 on failure
 */
/*@-mustmod@*/ /* sigh is modified */
static int makeHDRSignature(Header sigh, const char * file, rpmSigTag sigTag,
		/*@null@*/ const char * passPhrase)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies sigh, sigTag, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    Header h = NULL;
    FD_t fd = NULL;
    uint8_t * pkt;
    uint32_t pktlen;
    const char * fn = NULL;
    const char * msg;
    rpmRC rc;
    int ret = -1;	/* assume failure. */
    int xx;

    switch (sigTag) {
    default:
assert(0);	/* XXX never happens. */
	/*@notreached@*/ break;
    case RPMSIGTAG_SIZE:
    case RPMSIGTAG_MD5:
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
    case RPMSIGTAG_GPG:
	goto exit;
	/*@notreached@*/ break;
    case RPMSIGTAG_SHA1:
    {	const char * SHA1 = NULL;
	fd = Fopen(file, "r.fdio");
	if (fd == NULL || Ferror(fd))
	    goto exit;
	{   const char item[] = "Header";
	    msg = NULL;
	    rc = rpmpkgRead(item, fd, &h, &msg);
	    if (rc != RPMRC_OK) {
		rpmlog(RPMLOG_ERR, "%s: %s: %s\n", fn, item, msg);
		msg = _free(msg);
		goto exit;
	    }
	    msg = _free(msg);
	}
	(void) Fclose(fd);	fd = NULL;

	if (headerIsEntry(h, RPMTAG_HEADERIMMUTABLE)) {
	    unsigned char * hmagic = NULL;
	    size_t nmagic = 0;
	    DIGEST_CTX ctx;
	
	    he->tag = RPMTAG_HEADERIMMUTABLE;
	    if (!headerGet(h, he, 0) || he->p.ptr == NULL)
	    {
		h = headerFree(h);
		goto exit;
	    }
	    (void) headerGetMagic(NULL, &hmagic, &nmagic);
	    ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);
	    if (hmagic && nmagic > 0)
		(void) rpmDigestUpdate(ctx, hmagic, nmagic);
	    (void) rpmDigestUpdate(ctx, he->p.ptr, he->c);
	    (void) rpmDigestFinal(ctx, &SHA1, NULL, 1);
	    he->p.ptr = _free(he->p.ptr);
	}
	h = headerFree(h);

	if (SHA1 == NULL)
	    goto exit;
	he->tag = (rpmTag) RPMSIGTAG_SHA1;
	he->t = RPM_STRING_TYPE;
	he->p.str = SHA1;
	he->c = 1;
	xx = headerPut(sigh, he, 0);
	SHA1 = _free(SHA1);
	if (!xx)
	    goto exit;
	ret = 0;
   }	break;
   case RPMSIGTAG_DSA:
	fd = Fopen(file, "r.fdio");
	if (fd == NULL || Ferror(fd))
	    goto exit;
	{   const char item[] = "Header";
	    msg = NULL;
	    rc = rpmpkgRead(item, fd, &h, &msg);
	    if (rc != RPMRC_OK) {
		rpmlog(RPMLOG_ERR, "%s: %s: %s\n", fn, item, msg);
		msg = _free(msg);
		goto exit;
	    }
	    msg = _free(msg);
	}
	(void) Fclose(fd);	fd = NULL;

	if (rpmTempFile(NULL, &fn, &fd))
	    goto exit;
	{   const char item[] = "Header";
	    msg = NULL;
	    rc = rpmpkgWrite(item, fd, h, &msg);
	    if (rc != RPMRC_OK) {
		rpmlog(RPMLOG_ERR, "%s: %s: %s\n", fn, item, msg);
		msg = _free(msg);
		goto exit;
	    }
	    msg = _free(msg);
	}
	(void) Fclose(fd);	fd = NULL;

	if (makeGPGSignature(fn, &sigTag, &pkt, &pktlen, passPhrase))
	    goto exit;
	he->tag = (rpmTag) sigTag;
	he->t = RPM_BIN_TYPE;
	he->p.ptr = pkt;
	he->c = pktlen;
	xx = headerPut(sigh, he, 0);
	if (!xx)
	    goto exit;
	ret = 0;
	break;
    }

exit:
    if (fn) {
	(void) Unlink(fn);
	fn = _free(fn);
    }
    h = headerFree(h);
    if (fd != NULL) (void) Fclose(fd);
    return ret;
}
/*@=mustmod@*/

int rpmAddSignature(Header sigh, const char * file, rpmSigTag sigTag,
		const char * passPhrase)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    struct stat st;
    uint8_t * pkt;
    uint32_t pktlen;
    int ret = -1;	/* assume failure. */
    int xx;

    switch (sigTag) {
    default:
assert(0);	/* XXX never happens. */
	/*@notreached@*/ break;
    case RPMSIGTAG_SIZE:
	if (Stat(file, &st) != 0)
	    break;
	pktlen = st.st_size;
	he->tag = (rpmTag) sigTag;
	he->t = RPM_UINT32_TYPE;
	he->p.ui32p = &pktlen;
	he->c = 1;
/*@-compmempass@*/
	xx = headerPut(sigh, he, 0);
/*@=compmempass@*/
	if (!xx)
	    break;
	ret = 0;
	break;
    case RPMSIGTAG_MD5:
	pktlen = 128/8;
	pkt = memset(alloca(pktlen), 0, pktlen);
	if (dodigest(PGPHASHALGO_MD5, file, (unsigned char *)pkt, 0, NULL))
	    break;
	he->tag = (rpmTag) sigTag;
	he->t = RPM_BIN_TYPE;
	he->p.ptr = pkt;
	he->c = pktlen;
	xx = headerPut(sigh, he, 0);
	if (!xx)
	    break;
	ret = 0;
	break;
    case RPMSIGTAG_GPG:
	ret = makeHDRSignature(sigh, file, RPMSIGTAG_DSA, passPhrase);
	break;
    case RPMSIGTAG_RSA:
    case RPMSIGTAG_DSA:
    case RPMSIGTAG_SHA1:
	ret = makeHDRSignature(sigh, file, sigTag, passPhrase);
	break;
    }

    return ret;
}

int rpmCheckPassPhrase(const char * passPhrase)
{
    const char *pw;
    int p[2];
    pid_t pid;
    int status;
    int rc;
    int xx;

    p[0] = p[1] = 0;
    xx = pipe(p);

    if (!(pid = fork())) {
	const char * cmd;
	char *const *av;
	int fdno;

	xx = close(STDIN_FILENO);
	xx = close(STDOUT_FILENO);
	xx = close(p[1]);
	if (!rpmIsVerbose())
	    xx = close(STDERR_FILENO);
	if ((fdno = open("/dev/null", O_RDONLY)) != STDIN_FILENO) {
	    xx = dup2(fdno, STDIN_FILENO);
	    xx = close(fdno);
	}
	if ((fdno = open("/dev/null", O_WRONLY)) != STDOUT_FILENO) {
	    xx = dup2(fdno, STDOUT_FILENO);
	    xx = close(fdno);
	}
	xx = dup2(p[0], 3);

	unsetenv("MALLOC_CHECK_");
	{   const char *gpg_path = rpmExpand("%{?_gpg_path}", NULL);

	    if (gpg_path && *gpg_path != '\0')
  		(void) setenv("GNUPGHOME", gpg_path, 1);

	    cmd = rpmExpand("%{?__gpg_check_password_cmd}", NULL);
	    rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
	    if (!rc)
		rc = execve(av[0], av+1, environ);

	    rpmlog(RPMLOG_ERR, _("Could not exec %s: %s\n"), "gpg",
			strerror(errno));
	}
    }

#if defined(HAVE_KEYUTILS_H)
    if (!strcmp(passPhrase, "@u user rpm:passwd")) {
	long key;
	key_serial_t keyring = KEY_SPEC_PROCESS_KEYRING;

/*@-moduncon@*/
	key = keyctl_search(keyring, "user", "rpm:passwd", 0);
	pw = NULL;
	if ((xx = keyctl_read_alloc(key, (void **)&pw)) < 0) {
	    rpmlog(RPMLOG_ERR, _("Failed %s(%d) key(0x%lx): %s\n"),
			"keyctl_read_alloc of key", xx, key, strerror(errno));
	    return 1;
	}
/*@=moduncon@*/
    } else
#endif
	pw = passPhrase;

    xx = close(p[0]);
    xx = (int) write(p[1], pw, strlen(pw));
    xx = (int) write(p[1], "\n", 1);
    xx = close(p[1]);

/*@-mods@*/
    if (pw && pw != passPhrase) {
	(void) memset((void *)pw, 0, strlen(pw));
/*@-temptrans@*/	/* XXX mixed use in variable */
	pw = _free(pw);
/*@=temptrans@*/
    }
/*@=mods@*/

    (void) waitpid(pid, &status, 0);

    return ((!WIFEXITED(status) || WEXITSTATUS(status)) ? 1 : 0);
}

static /*@observer@*/ const char * rpmSigString(rpmRC res)
	/*@*/
{
    const char * str;
    switch (res) {
    case RPMRC_OK:		str = "OK";		break;
    case RPMRC_FAIL:		str = "BAD";		break;
    case RPMRC_NOKEY:		str = "NOKEY";		break;
    case RPMRC_NOTTRUSTED:	str = "NOTRUSTED";	break;
    default:
    case RPMRC_NOTFOUND:	str = "UNKNOWN";	break;
    }
    return str;
}

static rpmRC
verifySizeSignature(const pgpDig dig, /*@out@*/ char * t)
	/*@modifies *t @*/
{
    const void * sig = pgpGetSig(dig);
    rpmRC res;
    uint32_t size = 0xffffffff;

    *t = '\0';
    t = stpcpy(t, _("Header+Payload size: "));

    if (sig == NULL || dig == NULL || dig->nbytes == 0) {
	res = RPMRC_NOKEY;
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    memcpy(&size, sig, sizeof(size));

    if (size != dig->nbytes) {
	res = RPMRC_FAIL;
	t = stpcpy(t, rpmSigString(res));
	sprintf(t, " Expected(%u) != (%u)\n", (unsigned)size, (unsigned)dig->nbytes);
    } else {
	res = RPMRC_OK;
	t = stpcpy(t, rpmSigString(res));
	sprintf(t, " (%u)", (unsigned)dig->nbytes);
    }

exit:
    return res;
}

static rpmRC
verifyMD5Signature(const pgpDig dig, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX md5ctx)
	/*@globals internalState @*/
	/*@modifies *t, internalState @*/
{
    const void * sig = pgpGetSig(dig);
    uint32_t siglen = pgpGetSiglen(dig);
    rpmRC res;
    uint8_t * md5sum = NULL;
    size_t md5len = 0;

    *t = '\0';
    t = stpcpy(t, _("MD5 digest: "));

    if (md5ctx == NULL || sig == NULL || dig == NULL) {
	res = RPMRC_NOKEY;
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    {	rpmop op = pgpStatsAccumulator(dig, 10);	/* RPMTS_OP_DIGEST */
	(void) rpmswEnter(op, 0);
	(void) rpmDigestFinal(rpmDigestDup(md5ctx), &md5sum, &md5len, 0);
	(void) rpmswExit(op, 0);
	op->count--;	/* XXX one too many */
    }

    if (md5len != siglen || memcmp(md5sum, sig, md5len)) {
	res = RPMRC_FAIL;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " Expected(");
	(void) pgpHexCvt(t, sig, siglen);
	t += strlen(t);
	t = stpcpy(t, ") != (");
    } else {
	res = RPMRC_OK;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " (");
    }
    (void) pgpHexCvt(t, md5sum, md5len);
    t += strlen(t);
    t = stpcpy(t, ")");

exit:
    md5sum = _free(md5sum);
    return res;
}

/**
 * Verify header immutable region SHA1 digest.
 * @param dig		container
 * @retval t		verbose success/failure text
 * @param sha1ctx
 * @return 		RPMRC_OK on success
 */
static rpmRC
verifySHA1Signature(const pgpDig dig, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX sha1ctx)
	/*@globals internalState @*/
	/*@modifies *t, internalState @*/
{
    const void * sig = pgpGetSig(dig);
#ifdef	NOTYET
    uint32_t siglen = pgpGetSiglen(dig);
#endif
    rpmRC res;
    const char * SHA1 = NULL;

    *t = '\0';
    t = stpcpy(t, _("Header SHA1 digest: "));

    if (sha1ctx == NULL || sig == NULL || dig == NULL) {
	res = RPMRC_NOKEY;
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    {	rpmop op = pgpStatsAccumulator(dig, 10);	/* RPMTS_OP_DIGEST */
	(void) rpmswEnter(op, 0);
	(void) rpmDigestFinal(rpmDigestDup(sha1ctx), &SHA1, NULL, 1);
	(void) rpmswExit(op, 0);
    }

    if (SHA1 == NULL || strlen(SHA1) != strlen(sig) || strcmp(SHA1, sig)) {
	res = RPMRC_FAIL;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " Expected(");
	t = stpcpy(t, sig);
	t = stpcpy(t, ") != (");
    } else {
	res = RPMRC_OK;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " (");
    }
    if (SHA1)
	t = stpcpy(t, SHA1);
    t = stpcpy(t, ")");

exit:
    SHA1 = _free(SHA1);
    return res;
}

/**
 * Verify RSA signature.
 * @param dig		container
 * @retval t		verbose success/failure text
 * @param md5ctx
 * @return 		RPMRC_OK on success
 */
static rpmRC
verifyRSASignature(pgpDig dig, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX md5ctx)
	/*@globals internalState @*/
	/*@modifies dig, *t, internalState */
{
    const void * sig = pgpGetSig(dig);
#ifdef	NOTYET
    uint32_t siglen = pgpGetSiglen(dig);
#endif
    rpmSigTag sigtag = pgpGetSigtag(dig);
    pgpDigParams sigp = pgpGetSignature(dig);
    rpmRC res = RPMRC_OK;
    int xx;

assert(dig != NULL);
assert(sigp != NULL);
    *t = '\0';
    if (dig != NULL && dig->hdrmd5ctx == md5ctx)
	t = stpcpy(t, _("Header "));
    *t++ = 'V';
    switch (sigp->version) {
    case 3:	*t++ = '3';	break;
    case 4:	*t++ = '4';	break;
    }

    if (md5ctx == NULL || sig == NULL || dig == NULL || sigp == NULL) {
	res = RPMRC_NOKEY;
    }

    /* Verify the desired signature match. */
    switch (sigp->pubkey_algo) {
    case PGPPUBKEYALGO_RSA:
	if (sigtag == RPMSIGTAG_RSA)
	    break;
	/*@fallthrough@*/
    default:
	res = RPMRC_NOKEY;
	break;
    }

    /* Identify the RSA/hash. */
    switch (sigp->hash_algo) {
    case PGPHASHALGO_MD5:
	t = stpcpy(t, " RSA/MD5");
	break;
    case PGPHASHALGO_SHA1:
	t = stpcpy(t, " RSA/SHA1");
	break;
    case PGPHASHALGO_RIPEMD160:
	t = stpcpy(t, " RSA/RIPEMD160");
	break;
    case PGPHASHALGO_MD2:
	t = stpcpy(t, " RSA/MD2");
	break;
    case PGPHASHALGO_TIGER192:
	t = stpcpy(t, " RSA/TIGER192");
	break;
    case PGPHASHALGO_HAVAL_5_160:
	res = RPMRC_NOKEY;
	break;
    case PGPHASHALGO_SHA256:
	t = stpcpy(t, " RSA/SHA256");
	break;
    case PGPHASHALGO_SHA384:
	t = stpcpy(t, " RSA/SHA384");
	break;
    case PGPHASHALGO_SHA512:
	t = stpcpy(t, " RSA/SHA512");
	break;
    default:
	res = RPMRC_NOKEY;
	break;
    }

    t = stpcpy(t, _(" signature: "));
    if (res != RPMRC_OK)
	goto exit;

assert(md5ctx != NULL);	/* XXX can't happen. */
    {	rpmop op = pgpStatsAccumulator(dig, 10);	/* RPMTS_OP_DIGEST */
	DIGEST_CTX ctx;

	(void) rpmswEnter(op, 0);
	ctx = rpmDigestDup(md5ctx);
	if (sigp->hash != NULL)
	    xx = rpmDigestUpdate(ctx, sigp->hash, sigp->hashlen);
	(void) rpmswExit(op, sigp->hashlen);
	op->count--;	/* XXX one too many */

	if (pgpImplSetRSA(ctx, dig, sigp)) {
	    res = RPMRC_FAIL;
	    goto exit;
	}
    }

    /* Retrieve the matching public key. */
    res = pgpFindPubkey(dig);
    if (res != RPMRC_OK)
	goto exit;

    /* Verify the RSA signature. */
    {	rpmop op = pgpStatsAccumulator(dig, 11);	/* RPMTS_OP_SIGNATURE */
	(void) rpmswEnter(op, 0);
	xx = pgpImplVerifyRSA(dig);
	(void) rpmswExit(op, 0);
	res = (xx ? RPMRC_OK : RPMRC_FAIL);
    }

exit:
    t = stpcpy(t, rpmSigString(res));
    if (sigp != NULL) {
	t = stpcpy(t, ", key ID ");
	(void) pgpHexCvt(t, sigp->signid+4, sizeof(sigp->signid)-4);
	t += strlen(t);
    }
    return res;
}

/**
 * Verify DSA signature.
 * @param dig		container
 * @retval t		verbose success/failure text
 * @param sha1ctx
 * @return 		RPMRC_OK on success
 */
static rpmRC
verifyDSASignature(pgpDig dig, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX sha1ctx)
	/*@globals internalState @*/
	/*@modifies dig, *t, internalState */
{
    const void * sig = pgpGetSig(dig);
#ifdef	NOTYET
    uint32_t siglen = pgpGetSiglen(dig);
#endif
    rpmSigTag sigtag = pgpGetSigtag(dig);
    pgpDigParams sigp = pgpGetSignature(dig);
    rpmRC res;
    int xx;

assert(dig != NULL);
assert(sigp != NULL);
    *t = '\0';
    if (dig != NULL && dig->hdrsha1ctx == sha1ctx)
	t = stpcpy(t, _("Header "));
    *t++ = 'V';
    switch (sigp->version) {
    case 3:    *t++ = '3';     break;
    case 4:    *t++ = '4';     break;
    }
    t = stpcpy(t, _(" DSA signature: "));

    if (sha1ctx == NULL || sig == NULL || dig == NULL || sigp == NULL) {
	res = RPMRC_NOKEY;
	goto exit;
    }

    /* XXX sanity check on sigtag and signature agreement. */
    if (!(sigtag == RPMSIGTAG_DSA
    	&& sigp->pubkey_algo == PGPPUBKEYALGO_DSA
    	&& sigp->hash_algo == PGPHASHALGO_SHA1))
    {
	res = RPMRC_NOKEY;
	goto exit;
    }

    {	rpmop op = pgpStatsAccumulator(dig, 10);	/* RPMTS_OP_DIGEST */
	DIGEST_CTX ctx;

	(void) rpmswEnter(op, 0);
	ctx = rpmDigestDup(sha1ctx);
	if (sigp->hash != NULL)
	    xx = rpmDigestUpdate(ctx, sigp->hash, sigp->hashlen);

	if (sigp->version == 4) {
	    uint32_t nb = sigp->hashlen;
	    uint8_t trailer[6];
	    nb = htonl(nb);
	    trailer[0] = sigp->version;
	    trailer[1] = 0xff;
	    memcpy(trailer+2, &nb, sizeof(nb));
	    xx = rpmDigestUpdate(ctx, trailer, sizeof(trailer));
	}
	(void) rpmswExit(op, sigp->hashlen);
	op->count--;	/* XXX one too many */

	if (pgpImplSetDSA(ctx, dig, sigp)) {
	    res = RPMRC_FAIL;
	    goto exit;
	}
    }

    /* Retrieve the matching public key. */
    res = pgpFindPubkey(dig);
    if (res != RPMRC_OK)
	goto exit;

    /* Verify the DSA signature. */
    {	rpmop op = pgpStatsAccumulator(dig, 11);	/* RPMTS_OP_SIGNATURE */
	(void) rpmswEnter(op, 0);
	xx = pgpImplVerifyDSA(dig);
	res = (xx ? RPMRC_OK : RPMRC_FAIL);
	(void) rpmswExit(op, 0);
    }

exit:
    t = stpcpy(t, rpmSigString(res));
    if (sigp != NULL) {
	t = stpcpy(t, ", key ID ");
	(void) pgpHexCvt(t, sigp->signid+4, sizeof(sigp->signid)-4);
	t += strlen(t);
    }
    return res;
}

rpmRC
rpmVerifySignature(void * _dig, char * result)
{
    pgpDig dig = _dig;
    const void * sig = pgpGetSig(dig);
    uint32_t siglen = pgpGetSiglen(dig);
    rpmSigTag sigtag = pgpGetSigtag(dig);
    rpmRC res;

    if (dig == NULL || sig == NULL || siglen == 0) {
	sprintf(result, _("Verify signature: BAD PARAMETERS\n"));
	return RPMRC_NOTFOUND;
    }

    switch (sigtag) {
    case RPMSIGTAG_SIZE:
	res = verifySizeSignature(dig, result);
	break;
    case RPMSIGTAG_MD5:
	res = verifyMD5Signature(dig, result, dig->md5ctx);
	break;
    case RPMSIGTAG_SHA1:
	res = verifySHA1Signature(dig, result, dig->hdrsha1ctx);
	break;
    case RPMSIGTAG_RSA:
	res = verifyRSASignature(dig, result, dig->hdrmd5ctx);
	break;
    case RPMSIGTAG_DSA:
	res = verifyDSASignature(dig, result, dig->hdrsha1ctx);
	break;
    default:
	sprintf(result, _("Signature: UNKNOWN (%u)\n"), (unsigned)sigtag);
	res = RPMRC_NOTFOUND;
	break;
    }
    return res;
}
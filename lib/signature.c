/* signature.c - RPM signature functions */

/* NOTES
 *
 * Things have been cleaned up wrt PGP.  We can now handle
 * signatures of any length (which means you can use any
 * size key you like).  We also honor PGPPATH finally.
 */

#include "system.h"

#if HAVE_ASM_BYTEORDER_H
#include <asm/byteorder.h>
#endif

#include <rpmlib.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath */

#include "md5.h"
#include "misc.h"
#include "rpmlead.h"
#include "signature.h"

typedef int (*md5func)(const char * fn, /*@out@*/unsigned char * digest);

int rpmLookupSignatureType(int action)
{
    static int disabled = 0;
    int rc = 0;

    switch (action) {
    case RPMLOOKUPSIG_DISABLE:
	disabled = -2;
	break;
    case RPMLOOKUPSIG_ENABLE:
	disabled = 0;
	/* fall through */
	/*@fallthrough@*/
    case RPMLOOKUPSIG_QUERY:
	if (disabled)
	    break;	/* Disabled */
      { const char *name = rpmExpand("%{_signature}", NULL);
	if (!(name && *name != '%'))
	    rc = 0;
	else if (!strcasecmp(name, "none"))
	    rc = 0;
	else if (!strcasecmp(name, "pgp"))
	    rc = RPMSIGTAG_PGP;
	else if (!strcasecmp(name, "pgp5"))	/* XXX legacy */
	    rc = RPMSIGTAG_PGP;
	else if (!strcasecmp(name, "gpg"))
	    rc = RPMSIGTAG_GPG;
	else
	    rc = -1;	/* Invalid %_signature spec in macro file */
	xfree(name);
      }	break;
    }
    return rc;
}

/* rpmDetectPGPVersion() returns the absolute path to the "pgp"  */
/* executable of the requested version, or NULL when none found. */

const char * rpmDetectPGPVersion(pgpVersion *pgpVer)
{
    /* Actually this should support having more then one pgp version. */ 
    /* At the moment only one version is possible since we only       */
    /* have one %_pgpbin and one %_pgp_path.                          */

    static pgpVersion saved_pgp_version = PGP_UNKNOWN;
    const char *pgpbin = rpmGetPath("%{_pgpbin}", NULL);

    if (saved_pgp_version == PGP_UNKNOWN) {
	char *pgpvbin;
	struct stat statbuf;
	
	if (!(pgpbin && pgpbin[0] != '%') || ! (pgpvbin = (char *)alloca(strlen(pgpbin) + 2))) {
	  if (pgpbin) xfree(pgpbin);
	  saved_pgp_version = -1;
	  return NULL;
	}
	sprintf(pgpvbin, "%sv", pgpbin);

	if (stat(pgpvbin, &statbuf) == 0)
	  saved_pgp_version = PGP_5;
	else if (stat(pgpbin, &statbuf) == 0)
	  saved_pgp_version = PGP_2;
	else
	  saved_pgp_version = PGP_NOTDETECTED;
    }

    if (pgpbin && pgpVer)
	*pgpVer = saved_pgp_version;
    return pgpbin;
}

static int checkSize(FD_t fd, int size, int sigsize)
{
    int headerArchiveSize;
    struct stat statbuf;

    fstat(Fileno(fd), &statbuf);

    if (S_ISREG(statbuf.st_mode)) {
	headerArchiveSize = statbuf.st_size - sizeof(struct rpmlead) - sigsize;

	rpmMessage(RPMMESS_DEBUG, _("sigsize         : %d\n"), sigsize);
	rpmMessage(RPMMESS_DEBUG, _("Header + Archive: %d\n"), headerArchiveSize);
	rpmMessage(RPMMESS_DEBUG, _("expected size   : %d\n"), size);

	return size - headerArchiveSize;
    } else {
	rpmMessage(RPMMESS_DEBUG, _("file is not regular -- skipping size check\n"));
	return 0;
    }
}

/* rpmReadSignature() emulates the new style signatures if it finds an */
/* old-style one.  It also immediately verifies the header+archive  */
/* size and returns an error if it doesn't match.                   */

int rpmReadSignature(FD_t fd, Header *headerp, short sig_type)
{
    unsigned char buf[2048];
    int sigSize, pad;
    int_32 type, count;
    int_32 *archSize;
    Header h;

    if (headerp)
	*headerp = NULL;
    
    switch (sig_type) {
      case RPMSIG_NONE:
	rpmMessage(RPMMESS_DEBUG, _("No signature\n"));
	break;
      case RPMSIG_PGP262_1024:
	rpmMessage(RPMMESS_DEBUG, _("Old PGP signature\n"));
	/* These are always 256 bytes */
	if (timedRead(fd, buf, 256) != 256)
	    return 1;
	if (headerp) {
	    *headerp = headerNew();
	    headerAddEntry(*headerp, RPMSIGTAG_PGP, RPM_BIN_TYPE, buf, 152);
	}
	break;
      case RPMSIG_MD5:
      case RPMSIG_MD5_PGP:
	rpmError(RPMERR_BADSIGTYPE,
	      _("Old (internal-only) signature!  How did you get that!?"));
	return 1;
	/*@notreached@*/ break;
      case RPMSIG_HEADERSIG:
	rpmMessage(RPMMESS_DEBUG, _("New Header signature\n"));
	/* This is a new style signature */
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (h == NULL)
	    return 1;
	sigSize = headerSizeof(h, HEADER_MAGIC_YES);
	pad = (8 - (sigSize % 8)) % 8; /* 8-byte pad */
	rpmMessage(RPMMESS_DEBUG, _("Signature size: %d\n"), sigSize);
	rpmMessage(RPMMESS_DEBUG, _("Signature pad : %d\n"), pad);
	if (! headerGetEntry(h, RPMSIGTAG_SIZE, &type, (void **)&archSize, &count)) {
	    headerFree(h);
	    return 1;
	}
	if (checkSize(fd, *archSize, sigSize + pad)) {
	    headerFree(h);
	    return 1;
	}
	if (pad) {
	    if (timedRead(fd, buf, pad) != pad) {
		headerFree(h);
		return 1;
	    }
	}
	if (headerp) {
	    *headerp = h;
	} else {
	    headerFree(h);
	}
	break;
      default:
	return 1;
    }

    return 0;
}

int rpmWriteSignature(FD_t fd, Header header)
{
    int sigSize, pad;
    unsigned char buf[8];
    int rc = 0;
    
    rc = headerWrite(fd, header, HEADER_MAGIC_YES);
    if (rc)
	return rc;

    sigSize = headerSizeof(header, HEADER_MAGIC_YES);
    pad = (8 - (sigSize % 8)) % 8;
    if (pad) {
	rpmMessage(RPMMESS_DEBUG, _("Signature size: %d\n"), sigSize);
	rpmMessage(RPMMESS_DEBUG, _("Signature pad : %d\n"), pad);
	memset(buf, 0, pad);
	if (Fwrite(buf, pad, 1, fd) != pad)
	    rc = 1;
    }
    return rc;
}

Header rpmNewSignature(void)
{
    Header h = headerNew();
    return h;
}

void rpmFreeSignature(Header h)
{
    headerFree(h);
}

static int makePGPSignature(const char *file, /*@out@*/void **sig, /*@out@*/int_32 *size,
			    const char *passPhrase)
{
    char sigfile[1024];
    int pid, status;
    int inpipe[2];
    struct stat statbuf;

    sprintf(sigfile, "%s.sig", file);

    inpipe[0] = inpipe[1] = 0;
    pipe(inpipe);
    
    if (!(pid = fork())) {
	const char *pgp_path = rpmExpand("%{_pgp_path}", NULL);
	const char *name = rpmExpand("+myname=\"%{_pgp_name}\"", NULL);
	const char *path;
	pgpVersion pgpVer;

	close(STDIN_FILENO);
	dup2(inpipe[0], 3);
	close(inpipe[1]);

	dosetenv("PGPPASSFD", "3", 1);
	if (pgp_path && *pgp_path != '%')
	    dosetenv("PGPPATH", pgp_path, 1);

	/* dosetenv("PGPPASS", passPhrase, 1); */

	if ((path = rpmDetectPGPVersion(&pgpVer)) != NULL) {
	    switch(pgpVer) {
	    case PGP_2:
		execlp(path, "pgp", "+batchmode=on", "+verbose=0", "+armor=off",
		    name, "-sb", file, sigfile, NULL);
		break;
	    case PGP_5:
		execlp(path,"pgps", "+batchmode=on", "+verbose=0", "+armor=off",
		    name, "-b", file, "-o", sigfile, NULL);
		break;
	    case PGP_UNKNOWN:
	    case PGP_NOTDETECTED:
		break;
	    }
	}
	rpmError(RPMERR_EXEC, _("Couldn't exec pgp (%s)"), path);
	_exit(RPMERR_EXEC);
    }

    close(inpipe[0]);
    (void)write(inpipe[1], passPhrase, strlen(passPhrase));
    (void)write(inpipe[1], "\n", 1);
    close(inpipe[1]);

    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_SIGGEN, _("pgp failed"));
	return 1;
    }

    if (stat(sigfile, &statbuf)) {
	/* PGP failed to write signature */
	unlink(sigfile);  /* Just in case */
	rpmError(RPMERR_SIGGEN, _("pgp failed to write signature"));
	return 1;
    }

    *size = statbuf.st_size;
    rpmMessage(RPMMESS_DEBUG, _("PGP sig size: %d\n"), *size);
    *sig = xmalloc(*size);
    
    {	FD_t fd;
	int rc;
	fd = fdOpen(sigfile, O_RDONLY, 0);
	rc = timedRead(fd, *sig, *size);
	unlink(sigfile);
	Fclose(fd);
	if (rc != *size) {
	    free(*sig);
	    rpmError(RPMERR_SIGGEN, _("unable to read the signature"));
	    return 1;
	}
    }

    rpmMessage(RPMMESS_DEBUG, _("Got %d bytes of PGP sig\n"), *size);
    
    return 0;
}

/* This is an adaptation of the makePGPSignature function to use GPG instead
 * of PGP to create signatures.  I think I've made all the changes necessary,
 * but this could be a good place to start looking if errors in GPG signature
 * creation crop up.
 */
static int makeGPGSignature(const char *file, /*@out@*/void **sig, /*@out@*/int_32 *size,
			    const char *passPhrase)
{
    char sigfile[1024];
    int pid, status;
    int inpipe[2];
    FILE *fpipe;
    struct stat statbuf;

    sprintf(sigfile, "%s.sig", file);

    inpipe[0] = inpipe[1] = 0;
    pipe(inpipe);
    
    if (!(pid = fork())) {
	const char *gpg_path = rpmExpand("%{_gpg_path}", NULL);
	const char *name = rpmExpand("%{_gpg_name}", NULL);

	close(STDIN_FILENO);
	dup2(inpipe[0], 3);
	close(inpipe[1]);

	if (gpg_path && *gpg_path != '%')
	    dosetenv("GNUPGHOME", gpg_path, 1);
	execlp("gpg", "gpg",
	       "--batch", "--no-verbose", "--no-armor", "--passphrase-fd", "3",
	       "-u", name, "-sbo", sigfile, file,
	       NULL);
	rpmError(RPMERR_EXEC, _("Couldn't exec gpg"));
	_exit(RPMERR_EXEC);
    }

    fpipe = fdopen(inpipe[1], "w");
    close(inpipe[0]);
    fprintf(fpipe, "%s\n", passPhrase);
    fclose(fpipe);

    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_SIGGEN, _("gpg failed"));
	return 1;
    }

    if (stat(sigfile, &statbuf)) {
	/* GPG failed to write signature */
	unlink(sigfile);  /* Just in case */
	rpmError(RPMERR_SIGGEN, _("gpg failed to write signature"));
	return 1;
    }

    *size = statbuf.st_size;
    rpmMessage(RPMMESS_DEBUG, _("GPG sig size: %d\n"), *size);
    *sig = xmalloc(*size);
    
    {	FD_t fd;
	int rc;
	fd = fdOpen(sigfile, O_RDONLY, 0);
	rc = timedRead(fd, *sig, *size);
	unlink(sigfile);
	Fclose(fd);
	if (rc != *size) {
	    free(*sig);
	    rpmError(RPMERR_SIGGEN, _("unable to read the signature"));
	    return 1;
	}
    }

    rpmMessage(RPMMESS_DEBUG, _("Got %d bytes of GPG sig\n"), *size);
    
    return 0;
}

int rpmAddSignature(Header header, const char *file, int_32 sigTag, const char *passPhrase)
{
    struct stat statbuf;
    int_32 size;
    unsigned char buf[16];
    void *sig;
    int ret = -1;
    
    switch (sigTag) {
    case RPMSIGTAG_SIZE:
	stat(file, &statbuf);
	size = statbuf.st_size;
	ret = 0;
	headerAddEntry(header, RPMSIGTAG_SIZE, RPM_INT32_TYPE, &size, 1);
	break;
    case RPMSIGTAG_MD5:
	ret = mdbinfile(file, buf);
	if (ret == 0)
	    headerAddEntry(header, sigTag, RPM_BIN_TYPE, buf, 16);
	break;
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
	rpmMessage(RPMMESS_VERBOSE, _("Generating signature using PGP.\n"));
	ret = makePGPSignature(file, &sig, &size, passPhrase);
	if (ret == 0)
	    headerAddEntry(header, sigTag, RPM_BIN_TYPE, sig, size);
	break;
    case RPMSIGTAG_GPG:
	rpmMessage(RPMMESS_VERBOSE, _("Generating signature using GPG.\n"));
        ret = makeGPGSignature(file, &sig, &size, passPhrase);
	if (ret == 0)
	    headerAddEntry(header, sigTag, RPM_BIN_TYPE, sig, size);
	break;
    }

    return ret;
}

static int verifySizeSignature(const char *datafile, int_32 size, char *result)
{
    struct stat statbuf;

    stat(datafile, &statbuf);
    if (size != statbuf.st_size) {
	sprintf(result, "Header+Archive size mismatch.\n"
		"Expected %d, saw %d.\n",
		size, (int)statbuf.st_size);
	return 1;
    }

    sprintf(result, "Header+Archive size OK: %d bytes\n", size);
    return 0;
}

#define	X(_x)	(unsigned)((_x) & 0xff)

static int verifyMD5Signature(const char *datafile, unsigned char *sig, 
			      char *result, md5func fn)
{
    unsigned char md5sum[16];

    fn(datafile, md5sum);
    if (memcmp(md5sum, sig, 16)) {
	sprintf(result, "MD5 sum mismatch\n"
		"Expected: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
		"%02x%02x%02x%02x%02x\n"
		"Saw     : %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
		"%02x%02x%02x%02x%02x\n",
		X(sig[0]),  X(sig[1]),  X(sig[2]),  X(sig[3]),
		X(sig[4]),  X(sig[5]),  X(sig[6]),  X(sig[7]),
		X(sig[8]),  X(sig[9]),  X(sig[10]), X(sig[11]),
		X(sig[12]), X(sig[13]), X(sig[14]), X(sig[15]),
		X(md5sum[0]),  X(md5sum[1]),  X(md5sum[2]),  X(md5sum[3]),
		X(md5sum[4]),  X(md5sum[5]),  X(md5sum[6]),  X(md5sum[7]),
		X(md5sum[8]),  X(md5sum[9]),  X(md5sum[10]), X(md5sum[11]),
		X(md5sum[12]), X(md5sum[13]), X(md5sum[14]), X(md5sum[15]) );
	return 1;
    }

    sprintf(result, "MD5 sum OK: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
                    "%02x%02x%02x%02x%02x\n",
	    X(md5sum[0]),  X(md5sum[1]),  X(md5sum[2]),  X(md5sum[3]),
	    X(md5sum[4]),  X(md5sum[5]),  X(md5sum[6]),  X(md5sum[7]),
	    X(md5sum[8]),  X(md5sum[9]),  X(md5sum[10]), X(md5sum[11]),
	    X(md5sum[12]), X(md5sum[13]), X(md5sum[14]), X(md5sum[15]) );

    return 0;
}

static int verifyPGPSignature(const char *datafile, void *sig,
			      int count, char *result)
{
    int pid, status, outpipe[2];
    FD_t sfd;
    char *sigfile;
    unsigned char buf[8192];
    FILE *file;
    int res = RPMSIG_OK;
    const char *path;
    pgpVersion pgpVer;

    /* What version do we have? */
    if ((path = rpmDetectPGPVersion(&pgpVer)) == NULL) {
	errno = ENOENT;
	rpmError(RPMERR_EXEC, 
		 _("Could not run pgp.  Use --nopgp to skip PGP checks."));
	_exit(RPMERR_EXEC);
    }

    /*
     * Sad but true: pgp-5.0 returns exit value of 0 on bad signature.
     * Instead we have to use the text output to detect a bad signature.
     */
    if (pgpVer == PGP_5)
	res = RPMSIG_BAD;

    /* Write out the signature */
  { const char *tmppath = rpmGetPath("%{_tmppath}", NULL);
    sigfile = tempnam(tmppath, "rpmsig");
    xfree(tmppath);
  }
    sfd = fdOpen(sigfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    (void)Fwrite(sig, count, 1, sfd);
    Fclose(sfd);

    /* Now run PGP */
    outpipe[0] = outpipe[1] = 0;
    pipe(outpipe);

    if (!(pid = fork())) {
	const char *pgp_path = rpmExpand("%{_pgp_path}", NULL);

	close(outpipe[0]);
	close(STDOUT_FILENO);	/* XXX unnecessary */
	dup2(outpipe[1], STDOUT_FILENO);

	if (pgp_path && *pgp_path != '%')
	    dosetenv("PGPPATH", pgp_path, 1);

	switch (pgpVer) {
	case PGP_5:
	    /* Some output (in particular "This signature applies to */
	    /* another message") is _always_ written to stderr; we   */
	    /* want to catch that output, so dup stdout to stderr:   */
	{   int save_stderr = dup(2);
	    dup2(1, 2);
	    execlp(path, "pgpv", "+batchmode=on", "+verbose=0",
		   /* Write "Good signature..." to stdout: */
		   "+OutputInformationFD=1",
		   /* Write "WARNING: ... is not trusted to... to stdout: */
		   "+OutputWarningFD=1",
		   sigfile, "-o", datafile, NULL);
	    /* Restore stderr so we can print the error message below. */
	    dup2(save_stderr, 2);
	    close(save_stderr);
	}   break;
	case PGP_2:
	    execlp(path, "pgp", "+batchmode=on", "+verbose=0",
		   sigfile, datafile, NULL);
	    break;
	case PGP_UNKNOWN:
	case PGP_NOTDETECTED:
	    break;
	}

	fprintf(stderr, _("exec failed!\n"));
	rpmError(RPMERR_EXEC, 
		 _("Could not run pgp.  Use --nopgp to skip PGP checks."));
	_exit(RPMERR_EXEC);
    }

    close(outpipe[1]);
    file = fdopen(outpipe[0], "r");
    result[0] = '\0';
    while (fgets(buf, 1024, file)) {
	if (strncmp("File '", buf, 6) &&
	    strncmp("Text is assu", buf, 12) &&
	    strncmp("This signature applies to another message", buf, 41) &&
	    buf[0] != '\n') {
	    strcat(result, buf);
	}
	if (!strncmp("WARNING: Can't find the right public key", buf, 40))
	    res = RPMSIG_NOKEY;
	else if (!strncmp("Signature by unknown keyid:", buf, 27))
	    res = RPMSIG_NOKEY;
	else if (!strncmp("WARNING: The signing key is not trusted", buf, 39))
	    res = RPMSIG_NOTTRUSTED;
	else if (!strncmp("Good signature", buf, 14))
	    res = RPMSIG_OK;
    }
    fclose(file);

    (void)waitpid(pid, &status, 0);
    unlink(sigfile);
    if (!res && (!WIFEXITED(status) || WEXITSTATUS(status))) {
	res = RPMSIG_BAD;
    }
    
    return res;
}

static int verifyGPGSignature(const char *datafile, void *sig,
			      int count, char *result)
{
    int pid, status, outpipe[2];
    FD_t sfd;
    char *sigfile;
    unsigned char buf[8192];
    FILE *file;
    int res = RPMSIG_OK;
  
    /* Write out the signature */
  { const char *tmppath = rpmGetPath("%{_tmppath}", NULL);
    sigfile = tempnam(tmppath, "rpmsig");
    xfree(tmppath);
  }
    sfd = fdOpen(sigfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    (void)Fwrite(sig, count, 1, sfd);
    Fclose(sfd);

    /* Now run GPG */
    outpipe[0] = outpipe[1] = 0;
    pipe(outpipe);

    if (!(pid = fork())) {
	const char *gpg_path = rpmExpand("%{_gpg_path}", NULL);

	close(outpipe[0]);
	/* gpg version 0.9 sends its output to stderr. */
	dup2(outpipe[1], STDERR_FILENO);

	if (gpg_path && *gpg_path != '%')
	    dosetenv("GNUPGHOME", gpg_path, 1);

	execlp("gpg", "gpg",
	       "--batch", "--no-verbose", 
	       "--verify", sigfile, datafile,
	       NULL);
	fprintf(stderr, _("exec failed!\n"));
	rpmError(RPMERR_EXEC, 
		 _("Could not run gpg.  Use --nogpg to skip GPG checks."));
	_exit(RPMERR_EXEC);
    }

    close(outpipe[1]);
    file = fdopen(outpipe[0], "r");
    result[0] = '\0';
    while (fgets(buf, 1024, file)) {
	strcat(result, buf);
	if (!strncmp("gpg: Can't check signature: Public key not found", buf, 48)) {
	    res = RPMSIG_NOKEY;
	}
    }
    fclose(file);
  
    (void)waitpid(pid, &status, 0);
    unlink(sigfile);
    if (!res && (!WIFEXITED(status) || WEXITSTATUS(status))) {
	res = RPMSIG_BAD;
    }
    
    return res;
}

static int checkPassPhrase(const char *passPhrase, const int sigTag)
{
    int passPhrasePipe[2];
    int pid, status;
    int fd;

    passPhrasePipe[0] = passPhrasePipe[1] = 0;
    pipe(passPhrasePipe);
    if (!(pid = fork())) {
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(passPhrasePipe[1]);
	if (! rpmIsVerbose()) {
	    close(STDERR_FILENO);
	}
	if ((fd = open("/dev/null", O_RDONLY)) != STDIN_FILENO) {
	    dup2(fd, STDIN_FILENO);
	    close(fd);
	}
	if ((fd = open("/dev/null", O_WRONLY)) != STDOUT_FILENO) {
	    dup2(fd, STDOUT_FILENO);
	    close(fd);
	}
	dup2(passPhrasePipe[0], 3);

	switch (sigTag) {
	case RPMSIGTAG_GPG:
	{   const char *gpg_path = rpmExpand("%{_gpg_path}", NULL);
	    const char *name = rpmExpand("%{_gpg_name}", NULL);
	    if (gpg_path && *gpg_path != '%')
		dosetenv("GNUPGHOME", gpg_path, 1);
	    execlp("gpg", "gpg",
	           "--batch", "--no-verbose", "--passphrase-fd", "3",
	           "-u", name, "-so", "-",
	           NULL);
	    rpmError(RPMERR_EXEC, _("Couldn't exec gpg"));
	    _exit(RPMERR_EXEC);
	}   /*@notreached@*/ break;
	case RPMSIGTAG_PGP5:	/* XXX legacy */
	case RPMSIGTAG_PGP:
	{   const char *pgp_path = rpmExpand("%{_pgp_path}", NULL);
	    const char *name = rpmExpand("+myname=\"%{_pgp_name}\"", NULL);
	    const char *path;
	    pgpVersion pgpVer;

	    dosetenv("PGPPASSFD", "3", 1);
	    if (pgp_path && *pgp_path != '%')
		dosetenv("PGPPATH", pgp_path, 1);

	    if ((path = rpmDetectPGPVersion(&pgpVer)) != NULL) {
		switch(pgpVer) {
		case PGP_2:
		    execlp(path, "pgp", "+batchmode=on", "+verbose=0",
			name, "-sf", NULL);
		    break;
		case PGP_5:	/* XXX legacy */
		    execlp(path,"pgps", "+batchmode=on", "+verbose=0",
			name, "-f", NULL);
		    break;
		case PGP_UNKNOWN:
		case PGP_NOTDETECTED:
		    break;
		}
	    }
	    rpmError(RPMERR_EXEC, _("Couldn't exec pgp"));
	    _exit(RPMERR_EXEC);
	}   /*@notreached@*/ break;
	default: /* This case should have been screened out long ago. */
	    rpmError(RPMERR_SIGGEN, _("Invalid %%_signature spec in macro file"));
	    _exit(RPMERR_SIGGEN);
	    /*@notreached@*/ break;
	}
    }

    close(passPhrasePipe[0]);
    (void)write(passPhrasePipe[1], passPhrase, strlen(passPhrase));
    (void)write(passPhrasePipe[1], "\n", 1);
    close(passPhrasePipe[1]);

    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	return 1;
    }

    /* passPhrase is good */
    return 0;
}

char *rpmGetPassPhrase(const char *prompt, const int sigTag)
{
    char *pass;
    int aok;

    switch (sigTag) {
    case RPMSIGTAG_GPG:
      { const char *name = rpmExpand("%{_gpg_name}", NULL);
	aok = (name && *name != '%');
	xfree(name);
      }
	if (!aok) {
	    rpmError(RPMERR_SIGGEN,
		_("You must set \"%%_gpg_name\" in your macro file"));
	    return NULL;
	}
	break;
    case RPMSIGTAG_PGP5: 	/* XXX legacy */
    case RPMSIGTAG_PGP: 
      { const char *name = rpmExpand("%{_pgp_name}", NULL);
	aok = (name && *name != '%');
	xfree(name);
      }
	if (!aok) {
	    rpmError(RPMERR_SIGGEN,
		_("You must set \"%%_pgp_name\" in your macro file"));
	    return NULL;
	}
	break;
    default:
	/* Currently the calling function (rpm.c:main) is checking this and
	 * doing a better job.  This section should never be accessed.
	 */
	rpmError(RPMERR_SIGGEN, _("Invalid %%_signature spec in macro file"));
	return NULL;
	/*@notreached@*/ break;
    }

    pass = /*@-unrecog@*/ getpass( (prompt ? prompt : "") ) /*@=unrecog@*/ ;

    if (checkPassPhrase(pass, sigTag))
	return NULL;

    return pass;
}

int rpmVerifySignature(const char *file, int_32 sigTag, void *sig, int count,
		    char *result)
{
    switch (sigTag) {
    case RPMSIGTAG_SIZE:
	if (verifySizeSignature(file, *(int_32 *)sig, result)) {
	    return RPMSIG_BAD;
	}
	break;
    case RPMSIGTAG_MD5:
	if (verifyMD5Signature(file, sig, result, mdbinfile)) {
	    return 1;
	}
	break;
    case RPMSIGTAG_LEMD5_1:
    case RPMSIGTAG_LEMD5_2:
	if (verifyMD5Signature(file, sig, result, mdbinfileBroken)) {
	    return 1;
	}
	break;
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
	return verifyPGPSignature(file, sig, count, result);
	/*@notreached@*/ break;
    case RPMSIGTAG_GPG:
	return verifyGPGSignature(file, sig, count, result);
	/*@notreached@*/ break;
    default:
	sprintf(result, "Do not know how to verify sig type %d\n", sigTag);
	return RPMSIG_UNKNOWN;
    }

    return RPMSIG_OK;
}

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

#include "rpmlib.h"

#include "intl.h"
#include "md5.h"
#include "misc.h"
#include "rpmlead.h"
#include "signature.h"
#include "tread.h"
#include "messages.h"

typedef int (*md5func)(char * fn, unsigned char * digest);

static int makePGPSignature(char *file, void **sig, int_32 *size,
			    char *passPhrase);
static int checkSize(int fd, int size, int sigsize);
static int verifySizeSignature(char *datafile, int_32 size, char *result);
static int verifyMD5Signature(char *datafile, unsigned char *sig,
			      char *result, md5func fn);
static int verifyPGPSignature(char *datafile, void *sig,
			      int count, char *result);
static int checkPassPhrase(char *passPhrase);

int rpmLookupSignatureType(void)
{
    char *name;

    if (! (name = rpmGetVar(RPMVAR_SIGTYPE))) {
	return 0;
    }

    if (!strcasecmp(name, "none")) {
	return 0;
    } else if (!strcasecmp(name, "pgp")) {
	return RPMSIGTAG_PGP;
    } else {
	return -1;
    }
}

/* rpmReadSignature() emulates the new style signatures if it finds an */
/* old-style one.  It also immediately verifies the header+archive  */
/* size and returns an error if it doesn't match.                   */

int rpmReadSignature(int fd, Header *header, short sig_type)
{
    unsigned char buf[2048];
    int sigSize, pad;
    int_32 type, count;
    int_32 *archSize;
    Header h;

    if (header) {
	*header = NULL;
    }
    
    switch (sig_type) {
      case RPMSIG_NONE:
	rpmMessage(RPMMESS_DEBUG, "No signature\n");
	break;
      case RPMSIG_PGP262_1024:
	rpmMessage(RPMMESS_DEBUG, "Old PGP signature\n");
	/* These are always 256 bytes */
	if (timedRead(fd, buf, 256) != 256) {
	    return 1;
	}
	if (header) {
	    *header = headerNew();
	    headerAddEntry(*header, RPMSIGTAG_PGP, RPM_BIN_TYPE, buf, 152);
	}
	break;
      case RPMSIG_MD5:
      case RPMSIG_MD5_PGP:
	rpmError(RPMERR_BADSIGTYPE,
	      _("Old (internal-only) signature!  How did you get that!?"));
	return 1;
	break;
      case RPMSIG_HEADERSIG:
	rpmMessage(RPMMESS_DEBUG, "New Header signature\n");
	/* This is a new style signature */
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (! h) {
	    return 1;
	}
	sigSize = headerSizeof(h, HEADER_MAGIC_YES);
	pad = (8 - (sigSize % 8)) % 8; /* 8-byte pad */
	rpmMessage(RPMMESS_DEBUG, "Signature size: %d\n", sigSize);
	rpmMessage(RPMMESS_DEBUG, "Signature pad : %d\n", pad);
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
	if (header) {
	    *header = h;
	} else {
	    headerFree(h);
	}
	break;
      default:
	return 1;
    }

    return 0;
}

int rpmWriteSignature(int fd, Header header)
{
    int sigSize, pad;
    unsigned char buf[8];
    
    headerWrite(fd, header, HEADER_MAGIC_YES);
    sigSize = headerSizeof(header, HEADER_MAGIC_YES);
    pad = (8 - (sigSize % 8)) % 8;
    if (pad) {
	rpmMessage(RPMMESS_DEBUG, "Signature size: %d\n", sigSize);
	rpmMessage(RPMMESS_DEBUG, "Signature pad : %d\n", pad);
	memset(buf, 0, pad);
	write(fd, buf, pad);
    }
    return 0;
}

Header rpmNewSignature(void)
{
    return headerNew();
}

void rpmFreeSignature(Header h)
{
    headerFree(h);
}

int rpmAddSignature(Header header, char *file, int_32 sigTag, char *passPhrase)
{
    struct stat statbuf;
    int_32 size;
    unsigned char buf[16];
    void *sig;
    
    switch (sigTag) {
      case RPMSIGTAG_SIZE:
	stat(file, &statbuf);
	size = statbuf.st_size;
	headerAddEntry(header, RPMSIGTAG_SIZE, RPM_INT32_TYPE, &size, 1);
	break;
      case RPMSIGTAG_MD5:
	mdbinfile(file, buf);
	headerAddEntry(header, sigTag, RPM_BIN_TYPE, buf, 16);
	break;
      case RPMSIGTAG_PGP:
	makePGPSignature(file, &sig, &size, passPhrase);
	headerAddEntry(header, sigTag, RPM_BIN_TYPE, sig, size);
	break;
    }

    return 0;
}

static int makePGPSignature(char *file, void **sig, int_32 *size,
			    char *passPhrase)
{
    char name[1024];
    char sigfile[1024];
    int pid, status;
    int fd, inpipe[2];
    FILE *fpipe;
    struct stat statbuf;

    sprintf(name, "+myname=\"%s\"", rpmGetVar(RPMVAR_PGP_NAME));

    sprintf(sigfile, "%s.sig", file);

    pipe(inpipe);
    
    if (!(pid = fork())) {
	close(0);
	dup2(inpipe[0], 3);
	close(inpipe[1]);
	dosetenv("PGPPASSFD", "3", 1);
	if (rpmGetVar(RPMVAR_PGP_PATH)) {
	    dosetenv("PGPPATH", rpmGetVar(RPMVAR_PGP_PATH), 1);
	}
	/* dosetenv("PGPPASS", passPhrase, 1); */
	execlp("pgp", "pgp",
	       "+batchmode=on", "+verbose=0", "+armor=off",
	       name, "-sb", file, sigfile,
	       NULL);
	rpmError(RPMERR_EXEC, _("Couldn't exec pgp"));
	_exit(RPMERR_EXEC);
    }

    fpipe = fdopen(inpipe[1], "w");
    close(inpipe[0]);
    fprintf(fpipe, "%s\n", passPhrase);
    fclose(fpipe);

    waitpid(pid, &status, 0);
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
    rpmMessage(RPMMESS_DEBUG, "PGP sig size: %d\n", *size);
    *sig = malloc(*size);
    
    fd = open(sigfile, O_RDONLY);
    if (timedRead(fd, *sig, *size) != *size) {
	unlink(sigfile);
	close(fd);
	free(*sig);
	rpmError(RPMERR_SIGGEN, _("unable to read the signature"));
	return 1;
    }
    close(fd);
    unlink(sigfile);

    rpmMessage(RPMMESS_DEBUG, "Got %d bytes of PGP sig\n", *size);
    
    return 0;
}

static int checkSize(int fd, int size, int sigsize)
{
    int headerArchiveSize;
    struct stat statbuf;

    fstat(fd, &statbuf);

    if (S_ISREG(statbuf.st_mode)) {
	headerArchiveSize = statbuf.st_size - sizeof(struct rpmlead) - sigsize;

	rpmMessage(RPMMESS_DEBUG, "sigsize         : %d\n", sigsize);
	rpmMessage(RPMMESS_DEBUG, "Header + Archive: %d\n", headerArchiveSize);
	rpmMessage(RPMMESS_DEBUG, "expected size   : %d\n", size);

	return size - headerArchiveSize;
    } else {
	rpmMessage(RPMMESS_DEBUG, "file is not regular -- skipping size check\n");
	return 0;
    }
}

int rpmVerifySignature(char *file, int_32 sigTag, void *sig, int count,
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
      case RPMSIGTAG_PGP:
	return verifyPGPSignature(file, sig, count, result);
	break;
      default:
	sprintf(result, "Do not know how to verify sig type %d\n", sigTag);
	return RPMSIG_UNKNOWN;
    }

    return RPMSIG_OK;
}

static int verifySizeSignature(char *datafile, int_32 size, char *result)
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

static int verifyMD5Signature(char *datafile, unsigned char *sig, 
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
		sig[0],  sig[1],  sig[2],  sig[3],
		sig[4],  sig[5],  sig[6],  sig[7],
		sig[8],  sig[9],  sig[10], sig[11],
		sig[12], sig[13], sig[14], sig[15],
		md5sum[0],  md5sum[1],  md5sum[2],  md5sum[3],
		md5sum[4],  md5sum[5],  md5sum[6],  md5sum[7],
		md5sum[8],  md5sum[9],  md5sum[10], md5sum[11],
		md5sum[12], md5sum[13], md5sum[14], md5sum[15]);
	return 1;
    }

    sprintf(result, "MD5 sum OK: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
                    "%02x%02x%02x%02x%02x\n",
	    md5sum[0],  md5sum[1],  md5sum[2],  md5sum[3],
	    md5sum[4],  md5sum[5],  md5sum[6],  md5sum[7],
	    md5sum[8],  md5sum[9],  md5sum[10], md5sum[11],
	    md5sum[12], md5sum[13], md5sum[14], md5sum[15]);

    return 0;
}

static int verifyPGPSignature(char *datafile, void *sig,
			      int count, char *result)
{
    int pid, status, outpipe[2], sfd;
    char *sigfile;
    unsigned char buf[8192];
    FILE *file;
    int res = RPMSIG_OK;

    /* Write out the signature */
    sigfile = tempnam(rpmGetVar(RPMVAR_TMPPATH), "rpmsig");
    sfd = open(sigfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    write(sfd, sig, count);
    close(sfd);

    /* Now run PGP */
    pipe(outpipe);

    if (!(pid = fork())) {
	close(1);
	close(outpipe[0]);
	dup2(outpipe[1], 1);
	if (rpmGetVar(RPMVAR_PGP_PATH)) {
	    dosetenv("PGPPATH", rpmGetVar(RPMVAR_PGP_PATH), 1);
	}
	execlp("pgp", "pgp",
	       "+batchmode=on", "+verbose=0",
	       sigfile, datafile,
	       NULL);
	printf("exec failed!\n");
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
	    buf[0] != '\n') {
	    strcat(result, buf);
	}
	if (!strncmp("WARNING: Can't find the right public key", buf, 40)) {
	    res = RPMSIG_NOKEY;
	}
    }
    fclose(file);

    waitpid(pid, &status, 0);
    unlink(sigfile);
    if (!res && (!WIFEXITED(status) || WEXITSTATUS(status))) {
	res = RPMSIG_BAD;
    }
    
    return res;
}

char *rpmGetPassPhrase(char *prompt)
{
    char *pass;

    if (! rpmGetVar(RPMVAR_PGP_NAME)) {
	rpmError(RPMERR_SIGGEN,
	         _("You must set \"pgp_name:\" in your rpmrc file"));
	return NULL;
    }

    if (prompt) {
        pass = getpass(prompt);
    } else {
        pass = getpass("");
    }

    if (checkPassPhrase(pass)) {
	return NULL;
    }

    return pass;
}

static int checkPassPhrase(char *passPhrase)
{
    char name[1024];
    int passPhrasePipe[2];
    FILE *fpipe;
    int pid, status;
    int fd;

    sprintf(name, "+myname=\"%s\"", rpmGetVar(RPMVAR_PGP_NAME));

    pipe(passPhrasePipe);
    if (!(pid = fork())) {
	close(0);
	close(1);
	if (! rpmIsVerbose()) {
	    close(2);
	}
	if ((fd = open("/dev/null", O_RDONLY)) != 0) {
	    dup2(fd, 0);
	}
	if ((fd = open("/dev/null", O_WRONLY)) != 1) {
	    dup2(fd, 1);
	}
	dup2(passPhrasePipe[0], 3);
	dosetenv("PGPPASSFD", "3", 1);
	if (rpmGetVar(RPMVAR_PGP_PATH)) {
	    dosetenv("PGPPATH", rpmGetVar(RPMVAR_PGP_PATH), 1);
	}
	execlp("pgp", "pgp",
	       "+batchmode=on", "+verbose=0",
	       name, "-sf",
	       NULL);
	rpmError(RPMERR_EXEC, _("Couldn't exec pgp"));
	_exit(RPMERR_EXEC);
    }

    fpipe = fdopen(passPhrasePipe[1], "w");
    close(passPhrasePipe[0]);
    fprintf(fpipe, "%s\n", passPhrase);
    fclose(fpipe);

    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	return 1;
    }

    /* passPhrase is good */
    return 0;
}

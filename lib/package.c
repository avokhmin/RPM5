/** \ingroup header
 * \file lib/package.c
 */

#include "system.h"

#ifdef	__LCLINT__
#define	ntohl(_x)	(_x)
#define	ntohs(_x)	(_x)
#define	htonl(_x)	(_x)
#define	htons(_x)	(_x)
#else
#include <netinet/in.h>
#endif	/* __LCLINT__ */

#include <rpmlib.h>

#include "misc.h"
#include "rpmlead.h"
#include "signature.h"

/*@access Header@*/		/* XXX compared with NULL */

/**
 * Retrieve package components from file handle.
 * @param fd		file handle
 * @param leadPtr	address of lead (or NULL)
 * @param sigs		address of signatures (or NULL)
 * @param hdrPtr	address of header (or NULL)
 * @return		0 on success, 1 on bad magic, 2 on error
 */
static int readPackageHeaders(FD_t fd, /*@out@*/ struct rpmlead * leadPtr, 
			      /*@out@*/ Header * sigs, /*@out@*/ Header *hdrPtr)
	/*@modifies fd, *leadPtr, *sigs, *hdrPtr @*/
{
    Header hdrBlock;
    struct rpmlead leadBlock;
    Header * hdr = NULL;
    struct rpmlead * lead;
    char * defaultPrefix;
    struct stat sb;
    int_32 true = 1;

    hdr = hdrPtr ? hdrPtr : &hdrBlock;
    lead = leadPtr ? leadPtr : &leadBlock;

    fstat(Fileno(fd), &sb);
    /* if fd points to a socket, pipe, etc, sb.st_size is *always* zero */
    if (S_ISREG(sb.st_mode) && sb.st_size < sizeof(*lead)) return 1;

    if (readLead(fd, lead)) {
	return 2;
    }

    if (lead->magic[0] != RPMLEAD_MAGIC0 || lead->magic[1] != RPMLEAD_MAGIC1 ||
	lead->magic[2] != RPMLEAD_MAGIC2 || lead->magic[3] != RPMLEAD_MAGIC3) {
	return 1;
    }

    switch (lead->major) {
    case 1:
	rpmError(RPMERR_NEWPACKAGE, _("packaging version 1 is not"
		" supported by this version of RPM"));
	return 2;
	/*@notreached@*/ break;
    case 2:
    case 3:
    case 4:
	if (rpmReadSignature(fd, sigs, lead->signature_type)) {
	   return 2;
	}
	*hdr = headerRead(fd, (lead->major >= 3) ?
			  HEADER_MAGIC_YES : HEADER_MAGIC_NO);
	if (*hdr == NULL) {
	    if (sigs != NULL) {
		headerFree(*sigs);
	    }
	    return 2;
	}

	/* We don't use these entries (and rpm >= 2 never have) and they are
	   pretty misleading. Let's just get rid of them so they don't confuse
	   anyone. */
	if (headerIsEntry(*hdr, RPMTAG_FILEUSERNAME))
	    headerRemoveEntry(*hdr, RPMTAG_FILEUIDS);
	if (headerIsEntry(*hdr, RPMTAG_FILEGROUPNAME))
	    headerRemoveEntry(*hdr, RPMTAG_FILEGIDS);

	/* We switched the way we do relocateable packages. We fix some of
	   it up here, though the install code still has to be a bit 
	   careful. This fixup makes queries give the new values though,
	   which is quite handy. */
	if (headerGetEntry(*hdr, RPMTAG_DEFAULTPREFIX, NULL,
			   (void **) &defaultPrefix, NULL)) {
	    defaultPrefix = strcpy(alloca(strlen(defaultPrefix) + 1), 
				   defaultPrefix);
	    stripTrailingSlashes(defaultPrefix);
	    headerAddEntry(*hdr, RPMTAG_PREFIXES, RPM_STRING_ARRAY_TYPE,
			   &defaultPrefix, 1); 
	}

	/* The file list was moved to a more compressed format which not
	   only saves memory (nice), but gives fingerprinting a nice, fat
	   speed boost (very nice). Go ahead and convert old headers to
	   the new style (this is a noop for new headers) */
	if (lead->major < 4) {
	    compressFilelist(*hdr);
	}

    /* XXX binary rpms always have RPMTAG_SOURCERPM, source rpms do not */
        if (lead->type == RPMLEAD_SOURCE) {
	    if (!headerIsEntry(*hdr, RPMTAG_SOURCEPACKAGE))
	    	headerAddEntry(*hdr, RPMTAG_SOURCEPACKAGE, RPM_INT32_TYPE,
				&true, 1);
	} else if (lead->major < 4) {
    /* Retrofit "Provide: name = EVR" for binary packages. */
	    providePackageNVR(*hdr);
	}
	break;

    default:
	rpmError(RPMERR_NEWPACKAGE, _("only packaging with major numbers <= 4 "
		"is supported by this version of RPM"));
	return 2;
	/*@notreached@*/ break;
    } 

    if (hdrPtr == NULL) {
	headerFree(*hdr);
    }
    
    return 0;
}

int rpmReadPackageInfo(FD_t fd, Header * signatures, Header * hdr)
{
    return readPackageHeaders(fd, NULL, signatures, hdr);
}

int rpmReadPackageHeader(FD_t fd, Header * hdr, int * isSource, int * major,
		  int * minor)
{
    int rc;
    struct rpmlead lead;

    rc = readPackageHeaders(fd, &lead, NULL, hdr);
    if (rc) return rc;
   
    if (isSource) *isSource = lead.type == RPMLEAD_SOURCE;
    if (major) *major = lead.major;
    if (minor) *minor = lead.minor;
   
    return 0;
}

#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "errno.h"
#include "header.h"
#include "misc.h"
#include "oldheader.h"
#include "rpmlead.h"
#include "rpmlib.h"
#include "signature.h"
#include "messages.h"

/* 0 = success */
/* !0 = error */
static int readOldHeader(int fd, Header * hdr, int * isSource);

/* 0 = success */
/* 1 = bad magic */
/* 2 = error */
static int readPackageHeaders(int fd, struct rpmlead * leadPtr, 
			      Header * sigs, Header * hdrPtr) {
    Header hdrBlock;
    struct rpmlead leadBlock;
    Header * hdr;
    struct rpmlead * lead;
    struct oldrpmlead * oldLead;
    int_8 arch;
    int isSource;

    hdr = hdrPtr ? hdrPtr : &hdrBlock;
    lead = leadPtr ? leadPtr : &leadBlock;

    oldLead = (struct oldrpmlead *) lead;

    if (readLead(fd, lead)) {
	return 2;
    }

    if (lead->magic[0] != RPMLEAD_MAGIC0 || lead->magic[1] != RPMLEAD_MAGIC1 ||
	lead->magic[2] != RPMLEAD_MAGIC2 || lead->magic[3] != RPMLEAD_MAGIC3) {
	return 1;
    }

    if (lead->major == 1) {
	rpmMessage(RPMMESS_DEBUG, "package is a version one package!\n");

	if (lead->type == RPMLEAD_SOURCE) {
	    rpmMessage(RPMMESS_DEBUG, "old style source package -- "
			"I'll do my best\n");
	    oldLead->archiveOffset = ntohl(oldLead->archiveOffset);
	    rpmMessage(RPMMESS_DEBUG, "archive offset is %d\n", 
			oldLead->archiveOffset);
	    lseek(fd, oldLead->archiveOffset, SEEK_SET);
	    
	    /* we can't put togeher a header for old format source packages,
	       there just isn't enough information there. We'll return
	       NULL <gulp> */

	    *hdr = NULL;
	} else {
	    rpmMessage(RPMMESS_DEBUG, "old style binary package\n");
	    readOldHeader(fd, hdr, &isSource);
	    arch = lead->archnum;
	    headerAddEntry(*hdr, RPMTAG_ARCH, RPM_INT8_TYPE, &arch, 1);
	    arch = 1;		  /* old versions of RPM only supported Linux */
	    headerAddEntry(*hdr, RPMTAG_OS, RPM_INT8_TYPE, &arch, 1);
	}
    } else if (lead->major == 2 || lead->major == 3) {
	if (rpmReadSignature(fd, sigs, lead->signature_type)) {
	   return 2;
	}
	*hdr = headerRead(fd, (lead->major >= 3) ?
			  HEADER_MAGIC_YES : HEADER_MAGIC_NO);
	if (! *hdr) {
	    if (sigs) headerFree(*sigs);
	    return 2;
	}
    } else {
	rpmError(RPMERR_NEWPACKAGE, "only packages with major numbers <= 3 are"
		" supported by this version of RPM");
	return 2;
    } 

    if (!hdrPtr) headerFree(*hdr);
    
    return 0;
}

/* 0 = success */
/* 1 = bad magic */
/* 2 = error */
int rpmReadPackageInfo(int fd, Header * signatures, Header * hdr) {
    return readPackageHeaders(fd, NULL, signatures, hdr);
}

/* 0 = success */
/* 1 = bad magic */
/* 2 = error */
int rpmReadPackageHeader(int fd, Header * hdr, int * isSource, int * major,
		  int * minor) {
    int rc;
    struct rpmlead lead;

    rc = readPackageHeaders(fd, &lead, NULL, hdr);
    if (rc) return rc;
   
    if (isSource) *isSource = lead.type == RPMLEAD_SOURCE;
    if (major) *major = lead.major;
    if (minor) *minor = lead.minor;
   
    return 0;
}

static int readOldHeader(int fd, Header * hdr, int * isSource) {
    struct oldrpmHeader oldheader;
    struct oldrpmHeaderSpec spec;
    Header dbentry;
    int_32 installTime = 0;
    char ** fileList;
    char ** fileMD5List;
    char ** fileLinktoList;
    int_32 * fileSizeList;
    int_32 * fileUIDList;
    int_32 * fileGIDList;
    int_32 * fileMtimesList;
    int_32 * fileFlagsList;
    int_16 * fileModesList;
    int_16 * fileRDevsList;
    char * fileStatesList;
    int i, j;
    char ** unames, ** gnames;

    lseek(fd, 0, SEEK_SET);
    if (oldhdrReadFromStream(fd, &oldheader)) {
	return 1;
    }

    if (oldhdrParseSpec(&oldheader, &spec)) {
	return 1;
    }

    dbentry = headerNew();
    headerAddEntry(dbentry, RPMTAG_NAME, RPM_STRING_TYPE, oldheader.name, 1);
    headerAddEntry(dbentry, RPMTAG_VERSION, RPM_STRING_TYPE, oldheader.version, 1);
    headerAddEntry(dbentry, RPMTAG_RELEASE, RPM_STRING_TYPE, oldheader.release, 1);
    headerAddEntry(dbentry, RPMTAG_DESCRIPTION, RPM_STRING_TYPE, 
	     spec.description, 1);
    headerAddEntry(dbentry, RPMTAG_BUILDTIME, RPM_INT32_TYPE, &spec.buildTime, 1);
    headerAddEntry(dbentry, RPMTAG_BUILDHOST, RPM_STRING_TYPE, spec.buildHost, 1);
    headerAddEntry(dbentry, RPMTAG_INSTALLTIME, RPM_INT32_TYPE, &installTime, 1); 
    headerAddEntry(dbentry, RPMTAG_DISTRIBUTION, RPM_STRING_TYPE, 
	     spec.distribution, 1);
    headerAddEntry(dbentry, RPMTAG_VENDOR, RPM_STRING_TYPE, spec.vendor, 1);
    headerAddEntry(dbentry, RPMTAG_SIZE, RPM_INT32_TYPE, &oldheader.size, 1);
    headerAddEntry(dbentry, RPMTAG_COPYRIGHT, RPM_STRING_TYPE, spec.copyright, 1); 

    if (oldheader.group)
	headerAddEntry(dbentry, RPMTAG_GROUP, RPM_STRING_TYPE, oldheader.group, 1);
    else
	headerAddEntry(dbentry, RPMTAG_GROUP, RPM_STRING_TYPE, "Unknown", 1);

    if (spec.prein) 
	headerAddEntry(dbentry, RPMTAG_PREIN, RPM_STRING_TYPE, spec.prein, 1);
    if (spec.preun) 
	headerAddEntry(dbentry, RPMTAG_PREUN, RPM_STRING_TYPE, spec.preun, 1);
    if (spec.postin) 
	headerAddEntry(dbentry, RPMTAG_POSTIN, RPM_STRING_TYPE, spec.postin, 1);
    if (spec.postun) 
	headerAddEntry(dbentry, RPMTAG_POSTUN, RPM_STRING_TYPE, spec.postun, 1);

    *hdr = dbentry;

    if (spec.fileCount) {
	/* some packages have no file lists */

	fileList = malloc(sizeof(char *) * spec.fileCount);
	fileLinktoList = malloc(sizeof(char *) * spec.fileCount);
	fileMD5List = malloc(sizeof(char *) * spec.fileCount);
	fileSizeList = malloc(sizeof(int_32) * spec.fileCount);
	fileUIDList = malloc(sizeof(int_32) * spec.fileCount);
	fileGIDList = malloc(sizeof(int_32) * spec.fileCount);
	fileMtimesList = malloc(sizeof(int_32) * spec.fileCount);
	fileFlagsList = malloc(sizeof(int_32) * spec.fileCount);
	fileModesList = malloc(sizeof(int_16) * spec.fileCount);
	fileRDevsList = malloc(sizeof(int_16) * spec.fileCount);
	fileStatesList = malloc(sizeof(char) * spec.fileCount);
	unames = malloc(sizeof(char *) * spec.fileCount);
	gnames = malloc(sizeof(char *) * spec.fileCount);


	/* We also need to contstruct a file owner/group list. We'll just
	   hope the numbers all map to something, those that don't will
	   get set as 'id%d'. Not perfect, but this should be
	   good enough. */

	/* old packages were reverse sorted, new ones are forward sorted */
	j = spec.fileCount - 1;
	for (i = 0; i < spec.fileCount; i++, j--) {
	    fileList[j] = spec.files[i].path;
	    fileMD5List[j] = spec.files[i].md5;
	    fileSizeList[j] = spec.files[i].size;
	    fileUIDList[j] = spec.files[i].uid;
	    fileGIDList[j] = spec.files[i].gid;
	    fileMtimesList[j] = spec.files[i].mtime;
	    fileModesList[j] = spec.files[i].mode;
	    fileRDevsList[j] = spec.files[i].rdev;
	    fileStatesList[j] = spec.files[i].state;

	    if (spec.files[i].linkto)
		fileLinktoList[j] = spec.files[i].linkto;
	    else
		fileLinktoList[j] = "";
	    
	    fileFlagsList[j] = 0;
	    if (spec.files[i].isdoc) 
		fileFlagsList[j] |= RPMFILE_DOC;
	    if (spec.files[i].isconf)
		fileFlagsList[j] |= RPMFILE_CONFIG;

	    unames[j] = uidToUname(fileUIDList[j]);
	    if (unames[j])
		unames[j] = strdup(unames[j]);
	    else {
		unames[j] = malloc(20);
		sprintf(unames[j], "uid%d", fileUIDList[j]);
	    }

	    gnames[j] = gidToGname(fileGIDList[j]);
	    if (gnames[j])
		gnames[j] = strdup(gnames[j]);
	    else {
		gnames[j] = malloc(20);
		sprintf(gnames[j], "gid%d", fileGIDList[j]);
	    }
	}

	headerAddEntry(dbentry, RPMTAG_FILENAMES, RPM_STRING_ARRAY_TYPE, 
			fileList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILELINKTOS, RPM_STRING_ARRAY_TYPE, 
		 fileLinktoList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEMD5S, RPM_STRING_ARRAY_TYPE, 
			fileMD5List, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILESIZES, RPM_INT32_TYPE, fileSizeList, 
		 	spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEUIDS, RPM_INT32_TYPE, fileUIDList, 
		 	spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEGIDS, RPM_INT32_TYPE, fileGIDList, 
		 	spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEMTIMES, RPM_INT32_TYPE, 
			fileMtimesList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEFLAGS, RPM_INT32_TYPE, 
			fileFlagsList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEMODES, RPM_INT16_TYPE, 
			fileModesList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILERDEVS, RPM_INT16_TYPE, 
			fileRDevsList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILESTATES, RPM_INT8_TYPE, 
			fileStatesList, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEUSERNAME, RPM_STRING_ARRAY_TYPE, 
			unames, spec.fileCount);
	headerAddEntry(dbentry, RPMTAG_FILEGROUPNAME, RPM_STRING_ARRAY_TYPE, 
			gnames, spec.fileCount);

	free(fileList);
	free(fileLinktoList);
	free(fileMD5List);
	free(fileSizeList);
	free(fileUIDList);
	free(fileGIDList);
	free(fileMtimesList);
	free(fileFlagsList);
	free(fileModesList);
	free(fileRDevsList);
	free(fileStatesList);

	for (i = 0; i < spec.fileCount; i++) {
	    free(unames[i]);
	    free(gnames[i]);
	}

	free(unames);
	free(gnames);
    }

    oldhdrFree(&oldheader);

    return 0;
}

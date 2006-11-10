/** \ingroup rpmbuild
 * \file build/files.c
 *  The post-build, pre-packaging file tree walk to assemble the package
 *  manifest.
 */

#include "system.h"

#define	MYALLPERMS	07777

#include <regex.h>

#include <rpmio_internal.h>
#include <fts.h>

#include <rpmbuild.h>

#include "cpio.h"

#include "argv.h"
#include "rpmfc.h"

#define	_RPMFI_INTERNAL
#include "rpmfi.h"

#include "rpmsx.h"

#define	_RPMTE_INTERNAL
#include "rpmte.h"

#include "buildio.h"

#include "legacy.h"	/* XXX dodigest */
#include "misc.h"
#include "debug.h"

/*@access Header @*/
/*@access rpmfi @*/
/*@access rpmte @*/
/*@access FD_t @*/
/*@access StringBuf @*/		/* compared with NULL */

#define	SKIPWHITE(_x)	{while(*(_x) && (xisspace(*_x) || *(_x) == ',')) (_x)++;}
#define	SKIPNONWHITE(_x){while(*(_x) &&!(xisspace(*_x) || *(_x) == ',')) (_x)++;}

#define MAXDOCDIR 1024

/**
 */
typedef enum specdFlags_e {
    SPECD_DEFFILEMODE	= (1 << 0),
    SPECD_DEFDIRMODE	= (1 << 1),
    SPECD_DEFUID	= (1 << 2),
    SPECD_DEFGID	= (1 << 3),
    SPECD_DEFVERIFY	= (1 << 4),

    SPECD_FILEMODE	= (1 << 8),
    SPECD_DIRMODE	= (1 << 9),
    SPECD_UID		= (1 << 10),
    SPECD_GID		= (1 << 11),
    SPECD_VERIFY	= (1 << 12)
} specdFlags;

/**
 */
typedef struct FileListRec_s {
    struct stat fl_st;
#define	fl_dev	fl_st.st_dev
#define	fl_ino	fl_st.st_ino
#define	fl_mode	fl_st.st_mode
#define	fl_nlink fl_st.st_nlink
#define	fl_uid	fl_st.st_uid
#define	fl_gid	fl_st.st_gid
#define	fl_rdev	fl_st.st_rdev
#define	fl_size	fl_st.st_size
#define	fl_mtime fl_st.st_mtime

/*@only@*/
    const char *diskURL;	/* get file from here       */
/*@only@*/
    const char *fileURL;	/* filename in cpio archive */
/*@observer@*/
    const char *uname;
/*@observer@*/
    const char *gname;
    unsigned	flags;
    specdFlags	specdFlags;	/* which attributes have been explicitly specified. */
    unsigned	verifyFlags;
/*@only@*/
    const char *langs;		/* XXX locales separated with | */
} * FileListRec;

/**
 */
typedef struct AttrRec_s {
/*@null@*/
    const char *ar_fmodestr;
/*@null@*/
    const char *ar_dmodestr;
/*@null@*/
    const char *ar_user;
/*@null@*/
    const char *ar_group;
    mode_t	ar_fmode;
    mode_t	ar_dmode;
} * AttrRec;

/*@-readonlytrans@*/
/*@unchecked@*/ /*@observer@*/
static struct AttrRec_s root_ar = { NULL, NULL, "root", "root", 0, 0 };
/*@=readonlytrans@*/

/* list of files */
/*@unchecked@*/ /*@only@*/ /*@null@*/
static StringBuf check_fileList = NULL;

/**
 * Package file tree walk data.
 */
typedef struct FileList_s {
/*@only@*/
    const char * buildRootURL;
/*@only@*/
    const char * prefix;

    int fileCount;
    int totalFileSize;
    int processingFailed;

    int passedSpecialDoc;
    int isSpecialDoc;

    int noGlob;
    unsigned devtype;
    unsigned devmajor;
    int devminor;
    
    int isDir;
    int inFtw;
    int currentFlags;
    specdFlags currentSpecdFlags;
    int currentVerifyFlags;
    struct AttrRec_s cur_ar;
    struct AttrRec_s def_ar;
    specdFlags defSpecdFlags;
    int defVerifyFlags;
    int nLangs;
/*@only@*/ /*@null@*/
    const char ** currentLangs;

    /* Hard coded limit of MAXDOCDIR docdirs.         */
    /* If you break it you are doing something wrong. */
    const char * docDirs[MAXDOCDIR];
    int docDirCount;
    
/*@only@*/
    FileListRec fileList;
    int fileListRecsAlloced;
    int fileListRecsUsed;
} * FileList;

/**
 */
static void nullAttrRec(/*@out@*/ AttrRec ar)	/*@modifies ar @*/
{
    ar->ar_fmodestr = NULL;
    ar->ar_dmodestr = NULL;
    ar->ar_user = NULL;
    ar->ar_group = NULL;
    ar->ar_fmode = 0;
    ar->ar_dmode = 0;
}

/**
 */
static void freeAttrRec(AttrRec ar)	/*@modifies ar @*/
{
    ar->ar_fmodestr = _free(ar->ar_fmodestr);
    ar->ar_dmodestr = _free(ar->ar_dmodestr);
    ar->ar_user = _free(ar->ar_user);
    ar->ar_group = _free(ar->ar_group);
    /* XXX doesn't free ar (yet) */
    /*@-nullstate@*/
    return;
    /*@=nullstate@*/
}

/**
 */
static void dupAttrRec(const AttrRec oar, /*@in@*/ /*@out@*/ AttrRec nar)
	/*@modifies nar @*/
{
    if (oar == nar)
	return;
    freeAttrRec(nar);
    nar->ar_fmodestr = (oar->ar_fmodestr ? xstrdup(oar->ar_fmodestr) : NULL);
    nar->ar_dmodestr = (oar->ar_dmodestr ? xstrdup(oar->ar_dmodestr) : NULL);
    nar->ar_user = (oar->ar_user ? xstrdup(oar->ar_user) : NULL);
    nar->ar_group = (oar->ar_group ? xstrdup(oar->ar_group) : NULL);
    nar->ar_fmode = oar->ar_fmode;
    nar->ar_dmode = oar->ar_dmode;
}

#if 0
/**
 */
static void dumpAttrRec(const char * msg, AttrRec ar)
	/*@globals fileSystem@*/
	/*@modifies fileSystem @*/
{
    if (msg)
	fprintf(stderr, "%s:\t", msg);
    fprintf(stderr, "(%s, %s, %s, %s)\n",
	ar->ar_fmodestr,
	ar->ar_user,
	ar->ar_group,
	ar->ar_dmodestr);
}
#endif

/**
 * @param s
 * @param delim
 */
/*@-boundswrite@*/
/*@null@*/
static char *strtokWithQuotes(/*@null@*/ char *s, char *delim)
	/*@modifies *s @*/
{
    static char *olds = NULL;
    char *token;

    if (s == NULL)
	s = olds;
    if (s == NULL)
	return NULL;

    /* Skip leading delimiters */
    s += strspn(s, delim);
    if (*s == '\0')
	return NULL;

    /* Find the end of the token.  */
    token = s;
    if (*token == '"') {
	token++;
	/* Find next " char */
	s = strchr(token, '"');
    } else {
	s = strpbrk(token, delim);
    }

    /* Terminate it */
    if (s == NULL) {
	/* This token finishes the string */
	olds = strchr(token, '\0');
    } else {
	/* Terminate the token and make olds point past it */
	*s = '\0';
	olds = s+1;
    }

    /*@-retalias -temptrans @*/
    return token;
    /*@=retalias =temptrans @*/
}
/*@=boundswrite@*/

/**
 */
static void timeCheck(int tc, Header h)
	/*@globals internalState @*/
	/*@modifies internalState @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    int * mtime;
    const char ** files;
    rpmTagType fnt;
    int count, x;
    time_t currentTime = time(NULL);

    x = hge(h, RPMTAG_OLDFILENAMES, &fnt, (void **) &files, &count);
    x = hge(h, RPMTAG_FILEMTIMES, NULL, (void **) &mtime, NULL);
    
/*@-boundsread@*/
    for (x = 0; x < count; x++) {
	if ((currentTime - mtime[x]) > tc)
	    rpmMessage(RPMMESS_WARNING, _("TIMECHECK failure: %s\n"), files[x]);
    }
    files = hfd(files, fnt);
/*@=boundsread@*/
}

/**
 */
typedef struct VFA {
/*@observer@*/ /*@null@*/ const char * attribute;
    int not;
    int	flag;
} VFA_t;

/**
 */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
VFA_t verifyAttrs[] = {
    { "md5",	0,	RPMVERIFY_MD5 },
    { "size",	0,	RPMVERIFY_FILESIZE },
    { "link",	0,	RPMVERIFY_LINKTO },
    { "user",	0,	RPMVERIFY_USER },
    { "group",	0,	RPMVERIFY_GROUP },
    { "mtime",	0,	RPMVERIFY_MTIME },
    { "mode",	0,	RPMVERIFY_MODE },
    { "rdev",	0,	RPMVERIFY_RDEV },
    { NULL, 0,	0 }
};
/*@=exportlocal =exportheadervar@*/

/**
 * Parse %verify and %defverify from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		0 on success
 */
/*@-boundswrite@*/
static int parseForVerify(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->currentVerifyFlags, fl->defVerifyFlags,
		fl->currentSpecdFlags, fl->defSpecdFlags @*/
{
    char *p, *pe, *q;
    const char *name;
    int *resultVerify;
    int negated;
    int verifyFlags;
    specdFlags * specdFlags;

    if ((p = strstr(buf, (name = "%verify"))) != NULL) {
	resultVerify = &(fl->currentVerifyFlags);
	specdFlags = &fl->currentSpecdFlags;
    } else if ((p = strstr(buf, (name = "%defverify"))) != NULL) {
	resultVerify = &(fl->defVerifyFlags);
	specdFlags = &fl->defSpecdFlags;
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*verify args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    negated = 0;
    verifyFlags = RPMVERIFY_NONE;

    for (p = q; *p != '\0'; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe != '\0')
	    *pe++ = '\0';

	{   VFA_t *vfa;
	    for (vfa = verifyAttrs; vfa->attribute != NULL; vfa++) {
		if (strcmp(p, vfa->attribute))
		    /*@innercontinue@*/ continue;
		verifyFlags |= vfa->flag;
		/*@innerbreak@*/ break;
	    }
	    if (vfa->attribute)
		continue;
	}

	if (!strcmp(p, "not")) {
	    negated ^= 1;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s\n"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    *resultVerify = negated ? ~(verifyFlags) : verifyFlags;
    *specdFlags |= SPECD_VERIFY;

    return 0;
}
/*@=boundswrite@*/

#define	isAttrDefault(_ars)	((_ars)[0] == '-' && (_ars)[1] == '\0')

/**
 * Parse %dev from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		0 on success
 */
/*@-boundswrite@*/
static int parseForDev(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->noGlob, fl->devtype, fl->devmajor, fl->devminor @*/
{
    const char * name;
    const char * errstr = NULL;
    char *p, *pe, *q;
    int rc = RPMERR_BADSPEC;	/* assume error */

    if ((p = strstr(buf, (name = "%dev"))) == NULL)
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);

    if (*pe != '(') {
	errstr = "'('";
	goto exit;
    }

    /* Bracket %dev args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};
    if (*pe != ')') {
	errstr = "')'";
	goto exit;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    p = q; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    if (*p == 'b')
	fl->devtype = 'b';
    else if (*p == 'c')
	fl->devtype = 'c';
    else {
	errstr = "devtype";
	goto exit;
    }

    p = pe; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    for (pe = p; *pe && xisdigit(*pe); pe++)
	{} ;
    if (*pe == '\0') {
	fl->devmajor = atoi(p);
	/*@-unsignedcompare @*/	/* LCL: ge is ok */
	if (!(fl->devmajor >= 0 && fl->devmajor < 256)) {
	    errstr = "devmajor";
	    goto exit;
	}
	/*@=unsignedcompare @*/
	pe++;
    } else {
	errstr = "devmajor";
	goto exit;
    }

    p = pe; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    for (pe = p; *pe && xisdigit(*pe); pe++)
	{} ;
    if (*pe == '\0') {
	fl->devminor = atoi(p);
	if (!(fl->devminor >= 0 && fl->devminor < 256)) {
	    errstr = "devminor";
	    goto exit;
	}
	pe++;
    } else {
	errstr = "devminor";
	goto exit;
    }

    fl->noGlob = 1;

    rc = 0;

exit:
    if (rc) {
	rpmError(RPMERR_BADSPEC, _("Missing %s in %s %s\n"), errstr, name, p);
	fl->processingFailed = 1;
    }
    return rc;
}
/*@=boundswrite@*/

/**
 * Parse %attr and %defattr from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		0 on success
 */
/*@-boundswrite@*/
static int parseForAttr(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->cur_ar, fl->def_ar,
		fl->currentSpecdFlags, fl->defSpecdFlags @*/
{
    const char *name;
    char *p, *pe, *q;
    int x;
    struct AttrRec_s arbuf;
    AttrRec ar = &arbuf, ret_ar;
    specdFlags * specdFlags;

    if ((p = strstr(buf, (name = "%attr"))) != NULL) {
	ret_ar = &(fl->cur_ar);
	specdFlags = &fl->currentSpecdFlags;
    } else if ((p = strstr(buf, (name = "%defattr"))) != NULL) {
	ret_ar = &(fl->def_ar);
	specdFlags = &fl->defSpecdFlags;
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*attr args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (ret_ar == &(fl->def_ar)) {	/* %defattr */
	q = pe;
	q++;
	SKIPSPACE(q);
	if (*q != '\0') {
	    rpmError(RPMERR_BADSPEC,
		     _("Non-white space follows %s(): %s\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    nullAttrRec(ar);

    p = q; SKIPWHITE(p);
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_fmodestr = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_user = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_group = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0' && ret_ar == &(fl->def_ar)) {	/* %defattr */
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_dmodestr = p;
	p = pe; SKIPWHITE(p);
    }

    if (!(ar->ar_fmodestr && ar->ar_user && ar->ar_group) || *p != '\0') {
	rpmError(RPMERR_BADSPEC, _("Bad syntax: %s(%s)\n"), name, q);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Do a quick test on the mode argument and adjust for "-" */
    if (ar->ar_fmodestr && !isAttrDefault(ar->ar_fmodestr)) {
	unsigned int ui;
	x = sscanf(ar->ar_fmodestr, "%o", &ui);
	if ((x == 0) || (ar->ar_fmode & ~MYALLPERMS)) {
	    rpmError(RPMERR_BADSPEC, _("Bad mode spec: %s(%s)\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
	ar->ar_fmode = ui;
    } else
	ar->ar_fmodestr = NULL;

    if (ar->ar_dmodestr && !isAttrDefault(ar->ar_dmodestr)) {
	unsigned int ui;
	x = sscanf(ar->ar_dmodestr, "%o", &ui);
	if ((x == 0) || (ar->ar_dmode & ~MYALLPERMS)) {
	    rpmError(RPMERR_BADSPEC, _("Bad dirmode spec: %s(%s)\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
	ar->ar_dmode = ui;
    } else
	ar->ar_dmodestr = NULL;

    if (!(ar->ar_user && !isAttrDefault(ar->ar_user)))
	ar->ar_user = NULL;

    if (!(ar->ar_group && !isAttrDefault(ar->ar_group)))
	ar->ar_group = NULL;

    dupAttrRec(ar, ret_ar);

    /* XXX fix all this */
    *specdFlags |= SPECD_UID | SPECD_GID | SPECD_FILEMODE | SPECD_DIRMODE;
    
    return 0;
}
/*@=boundswrite@*/

/**
 * Parse %config from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		0 on success
 */
/*@-boundswrite@*/
static int parseForConfig(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed, fl->currentFlags @*/
{
    char *p, *pe, *q;
    const char *name;

    if ((p = strstr(buf, (name = "%config"))) == NULL)
	return 0;

    fl->currentFlags |= RPMFILE_CONFIG;

    /* Erase "%config" token. */
    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);
    if (*pe != '(')
	return 0;

    /* Bracket %config args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string. */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    for (p = q; *p != '\0'; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe != '\0')
	    *pe++ = '\0';
	if (!strcmp(p, "missingok")) {
	    fl->currentFlags |= RPMFILE_MISSINGOK;
	} else if (!strcmp(p, "noreplace")) {
	    fl->currentFlags |= RPMFILE_NOREPLACE;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s\n"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    return 0;
}
/*@=boundswrite@*/

/**
 */
static int langCmp(const void * ap, const void * bp)
	/*@*/
{
/*@-boundsread@*/
    return strcmp(*(const char **)ap, *(const char **)bp);
/*@=boundsread@*/
}

/**
 * Parse %lang from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		0 on success
 */
/*@-bounds@*/
static int parseForLang(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->currentLangs, fl->nLangs @*/
{
    char *p, *pe, *q;
    const char *name;

  while ((p = strstr(buf, (name = "%lang"))) != NULL) {

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %lang args */
    *pe++ = ' ';
    for (pe = p; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string. */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    /* Parse multiple arguments from %lang */
    for (p = q; *p != '\0'; p = pe) {
	char *newp;
	size_t np;
	int i;

	SKIPWHITE(p);
	pe = p;
	SKIPNONWHITE(pe);

	np = pe - p;
	
	/* Sanity check on locale lengths */
	if (np < 1 || (np == 1 && *p != 'C') || np >= 32) {
	    rpmError(RPMERR_BADSPEC,
		_("Unusual locale length: \"%.*s\" in %%lang(%s)\n"),
		(int)np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Check for duplicate locales */
	if (fl->currentLangs != NULL)
	for (i = 0; i < fl->nLangs; i++) {
	    if (strncmp(fl->currentLangs[i], p, np))
		/*@innercontinue@*/ continue;
	    rpmError(RPMERR_BADSPEC, _("Duplicate locale %.*s in %%lang(%s)\n"),
		(int)np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Add new locale */
	fl->currentLangs = xrealloc(fl->currentLangs,
				(fl->nLangs + 1) * sizeof(*fl->currentLangs));
	newp = xmalloc( np+1 );
	strncpy(newp, p, np);
	newp[np] = '\0';
	fl->currentLangs[fl->nLangs++] = newp;
	if (*pe == ',') pe++;	/* skip , if present */
    }
  }

    /* Insure that locales are sorted. */
    if (fl->currentLangs)
	qsort(fl->currentLangs, fl->nLangs, sizeof(*fl->currentLangs), langCmp);

    return 0;
}
/*@=bounds@*/

/**
 */
/*@-boundswrite@*/
static int parseForRegexLang(const char * fileName, /*@out@*/ char ** lang)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *lang, rpmGlobalMacroContext @*/
{
    static int initialized = 0;
    static int hasRegex = 0;
    static regex_t compiledPatt;
    static char buf[BUFSIZ];
    int x;
    regmatch_t matches[2];
    const char *s;

    if (! initialized) {
	const char *patt = rpmExpand("%{?_langpatt}", NULL);
	int rc = 0;
	if (!(patt && *patt != '\0'))
	    rc = 1;
	else if (regcomp(&compiledPatt, patt, REG_EXTENDED))
	    rc = -1;
	patt = _free(patt);
	if (rc)
	    return rc;
	hasRegex = 1;
	initialized = 1;
    }
    
    memset(matches, 0, sizeof(matches));
    if (! hasRegex || regexec(&compiledPatt, fileName, 2, matches, REG_NOTEOL))
	return 1;

    /* Got match */
    s = fileName + matches[1].rm_eo - 1;
    x = matches[1].rm_eo - matches[1].rm_so;
    buf[x] = '\0';
    while (x) {
	buf[--x] = *s--;
    }
    if (lang)
	*lang = buf;
    return 0;
}
/*@=boundswrite@*/

/**
 */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
VFA_t virtualFileAttributes[] = {
	{ "%dir",	0,	0 },	/* XXX why not RPMFILE_DIR? */
	{ "%doc",	0,	RPMFILE_DOC },
	{ "%ghost",	0,	RPMFILE_GHOST },
	{ "%exclude",	0,	RPMFILE_EXCLUDE },
	{ "%readme",	0,	RPMFILE_README },
	{ "%license",	0,	RPMFILE_LICENSE },
	{ "%pubkey",	0,	RPMFILE_PUBKEY },
	{ "%policy",	0,	RPMFILE_POLICY },

#if WHY_NOT
	{ "%icon",	0,	RPMFILE_ICON },
	{ "%spec",	0,	RPMFILE_SPEC },
	{ "%config",	0,	RPMFILE_CONFIG },
	{ "%missingok",	0,	RPMFILE_CONFIG|RPMFILE_MISSINGOK },
	{ "%noreplace",	0,	RPMFILE_CONFIG|RPMFILE_NOREPLACE },
#endif

	{ NULL, 0, 0 }
};
/*@=exportlocal =exportheadervar@*/

/**
 * Parse simple attributes (e.g. %dir) from file manifest.
 * @param spec
 * @param pkg
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @retval *fileName	file name
 * @return		0 on success
 */
/*@-boundswrite@*/
static int parseForSimple(/*@unused@*/Spec spec, Package pkg, char * buf,
			  FileList fl, /*@out@*/ const char ** fileName)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies buf, fl->processingFailed, *fileName,
		fl->currentFlags,
		fl->docDirs, fl->docDirCount, fl->isDir,
		fl->passedSpecialDoc, fl->isSpecialDoc,
		pkg->specialDoc, rpmGlobalMacroContext @*/
{
    char *s, *t;
    int res, specialDoc = 0;
    char specialDocBuf[BUFSIZ];

    specialDocBuf[0] = '\0';
    *fileName = NULL;
    res = 0;

    t = buf;
    while ((s = strtokWithQuotes(t, " \t\n")) != NULL) {
	t = NULL;
	if (!strcmp(s, "%docdir")) {
	    s = strtokWithQuotes(NULL, " \t\n");
	    if (fl->docDirCount == MAXDOCDIR) {
		rpmError(RPMERR_INTERNAL, _("Hit limit for %%docdir\n"));
		fl->processingFailed = 1;
		res = 1;
	    }
	
	    if (s != NULL)
		fl->docDirs[fl->docDirCount++] = xstrdup(s);
	    if (s == NULL || strtokWithQuotes(NULL, " \t\n")) {
		rpmError(RPMERR_INTERNAL, _("Only one arg for %%docdir\n"));
		fl->processingFailed = 1;
		res = 1;
	    }
	    break;
	}
#if defined(__LCLINT__)
	assert(s != NULL);
#endif

    /* Set flags for virtual file attributes */
    {	VFA_t *vfa;
	for (vfa = virtualFileAttributes; vfa->attribute != NULL; vfa++) {
	    if (strcmp(s, vfa->attribute))
		/*@innercontinue@*/ continue;
	    if (!vfa->flag) {
		if (!strcmp(s, "%dir"))
		    fl->isDir = 1;	/* XXX why not RPMFILE_DIR? */
	    } else {
		if (vfa->not)
		    fl->currentFlags &= ~vfa->flag;
		else
		    fl->currentFlags |= vfa->flag;
	    }

	    /*@innerbreak@*/ break;
	}
	/* if we got an attribute, continue with next token */
	if (vfa->attribute != NULL)
	    continue;
    }

	if (*fileName) {
	    /* We already got a file -- error */
	    rpmError(RPMERR_BADSPEC, _("Two files on one line: %s\n"),
		*fileName);
	    fl->processingFailed = 1;
	    res = 1;
	}

	/*@-branchstate@*/
	if (*s != '/') {
	    if (fl->currentFlags & RPMFILE_DOC) {
		specialDoc = 1;
		strcat(specialDocBuf, " ");
		strcat(specialDocBuf, s);
	    } else
	    if (fl->currentFlags & (RPMFILE_POLICY|RPMFILE_PUBKEY|RPMFILE_ICON))
	    {
		*fileName = s;
	    } else {
		const char * sfn = NULL;
		int urltype = urlPath(s, &sfn);
		switch (urltype) {
		default: /* relative path, not in %doc and not a URL */
		    rpmError(RPMERR_BADSPEC,
			_("File must begin with \"/\": %s\n"), s);
		    fl->processingFailed = 1;
		    res = 1;
		    break;
		case URL_IS_PATH:
		    *fileName = s;
		    break;
		}
	    }
	} else {
	    *fileName = s;
	}
	/*@=branchstate@*/
    }

    if (specialDoc) {
	if (*fileName || (fl->currentFlags & ~(RPMFILE_DOC))) {
	    rpmError(RPMERR_BADSPEC,
		     _("Can't mix special %%doc with other forms: %s\n"),
		     (*fileName ? *fileName : ""));
	    fl->processingFailed = 1;
	    res = 1;
	} else {
	/* XXX WATCHOUT: buf is an arg */
	    {	const char *ddir, *n, *v;

		(void) headerNVR(pkg->header, &n, &v, NULL);

		ddir = rpmGetPath("%{_docdir}/", n, "-", v, NULL);
		strcpy(buf, ddir);
		ddir = _free(ddir);
	    }

	/* XXX FIXME: this is easy to do as macro expansion */

	    if (! fl->passedSpecialDoc) {
		pkg->specialDoc = newStringBuf();
		appendStringBuf(pkg->specialDoc, "DOCDIR=$RPM_BUILD_ROOT");
		appendLineStringBuf(pkg->specialDoc, buf);
		appendLineStringBuf(pkg->specialDoc, "export DOCDIR");
		appendLineStringBuf(pkg->specialDoc, "rm -rf $DOCDIR");
		appendLineStringBuf(pkg->specialDoc, MKDIR_P " $DOCDIR");

		/*@-temptrans@*/
		*fileName = buf;
		/*@=temptrans@*/
		fl->passedSpecialDoc = 1;
		fl->isSpecialDoc = 1;
	    }

	    appendStringBuf(pkg->specialDoc, "cp -pr ");
	    appendStringBuf(pkg->specialDoc, specialDocBuf);
	    appendLineStringBuf(pkg->specialDoc, " $DOCDIR");
	}
    }

    return res;
}
/*@=boundswrite@*/

/**
 */
static int compareFileListRecs(const void * ap, const void * bp)	/*@*/
{
    const char *aurl = ((FileListRec)ap)->fileURL;
    const char *a = NULL;
    const char *burl = ((FileListRec)bp)->fileURL;
    const char *b = NULL;
    (void) urlPath(aurl, &a);
    (void) urlPath(burl, &b);
    return strcmp(a, b);
}

/**
 * Test if file is located in a %docdir.
 * @bug Use of strstr(3) might result in false positives.
 * @param fl		package file tree walk data
 * @param fileName	file path
 * @return		1 if doc file, 0 if not
 */
static int isDoc(FileList fl, const char * fileName)	/*@*/
{
    int x = fl->docDirCount;

    while (x--) {
	if (strstr(fileName, fl->docDirs[x]) == fileName)
	    return 1;
    }
    return 0;
}

/**
 * Verify that file attributes scope over hardlinks correctly.
 * If partial hardlink sets are possible, then add tracking dependency.
 * @param fl		package file tree walk data
 * @return		1 if partial hardlink sets can exist, 0 otherwise.
 */
static int checkHardLinks(FileList fl)
	/*@*/
{
    FileListRec ilp, jlp;
    int i, j;

    for (i = 0;  i < fl->fileListRecsUsed; i++) {
	ilp = fl->fileList + i;
	if (!(S_ISREG(ilp->fl_mode) && ilp->fl_nlink > 1))
	    continue;

	for (j = i + 1; j < fl->fileListRecsUsed; j++) {
	    jlp = fl->fileList + j;
	    if (!S_ISREG(jlp->fl_mode))
		/*@innercontinue@*/ continue;
	    if (ilp->fl_nlink != jlp->fl_nlink)
		/*@innercontinue@*/ continue;
	    if (ilp->fl_ino != jlp->fl_ino)
		/*@innercontinue@*/ continue;
	    if (ilp->fl_dev != jlp->fl_dev)
		/*@innercontinue@*/ continue;
	    return 1;
	}
    }
    return 0;
}

/*@-boundsread@*/
static int dncmp(const void * a, const void * b)
	/*@*/
{
    const char *const * aurlp = a;
    const char *const * burlp = b;
    const char * adn = a;
    const char * bdn = b;
    (void) urlPath(*aurlp, &adn);
    (void) urlPath(*burlp, &bdn);
    return strcmp(adn, bdn);
}
/*@=boundsread@*/

/*@-bounds@*/
/**
 * Convert absolute path tag to (dirname,basename,dirindex) tags.
 * @param h             header
 */
static void compressFilelist(Header h)
	/*@modifies h @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HAE_t hae = (HAE_t)headerAddEntry;
    HRE_t hre = (HRE_t)headerRemoveEntry;
    HFD_t hfd = headerFreeData;
    char ** fileNames;
    const char * fn;
    const char ** dirNames;
    const char ** baseNames;
    int_32 * dirIndexes;
    rpmTagType fnt;
    int count;
    int i, xx;
    int dirIndex = -1;

    /*
     * This assumes the file list is already sorted, and begins with a
     * single '/'. That assumption isn't critical, but it makes things go
     * a bit faster.
     */

    if (headerIsEntry(h, RPMTAG_DIRNAMES)) {
	xx = hre(h, RPMTAG_OLDFILENAMES);
	return;		/* Already converted. */
    }

    if (!hge(h, RPMTAG_OLDFILENAMES, &fnt, (void **) &fileNames, &count))
	return;		/* no file list */
    if (fileNames == NULL || count <= 0)
	return;

    dirNames = alloca(sizeof(*dirNames) * count);	/* worst case */
    baseNames = alloca(sizeof(*dirNames) * count);
    dirIndexes = alloca(sizeof(*dirIndexes) * count);

    (void) urlPath(fileNames[0], &fn);
    if (fn[0] != '/') {
	/* HACK. Source RPM, so just do things differently */
	dirIndex = 0;
	dirNames[dirIndex] = "";
	for (i = 0; i < count; i++) {
	    dirIndexes[i] = dirIndex;
	    baseNames[i] = fileNames[i];
	}
	goto exit;
    }

    /*@-branchstate@*/
    for (i = 0; i < count; i++) {
	const char ** needle;
	char savechar;
	char * baseName;
	int len;

	if (fileNames[i] == NULL)	/* XXX can't happen */
	    continue;
	baseName = strrchr(fileNames[i], '/') + 1;
	len = baseName - fileNames[i];
	needle = dirNames;
	savechar = *baseName;
	*baseName = '\0';
/*@-compdef@*/
	if (dirIndex < 0 ||
	    (needle = bsearch(&fileNames[i], dirNames, dirIndex + 1, sizeof(dirNames[0]), dncmp)) == NULL) {
	    char *s = alloca(len + 1);
	    memcpy(s, fileNames[i], len + 1);
	    s[len] = '\0';
	    dirIndexes[i] = ++dirIndex;
	    dirNames[dirIndex] = s;
	} else
	    dirIndexes[i] = needle - dirNames;
/*@=compdef@*/

	*baseName = savechar;
	baseNames[i] = baseName;
    }
    /*@=branchstate@*/

exit:
    if (count > 0) {
	xx = hae(h, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, dirIndexes, count);
	xx = hae(h, RPMTAG_BASENAMES, RPM_STRING_ARRAY_TYPE,
			baseNames, count);
	xx = hae(h, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE,
			dirNames, dirIndex + 1);
    }

    fileNames = hfd(fileNames, fnt);

    xx = hre(h, RPMTAG_OLDFILENAMES);
}
/*@=bounds@*/

/**
 * Add file entries to header.
 * @todo Should directories have %doc/%config attributes? (#14531)
 * @todo Remove RPMTAG_OLDFILENAMES, add dirname/basename instead.
 * @param fl		package file tree walk data
 * @retval *fip		file info for package
 * @param h
 * @param isSrc
 */
/*@-bounds@*/
static void genCpioListAndHeader(/*@partial@*/ FileList fl,
		rpmfi * fip, Header h, int isSrc)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fip, fl->processingFailed, fl->fileList,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const char * apath;
    int _addDotSlash = !(isSrc || rpmExpandNumeric("%{_noPayloadPrefix}"));
    int apathlen = 0;
    int dpathlen = 0;
    int skipLen = 0;
    rpmsx sx = NULL;
    const char * sxfn;
    size_t fnlen;
    FileListRec flp;
    char buf[BUFSIZ];
    int i;
    
    /* Sort the big list */
    qsort(fl->fileList, fl->fileListRecsUsed,
	  sizeof(*(fl->fileList)), compareFileListRecs);
    
    /* Generate the header. */
    if (! isSrc) {
	skipLen = 1;
	if (fl->prefix)
	    skipLen += strlen(fl->prefix);
    }

    sxfn = rpmGetPath("%{?_build_file_context_path}", NULL);
    if (sxfn != NULL && *sxfn != '\0')
   	sx = rpmsxNew(sxfn);

    for (i = 0, flp = fl->fileList; i < fl->fileListRecsUsed; i++, flp++) {
	const char *s;

 	/* Merge duplicate entries. */
	while (i < (fl->fileListRecsUsed - 1) &&
	    !strcmp(flp->fileURL, flp[1].fileURL)) {

	    /* Two entries for the same file found, merge the entries. */
	    /* Note that an %exclude is a duplication of a file reference */

	    /* file flags */
	    flp[1].flags |= flp->flags;	

	    if (!(flp[1].flags & RPMFILE_EXCLUDE))
		rpmMessage(RPMMESS_WARNING, _("File listed twice: %s\n"),
			flp->fileURL);
   
	    /* file mode */
	    if (S_ISDIR(flp->fl_mode)) {
		if ((flp[1].specdFlags & (SPECD_DIRMODE | SPECD_DEFDIRMODE)) <
		    (flp->specdFlags & (SPECD_DIRMODE | SPECD_DEFDIRMODE)))
			flp[1].fl_mode = flp->fl_mode;
	    } else {
		if ((flp[1].specdFlags & (SPECD_FILEMODE | SPECD_DEFFILEMODE)) <
		    (flp->specdFlags & (SPECD_FILEMODE | SPECD_DEFFILEMODE)))
			flp[1].fl_mode = flp->fl_mode;
	    }

	    /* uid */
	    if ((flp[1].specdFlags & (SPECD_UID | SPECD_DEFUID)) <
		(flp->specdFlags & (SPECD_UID | SPECD_DEFUID)))
	    {
		flp[1].fl_uid = flp->fl_uid;
		flp[1].uname = flp->uname;
	    }

	    /* gid */
	    if ((flp[1].specdFlags & (SPECD_GID | SPECD_DEFGID)) <
		(flp->specdFlags & (SPECD_GID | SPECD_DEFGID)))
	    {
		flp[1].fl_gid = flp->fl_gid;
		flp[1].gname = flp->gname;
	    }

	    /* verify flags */
	    if ((flp[1].specdFlags & (SPECD_VERIFY | SPECD_DEFVERIFY)) <
		(flp->specdFlags & (SPECD_VERIFY | SPECD_DEFVERIFY)))
		    flp[1].verifyFlags = flp->verifyFlags;

	    /* XXX to-do: language */

	    flp++; i++;
	}

	/* Skip files that were marked with %exclude. */
	if (flp->flags & RPMFILE_EXCLUDE) continue;

	/* Omit '/' and/or URL prefix, leave room for "./" prefix */
	(void) urlPath(flp->fileURL, &apath);
	apathlen += (strlen(apath) - skipLen + (_addDotSlash ? 3 : 1));

	/* Leave room for both dirname and basename NUL's */
	dpathlen += (strlen(flp->diskURL) + 2);

	/*
	 * Make the header, the OLDFILENAMES will get converted to a 
	 * compressed file list write before we write the actual package to
	 * disk.
	 */
	(void) headerAddOrAppendEntry(h, RPMTAG_OLDFILENAMES, RPM_STRING_ARRAY_TYPE,
			       &(flp->fileURL), 1);

/*@-sizeoftype@*/
      if (sizeof(flp->fl_size) != sizeof(uint_32)) {
	uint_32 psize = (uint_32)flp->fl_size;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(psize), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(flp->fl_size), 1);
      }
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEUSERNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->uname), 1);
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEGROUPNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->gname), 1);
      if (sizeof(flp->fl_mtime) != sizeof(uint_32)) {
	uint_32 mtime = (uint_32)flp->fl_mtime;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMTIMES, RPM_INT32_TYPE,
			       &(mtime), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMTIMES, RPM_INT32_TYPE,
			       &(flp->fl_mtime), 1);
      }
      if (sizeof(flp->fl_mode) != sizeof(uint_16)) {
	uint_16 pmode = (uint_16)flp->fl_mode;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(pmode), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(flp->fl_mode), 1);
      }
      if (sizeof(flp->fl_rdev) != sizeof(uint_16)) {
	uint_16 prdev = (uint_16)flp->fl_rdev;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(prdev), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(flp->fl_rdev), 1);
      }
      if (sizeof(flp->fl_dev) != sizeof(uint_32)) {
	uint_32 pdevice = (uint_32)flp->fl_dev;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEDEVICES, RPM_INT32_TYPE,
			       &(pdevice), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEDEVICES, RPM_INT32_TYPE,
			       &(flp->fl_dev), 1);
      }
      if (sizeof(flp->fl_ino) != sizeof(uint_32)) {
	uint_32 ino = (uint_32)flp->fl_ino;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEINODES, RPM_INT32_TYPE,
				&(ino), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEINODES, RPM_INT32_TYPE,
				&(flp->fl_ino), 1);
      }
/*@=sizeoftype@*/

	(void) headerAddOrAppendEntry(h, RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE,
			       &(flp->langs),  1);
	
      { static uint_32 source_file_dalgo = 0;
	static uint_32 binary_file_dalgo = 0;
	static int oneshot = 0;
	uint_32 dalgo = 0;

	if (!oneshot) {
	    source_file_dalgo =
		rpmExpandNumeric("%{?_build_source_file_digest_algo}");
	    binary_file_dalgo =
		rpmExpandNumeric("%{?_build_binary_file_digest_algo}");
	    oneshot++;
	}

	dalgo = (isSrc ? source_file_dalgo : binary_file_dalgo);
	switch (dalgo) {
	case PGPHASHALGO_SHA1:
	case PGPHASHALGO_RIPEMD160:
	case PGPHASHALGO_MD2:
	case PGPHASHALGO_TIGER192:
	case PGPHASHALGO_SHA256:
	case PGPHASHALGO_SHA384:
	case PGPHASHALGO_SHA512:
	case PGPHASHALGO_MD4:
	case PGPHASHALGO_RIPEMD128:
	case PGPHASHALGO_CRC32:
	case PGPHASHALGO_ADLER32:
	case PGPHASHALGO_CRC64:
	    (void) rpmlibNeedsFeature(h, "FileDigestParameterized", "4.4.6-1");
	    /*@switchbreak@*/ break;
	case PGPHASHALGO_MD5:
	case PGPHASHALGO_HAVAL_5_160:		/* XXX unimplemented */
	default:
	    dalgo = PGPHASHALGO_MD5;
	    /*@switchbreak@*/ break;
	}
	    
	buf[0] = '\0';
	if (S_ISREG(flp->fl_mode))
	    (void) dodigest(dalgo, flp->diskURL, (unsigned char *)buf, 1, NULL);
	s = buf;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEDIGESTS, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEDIGESTALGOS, RPM_INT32_TYPE,
			       &dalgo, 1);
      }
	
	buf[0] = '\0';
	if (S_ISLNK(flp->fl_mode)) {
	    int xx = Readlink(flp->diskURL, buf, BUFSIZ);
	    if (xx >= 0)
		buf[xx] = '\0';
	    if (fl->buildRootURL) {
		const char * buildRoot;
		(void) urlPath(fl->buildRootURL, &buildRoot);

		if (buf[0] == '/' && strcmp(buildRoot, "/") &&
		    !strncmp(buf, buildRoot, strlen(buildRoot))) {
		     rpmError(RPMERR_BADSPEC,
				_("Symlink points to BuildRoot: %s -> %s\n"),
				flp->fileURL, buf);
		    fl->processingFailed = 1;
		}
	    }
	}
	s = buf;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILELINKTOS, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	
	if (flp->flags & RPMFILE_GHOST) {
	    flp->verifyFlags &= ~(RPMVERIFY_MD5 | RPMVERIFY_FILESIZE |
				RPMVERIFY_LINKTO | RPMVERIFY_MTIME);
	}
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEVERIFYFLAGS, RPM_INT32_TYPE,
			       &(flp->verifyFlags), 1);
	
	if (!isSrc && isDoc(fl, flp->fileURL))
	    flp->flags |= RPMFILE_DOC;
	/* XXX Should directories have %doc/%config attributes? (#14531) */
	if (S_ISDIR(flp->fl_mode))
	    flp->flags &= ~(RPMFILE_CONFIG|RPMFILE_DOC);

	(void) headerAddOrAppendEntry(h, RPMTAG_FILEFLAGS, RPM_INT32_TYPE,
			       &(flp->flags), 1);

	/* Add file security context to package. */
/*@-branchstate@*/
	if (sx != NULL) {
	    mode_t fmode = (uint_16)flp->fl_mode;
	    s = rpmsxFContext(sx, flp->fileURL, fmode);
	    if (s == NULL) s = "";
	    (void) headerAddOrAppendEntry(h, RPMTAG_FILECONTEXTS, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	}
/*@=branchstate@*/

    }
    sx = rpmsxFree(sx);
    sxfn = _free(sxfn);

    (void) headerAddEntry(h, RPMTAG_SIZE, RPM_INT32_TYPE,
		   &(fl->totalFileSize), 1);

    if (_addDotSlash)
	(void) rpmlibNeedsFeature(h, "PayloadFilesHavePrefix", "4.0-1");

    {
	compressFilelist(h);
	/* Binary packages with dirNames cannot be installed by legacy rpm. */
	(void) rpmlibNeedsFeature(h, "CompressedFileNames", "3.0.4-1");
    }

  { int scareMem = 0;
    rpmts ts = NULL;	/* XXX FIXME drill rpmts ts all the way down here */
    rpmfi fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, scareMem);
    char * a, * d;

    if (fi == NULL) return;		/* XXX can't happen */

/*@-onlytrans@*/
    fi->te = xcalloc(1, sizeof(*fi->te));
/*@=onlytrans@*/
    fi->te->type = TR_ADDED;

    fi->dnl = _free(fi->dnl);
    fi->bnl = _free(fi->bnl);
    if (!scareMem) fi->dil = _free(fi->dil);

    fi->dnl = xmalloc(fi->fc * sizeof(*fi->dnl) + dpathlen);
    d = (char *)(fi->dnl + fi->fc);
    *d = '\0';

    fi->bnl = xmalloc(fi->fc * (sizeof(*fi->bnl) + sizeof(*fi->dil)));
/*@-dependenttrans@*/ /* FIX: artifact of spoofing headerGetEntry */
    fi->dil = (!scareMem)
	? xcalloc(sizeof(*fi->dil), fi->fc)
	: (int *)(fi->bnl + fi->fc);
/*@=dependenttrans@*/

    fi->apath = xmalloc(fi->fc * sizeof(*fi->apath) + apathlen);
    a = (char *)(fi->apath + fi->fc);
    *a = '\0';

    fi->actions = xcalloc(sizeof(*fi->actions), fi->fc);
    fi->fmapflags = xcalloc(sizeof(*fi->fmapflags), fi->fc);
    fi->astriplen = 0;
    if (fl->buildRootURL)
	fi->astriplen = strlen(fl->buildRootURL);
    fi->striplen = 0;
    fi->fuser = NULL;
    fi->fgroup = NULL;

    /* Make the cpio list */
    if (fi->dil != NULL)	/* XXX can't happen */
    for (i = 0, flp = fl->fileList; i < fi->fc; i++, flp++) {
	char * b;

	/* Skip (possible) duplicate file entries, use last entry info. */
	while (((flp - fl->fileList) < (fl->fileListRecsUsed - 1)) &&
		!strcmp(flp->fileURL, flp[1].fileURL))
	    flp++;

	if (flp->flags & RPMFILE_EXCLUDE) {
	    i--;
	    continue;
	}

	if ((fnlen = strlen(flp->diskURL) + 1) > fi->fnlen)
	    fi->fnlen = fnlen;

	/* Create disk directory and base name. */
	fi->dil[i] = i;
/*@-dependenttrans@*/ /* FIX: artifact of spoofing headerGetEntry */
	fi->dnl[fi->dil[i]] = d;
/*@=dependenttrans@*/
	d = stpcpy(d, flp->diskURL);

	/* Make room for the dirName NUL, find start of baseName. */
	for (b = d; b > fi->dnl[fi->dil[i]] && *b != '/'; b--)
	    b[1] = b[0];
	b++;		/* dirname's end in '/' */
	*b++ = '\0';	/* terminate dirname, b points to basename */
	fi->bnl[i] = b;
	d += 2;		/* skip both dirname and basename NUL's */

	/* Create archive path, normally adding "./" */
	/*@-dependenttrans@*/	/* FIX: xstrdup? nah ... */
	fi->apath[i] = a;
 	/*@=dependenttrans@*/
	if (_addDotSlash)
	    a = stpcpy(a, "./");
	(void) urlPath(flp->fileURL, &apath);
	a = stpcpy(a, (apath + skipLen));
	a++;		/* skip apath NUL */

	if (flp->flags & RPMFILE_GHOST) {
	    fi->actions[i] = FA_SKIP;
	    continue;
	}
	fi->actions[i] = FA_COPYOUT;
	fi->fmapflags[i] = CPIO_MAP_PATH |
		CPIO_MAP_TYPE | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	if (isSrc)
	    fi->fmapflags[i] |= CPIO_FOLLOW_SYMLINKS;

    }
    /*@-branchstate -compdef@*/
    if (fip)
	*fip = fi;
    else
	fi = rpmfiFree(fi);
    /*@=branchstate =compdef@*/
  }
}
/*@=bounds@*/

/**
 */
/*@-boundswrite@*/
static /*@null@*/ FileListRec freeFileList(/*@only@*/ FileListRec fileList,
			int count)
	/*@*/
{
    while (count--) {
	fileList[count].diskURL = _free(fileList[count].diskURL);
	fileList[count].fileURL = _free(fileList[count].fileURL);
	fileList[count].langs = _free(fileList[count].langs);
    }
    fileList = _free(fileList);
    return NULL;
}
/*@=boundswrite@*/

/* forward ref */
static int recurseDir(FileList fl, const char * diskURL)
	/*@globals check_fileList, rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *fl, fl->processingFailed,
		fl->fileList, fl->fileListRecsAlloced, fl->fileListRecsUsed,
		fl->totalFileSize, fl->fileCount, fl->inFtw, fl->isDir,
		check_fileList, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Add a file to the package manifest.
 * @param fl		package file tree walk data
 * @param diskURL	path to file
 * @param statp		file stat (possibly NULL)
 * @return		0 on success
 */
/*@-boundswrite@*/
static int addFile(FileList fl, const char * diskURL,
		/*@null@*/ struct stat * statp)
	/*@globals check_fileList, rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *statp, *fl, fl->processingFailed,
		fl->fileList, fl->fileListRecsAlloced, fl->fileListRecsUsed,
		fl->totalFileSize, fl->fileCount,
		check_fileList, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const char *fn = xstrdup(diskURL);
    const char *fileURL = fn;
    struct stat statbuf;
    mode_t fileMode;
    uid_t fileUid;
    gid_t fileGid;
    const char *fileUname;
    const char *fileGname;
    char *lang;
    
    /* Path may have prepended buildRootURL, so locate the original filename. */
    /*
     * XXX There are 3 types of entry into addFile:
     *
     *	From			diskUrl			statp
     *	=====================================================
     *  processBinaryFile	path			NULL
     *  processBinaryFile	glob result path	NULL
     *  myftw			path			stat
     *
     */
    {	const char *fileName;
	int urltype = urlPath(fileURL, &fileName);
	switch (urltype) {
	case URL_IS_PATH:
	    fileURL += (fileName - fileURL);
	    if (fl->buildRootURL && strcmp(fl->buildRootURL, "/")) {
		size_t nb = strlen(fl->buildRootURL);
		const char * s = fileURL + nb;
		char * t = (char *) fileURL;
		(void) memmove(t, s, nb);
	    }
	    fileURL = fn;
	    break;
	default:
	    if (fl->buildRootURL && strcmp(fl->buildRootURL, "/"))
		fileURL += strlen(fl->buildRootURL);
	    break;
	}
    }

    /* XXX make sure '/' can be packaged also */
    /*@-branchstate@*/
    if (*fileURL == '\0')
	fileURL = "/";
    /*@=branchstate@*/

    /* If we are using a prefix, validate the file */
    if (!fl->inFtw && fl->prefix) {
	const char *prefixTest;
	const char *prefixPtr = fl->prefix;

	(void) urlPath(fileURL, &prefixTest);
	while (*prefixPtr && *prefixTest && (*prefixTest == *prefixPtr)) {
	    prefixPtr++;
	    prefixTest++;
	}
	if (*prefixPtr || (*prefixTest && *prefixTest != '/')) {
	    rpmError(RPMERR_BADSPEC, _("File doesn't match prefix (%s): %s\n"),
		     fl->prefix, fileURL);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    if (statp == NULL) {
	statp = &statbuf;
	memset(statp, 0, sizeof(*statp));
	if (fl->devtype) {
	    time_t now = time(NULL);

	    /* XXX hack up a stat structure for a %dev(...) directive. */
	    statp->st_nlink = 1;
	    statp->st_rdev =
		((fl->devmajor & 0xff) << 8) | (fl->devminor & 0xff);
	    statp->st_dev = statp->st_rdev;
	    statp->st_mode = (fl->devtype == 'b' ? S_IFBLK : S_IFCHR);
	    statp->st_mode |= (fl->cur_ar.ar_fmode & 0777);
	    statp->st_atime = now;
	    statp->st_mtime = now;
	    statp->st_ctime = now;
	} else if (Lstat(diskURL, statp)) {
	    rpmError(RPMERR_BADSPEC, _("File not found: %s\n"), diskURL);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    if ((! fl->isDir) && S_ISDIR(statp->st_mode)) {
/*@-nullstate@*/ /* FIX: fl->buildRootURL may be NULL */
	return recurseDir(fl, diskURL);
/*@=nullstate@*/
    }

    fileMode = statp->st_mode;
    fileUid = statp->st_uid;
    fileGid = statp->st_gid;

    if (S_ISDIR(fileMode) && fl->cur_ar.ar_dmodestr) {
	fileMode &= S_IFMT;
	fileMode |= fl->cur_ar.ar_dmode;
    } else if (fl->cur_ar.ar_fmodestr != NULL) {
	fileMode &= S_IFMT;
	fileMode |= fl->cur_ar.ar_fmode;
    }
    if (fl->cur_ar.ar_user) {
	fileUname = getUnameS(fl->cur_ar.ar_user);
    } else {
	fileUname = getUname(fileUid);
    }
    if (fl->cur_ar.ar_group) {
	fileGname = getGnameS(fl->cur_ar.ar_group);
    } else {
	fileGname = getGname(fileGid);
    }
	
    /* Default user/group to builder's user/group */
    if (fileUname == NULL)
	fileUname = getUname(getuid());
    if (fileGname == NULL)
	fileGname = getGname(getgid());
    
    /* S_XXX macro must be consistent with type in find call at check-files script */
    if (check_fileList && (S_ISREG(fileMode) || S_ISLNK(fileMode))) {
	const char * diskfn = NULL;
	(void) urlPath(diskURL, &diskfn);
	appendStringBuf(check_fileList, diskfn);
	appendStringBuf(check_fileList, "\n");
    }

    /* Add to the file list */
    if (fl->fileListRecsUsed == fl->fileListRecsAlloced) {
	fl->fileListRecsAlloced += 128;
	fl->fileList = xrealloc(fl->fileList,
			fl->fileListRecsAlloced * sizeof(*(fl->fileList)));
    }
	    
    {	FileListRec flp = &fl->fileList[fl->fileListRecsUsed];
	int i;

	flp->fl_st = *statp;	/* structure assignment */
	flp->fl_mode = fileMode;
	flp->fl_uid = fileUid;
	flp->fl_gid = fileGid;

	flp->fileURL = xstrdup(fileURL);
	flp->diskURL = xstrdup(diskURL);
	flp->uname = fileUname;
	flp->gname = fileGname;

	if (fl->currentLangs && fl->nLangs > 0) {
	    char * ncl;
	    size_t nl = 0;
	    
	    for (i = 0; i < fl->nLangs; i++)
		nl += strlen(fl->currentLangs[i]) + 1;

	    flp->langs = ncl = xmalloc(nl);
	    for (i = 0; i < fl->nLangs; i++) {
	        const char *ocl;
		if (i)	*ncl++ = '|';
		for (ocl = fl->currentLangs[i]; *ocl != '\0'; ocl++)
			*ncl++ = *ocl;
		*ncl = '\0';
	    }
	} else if (! parseForRegexLang(fileURL, &lang)) {
	    flp->langs = xstrdup(lang);
	} else {
	    flp->langs = xstrdup("");
	}

	flp->flags = fl->currentFlags;
	flp->specdFlags = fl->currentSpecdFlags;
	flp->verifyFlags = fl->currentVerifyFlags;

	/* Hard links need be counted only once. */
	if (S_ISREG(flp->fl_mode) && flp->fl_nlink > 1) {
	    FileListRec ilp;
	    for (i = 0;  i < fl->fileListRecsUsed; i++) {
		ilp = fl->fileList + i;
		if (!S_ISREG(ilp->fl_mode))
		    continue;
		if (flp->fl_nlink != ilp->fl_nlink)
		    continue;
		if (flp->fl_ino != ilp->fl_ino)
		    continue;
		if (flp->fl_dev != ilp->fl_dev)
		    continue;
		break;
	    }
	} else
	    i = fl->fileListRecsUsed;

	if (!(flp->flags & RPMFILE_EXCLUDE) && S_ISREG(flp->fl_mode) && i >= fl->fileListRecsUsed) 
	    fl->totalFileSize += flp->fl_size;
    }

    fl->fileListRecsUsed++;
    fl->fileCount++;
    fn = _free(fn);

    return 0;
}
/*@=boundswrite@*/

/**
 * Add directory (and all of its files) to the package manifest.
 * @param fl		package file tree walk data
 * @param diskURL	path to file
 * @return		0 on success
 */
static int recurseDir(FileList fl, const char * diskURL)
{
    char * ftsSet[2];
    FTS * ftsp;
    FTSENT * fts;
    int myFtsOpts = (FTS_COMFOLLOW | FTS_NOCHDIR | FTS_PHYSICAL);
    int rc = RPMERR_BADSPEC;

    fl->inFtw = 1;  /* Flag to indicate file has buildRootURL prefixed */
    fl->isDir = 1;  /* Keep it from following myftw() again         */

    ftsSet[0] = (char *) diskURL;
    ftsSet[1] = NULL;
    ftsp = Fts_open(ftsSet, myFtsOpts, NULL);
    while ((fts = Fts_read(ftsp)) != NULL) {
	switch (fts->fts_info) {
	case FTS_D:		/* preorder directory */
	case FTS_F:		/* regular file */
	case FTS_SL:		/* symbolic link */
	case FTS_SLNONE:	/* symbolic link without target */
	case FTS_DEFAULT:	/* none of the above */
	    rc = addFile(fl, fts->fts_accpath, fts->fts_statp);
	    /*@switchbreak@*/ break;
	case FTS_DOT:		/* dot or dot-dot */
	case FTS_DP:		/* postorder directory */
	    rc = 0;
	    /*@switchbreak@*/ break;
	case FTS_NS:		/* stat(2) failed */
	case FTS_DNR:		/* unreadable directory */
	case FTS_ERR:		/* error; errno is set */
	case FTS_DC:		/* directory that causes cycles */
	case FTS_NSOK:		/* no stat(2) requested */
	case FTS_INIT:		/* initialized only */
	case FTS_W:		/* whiteout object */
	default:
	    rc = RPMERR_BADSPEC;
	    /*@switchbreak@*/ break;
	}
	if (rc)
	    break;
    }
    (void) Fts_close(ftsp);

    fl->isDir = 0;
    fl->inFtw = 0;

    return rc;
}

/**
 * Add a pubkey/policy/icon to a binary package.
 * @param pkg
 * @param fl		package file tree walk data
 * @param fileURL	path to file, relative is builddir, absolute buildroot.
 * @param tag		tag to add
 * @return		0 on success
 */
static int processMetadataFile(Package pkg, FileList fl, const char * fileURL,
		rpmTag tag)
	/*@globals check_fileList, rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies pkg->header, *fl, fl->processingFailed,
		fl->fileList, fl->fileListRecsAlloced, fl->fileListRecsUsed,
		fl->totalFileSize, fl->fileCount,
		check_fileList, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const char * buildURL = "%{_builddir}/%{?buildsubdir}/";
    const char * fn = NULL;
    const char * apkt = NULL;
    const unsigned char * pkt = NULL;
    ssize_t pktlen = 0;
    int absolute = 0;
    int rc = 1;
    int xx;

    (void) urlPath(fileURL, &fn);
    if (*fn == '/') {
	fn = rpmGenPath(fl->buildRootURL, NULL, fn);
	absolute = 1;
    } else
	fn = rpmGenPath(buildURL, NULL, fn);

/*@-branchstate@*/
    switch (tag) {
    default:
	rpmError(RPMERR_BADSPEC, _("%s: can't load unknown tag (%d).\n"),
		fn, tag);
	goto exit;
	/*@notreached@*/ break;
    case RPMTAG_PUBKEYS:
	if ((rc = pgpReadPkts(fn, &pkt, &pktlen)) <= 0) {
	    rpmError(RPMERR_BADSPEC, _("%s: public key read failed.\n"), fn);
	    goto exit;
	}
	if (rc != PGPARMOR_PUBKEY) {
	    rpmError(RPMERR_BADSPEC, _("%s: not an armored public key.\n"), fn);
	    goto exit;
	}
	apkt = pgpArmorWrap(PGPARMOR_PUBKEY, pkt, pktlen);
	break;
    case RPMTAG_POLICIES:
	if ((rc = rpmioSlurp(fn, &pkt, &pktlen)) != 0) {
	    rpmError(RPMERR_BADSPEC, _("%s: *.te policy read failed.\n"), fn);
	    goto exit;
	}
	apkt = (const char *) pkt;	/* XXX unsigned char */
	pkt = NULL;
	break;
    }
/*@=branchstate@*/

    xx = headerAddOrAppendEntry(pkg->header, tag,
		RPM_STRING_ARRAY_TYPE, &apkt, 1);

    rc = 0;
    if (absolute)
	rc = addFile(fl, fn, NULL);

exit:
    apkt = _free(apkt);
    pkt = _free(pkt);
    fn = _free(fn);
    if (rc) {
	fl->processingFailed = 1;
	rc = RPMERR_BADSPEC;
    }
    return rc;
}

/**
 * Add a file to a binary package.
 * @param pkg
 * @param fl		package file tree walk data
 * @param fileURL
 * @return		0 on success
 */
static int processBinaryFile(/*@unused@*/ Package pkg, FileList fl,
		const char * fileURL)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies *fl, fl->processingFailed,
		fl->fileList, fl->fileListRecsAlloced, fl->fileListRecsUsed,
		fl->totalFileSize, fl->fileCount,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    int quote = 1;	/* XXX permit quoted glob characters. */
    int doGlob;
    const char *diskURL = NULL;
    int rc = 0;
    
    doGlob = Glob_pattern_p(fileURL, quote);

    /* Check that file starts with leading "/" */
    {	const char * fileName;
	(void) urlPath(fileURL, &fileName);
	if (*fileName != '/') {
	    rpmError(RPMERR_BADSPEC, _("File needs leading \"/\": %s\n"),
			fileName);
	    rc = 1;
	    goto exit;
	}
    }
    
    /* Copy file name or glob pattern removing multiple "/" chars. */
    /*
     * Note: rpmGetPath should guarantee a "canonical" path. That means
     * that the following pathologies should be weeded out:
     *		//bin//sh
     *		//usr//bin/
     *		/.././../usr/../bin//./sh
     */
    diskURL = rpmGenPath(fl->buildRootURL, NULL, fileURL);

    if (doGlob) {
	const char ** argv = NULL;
	int argc = 0;
	int i;

	/* XXX for %dev marker in file manifest only */
	if (fl->noGlob) {
	    rpmError(RPMERR_BADSPEC, _("Glob not permitted: %s\n"),
			diskURL);
	    rc = 1;
	    goto exit;
	}

	/*@-branchstate@*/
	rc = rpmGlob(diskURL, &argc, &argv);
	if (rc == 0 && argc >= 1 && !Glob_pattern_p(argv[0], quote)) {
	    for (i = 0; i < argc; i++) {
		rc = addFile(fl, argv[i], NULL);
/*@-boundswrite@*/
		argv[i] = _free(argv[i]);
/*@=boundswrite@*/
	    }
	    argv = _free(argv);
	} else {
	    rpmError(RPMERR_BADSPEC, _("File not found by glob: %s\n"),
			diskURL);
	    rc = 1;
	    goto exit;
	}
	/*@=branchstate@*/
    } else {
	rc = addFile(fl, diskURL, NULL);
    }

exit:
    diskURL = _free(diskURL);
    if (rc) {
	fl->processingFailed = 1;
	rc = RPMERR_BADSPEC;
    }
    return rc;
}

/**
 */
/*@-boundswrite@*/
static int processPackageFiles(Spec spec, Package pkg,
			       int installSpecialDoc, int test)
	/*@globals rpmGlobalMacroContext, h_errno,
		fileSystem, internalState@*/
	/*@modifies spec->macros,
		pkg->cpioList, pkg->fileList, pkg->specialDoc, pkg->header,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    struct FileList_s fl;
    char *s, **files, **fp;
    const char *fileName;
    char buf[BUFSIZ];
    struct AttrRec_s arbuf;
    AttrRec specialDocAttrRec = &arbuf;
    char *specialDoc = NULL;

    nullAttrRec(specialDocAttrRec);
    pkg->cpioList = NULL;

    if (pkg->fileFile) {
	const char *ffn;
	FILE * f;
	FD_t fd;

	/* XXX W2DO? urlPath might be useful here. */
	if (*pkg->fileFile == '/') {
	    ffn = rpmGetPath(pkg->fileFile, NULL);
	} else {
	    /* XXX FIXME: add %{buildsubdir} */
	    ffn = rpmGetPath("%{_builddir}/",
		(spec->buildSubdir ? spec->buildSubdir : "") ,
		"/", pkg->fileFile, NULL);
	}
	fd = Fopen(ffn, "r.fpio");

	if (fd == NULL || Ferror(fd)) {
	    rpmError(RPMERR_BADFILENAME,
		_("Could not open %%files file %s: %s\n"),
		ffn, Fstrerror(fd));
	    return RPMERR_BADFILENAME;
	}
	ffn = _free(ffn);

	/*@+voidabstract@*/ f = fdGetFp(fd); /*@=voidabstract@*/
	if (f != NULL)
	while (fgets(buf, sizeof(buf), f)) {
	    handleComments(buf);
	    if (expandMacros(spec, spec->macros, buf, sizeof(buf))) {
		rpmError(RPMERR_BADSPEC, _("line: %s\n"), buf);
		return RPMERR_BADSPEC;
	    }
	    appendStringBuf(pkg->fileList, buf);
	}
	(void) Fclose(fd);
    }
    
    /* Init the file list structure */
    memset(&fl, 0, sizeof(fl));

    fl.buildRootURL = rpmGenPath(spec->rootURL, "%{?buildroot}", NULL);

    if (hge(pkg->header, RPMTAG_DEFAULTPREFIX, NULL, (void **)&fl.prefix, NULL))
	fl.prefix = xstrdup(fl.prefix);
    else
	fl.prefix = NULL;

    fl.fileCount = 0;
    fl.totalFileSize = 0;
    fl.processingFailed = 0;

    fl.passedSpecialDoc = 0;
    fl.isSpecialDoc = 0;

    fl.isDir = 0;
    fl.inFtw = 0;
    fl.currentFlags = 0;
    fl.currentVerifyFlags = 0;
    
    fl.noGlob = 0;
    fl.devtype = 0;
    fl.devmajor = 0;
    fl.devminor = 0;

    nullAttrRec(&fl.cur_ar);
    nullAttrRec(&fl.def_ar);
    dupAttrRec(&root_ar, &fl.def_ar);	/* XXX assume %defattr(-,root,root) */

    fl.defVerifyFlags = RPMVERIFY_ALL;
    fl.nLangs = 0;
    fl.currentLangs = NULL;

    fl.currentSpecdFlags = 0;
    fl.defSpecdFlags = 0;

    fl.docDirCount = 0;
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/doc");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/info");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/X11R6/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/doc");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/info");
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_docdir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_mandir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_infodir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_javadocdir}", NULL);
    
    fl.fileList = NULL;
    fl.fileListRecsAlloced = 0;
    fl.fileListRecsUsed = 0;

    s = getStringBuf(pkg->fileList);
    files = splitString(s, strlen(s), '\n');

    for (fp = files; *fp != NULL; fp++) {
	s = *fp;
	SKIPSPACE(s);
	if (*s == '\0')
	    continue;
	fileName = NULL;
	/*@-nullpass@*/	/* LCL: buf is NULL ?!? */
	strcpy(buf, s);
	/*@=nullpass@*/
	
	/* Reset for a new line in %files */
	fl.isDir = 0;
	fl.inFtw = 0;
	fl.currentFlags = 0;
	/* turn explicit flags into %def'd ones (gosh this is hacky...) */
	fl.currentSpecdFlags = ((unsigned)fl.defSpecdFlags) >> 8;
	fl.currentVerifyFlags = fl.defVerifyFlags;
	fl.isSpecialDoc = 0;

	fl.noGlob = 0;
 	fl.devtype = 0;
 	fl.devmajor = 0;
 	fl.devminor = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		/*@-unqualifiedtrans@*/
		fl.currentLangs[i] = _free(fl.currentLangs[i]);
		/*@=unqualifiedtrans@*/
	    fl.currentLangs = _free(fl.currentLangs);
	}
  	fl.nLangs = 0;

	dupAttrRec(&fl.def_ar, &fl.cur_ar);

	/*@-nullpass@*/	/* LCL: buf is NULL ?!? */
	if (parseForVerify(buf, &fl))
	    continue;
	if (parseForAttr(buf, &fl))
	    continue;
	if (parseForDev(buf, &fl))
	    continue;
	if (parseForConfig(buf, &fl))
	    continue;
	if (parseForLang(buf, &fl))
	    continue;
	/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	if (parseForSimple(spec, pkg, buf, &fl, &fileName))
	/*@=nullstate@*/
	    continue;
	/*@=nullpass@*/
	if (fileName == NULL)
	    continue;

	/*@-branchstate@*/
	if (fl.isSpecialDoc) {
	    /* Save this stuff for last */
	    specialDoc = _free(specialDoc);
	    specialDoc = xstrdup(fileName);
	    dupAttrRec(&fl.cur_ar, specialDocAttrRec);
	} else if (fl.currentFlags & RPMFILE_PUBKEY) {
/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	    (void) processMetadataFile(pkg, &fl, fileName, RPMTAG_PUBKEYS);
/*@=nullstate@*/
	} else if (fl.currentFlags & RPMFILE_POLICY) {
/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	    (void) processMetadataFile(pkg, &fl, fileName, RPMTAG_POLICIES);
/*@=nullstate@*/
	} else {
/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	    (void) processBinaryFile(pkg, &fl, fileName);
/*@=nullstate@*/
	}
	/*@=branchstate@*/
    }

    /* Now process special doc, if there is one */
    if (specialDoc) {
	if (installSpecialDoc) {
	    int _missing_doc_files_terminate_build =
		    rpmExpandNumeric("%{?_missing_doc_files_terminate_build}");
	    int rc;

	    rc = doScript(spec, RPMBUILD_STRINGBUF, "%doc", pkg->specialDoc, test);
	    if (rc && _missing_doc_files_terminate_build)
		fl.processingFailed = rc;
	}

	/* Reset for %doc */
	fl.isDir = 0;
	fl.inFtw = 0;
	fl.currentFlags = 0;
	fl.currentVerifyFlags = 0;

	fl.noGlob = 0;
 	fl.devtype = 0;
 	fl.devmajor = 0;
 	fl.devminor = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		/*@-unqualifiedtrans@*/
		fl.currentLangs[i] = _free(fl.currentLangs[i]);
		/*@=unqualifiedtrans@*/
	    fl.currentLangs = _free(fl.currentLangs);
	}
  	fl.nLangs = 0;

	dupAttrRec(specialDocAttrRec, &fl.cur_ar);
	freeAttrRec(specialDocAttrRec);

	/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	(void) processBinaryFile(pkg, &fl, specialDoc);
	/*@=nullstate@*/

	specialDoc = _free(specialDoc);
    }
    
    freeSplitString(files);

    if (fl.processingFailed)
	goto exit;

    /* Verify that file attributes scope over hardlinks correctly. */
    if (checkHardLinks(&fl))
	(void) rpmlibNeedsFeature(pkg->header,
			"PartialHardlinkSets", "4.0.4-1");

    genCpioListAndHeader(&fl, &pkg->cpioList, pkg->header, 0);

    if (spec->timeCheck)
	timeCheck(spec->timeCheck, pkg->header);
    
exit:
    fl.buildRootURL = _free(fl.buildRootURL);
    fl.prefix = _free(fl.prefix);

    freeAttrRec(&fl.cur_ar);
    freeAttrRec(&fl.def_ar);

    if (fl.currentLangs) {
	int i;
	for (i = 0; i < fl.nLangs; i++)
	    /*@-unqualifiedtrans@*/
	    fl.currentLangs[i] = _free(fl.currentLangs[i]);
	    /*@=unqualifiedtrans@*/
	fl.currentLangs = _free(fl.currentLangs);
    }

    fl.fileList = freeFileList(fl.fileList, fl.fileListRecsUsed);
    while (fl.docDirCount--)
	fl.docDirs[fl.docDirCount] = _free(fl.docDirs[fl.docDirCount]);
    return fl.processingFailed;
}
/*@=boundswrite@*/

void initSourceHeader(Spec spec)
{
    HeaderIterator hi;
    int_32 tag, type, count;
    const void * ptr;

    spec->sourceHeader = headerNew();
    /* Only specific tags are added to the source package header */
    /*@-branchstate@*/
    for (hi = headerInitIterator(spec->packages->header);
	headerNextIterator(hi, &tag, &type, &ptr, &count);
	ptr = headerFreeData(ptr, type))
    {
	switch (tag) {
	case RPMTAG_NAME:
	case RPMTAG_VERSION:
	case RPMTAG_RELEASE:
	case RPMTAG_EPOCH:
	case RPMTAG_SUMMARY:
	case RPMTAG_DESCRIPTION:
	case RPMTAG_PACKAGER:
	case RPMTAG_DISTRIBUTION:
	case RPMTAG_DISTURL:
	case RPMTAG_VENDOR:
	case RPMTAG_LICENSE:
	case RPMTAG_GROUP:
	case RPMTAG_OS:
	case RPMTAG_ARCH:
	case RPMTAG_CHANGELOGTIME:
	case RPMTAG_CHANGELOGNAME:
	case RPMTAG_CHANGELOGTEXT:
	case RPMTAG_URL:
	case HEADER_I18NTABLE:
	    if (ptr)
		(void)headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
	    /*@switchbreak@*/ break;
	default:
	    /* do not copy */
	    /*@switchbreak@*/ break;
	}
    }
    hi = headerFreeIterator(hi);
    /*@=branchstate@*/

    /* Add the build restrictions */
    /*@-branchstate@*/
    for (hi = headerInitIterator(spec->buildRestrictions);
	headerNextIterator(hi, &tag, &type, &ptr, &count);
	ptr = headerFreeData(ptr, type))
    {
	if (ptr)
	    (void) headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
    }
    hi = headerFreeIterator(hi);
    /*@=branchstate@*/

    if (spec->BANames && spec->BACount > 0) {
	(void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDARCHS,
		       RPM_STRING_ARRAY_TYPE,
		       spec->BANames, spec->BACount);
    }
}

int processSourceFiles(Spec spec)
{
    struct Source *srcPtr;
    StringBuf sourceFiles;
    int x, isSpec = 1;
    struct FileList_s fl;
    char *s, **files, **fp;
    Package pkg;

    sourceFiles = newStringBuf();

    /* XXX
     * XXX This is where the source header for noarch packages needs
     * XXX to be initialized.
     */
    if (spec->sourceHeader == NULL)
	initSourceHeader(spec);

    /* Construct the file list and source entries */
    appendLineStringBuf(sourceFiles, spec->specFile);
    if (spec->sourceHeader != NULL)
    for (srcPtr = spec->sources; srcPtr != NULL; srcPtr = srcPtr->next) {
	if (srcPtr->flags & RPMBUILD_ISSOURCE) {
	    (void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_SOURCE,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		(void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOSOURCE,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}
	if (srcPtr->flags & RPMBUILD_ISPATCH) {
	    (void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_PATCH,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		(void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOPATCH,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}

      {	const char * sfn;
	sfn = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	appendLineStringBuf(sourceFiles, sfn);
	sfn = _free(sfn);
      }
    }

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	for (srcPtr = pkg->icon; srcPtr != NULL; srcPtr = srcPtr->next) {
	    const char * sfn;
	    sfn = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	    appendLineStringBuf(sourceFiles, sfn);
	    sfn = _free(sfn);
	}
    }

    spec->sourceCpioList = NULL;

    fl.fileList = xcalloc((spec->numSources + 1), sizeof(*fl.fileList));
    fl.processingFailed = 0;
    fl.fileListRecsUsed = 0;
    fl.totalFileSize = 0;
    fl.prefix = NULL;
    fl.buildRootURL = NULL;

    s = getStringBuf(sourceFiles);
    files = splitString(s, strlen(s), '\n');

    /* The first source file is the spec file */
    x = 0;
    for (fp = files; *fp != NULL; fp++) {
	const char * diskURL, *diskPath;
	FileListRec flp;

	diskURL = *fp;
	SKIPSPACE(diskURL);
	if (! *diskURL)
	    continue;

	flp = &fl.fileList[x];

	flp->flags = isSpec ? RPMFILE_SPECFILE : 0;
	/* files with leading ! are no source files */
	if (*diskURL == '!') {
	    flp->flags |= RPMFILE_GHOST;
	    diskURL++;
	}

	(void) urlPath(diskURL, &diskPath);

	flp->diskURL = xstrdup(diskURL);
	diskPath = strrchr(diskPath, '/');
	if (diskPath)
	    diskPath++;
	else
	    diskPath = diskURL;

	flp->fileURL = xstrdup(diskPath);
	flp->verifyFlags = RPMVERIFY_ALL;

	if (Stat(diskURL, &flp->fl_st)) {
	    rpmError(RPMERR_BADSPEC, _("Bad file: %s: %s\n"),
		diskURL, strerror(errno));
	    fl.processingFailed = 1;
	}

	flp->uname = getUname(flp->fl_uid);
	flp->gname = getGname(flp->fl_gid);
	flp->langs = xstrdup("");
	
	fl.totalFileSize += flp->fl_size;
	
	if (! (flp->uname && flp->gname)) {
	    rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s\n"), diskURL);
	    fl.processingFailed = 1;
	}

	isSpec = 0;
	x++;
    }
    fl.fileListRecsUsed = x;
    freeSplitString(files);

    if (! fl.processingFailed) {
	if (spec->sourceHeader != NULL)
	    genCpioListAndHeader(&fl, &spec->sourceCpioList,
			spec->sourceHeader, 1);
    }

    sourceFiles = freeStringBuf(sourceFiles);
    fl.fileList = freeFileList(fl.fileList, fl.fileListRecsUsed);
    return fl.processingFailed;
}

/**
 * Check packaged file list against what's in the build root.
 * @param fileList	packaged file list
 * @return		-1 if skipped, 0 on OK, 1 on error
 */
static int checkFiles(StringBuf fileList)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
/*@-readonlytrans@*/
    static const char * av_ckfile[] = { "%{?__check_files}", NULL };
/*@=readonlytrans@*/
    StringBuf sb_stdout = NULL;
    const char * s;
    int rc;
    
    s = rpmExpand(av_ckfile[0], NULL);
    if (!(s && *s)) {
	rc = -1;
	goto exit;
    }
    rc = 0;

    rpmMessage(RPMMESS_NORMAL, _("Checking for unpackaged file(s): %s\n"), s);

/*@-boundswrite@*/
    rc = rpmfcExec(av_ckfile, fileList, &sb_stdout, 0);
/*@=boundswrite@*/
    if (rc < 0)
	goto exit;
    
    if (sb_stdout) {
	int _unpackaged_files_terminate_build =
		rpmExpandNumeric("%{?_unpackaged_files_terminate_build}");
	const char * t;

	t = getStringBuf(sb_stdout);
	if ((*t != '\0') && (*t != '\n')) {
	    rc = (_unpackaged_files_terminate_build) ? 1 : 0;
	    rpmMessage((rc ? RPMMESS_ERROR : RPMMESS_WARNING),
		_("Installed (but unpackaged) file(s) found:\n%s"), t);
	}
    }
    
exit:
    sb_stdout = freeStringBuf(sb_stdout);
    s = _free(s);
    return rc;
}

/*@-incondefs@*/
int processBinaryFiles(Spec spec, int installSpecialDoc, int test)
	/*@globals check_fileList @*/
	/*@modifies check_fileList @*/
{
    Package pkg;
    int res = 0;
    
    check_fileList = newStringBuf();
    
    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	const char *n, *v, *r;
	int rc;

	if (pkg->fileList == NULL)
	    continue;

	(void) headerNVR(pkg->header, &n, &v, &r);
	rpmMessage(RPMMESS_NORMAL, _("Processing files: %s-%s-%s\n"), n, v, r);
		   
	if ((rc = processPackageFiles(spec, pkg, installSpecialDoc, test)))
	    res = rc;

	/* Finalize package scriptlets before extracting dependencies. */
	if ((rc = processScriptFiles(spec, pkg)))
	    res = rc;

	(void) rpmfcGenerateDepends(spec, pkg);

	/* XXX this should be earlier for deps to be entirely sorted. */
	providePackageNVR(pkg->header);
    }

    /* Now we have in fileList list of files from all packages.
     * We pass it to a script which does the work of finding missing
     * and duplicated files.
     */
    
    if (res == 0)  {
	if (checkFiles(check_fileList) > 0)
	    res = 1;
    }
    
    check_fileList = freeStringBuf(check_fileList);
    
    return res;
}
/*@=incondefs@*/

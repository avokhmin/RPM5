#include "system.h"

#define	MYALLPERMS	07777

#include <regex.h>
#include <signal.h>	/* getOutputFrom() */

#include "rpmbuild.h"
#include "buildio.h"

#include "myftw.h"
#include "md5.h"
#include "rpmmacro.h"

#define	SKIPWHITE(_x)	{while(*(_x) && (isspace(*_x) || *(_x) == ',')) (_x)++;}
#define	SKIPNONWHITE(_x){while(*(_x) &&!(isspace(*_x) || *(_x) == ',')) (_x)++;}

#define MAXDOCDIR 1024

typedef struct {
    struct stat fl_st;
#define	fl_dev	fl_st.st_dev
#define	fl_ino	fl_st.st_ino
#define	fl_mode	fl_st.st_mode
#define	fl_nlink fl_st.st_nlink	/* unused */
#define	fl_uid	fl_st.st_uid
#define	fl_gid	fl_st.st_gid
#define	fl_rdev	fl_st.st_rdev
#define	fl_size	fl_st.st_size
#define	fl_mtime fl_st.st_mtime

    const char *diskName; /* get file from here       */
    const char *fileName; /* filename in cpio archive */
    const char *uname;
    const char *gname;
    int		flags;
    int		verifyFlags;
    const char *langs;	/* XXX locales separated with | */
} FileListRec;

typedef struct {
    const char *ar_fmodestr;
    const char *ar_dmodestr;
    const char *ar_user;
    const char *ar_group;
    mode_t	ar_fmode;
    mode_t	ar_dmode;
} AttrRec;

static AttrRec empty_ar = { NULL, NULL, NULL, NULL, 0, 0 };

struct FileList {
    const char *buildRoot;
    const char *prefix;

    int fileCount;
    int totalFileSize;
    int processingFailed;

    int passedSpecialDoc;
    int isSpecialDoc;
    
    int isDir;
    int inFtw;
    int currentFlags;
    int currentVerifyFlags;
    AttrRec cur_ar;
    AttrRec def_ar;
    int defVerifyFlags;
    int nLangs;
    const char **currentLangs;

    /* Hard coded limit of MAXDOCDIR docdirs.         */
    /* If you break it you are doing something wrong. */
    const char *docDirs[MAXDOCDIR];
    int docDirCount;
    
    FileListRec *fileList;
    int fileListRecsAlloced;
    int fileListRecsUsed;
};

static void freeAttrRec(AttrRec *ar) {
    FREE(ar->ar_fmodestr);
    FREE(ar->ar_dmodestr);
    FREE(ar->ar_user);
    FREE(ar->ar_group);
    /* XXX doesn't free ar (yet) */
}

static void dupAttrRec(AttrRec *oar, AttrRec *nar) {
    if (oar == nar)	/* XXX pathological paranoia */
	return;
    freeAttrRec(nar);
    *nar = *oar;		/* structure assignment */
    if (nar->ar_fmodestr)
	nar->ar_fmodestr = strdup(nar->ar_fmodestr);
    if (nar->ar_dmodestr)
	nar->ar_dmodestr = strdup(nar->ar_dmodestr);
    if (nar->ar_user)
	nar->ar_user = strdup(nar->ar_user);
    if (nar->ar_group)
	nar->ar_group = strdup(nar->ar_group);
}

#if 0
static void dumpAttrRec(const char *msg, AttrRec *ar) {
    if (msg)
	fprintf(stderr, "%s:\t", msg);
    fprintf(stderr, "(%s, %s, %s, %s)\n",
	ar->ar_fmodestr,
	ar->ar_user,
	ar->ar_group,
	ar->ar_dmodestr);
}
#endif

/* glob_pattern_p() taken from bash
 * Copyright (C) 1985, 1988, 1989 Free Software Foundation, Inc.
 *
 * Return nonzero if PATTERN has any special globbing chars in it.
 */
static int myGlobPatternP (const char *pattern)
{
    register const char *p = pattern;
    register char c;
    int open = 0;
  
    while ((c = *p++) != '\0')
	switch (c) {
	case '?':
	case '*':
	    return (1);
	case '[':      /* Only accept an open brace if there is a close */
	    open++;    /* brace to match it.  Bracket expressions must be */
	    continue;  /* complete, according to Posix.2 */
	case ']':
	    if (open)
		return (1);
	    continue;      
	case '\\':
	    if (*p++ == '\0')
		return (0);
	}

    return (0);
}

static int glob_error(const char *foo, int bar)
{
    return 1;
}

/* strtokWithQuotes() modified from glibc strtok() */
/* Copyright (C) 1991, 1996 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

static char *strtokWithQuotes(char *s, char *delim)
{
    static char *olds = NULL;
    char *token;

    if (s == NULL) {
	s = olds;
    }

    /* Skip leading delimiters */
    s += strspn(s, delim);
    if (*s == '\0') {
	return NULL;
    }

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

    return token;
}

static void timeCheck(int tc, Header h)
{
    int *mtime;
    char **file;
    int count, x, currentTime;

    headerGetEntry(h, RPMTAG_FILENAMES, NULL, (void **) &file, &count);
    headerGetEntry(h, RPMTAG_FILEMTIMES, NULL, (void **) &mtime, NULL);

    currentTime = time(NULL);
    
    for (x = 0; x < count; x++) {
	if (currentTime - mtime[x] > tc) {
	    rpmMessage(RPMMESS_WARNING, _("TIMECHECK failure: %s\n"), file[x]);
	}
    }
}

typedef struct VFA {
	char *	attribute;
	int	flag;
} VFA_t;

VFA_t verifyAttrs[] = {
	{ "md5",	RPMVERIFY_MD5 },
	{ "size",	RPMVERIFY_FILESIZE },
	{ "link",	RPMVERIFY_LINKTO },
	{ "user",	RPMVERIFY_USER },
	{ "group",	RPMVERIFY_GROUP },
	{ "mtime",	RPMVERIFY_MTIME },
	{ "mode",	RPMVERIFY_MODE },
	{ "rdev",	RPMVERIFY_RDEV },
	{ NULL, 0 }
};

static int parseForVerify(char *buf, struct FileList *fl)
{
    char *p, *pe, *q;
    const char *name;
    int *resultVerify;
    int not;
    int verifyFlags;

    if ((p = strstr(buf, (name = "%verify"))) != NULL) {
	resultVerify = &(fl->currentVerifyFlags);
    } else if ((p = strstr(buf, (name = "%defverify"))) != NULL) {
	resultVerify = &(fl->defVerifyFlags);
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*verify args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	;

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    not = 0;
    verifyFlags = RPMVERIFY_NONE;

    for (p = q; *p; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe)
	    *pe++ = '\0';

	{   VFA_t *vfa;
	    for (vfa = verifyAttrs; vfa->attribute != NULL; vfa++) {
		if (strcmp(p, vfa->attribute))
		    continue;
		verifyFlags |= vfa->flag;
		break;
	    }
	    if (vfa->attribute)
		continue;
	}

	if (!strcmp(p, "not")) {
	    not ^= 1;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    *resultVerify = not ? ~(verifyFlags) : verifyFlags;

    return 0;
}

#define	isAttrDefault(_ars)	((_ars)[0] == '-' && (_ars)[1] == '\0')

static int parseForAttr(char *buf, struct FileList *fl)
{
    char *p, *pe, *q;
    const char *name;
    int x;
    AttrRec arbuf, *ar = &arbuf, *ret_ar;

    if ((p = strstr(buf, (name = "%attr"))) != NULL) {
	ret_ar = &(fl->cur_ar);
    } else if ((p = strstr(buf, (name = "%defattr"))) != NULL) {
	ret_ar = &(fl->def_ar);
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*attr args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	;

    if (ret_ar == &(fl->def_ar)) {	/* %defattr */
	q = pe;
	q++;
	SKIPSPACE(q);
	if (*q) {
	    rpmError(RPMERR_BADSPEC,
		     _("Non-white space follows %s(): %s"), name, q);
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

    *ar = empty_ar;	/* structure assignment */

    p = q; SKIPWHITE(p);
    if (*p) {
	pe = p; SKIPNONWHITE(pe); if (*pe) *pe++ = '\0';
	ar->ar_fmodestr = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p) {
	pe = p; SKIPNONWHITE(pe); if (*pe) *pe++ = '\0';
	ar->ar_user = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p) {
	pe = p; SKIPNONWHITE(pe); if (*pe) *pe++ = '\0';
	ar->ar_group = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p && ret_ar == &(fl->def_ar)) {	/* %defattr */
	pe = p; SKIPNONWHITE(pe); if (*pe) *pe++ = '\0';
        ar->ar_dmodestr = p;
	p = pe; SKIPWHITE(p);
    }

    if (!(ar->ar_fmodestr && ar->ar_user && ar->ar_group) || *p != '\0') {
	rpmError(RPMERR_BADSPEC, _("Bad syntax: %s(%s)"), name, q);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Do a quick test on the mode argument and adjust for "-" */
    if (ar->ar_fmodestr && !isAttrDefault(ar->ar_fmodestr)) {
	unsigned int ui;
	x = sscanf(ar->ar_fmodestr, "%o", &ui);
	if ((x == 0) || (ar->ar_fmode & ~MYALLPERMS)) {
	    rpmError(RPMERR_BADSPEC, _("Bad mode spec: %s(%s)"), name, q);
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
	    rpmError(RPMERR_BADSPEC, _("Bad dirmode spec: %s(%s)"), name, q);
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
    
    return 0;
}

static int parseForConfig(char *buf, struct FileList *fl)
{
    char *p, *pe, *q;
    const char *name;

    if ((p = strstr(buf, (name = "%config"))) == NULL)
	return 0;

    fl->currentFlags = RPMFILE_CONFIG;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);
    if (*pe != '(')
	return 0;

    /* Bracket %config args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	;

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    for (p = q; *p; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe)
	    *pe++ = '\0';
	if (!strcmp(p, "missingok")) {
	    fl->currentFlags |= RPMFILE_MISSINGOK;
	} else if (!strcmp(p, "noreplace")) {
	    fl->currentFlags |= RPMFILE_NOREPLACE;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    return 0;
}

static int parseForLang(char *buf, struct FileList *fl)
{
    char *p, *pe, *q;
    const char *name;

  while ((p = strstr(buf, (name = "%lang"))) != NULL) {

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %lang args */
    *pe++ = ' ';
    for (pe = p; *pe && *pe != ')'; pe++)
	;

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    /* Parse multiple arguments from %lang */
    for (p = q; *p; p = pe) {
	char *newp;
	size_t np;
	int i;

	SKIPWHITE(p);
	pe = p;
	SKIPNONWHITE(pe);

	np = pe - p;
	
	/* Sanity check on locale lengths */
	if (np < 1 || (np == 1 && *p != 'C') || np >= 16) {
	    rpmError(RPMERR_BADSPEC, _("Unusual locale length: \"%.*s\" in %%lang(%s)"),
		np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Check for duplicate locales */
	for (i = 0; i < fl->nLangs; i++) {
	    if (strncmp(fl->currentLangs[i], p, np))
		continue;
	    rpmError(RPMERR_BADSPEC, _("Duplicate locale %.*s in %%lang(%s)"),
		np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Add new locale */
	fl->currentLangs = (const char **) ((fl->currentLangs == NULL)
	  ? malloc(sizeof(*fl->currentLangs))
	  : realloc(fl->currentLangs,(fl->nLangs+1)*sizeof(*fl->currentLangs)));
	newp = malloc( np+1 );
	strncpy(newp, p, np);
	newp[np] = '\0';
	fl->currentLangs[fl->nLangs++] = newp;
	if (*pe == ',') pe++;	/* skip , if present */
    }
  }
    
    return 0;
}

static int parseForRegexLang(const char *fileName, char **lang)
{
    static int initialized = 0;
    static int hasRegex = 0;
    static regex_t compiledPatt;
    static char buf[BUFSIZ];
    int x;
    regmatch_t matches[2];
    const char *s;

    if (! initialized) {
	const char *patt = rpmExpand("%{_langpatt}", NULL);
	if (!(patt && *patt != '%'))
	    return 1;
	if (regcomp(&compiledPatt, patt, REG_EXTENDED))
	    return -1;
	xfree(patt);
	hasRegex = 1;
	initialized = 1;
    }
    
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

VFA_t virtualFileAttributes[] = {
	{ "%dir",	0 },	/* XXX why not RPMFILE_DIR? */
	{ "%doc",	RPMFILE_DOC },
	{ "%ghost",	RPMFILE_GHOST },
	{ "%readme",	RPMFILE_README },
	{ "%license",	RPMFILE_LICENSE },

#if WHY_NOT
	{ "%spec",	RPMFILE_SPEC },
	{ "%config",	RPMFILE_CONFIG },
	{ "%donotuse",	RPMFILE_DONOTUSE },	/* XXX WTFO? */
	{ "%missingok",	RPMFILE_CONFIG|RPMFILE_MISSINGOK },
	{ "%noreplace",	RPMFILE_CONFIG|RPMFILE_NOREPLACE },
#endif

	{ NULL, 0 }
};

static int parseForSimple(Spec spec, Package pkg, char *buf,
			  struct FileList *fl, const char **fileName)
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
		rpmError(RPMERR_INTERNAL, _("Hit limit for %%docdir"));
		fl->processingFailed = 1;
		res = 1;
	    }
	    fl->docDirs[fl->docDirCount++] = strdup(s);
	    if (strtokWithQuotes(NULL, " \t\n")) {
		rpmError(RPMERR_INTERNAL, _("Only one arg for %%docdir"));
		fl->processingFailed = 1;
		res = 1;
	    }
	    break;
	}

    /* Set flags for virtual file attributes */
    {	VFA_t *vfa;
	for (vfa = virtualFileAttributes; vfa->attribute != NULL; vfa++) {
	    if (strcmp(s, vfa->attribute))
		continue;
	    if (!strcmp(s, "%dir"))
		fl->isDir = 1;	/* XXX why not RPMFILE_DIR? */
	    else
		fl->currentFlags |= vfa->flag;
	    break;
	}
	/* if we got an attribute, continue with next token */
	if (vfa->attribute != NULL)
	    continue;
    }

	if (*fileName) {
	    /* We already got a file -- error */
	    rpmError(RPMERR_BADSPEC, _("Two files on one line: %s"), *fileName);
	    fl->processingFailed = 1;
	    res = 1;
	}

	if (*s != '/') {
	    if (fl->currentFlags & RPMFILE_DOC) {
		specialDoc = 1;
		strcat(specialDocBuf, " ");
		strcat(specialDocBuf, s);
	    } else {
		/* not in %doc, does not begin with / -- error */
		rpmError(RPMERR_BADSPEC,
		    _("File must begin with \"/\": %s"), s);
		fl->processingFailed = 1;
		res = 1;
	    }
	} else {
	    *fileName = s;
	}
    }

    if (specialDoc) {
	if (*fileName || (fl->currentFlags & ~(RPMFILE_DOC))) {
	    rpmError(RPMERR_BADSPEC,
		     _("Can't mix special %%doc with other forms: %s"),
		     *fileName);
	    fl->processingFailed = 1;
	    res = 1;
	} else {
	/* XXX WATCHOUT: buf is an arg */
	    {	const char *ddir, *n, *v;

		headerNVR(pkg->header, &n, &v, NULL);

		ddir = rpmGetPath("%{_docdir}/", n, "-", v, NULL);
		strcpy(buf, ddir);
		xfree(ddir);
	    }

	/* XXX FIXME: this is easy to do as macro expansion */

	    if (! fl->passedSpecialDoc) {
		pkg->specialDoc = newStringBuf();
		appendStringBuf(pkg->specialDoc, "DOCDIR=$RPM_BUILD_ROOT");
		appendLineStringBuf(pkg->specialDoc, buf);
		appendLineStringBuf(pkg->specialDoc, "export DOCDIR");
		appendLineStringBuf(pkg->specialDoc, "rm -rf $DOCDIR");
		appendLineStringBuf(pkg->specialDoc, MKDIR_P " $DOCDIR");

		*fileName = buf;
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

static int compareFileListRecs(const void *ap, const void *bp)
{
    const char *a = ((FileListRec *)ap)->fileName;
    const char *b = ((FileListRec *)bp)->fileName;
    return strcmp(a, b);
}

static int isDoc(struct FileList *fl, const char *fileName)
{
    int x = fl->docDirCount;

    while (x--) {
        if (strstr(fileName, fl->docDirs[x]) == fileName) {
	    return 1;
        }
    }
    return 0;
}

static void genCpioListAndHeader(struct FileList *fl,
				 struct cpioFileMapping **cpioList,
				 int *cpioCount, Header h, int isSrc)
{
    int skipLen;
    int count;
    FileListRec *flp;
    struct cpioFileMapping *clp;
    char *s, buf[BUFSIZ];
    
    /* Sort the big list */
    qsort(fl->fileList, fl->fileListRecsUsed,
	  sizeof(*(fl->fileList)), compareFileListRecs);
    
    /* Generate the cpio list and the header */
    skipLen = 0;
    if (! isSrc) {
	skipLen = 1;
	if (fl->prefix)
	    skipLen += strlen(fl->prefix);
    }

    *cpioCount = 0;
    clp = *cpioList = malloc(sizeof(**cpioList) * fl->fileListRecsUsed);

    for (flp = fl->fileList, count = fl->fileListRecsUsed; count > 0; flp++, count--) {
	if ((count > 1) && !strcmp(flp->fileName, flp[1].fileName)) {
	    rpmError(RPMERR_BADSPEC, _("File listed twice: %s"), flp->fileName);
	    fl->processingFailed = 1;
	}
	
	/* Make the cpio list */
	if (! (flp->flags & RPMFILE_GHOST)) {
	    clp->fsPath = strdup(flp->diskName);
	    clp->archivePath = strdup(flp->fileName + skipLen);
	    clp->finalMode = flp->fl_mode;
	    clp->finalUid = flp->fl_uid;
	    clp->finalGid = flp->fl_gid;
	    clp->mapFlags = CPIO_MAP_PATH | CPIO_MAP_MODE |
		CPIO_MAP_UID | CPIO_MAP_GID;
	    if (isSrc) {
		clp->mapFlags |= CPIO_FOLLOW_SYMLINKS;
	    }
	    clp++;
	    (*cpioCount)++;
	}
	
	/* Make the header */
	headerAddOrAppendEntry(h, RPMTAG_FILENAMES, RPM_STRING_ARRAY_TYPE,
			       &(flp->fileName), 1);

      if (sizeof(flp->fl_size) != sizeof(uint_32)) {
	uint_32 psize = (uint_32)flp->fl_size;
	headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(psize), 1);
      } else {
	headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(flp->fl_size), 1);
      }
	headerAddOrAppendEntry(h, RPMTAG_FILEUSERNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->uname), 1);
	headerAddOrAppendEntry(h, RPMTAG_FILEGROUPNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->gname), 1);
	headerAddOrAppendEntry(h, RPMTAG_FILEMTIMES, RPM_INT32_TYPE,
			       &(flp->fl_mtime), 1);
      if (sizeof(flp->fl_mode) != sizeof(uint_16)) {
	uint_16 pmode = (uint_16)flp->fl_mode;
	headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(pmode), 1);
      } else {
	headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(flp->fl_mode), 1);
      }
      if (sizeof(flp->fl_rdev) != sizeof(uint_16)) {
	uint_16 prdev = (uint_16)flp->fl_rdev;
	headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(prdev), 1);
      } else {
	headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(flp->fl_rdev), 1);
      }
      if (sizeof(flp->fl_dev) != sizeof(uint_32)) {
	uint_32 pdevice = (uint_32)flp->fl_dev;
	headerAddOrAppendEntry(h, RPMTAG_FILEDEVICES, RPM_INT32_TYPE,
			       &(pdevice), 1);
      } else {
	headerAddOrAppendEntry(h, RPMTAG_FILEDEVICES, RPM_INT32_TYPE,
			       &(flp->fl_dev), 1);
      }
	headerAddOrAppendEntry(h, RPMTAG_FILEINODES, RPM_INT32_TYPE,
			       &(flp->fl_ino), 1);

	headerAddOrAppendEntry(h, RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE,
			       &(flp->langs),  1);
	
	/* We used to add these, but they should not be needed */
	/* headerAddOrAppendEntry(h, RPMTAG_FILEUIDS,
	 *		   RPM_INT32_TYPE, &(flp->fl_uid), 1);
	 * headerAddOrAppendEntry(h, RPMTAG_FILEGIDS,
	 *		   RPM_INT32_TYPE, &(flp->fl_gid), 1);
	 */
	
	buf[0] = '\0';
	if (S_ISREG(flp->fl_mode))
	    mdfile(flp->diskName, buf);
	s = buf;
	headerAddOrAppendEntry(h, RPMTAG_FILEMD5S, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	
	buf[0] = '\0';
	if (S_ISLNK(flp->fl_mode))
	    buf[readlink(flp->diskName, buf, BUFSIZ)] = '\0';
	s = buf;
	headerAddOrAppendEntry(h, RPMTAG_FILELINKTOS, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	
	if (flp->flags & RPMFILE_GHOST) {
	    flp->verifyFlags &= ~(RPMVERIFY_MD5 | RPMVERIFY_FILESIZE |
				RPMVERIFY_LINKTO | RPMVERIFY_MTIME);
	}
	headerAddOrAppendEntry(h, RPMTAG_FILEVERIFYFLAGS, RPM_INT32_TYPE,
			       &(flp->verifyFlags), 1);
	
	if (!isSrc && isDoc(fl, flp->fileName))
	    flp->flags |= RPMFILE_DOC;
	if (S_ISDIR(flp->fl_mode))
	    flp->flags &= ~(RPMFILE_CONFIG|RPMFILE_DOC);

	headerAddOrAppendEntry(h, RPMTAG_FILEFLAGS, RPM_INT32_TYPE,
			       &(flp->flags), 1);
    }
    headerAddEntry(h, RPMTAG_SIZE, RPM_INT32_TYPE,
		   &(fl->totalFileSize), 1);
}

static void freeFileList(FileListRec *fileList, int count)
{
    while (count--) {
	FREE(fileList[count].diskName);
	FREE(fileList[count].fileName);
	FREE(fileList[count].langs);
    }
    FREE(fileList);
}

static int addFile(struct FileList *fl, const char *name, struct stat *statp)
{
    char fileName[BUFSIZ];
    char diskName[BUFSIZ];
    struct stat statbuf;
    mode_t fileMode;
    uid_t fileUid;
    gid_t fileGid;
    char *fileUname, *fileGname;
    char *lang;
    
    strcpy(fileName, cleanFileName(name));

    if (fl->inFtw) {
	/* Any buildRoot is already prepended */
	strcpy(diskName, fileName);
	if (fl->buildRoot) {
	    strcpy(fileName, diskName + strlen(fl->buildRoot));
	    /* Special case for "/" */
	    if (*fileName == '\0') {
		fileName[0] = '/';
		fileName[1] = '\0';
	    }
	}
    } else {
	if (fl->buildRoot) {
	    sprintf(diskName, "%s%s", fl->buildRoot, fileName);
	} else {
	    strcpy(diskName, fileName);
	}
    }

    /* If we are using a prefix, validate the file */
    if (!fl->inFtw && fl->prefix) {
	const char *prefixTest = fileName;
	const char *prefixPtr = fl->prefix;

	while (*prefixPtr && *prefixTest && (*prefixTest == *prefixPtr)) {
	    prefixPtr++;
	    prefixTest++;
	}
	if (*prefixPtr || (*prefixTest && *prefixTest != '/')) {
	    rpmError(RPMERR_BADSPEC, _("File doesn't match prefix (%s): %s"),
		     fl->prefix, fileName);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    if (! statp) {
	statp = &statbuf;
	if (lstat(diskName, statp)) {
	    rpmError(RPMERR_BADSPEC, _("File not found: %s"), diskName);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    if ((! fl->isDir) && S_ISDIR(statp->st_mode)) {
	/* We use our own ftw() call, because ftw() uses stat()    */
	/* instead of lstat(), which causes it to follow symlinks! */
	/* It also has better callback support.                    */
	
	fl->inFtw = 1;  /* Flag to indicate file has buildRoot prefixed */
	fl->isDir = 1;  /* Keep it from following myftw() again         */
	myftw(diskName, 16, (myftwFunc) addFile, fl);
	fl->isDir = 0;
	fl->inFtw = 0;
	return 0;
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
	
#if 0	/* XXX this looks dumb to me */
    if (! (fileUname && fileGname)) {
	rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s\n"), diskName);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }
#else
    /* Default user/group to builder's user/group */
    if (fileUname == NULL) {
	fileUname = getUname(getuid());
    }
    if (fileGname == NULL) {
	fileGname = getGname(getgid());
    }
#endif
    
    rpmMessage(RPMMESS_DEBUG, _("File %4d: 0%o %s.%s\t %s\n"), fl->fileCount,
	fileMode, fileUname, fileGname, fileName);

    /* Add to the file list */
    if (fl->fileListRecsUsed == fl->fileListRecsAlloced) {
	fl->fileListRecsAlloced += 128;
	fl->fileList = realloc(fl->fileList,
			fl->fileListRecsAlloced * sizeof(*(fl->fileList)));
    }
	    
    {	FileListRec * flp = &fl->fileList[fl->fileListRecsUsed];

	flp->fl_st = *statp;	/* structure assignment */
	flp->fl_mode = fileMode;
	flp->fl_uid = fileUid;
	flp->fl_gid = fileGid;

	flp->fileName = strdup(fileName);
	flp->diskName = strdup(diskName);
	flp->uname = fileUname;
	flp->gname = fileGname;

	if (fl->currentLangs && fl->nLangs > 0) {
	    char *ncl;
	    size_t nl = 0;
	    int i;
	    
	    for (i = 0; i < fl->nLangs; i++)
		nl += strlen(fl->currentLangs[i]) + 1;

	    flp->langs = ncl = malloc(nl);
	    for (i = 0; i < fl->nLangs; i++) {
	        const char *ocl;
		if (i)	*ncl++ = '|';
		for (ocl = fl->currentLangs[i]; *ocl; ocl++)
			*ncl++ = *ocl;
		*ncl = '\0';
	    }
	} else if (! parseForRegexLang(fileName, &lang)) {
	    flp->langs = strdup(lang);
	} else {
	    flp->langs = strdup("");
	}

	flp->flags = fl->currentFlags;
	flp->verifyFlags = fl->currentVerifyFlags;

	fl->totalFileSize += flp->fl_size;
    }

    fl->fileListRecsUsed++;
    fl->fileCount++;

    return 0;
}

static int processBinaryFile(Package pkg, struct FileList *fl, const char *fileName)
{
    char fullname[BUFSIZ];
    glob_t glob_result;
    int x, offset, rc = 0;
    
    /* check that file starts with leading "/" */
    if (*fileName != '/') {
	rpmError(RPMERR_BADSPEC, _("File needs leading \"/\": %s"), fileName);
	fl->processingFailed = 1;
	return 1;
    }
    
    if (myGlobPatternP(fileName)) {
	if (fl->buildRoot) {
	    sprintf(fullname, "%s%s", fl->buildRoot, fileName);
	    offset = strlen(fl->buildRoot);
	} else {
	    strcpy(fullname, fileName);
	    offset = 0;
	}
	
	if (glob(fullname, 0, glob_error, &glob_result) ||
	    (glob_result.gl_pathc < 1)) {
	    rpmError(RPMERR_BADSPEC, _("File not found: %s"), fullname);
	    fl->processingFailed = 1;
	    globfree(&glob_result);
	    return 1;
	}
	
	for (x = 0; x < glob_result.gl_pathc; x++) {
	    rc = addFile(fl, &(glob_result.gl_pathv[x][offset]), NULL);
	}
	globfree(&glob_result);
    } else {
	rc = addFile(fl, fileName, NULL);
    }

    return rc;
}

static int processPackageFiles(Spec spec, Package pkg,
			       int installSpecialDoc, int test)
{
    struct FileList fl;
    char *s, **files, **fp;
    const char *fileName;
    char buf[BUFSIZ];
    FILE *f;
    AttrRec specialDocAttrRec = empty_ar;	/* structure assignment */
    char *specialDoc = NULL;
    
    pkg->cpioList = NULL;
    pkg->cpioCount = 0;

    if (pkg->fileFile) {
	const char *ffn;

	/* XXX FIXME: add %{_buildsubdir} and use rpmGetPath() */
	ffn = rpmGetPath("%{_builddir}/",
	    (spec->buildSubdir ? spec->buildSubdir : "") ,
	    "/", pkg->fileFile, NULL);

	if ((f = fopen(ffn, "r")) == NULL) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open %%files file: %s"), pkg->fileFile);
	    FREE(ffn);
	    return RPMERR_BADFILENAME;
	}
	while (fgets(buf, sizeof(buf), f)) {
	    handleComments(buf);
	    if (expandMacros(spec, spec->macros, buf, sizeof(buf))) {
		rpmError(RPMERR_BADSPEC, _("line: %s"), buf);
		return RPMERR_BADSPEC;
	    }
	    appendStringBuf(pkg->fileList, buf);
	}
	fclose(f);
	FREE(ffn);
    }
    
    /* Init the file list structure */
    
    /* XXX spec->buildRoot == NULL, then strdup("") is returned */
    fl.buildRoot = rpmGetPath(spec->buildRoot, NULL);

    if (headerGetEntry(pkg->header, RPMTAG_DEFAULTPREFIX,
		       NULL, (void **)&fl.prefix, NULL)) {
	fl.prefix = strdup(fl.prefix);
    } else {
	fl.prefix = NULL;
    }

    fl.fileCount = 0;
    fl.totalFileSize = 0;
    fl.processingFailed = 0;

    fl.passedSpecialDoc = 0;
    
    fl.cur_ar = empty_ar;	/* structure assignment */
    fl.def_ar = empty_ar;	/* structure assignment */

    fl.nLangs = 0;
    fl.currentLangs = NULL;

    fl.defVerifyFlags = RPMVERIFY_ALL;

    fl.docDirCount = 0;
    fl.docDirs[fl.docDirCount++] = strdup("/usr/doc");
    fl.docDirs[fl.docDirCount++] = strdup("/usr/man");
    fl.docDirs[fl.docDirCount++] = strdup("/usr/info");
    fl.docDirs[fl.docDirCount++] = strdup("/usr/X11R6/man");
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_docdir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_mandir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_infodir}", NULL);
    
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
	strcpy(buf, s);
	
	/* Reset for a new line in %files */
	fl.isDir = 0;
	fl.inFtw = 0;
	fl.currentVerifyFlags = fl.defVerifyFlags;
	fl.currentFlags = 0;
	fl.isSpecialDoc = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		xfree(fl.currentLangs[i]);
	    FREE(fl.currentLangs);
	}
  	fl.nLangs = 0;

	dupAttrRec(&fl.def_ar, &fl.cur_ar);

	if (parseForVerify(buf, &fl))
	    continue;
	if (parseForAttr(buf, &fl))
	    continue;
	if (parseForConfig(buf, &fl))
	    continue;
	if (parseForLang(buf, &fl))
	    continue;
	if (parseForSimple(spec, pkg, buf, &fl, &fileName))
	    continue;
	if (fileName == NULL)
	    continue;

	if (fl.isSpecialDoc) {
	    /* Save this stuff for last */
	    FREE(specialDoc);
	    specialDoc = strdup(fileName);
	    dupAttrRec(&fl.cur_ar, &specialDocAttrRec);
	} else {
	    processBinaryFile(pkg, &fl, fileName);
	}
    }

    /* Now process special doc, if there is one */
    if (specialDoc) {
	if (installSpecialDoc) {
	    doScript(spec, RPMBUILD_STRINGBUF, "%doc", pkg->specialDoc, test);
	}

	/* Reset for %doc */
	fl.isDir = 0;
	fl.inFtw = 0;
	fl.currentFlags = 0;
	fl.currentVerifyFlags = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		xfree(fl.currentLangs[i]);
	    FREE(fl.currentLangs);
	}
  	fl.nLangs = 0;

	dupAttrRec(&specialDocAttrRec, &fl.cur_ar);
	freeAttrRec(&specialDocAttrRec);

	processBinaryFile(pkg, &fl, specialDoc);

	FREE(specialDoc);
    }
    
    freeSplitString(files);

    if (! fl.processingFailed) {
	genCpioListAndHeader(&fl, &(pkg->cpioList), &(pkg->cpioCount),
			     pkg->header, 0);

	if (spec->timeCheck) {
	    timeCheck(spec->timeCheck, pkg->header);
	}
    }
    
    /* Clean up */
    FREE(fl.buildRoot);
    FREE(fl.prefix);

    freeAttrRec(&fl.cur_ar);
    freeAttrRec(&fl.def_ar);

    if (fl.currentLangs) {
	int i;
	for (i = 0; i < fl.nLangs; i++)
	    xfree(fl.currentLangs[i]);
	FREE(fl.currentLangs);
    }

    freeFileList(fl.fileList, fl.fileListRecsUsed);
    while (fl.docDirCount--) {
        FREE(fl.docDirs[fl.docDirCount]);
    }
    return fl.processingFailed;
}

void initSourceHeader(Spec spec)
{
    HeaderIterator hi;
    int tag, type, count;
    void * ptr;

    spec->sourceHeader = headerNew();
    /* Only specific tags are added to the source package header */
    hi = headerInitIterator(spec->packages->header);
    while (headerNextIterator(hi, &tag, &type, &ptr, &count)) {
	switch (tag) {
	  case RPMTAG_NAME:
	  case RPMTAG_VERSION:
	  case RPMTAG_RELEASE:
	  case RPMTAG_EPOCH:
	  case RPMTAG_SUMMARY:
	  case RPMTAG_DESCRIPTION:
	  case RPMTAG_PACKAGER:
	  case RPMTAG_DISTRIBUTION:
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
	    headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
	    break;
	  default:
	    /* do not copy */
	    break;
	}
	if (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE) {
	    FREE(ptr);
	}
    }
    headerFreeIterator(hi);

    /* Add the build restrictions */
    hi = headerInitIterator(spec->buildRestrictions);
    while (headerNextIterator(hi, &tag, &type, &ptr, &count)) {
	headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
	if (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE)
	    FREE(ptr);
    }
    headerFreeIterator(hi);

    if (spec->buildArchitectureCount) {
	headerAddEntry(spec->sourceHeader, RPMTAG_BUILDARCHS,
		       RPM_STRING_ARRAY_TYPE,
		       spec->buildArchitectures, spec->buildArchitectureCount);
    }
}

int processSourceFiles(Spec spec)
{
    struct Source *srcPtr;
    StringBuf sourceFiles;
    int x, isSpec = 1;
    struct FileList fl;
    char *s, **files, **fp, *fn;
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
    for (srcPtr = spec->sources; srcPtr != NULL; srcPtr = srcPtr->next) {
	if (srcPtr->flags & RPMBUILD_ISSOURCE) {
	    headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_SOURCE,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOSOURCE,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}
	if (srcPtr->flags & RPMBUILD_ISPATCH) {
	    headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_PATCH,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOPATCH,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}

      {	const char *s;
	s = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	appendLineStringBuf(sourceFiles, s);
	xfree(s);
      }
    }

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	for (srcPtr = pkg->icon; srcPtr != NULL; srcPtr = srcPtr->next) {
	    const char *s;
	    s = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	    appendLineStringBuf(sourceFiles, s);
	    xfree(s);
	}
    }

    spec->sourceCpioList = NULL;
    spec->sourceCpioCount = 0;

    fl.fileList = malloc((spec->numSources + 1) * sizeof(FileListRec));
    fl.processingFailed = 0;
    fl.fileListRecsUsed = 0;
    fl.totalFileSize = 0;
    fl.prefix = NULL;

    s = getStringBuf(sourceFiles);
    files = splitString(s, strlen(s), '\n');

    /* The first source file is the spec file */
    x = 0;
    for (fp = files; *fp != NULL; fp++) {
	FileListRec *flp;
	s = *fp;
	SKIPSPACE(s);
	if (! *s)
	    continue;

	flp = &fl.fileList[x];

	flp->flags = isSpec ? RPMFILE_SPECFILE : 0;
	/* files with leading ! are no source files */
	if (*s == '!') {
	    flp->flags |= RPMFILE_GHOST;
	    s++;
	}
	flp->diskName = strdup(s);
	fn = strrchr(s, '/');
	if (fn) {
	    fn++;
	} else {
	    fn = s;
	}
	flp->fileName = strdup(fn);
	flp->verifyFlags = RPMVERIFY_ALL;

	stat(s, &flp->fl_st);

	flp->uname = getUname(flp->fl_uid);
	flp->gname = getGname(flp->fl_gid);
	flp->langs = strdup("");
	
	fl.totalFileSize += flp->fl_size;
	
	if (! (flp->uname && flp->gname)) {
	    rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s"), s);
	    fl.processingFailed = 1;
	}

	isSpec = 0;
	x++;
    }
    fl.fileListRecsUsed = x;
    freeSplitString(files);

    if (! fl.processingFailed) {
	genCpioListAndHeader(&fl, &(spec->sourceCpioList),
			     &(spec->sourceCpioCount), spec->sourceHeader, 1);
    }

    freeStringBuf(sourceFiles);
    freeFileList(fl.fileList, fl.fileListRecsUsed);
    return fl.processingFailed;
}

static StringBuf getOutputFrom(char *dir, char *argv[],
			const char *writePtr, int writeBytesLeft,
			int failNonZero)
{
    int progPID;
    int toProg[2];
    int fromProg[2];
    int status;
    void *oldhandler;
    StringBuf readBuff;
    unsigned char buf[BUFSIZ+1];
    int done;

    oldhandler = signal(SIGPIPE, SIG_IGN);

    pipe(toProg);
    pipe(fromProg);
    
    if (!(progPID = fork())) {
	close(toProg[1]);
	close(fromProg[0]);
	
	dup2(toProg[0], STDIN_FILENO);   /* Make stdin the in pipe */
	dup2(fromProg[1], STDOUT_FILENO); /* Make stdout the out pipe */

	close(toProg[0]);
	close(fromProg[1]);

	if (dir) {
	    chdir(dir);
	}
	
	execvp(argv[0], argv);
	rpmError(RPMERR_EXEC, _("Couldn't exec %s"), argv[0]);
	_exit(RPMERR_EXEC);
    }
    if (progPID < 0) {
	rpmError(RPMERR_FORK, _("Couldn't fork %s"), argv[0]);
	return NULL;
    }

    close(toProg[0]);
    close(fromProg[1]);

    /* Do not block reading or writing from/to prog. */
    fcntl(fromProg[0], F_SETFL, O_NONBLOCK);
    fcntl(toProg[1], F_SETFL, O_NONBLOCK);
    
    readBuff = newStringBuf();

    do {
	fd_set ibits, obits;
	struct timeval tv;
	int nfd, nbw, nbr;
	int rc;

	done = 0;
top:
	/* XXX the select is mainly a timer since all I/O is non-blocking */
	FD_ZERO(&ibits);
	FD_ZERO(&obits);
	if (fromProg[0] >= 0) {
	    FD_SET(fromProg[0], &ibits);
	}
	if (toProg[1] >= 0) {
	    FD_SET(toProg[1], &obits);
	}
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	nfd = ((fromProg[0] > toProg[1]) ? fromProg[0] : toProg[1]);
	if ((rc = select(nfd, &ibits, &obits, NULL, &tv)) < 0) {
	    if (errno == EINTR)
		goto top;
	    break;
	}

	/* Write any data to program */
	if (toProg[1] >= 0 && FD_ISSET(toProg[1], &obits)) {
          if (writeBytesLeft) {
	    if ((nbw = write(toProg[1], writePtr,
		    (1024<writeBytesLeft) ? 1024 : writeBytesLeft)) < 0) {
	        if (errno != EAGAIN) {
		    perror("getOutputFrom()");
	            exit(EXIT_FAILURE);
		}
	        nbw = 0;
	    }
	    writeBytesLeft -= nbw;
	    writePtr += nbw;
	  } else if (toProg[1] >= 0) {	/* close write fd */
	    close(toProg[1]);
	    toProg[1] = -1;
	  }
	}
	
	/* Read any data from prog */
	while ((nbr = read(fromProg[0], buf, sizeof(buf)-1)) > 0) {
	    buf[nbr] = '\0';
	    appendStringBuf(readBuff, buf);
	}

	/* terminate on (non-blocking) EOF or error */
	done = (nbr == 0 || (nbr < 0 && errno != EAGAIN));

    } while (!done);

    /* Clean up */
    if (toProg[1] >= 0)
    	close(toProg[1]);
    if (fromProg[0] >= 0)
	close(fromProg[0]);
    (void)signal(SIGPIPE, oldhandler);

    /* Collect status from prog */
    (void)waitpid(progPID, &status, 0);
    if (failNonZero && (!WIFEXITED(status) || WEXITSTATUS(status))) {
	rpmError(RPMERR_EXEC, _("%s failed"), argv[0]);
	return NULL;
    }
    if (writeBytesLeft) {
	rpmError(RPMERR_EXEC, _("failed to write all data to %s"), argv[0]);
	return NULL;
    }
    return readBuff;
}

typedef struct {
    const char *msg;
    const char *argv[4];
    int ntag;
    int vtag;
    int ftag;
    int mask;
    int xor;
} DepMsg_t;

DepMsg_t depMsgs[] = {
  { "Provides",		{ "%{__find_provides}", NULL, NULL, NULL },
	RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS,
	0, -1 },
  { "PreReq",		{ "%{__find_prereq}", NULL, NULL, NULL },
	RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS,
	RPMSENSE_PREREQ, 0 },
  { "Requires",		{ "%{__find_requires}", NULL, NULL, NULL },
	-1, -1, RPMTAG_REQUIREFLAGS,	/* XXX inherit name/version arrays */
	RPMSENSE_PREREQ, RPMSENSE_PREREQ },
  { "Conflicts",	{ "%{__find_conflicts}", NULL, NULL, NULL },
	RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS,
	0, -1 },
  { "Obsoletes",	{ "%{__find_obsoletes}", NULL, NULL, NULL },
	RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS,
	0, -1 },
  { NULL,		{ NULL, NULL, NULL, NULL },	0, 0, 0, 0, 0 }
};

static int generateDepends(Spec spec, Package pkg,
			struct cpioFileMapping *cpioList, int cpioCount)
{
    StringBuf writeBuf;
    int writeBytes;
    StringBuf readBuf;
    DepMsg_t *dm;
    char *myargv[4];
    int rc = 0;

    if (cpioCount <= 0)
	return 0;

    if (! (pkg->autoReq || pkg->autoProv))
	return 0;
    
    writeBuf = newStringBuf();
    writeBytes = 0;
    while (cpioCount--) {
	writeBytes += strlen(cpioList->fsPath) + 1;
	appendLineStringBuf(writeBuf, cpioList->fsPath);
	cpioList++;
    }

    for (dm = depMsgs; dm->msg != NULL; dm++) {
	int i, tag, failnonzero;

	tag = (dm->ftag > 0) ? dm->ftag : dm->ntag;

	switch(tag) {
	case RPMTAG_PROVIDEFLAGS:
	    if (!pkg->autoProv)
		continue;
	    failnonzero = 1;
	    break;
	case RPMTAG_REQUIREFLAGS:
	    if (!pkg->autoReq)
		continue;
	    failnonzero = 0;
	    break;
	default:
	    continue;
	    break;
	}

	/* Get the script name to run */
	myargv[0] = rpmExpand(dm->argv[0], NULL);

	if (!(myargv[0] && *myargv[0] != '%'))
	    continue;

	rpmMessage(RPMMESS_NORMAL, _("Finding  %s: (using %s)...\n"),
		dm->msg, myargv[0]);

#if 0
	if (*myargv[0] != '/') {	/* XXX FIXME: stat script here */
	    free(myargv[0]);
	    myargv[0] = NULL;
	    continue;
	}
#endif

	/* Expand rest of script arguments (if any) */
	for (i = 1; i < 4; i++) {
	    myargv[i] = dm->argv[i] ? rpmExpand(dm->argv[i], NULL) : NULL;
	}

	readBuf = getOutputFrom(NULL, myargv,
			getStringBuf(writeBuf), writeBytes, failnonzero);

	/* Free expanded args */
	for (i = 0; i < 4; i++) {
	    if (myargv[i] == NULL) continue;
	    free(myargv[i]);
	    myargv[i] = NULL;
	}

	if (readBuf == NULL) {
	    rc = RPMERR_EXEC;
	    rpmError(rc, _("Failed to find %s:"), dm->msg);
	    break;
	}

	/* Parse dependencies into header */
	rc = parseRCPOT(spec, pkg, getStringBuf(readBuf), tag, 0);
	freeStringBuf(readBuf);

	if (rc) {
	    rpmError(rc, _("Failed to find %s:"), dm->msg);
	    break;
	}
    }

    freeStringBuf(writeBuf);
    return rc;
}

static void printDepMsg(DepMsg_t *dm, int count, const char **names,
	const char **versions, int *flags)
{
    int hasVersions = (versions != NULL);
    int hasFlags = (flags != NULL);
    int i, bingo;

    bingo = 0;
    for (i = 0; i < count; i++, names++, versions++, flags++) {
	if (hasFlags && !((*flags & dm->mask) ^ dm->xor))
	    continue;
	if (bingo == 0) {
	    rpmMessage(RPMMESS_NORMAL, "%s:", dm->msg);
	    bingo = 1;
	}
	rpmMessage(RPMMESS_NORMAL, " %s", *names);

	if (hasVersions && !(*versions != NULL && **versions != '\0'))
	    continue;
	if (!(hasFlags && (*flags && RPMSENSE_SENSEMASK)))
	    continue;

	rpmMessage(RPMMESS_NORMAL, " ");
	if (*flags & RPMSENSE_LESS)
	    rpmMessage(RPMMESS_NORMAL, "<");
	if (*flags & RPMSENSE_GREATER)
	    rpmMessage(RPMMESS_NORMAL, ">");
	if (*flags & RPMSENSE_EQUAL)
	    rpmMessage(RPMMESS_NORMAL, "=");

	rpmMessage(RPMMESS_NORMAL, " %s", *versions);
    }
    if (bingo)
	rpmMessage(RPMMESS_NORMAL, "\n");
}

static void printDeps(Header h)
{
    const char **names = NULL;
    const char **versions = NULL;
    int *flags = NULL;
    DepMsg_t *dm;
    int type, count;

    for (dm = depMsgs; dm->msg != NULL; dm++) {
	switch (dm->ntag) {
	case 0:
	    FREE(names);
	    break;
	case -1:
	    break;
	default:
	    FREE(names);
	    if (!headerGetEntry(h, dm->ntag, &type, (void **) &names, &count))
		continue;
	    break;
	}
	switch (dm->vtag) {
	case 0:
	    FREE(versions);
	    break;
	case -1:
	    break;
	default:
	    FREE(versions);
	    headerGetEntry(h, dm->vtag, NULL, (void **) &versions, NULL);
	    break;
	}
	switch (dm->ftag) {
	case 0:
	    flags = NULL;
	    break;
	case -1:
	    break;
	default:
	    headerGetEntry(h, dm->ftag, NULL, (void **) &flags, NULL);
	    break;
	}
	printDepMsg(dm, count, names, versions, flags);
    }
    FREE(names);
    FREE(versions);
}

int processBinaryFiles(Spec spec, int installSpecialDoc, int test)
{
    Package pkg;
    int res, rc;
    char *name;
    
    res = 0;
    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	if (pkg->fileList == NULL) {
	    continue;
	}

	headerGetEntry(pkg->header, RPMTAG_NAME, NULL, (void **)&name, NULL);
	rpmMessage(RPMMESS_NORMAL, _("Processing files: %s\n"), name);
		   
	if ((rc = processPackageFiles(spec, pkg, installSpecialDoc, test))) {
	    res = rc;
	}

	generateDepends(spec, pkg, pkg->cpioList, pkg->cpioCount);
	printDeps(pkg->header);
	
    }

    return res;
}

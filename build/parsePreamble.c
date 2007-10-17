/** \ingroup rpmbuild
 * \file build/parsePreamble.c
 *  Parse tags in global section from spec file.
 */

#include "system.h"

#include <rpmio_internal.h>
#define	_RPMEVR_INTERNAL
#include <rpmbuild.h>
#include "debug.h"

/*@access FD_t @*/	/* compared with NULL */

/**
 */
/*@observer@*/ /*@unchecked@*/
static rpmTag copyTagsDuringParse[] = {
    RPMTAG_EPOCH,
    RPMTAG_VERSION,
    RPMTAG_RELEASE,
    RPMTAG_LICENSE,
    RPMTAG_PACKAGER,
    RPMTAG_DISTRIBUTION,
    RPMTAG_DISTURL,
    RPMTAG_VENDOR,
    RPMTAG_ICON,
    RPMTAG_GIF,
    RPMTAG_XPM,
    RPMTAG_URL,
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    RPMTAG_PREFIXES,
    RPMTAG_DISTTAG,
    RPMTAG_CVSID,
    RPMTAG_VARIANTS,
    RPMTAG_XMAJOR,
    RPMTAG_XMINOR,
    RPMTAG_REPOTAG,
    RPMTAG_KEYWORDS,
    0
};

/**
 */
/*@observer@*/ /*@unchecked@*/
static rpmTag requiredTags[] = {
    RPMTAG_NAME,
    RPMTAG_VERSION,
    RPMTAG_RELEASE,
    RPMTAG_SUMMARY,
    RPMTAG_GROUP,
    RPMTAG_LICENSE,
    0
};

/**
 */
static void addOrAppendListEntry(Header h, int_32 tag, char * line)
	/*@modifies h @*/
{
    int xx;
    int argc;
    const char **argv;

    xx = poptParseArgvString(line, &argc, &argv);
    if (argc)
	xx = headerAddOrAppendEntry(h, tag, RPM_STRING_ARRAY_TYPE, argv, argc);
    argv = _free(argv);
}

/* Parse a simple part line that only take -n <pkg> or <pkg> */
/* <pkg> is return in name as a pointer into a static buffer */

/**
 */
/*@-boundswrite@*/
static int parseSimplePart(char *line, /*@out@*/char **name,
		/*@out@*/rpmParseState *flag)
	/*@globals internalState@*/
	/*@modifies *name, *flag, internalState @*/
{
    char *tok;
    char linebuf[BUFSIZ];
    static char buf[BUFSIZ];

    strcpy(linebuf, line);

    /* Throw away the first token (the %xxxx) */
    (void)strtok(linebuf, " \t\n");
    
    if (!(tok = strtok(NULL, " \t\n"))) {
	*name = NULL;
	return 0;
    }
    
    if (!strcmp(tok, "-n")) {
	if (!(tok = strtok(NULL, " \t\n")))
	    return 1;
	*flag = PART_NAME;
    } else {
	*flag = PART_SUBNAME;
    }
    strcpy(buf, tok);
    *name = buf;

    return (strtok(NULL, " \t\n")) ? 1 : 0;
}
/*@=boundswrite@*/

/**
 */
static inline int parseYesNo(const char * s)
	/*@*/
{
    return ((!s || (s[0] == 'n' || s[0] == 'N' || s[0] == '0') ||
	!xstrcasecmp(s, "false") || !xstrcasecmp(s, "off"))
	    ? 0 : 1);
}

typedef struct tokenBits_s {
/*@observer@*/ /*@null@*/
    const char * name;
    rpmsenseFlags bits;
} * tokenBits;

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct tokenBits_s installScriptBits[] = {
    { "interp",		RPMSENSE_INTERP },
    { "preun",		RPMSENSE_SCRIPT_PREUN },
    { "pre",		RPMSENSE_SCRIPT_PRE },
    { "postun",		RPMSENSE_SCRIPT_POSTUN },
    { "post",		RPMSENSE_SCRIPT_POST },
    { "rpmlib",		RPMSENSE_RPMLIB },
    { "verify",		RPMSENSE_SCRIPT_VERIFY },
    { "hint",		RPMSENSE_MISSINGOK },
    { NULL, 0 }
};

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct tokenBits_s buildScriptBits[] = {
    { "prep",		RPMSENSE_SCRIPT_PREP },
    { "build",		RPMSENSE_SCRIPT_BUILD },
    { "install",	RPMSENSE_SCRIPT_INSTALL },
    { "clean",		RPMSENSE_SCRIPT_CLEAN },
    { "hint",		RPMSENSE_MISSINGOK },
    { NULL, 0 }
};

/**
 */
/*@-boundswrite@*/
static int parseBits(const char * s, const tokenBits tokbits,
		/*@out@*/ rpmsenseFlags * bp)
	/*@modifies *bp @*/
{
    tokenBits tb;
    const char * se;
    rpmsenseFlags bits = RPMSENSE_ANY;
    int c = 0;

    if (s) {
	while (*s != '\0') {
	    while ((c = *s) && xisspace(c)) s++;
	    se = s;
	    while ((c = *se) && xisalpha(c)) se++;
	    if (s == se)
		break;
	    for (tb = tokbits; tb->name; tb++) {
		if (tb->name != NULL &&
		    strlen(tb->name) == (se-s) && !strncmp(tb->name, s, (se-s)))
		    /*@innerbreak@*/ break;
	    }
	    if (tb->name == NULL)
		break;
	    bits |= tb->bits;
	    while ((c = *se) && xisspace(c)) se++;
	    if (c != ',')
		break;
	    s = ++se;
	}
    }
    if (c == 0 && bp) *bp = bits;
    return (c ? RPMRC_FAIL : RPMRC_OK);
}
/*@=boundswrite@*/

/**
 */
static inline char * findLastChar(char * s)
	/*@modifies *s @*/
{
    char *se = s + strlen(s);

/*@-bounds@*/
    while (--se > s && strchr(" \t\n\r", *se) != NULL)
	*se = '\0';
/*@=bounds@*/
/*@-temptrans -retalias @*/
    return se;
/*@=temptrans =retalias @*/
}

/**
 */
static int isMemberInEntry(Header h, const char *name, rpmTag tag)
	/*@*/
{
    HGE_t hge = (HGE_t)headerGetExtension;
    rpmTagType he_t = 0;
    hRET_t he_p = { .ptr = NULL };
    rpmTagCount he_c = 0;
    HE_s he_s = { .tag = 0, .t = &he_t, .p = &he_p, .c = &he_c, .freeData = 0 };
    HE_t he = &he_s;
    int xx;

    he->tag = tag;
    xx = hge(h, he->tag, he->t, he->p, he->c);
    if (!xx)
	return -1;
/*@-boundsread@*/
    while (he_c--) {
	if (!xstrcasecmp(he_p.argv[he_c], name))
	    break;
    }
    he_p.ptr = _free(he_p.ptr);
/*@=boundsread@*/
    return (he_c >= 0 ? 1 : 0);
}

/**
 */
static int checkForValidArchitectures(Spec spec)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies rpmGlobalMacroContext @*/
{
    const char *arch = rpmExpand("%{_target_cpu}", NULL);
    const char *os = rpmExpand("%{_target_os}", NULL);
    int rc = RPMRC_FAIL;	/* assume failure. */
    
    if (isMemberInEntry(spec->sourceHeader, arch, RPMTAG_EXCLUDEARCH) == 1) {
	rpmlog(RPMLOG_ERR, _("Architecture is excluded: %s\n"), arch);
	goto exit;
    }
    if (isMemberInEntry(spec->sourceHeader, arch, RPMTAG_EXCLUSIVEARCH) == 0) {
	rpmlog(RPMLOG_ERR, _("Architecture is not included: %s\n"), arch);
	goto exit;
    }
    if (isMemberInEntry(spec->sourceHeader, os, RPMTAG_EXCLUDEOS) == 1) {
	rpmlog(RPMLOG_ERR, _("OS is excluded: %s\n"), os);
	goto exit;
    }
    if (isMemberInEntry(spec->sourceHeader, os, RPMTAG_EXCLUSIVEOS) == 0) {
	rpmlog(RPMLOG_ERR, _("OS is not included: %s\n"), os);
	goto exit;
    }
    rc = 0;
exit:
    arch = _free(arch);
    os = _free(os);
    return rc;
}

/**
 * Check that required tags are present in header.
 * @param h		header
 * @param NVR		package name-version-release
 * @return		0 if OK
 */
static int checkForRequired(Header h, const char * NVR)
	/*@modifies h @*/ /* LCL: parse error here with modifies */
{
    int res = 0;
    rpmTag * p;

/*@-boundsread@*/
    for (p = requiredTags; *p != 0; p++) {
	if (!headerIsEntry(h, *p)) {
	    rpmlog(RPMLOG_ERR,
			_("%s field must be present in package: %s\n"),
			tagName(*p), NVR);
	    res = 1;
	}
    }
/*@=boundsread@*/

    return res;
}

/**
 * Check that no duplicate tags are present in header.
 * @param h		header
 * @param NVR		package name-version-release
 * @return		0 if OK
 */
static int checkForDuplicates(Header h, const char * NVR)
	/*@modifies h @*/
{
    int res = 0;
    int lastTag, tag;
    HeaderIterator hi;
    
    for (hi = headerInitIterator(h), lastTag = 0;
	headerNextIterator(hi, &tag, NULL, NULL, NULL);
	lastTag = tag)
    {
	if (tag != lastTag)
	    continue;
	rpmlog(RPMLOG_ERR, _("Duplicate %s entries in package: %s\n"),
		     tagName(tag), NVR);
	res = 1;
    }
    hi = headerFreeIterator(hi);

    return res;
}

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct optionalTag {
    rpmTag	ot_tag;
/*@observer@*/ /*@null@*/
    const char * ot_mac;
} optionalTags[] = {
    { RPMTAG_VENDOR,		"%{vendor}" },
    { RPMTAG_PACKAGER,		"%{packager}" },
    { RPMTAG_DISTRIBUTION,	"%{distribution}" },
    { RPMTAG_DISTURL,		"%{disturl}" },
    { -1, NULL }
};

/**
 */
static void fillOutMainPackage(Header h)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies h, rpmGlobalMacroContext @*/
{
    struct optionalTag *ot;

    for (ot = optionalTags; ot->ot_mac != NULL; ot++) {
	if (!headerIsEntry(h, ot->ot_tag)) {
/*@-boundsread@*/
	    const char *val = rpmExpand(ot->ot_mac, NULL);
	    if (val && *val != '%')
		(void) headerAddEntry(h, ot->ot_tag, RPM_STRING_TYPE, (void *)val, 1);
	    val = _free(val);
/*@=boundsread@*/
	}
    }
}

/**
 */
/*@-boundswrite@*/
static int doIcon(Spec spec, Header h)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState  @*/
{
    const char *fn, *Lurlfn = NULL;
    struct Source *sp;
    size_t iconsize = 2048;	/* XXX big enuf */
    size_t nb;
    char *icon = alloca(iconsize+1);
    FD_t fd = NULL;
    int rc = RPMRC_FAIL;	/* assume error */
    int urltype;
    int xx;

    for (sp = spec->sources; sp != NULL; sp = sp->next) {
	if (sp->flags & RPMFILE_ICON)
	    break;
    }
    if (sp == NULL) {
	rpmlog(RPMLOG_ERR, _("No icon file in sources\n"));
	goto exit;
    }

    Lurlfn = rpmGenPath(NULL, "%{_icondir}/", sp->source);

    fn = NULL;
    urltype = urlPath(Lurlfn, &fn);
    switch (urltype) {  
    case URL_IS_HTTPS: 
    case URL_IS_HTTP:
    case URL_IS_FTP:
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	break;
    case URL_IS_DASH:
    case URL_IS_HKP:
	rpmlog(RPMLOG_ERR, _("Invalid icon URL: %s\n"), Lurlfn);
	goto exit;
	/*@notreached@*/ break;
    }

    fd = Fopen(fn, "r.fdio");
    if (fd == NULL || Ferror(fd)) {
	rpmlog(RPMLOG_ERR, _("Unable to open icon %s: %s\n"),
		fn, Fstrerror(fd));
	rc = RPMRC_FAIL;
	goto exit;
    }

    *icon = '\0';
    nb = Fread(icon, sizeof(icon[0]), iconsize, fd);
    if (Ferror(fd) || nb == 0) {
	rpmlog(RPMLOG_ERR, _("Unable to read icon %s: %s\n"),
		fn, Fstrerror(fd));
	goto exit;
    }
    if (nb >= iconsize) {
	rpmlog(RPMLOG_ERR, _("Icon %s is too big (max. %d bytes)\n"),
		fn, iconsize);
	goto exit;
    }

    if (!strncmp(icon, "GIF", sizeof("GIF")-1))
	xx = headerAddEntry(h, RPMTAG_GIF, RPM_BIN_TYPE, icon, nb);
    else if (!strncmp(icon, "/* XPM", sizeof("/* XPM")-1))
	xx = headerAddEntry(h, RPMTAG_XPM, RPM_BIN_TYPE, icon, nb);
    else {
	rpmlog(RPMLOG_ERR, _("Unknown icon type: %s\n"), fn);
	goto exit;
    }
    rc = 0;
    
exit:
    if (fd) {
	(void) Fclose(fd);
	fd = NULL;
    }
    Lurlfn = _free(Lurlfn);
    return rc;
}
/*@=boundswrite@*/

spectag stashSt(Spec spec, Header h, int tag, const char * lang)
{
    HGE_t hge = (HGE_t)headerGetExtension;
    rpmTagType he_t = 0;
    hRET_t he_p = { .ptr = NULL };
    rpmTagCount he_c = 0;
    HE_s he_s = { .tag = 0, .t = &he_t, .p = &he_p, .c = &he_c, .freeData = 0 };
    HE_t he = &he_s;
    spectag t = NULL;
    int xx;

    if (spec->st) {
	spectags st = spec->st;
	if (st->st_ntags == st->st_nalloc) {
	    st->st_nalloc += 10;
	    st->st_t = xrealloc(st->st_t, st->st_nalloc * sizeof(*(st->st_t)));
	}
	t = st->st_t + st->st_ntags++;
	t->t_tag = tag;
	t->t_startx = spec->lineNum - 1;
	t->t_nlines = 1;
	t->t_lang = xstrdup(lang);
	t->t_msgid = NULL;
	if (!(t->t_lang && strcmp(t->t_lang, RPMBUILD_DEFAULT_LANG))) {
	    he->tag = RPMTAG_NAME;
	    xx = hge(h, he->tag, he->t, he->p, he->c);
	    if (xx) {
		char buf[1024];
		sprintf(buf, "%s(%s)", he_p.str, tagName(tag));
		t->t_msgid = xstrdup(buf);
	    }
	    he_p.ptr = _free(he_p.ptr);
	}
    }
    /*@-usereleased -compdef@*/
    return t;
    /*@=usereleased =compdef@*/
}

#define SINGLE_TOKEN_ONLY \
if (multiToken) { \
    rpmlog(RPMLOG_ERR, _("line %d: Tag takes single token only: %s\n"), \
	     spec->lineNum, spec->line); \
    return RPMRC_FAIL; \
}

/*@-redecl@*/
extern int noLang;
/*@=redecl@*/

/**
 */
/*@-boundswrite@*/
static int handlePreambleTag(Spec spec, Package pkg, rpmTag tag,
		const char *macro, const char *lang)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies spec->macros, spec->st,
		spec->sources, spec->numSources, spec->noSource,
		spec->sourceHeader, spec->BANames, spec->BACount,
		spec->line,
		pkg->header, pkg->autoProv, pkg->autoReq, pkg->icon,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HGE_t hge = (HGE_t)headerGetExtension;
    rpmTagType he_t = 0;
    hRET_t he_p = { .ptr = NULL };
    rpmTagCount he_c = 0;
    HE_s he_s = { .tag = 0, .t = &he_t, .p = &he_p, .c = &he_c, .freeData = 0 };
    HE_t he = &he_s;
    char * field = spec->line;
    char * end;
    int multiToken = 0;
    rpmsenseFlags tagflags;
    int len;
    int num;
    int rc;
    int xx;
    
    if (field == NULL) return RPMRC_FAIL;	/* XXX can't happen */
    /* Find the start of the "field" and strip trailing space */
    while ((*field) && (*field != ':'))
	field++;
    if (*field != ':') {
	rpmlog(RPMLOG_ERR, _("line %d: Malformed tag: %s\n"),
		 spec->lineNum, spec->line);
	return RPMRC_FAIL;
    }
    field++;
    SKIPSPACE(field);
    if (!*field) {
	/* Empty field */
	rpmlog(RPMLOG_ERR, _("line %d: Empty tag: %s\n"),
		 spec->lineNum, spec->line);
	return RPMRC_FAIL;
    }
    end = findLastChar(field);

    /* See if this is multi-token */
    end = field;
    SKIPNONSPACE(end);
    if (*end != '\0')
	multiToken = 1;

    switch (tag) {
    case RPMTAG_NAME:
    case RPMTAG_VERSION:
    case RPMTAG_RELEASE:
    case RPMTAG_URL:
    case RPMTAG_DISTTAG:
    case RPMTAG_REPOTAG:
    case RPMTAG_CVSID:
	SINGLE_TOKEN_ONLY;
	/* These macros are for backward compatibility */
	if (tag == RPMTAG_VERSION) {
	    if (strchr(field, '-') != NULL) {
		rpmlog(RPMLOG_ERR, _("line %d: Illegal char '-' in %s: %s\n"),
		    spec->lineNum, "version", spec->line);
		return RPMRC_FAIL;
	    }
	    addMacro(spec->macros, "PACKAGE_VERSION", NULL, field, RMIL_OLDSPEC);
	} else if (tag == RPMTAG_RELEASE) {
	    if (strchr(field, '-') != NULL) {
		rpmlog(RPMLOG_ERR, _("line %d: Illegal char '-' in %s: %s\n"),
		    spec->lineNum, "release", spec->line);
		return RPMRC_FAIL;
	    }
	    addMacro(spec->macros, "PACKAGE_RELEASE", NULL, field, RMIL_OLDSPEC-1);
	}
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
    case RPMTAG_GROUP:
    case RPMTAG_SUMMARY:
	(void) stashSt(spec, pkg->header, tag, lang);
	/*@fallthrough@*/
    case RPMTAG_DISTRIBUTION:
    case RPMTAG_VENDOR:
    case RPMTAG_LICENSE:
    case RPMTAG_PACKAGER:
	if (!*lang)
	    (void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	else if (!(noLang && strcmp(lang, RPMBUILD_DEFAULT_LANG)))
	    (void) headerAddI18NString(pkg->header, tag, field, lang);
	break;
    /* XXX silently ignore BuildRoot: */
    case RPMTAG_BUILDROOT:
	SINGLE_TOKEN_ONLY;
	macro = NULL;
#ifdef	DYING
	buildRootURL = rpmGenPath(spec->rootURL, "%{?buildroot}", NULL);
	(void) urlPath(buildRootURL, &buildRoot);
	/*@-branchstate@*/
	if (*buildRoot == '\0') buildRoot = "/";
	/*@=branchstate@*/
	if (!strcmp(buildRoot, "/")) {
	    rpmlog(RPMLOG_ERR,
		     _("BuildRoot can not be \"/\": %s\n"), spec->buildRootURL);
	    buildRootURL = _free(buildRootURL);
	    return RPMRC_FAIL;
	}
	buildRootURL = _free(buildRootURL);
#endif
	break;
    case RPMTAG_KEYWORDS:
    case RPMTAG_VARIANTS:
    case RPMTAG_PREFIXES:
	addOrAppendListEntry(pkg->header, tag, field);
	he->tag = tag;
	xx = hge(pkg->header, he->tag, he->t, he->p, he->c);
	if (tag == RPMTAG_PREFIXES)
	while (he_c--) {
	    if (he_p.argv[he_c][0] != '/') {
		rpmlog(RPMLOG_ERR,
			 _("line %d: Prefixes must begin with \"/\": %s\n"),
			 spec->lineNum, spec->line);
		he_p.ptr = _free(he_p.ptr);
		return RPMRC_FAIL;
	    }
	    len = strlen(he_p.argv[he_c]);
	    if (he_p.argv[he_c][len - 1] == '/' && len > 1) {
		rpmlog(RPMLOG_ERR,
			 _("line %d: Prefixes must not end with \"/\": %s\n"),
			 spec->lineNum, spec->line);
		he_p.ptr = _free(he_p.ptr);
		return RPMRC_FAIL;
	    }
	}
	he_p.ptr = _free(he_p.ptr);
	break;
    case RPMTAG_DOCDIR:
	SINGLE_TOKEN_ONLY;
	if (field[0] != '/') {
	    rpmlog(RPMLOG_ERR,
		     _("line %d: Docdir must begin with '/': %s\n"),
		     spec->lineNum, spec->line);
	    return RPMRC_FAIL;
	}
	macro = NULL;
	delMacro(NULL, "_docdir");
	addMacro(NULL, "_docdir", NULL, field, RMIL_SPEC);
	break;
    case RPMTAG_XMAJOR:
    case RPMTAG_XMINOR:
    case RPMTAG_EPOCH:
	SINGLE_TOKEN_ONLY;
	if (parseNum(field, &num)) {
	    rpmlog(RPMLOG_ERR,
		     _("line %d: %s takes an integer value: %s\n"),
		     spec->lineNum, tagName(tag), spec->line);
	    return RPMRC_FAIL;
	}
	xx = headerAddEntry(pkg->header, tag, RPM_INT32_TYPE, &num, 1);
	break;
    case RPMTAG_AUTOREQPROV:
	pkg->autoReq = parseYesNo(field);
	pkg->autoProv = pkg->autoReq;
	break;
    case RPMTAG_AUTOREQ:
	pkg->autoReq = parseYesNo(field);
	break;
    case RPMTAG_AUTOPROV:
	pkg->autoProv = parseYesNo(field);
	break;
    case RPMTAG_SOURCE:
    case RPMTAG_PATCH:
	SINGLE_TOKEN_ONLY;
	macro = NULL;
	if ((rc = addSource(spec, pkg, field, tag)))
	    return rc;
	break;
    case RPMTAG_ICON:
	SINGLE_TOKEN_ONLY;
	macro = NULL;
	if ((rc = addSource(spec, pkg, field, tag)))
	    return rc;
	/* XXX the fetch/load of icon needs to be elsewhere. */
	if ((rc = doIcon(spec, pkg->header)))
	    return rc;
	break;
    case RPMTAG_NOSOURCE:
    case RPMTAG_NOPATCH:
	spec->noSource = 1;
	if ((rc = parseNoSource(spec, field, tag)))
	    return rc;
	break;
    case RPMTAG_BUILDPREREQ:
    case RPMTAG_BUILDREQUIRES:
	if ((rc = parseBits(lang, buildScriptBits, &tagflags))) {
	    rpmlog(RPMLOG_ERR,
		     _("line %d: Bad %s: qualifiers: %s\n"),
		     spec->lineNum, tagName(tag), spec->line);
	    return rc;
	}
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
    case RPMTAG_PREREQ:
    case RPMTAG_REQUIREFLAGS:
	if ((rc = parseBits(lang, installScriptBits, &tagflags))) {
	    rpmlog(RPMLOG_ERR,
		     _("line %d: Bad %s: qualifiers: %s\n"),
		     spec->lineNum, tagName(tag), spec->line);
	    return rc;
	}
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
    /* Aliases for BuildRequires(hint): */
    case RPMTAG_BUILDSUGGESTS:
    case RPMTAG_BUILDENHANCES:
	tagflags = RPMSENSE_MISSINGOK;
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
    /* Aliases for Requires(hint): */
    case RPMTAG_SUGGESTSFLAGS:
    case RPMTAG_ENHANCESFLAGS:
	tag = RPMTAG_REQUIREFLAGS;
	tagflags = RPMSENSE_MISSINGOK;
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
    case RPMTAG_BUILDOBSOLETES:
    case RPMTAG_BUILDPROVIDES:
    case RPMTAG_BUILDCONFLICTS:
    case RPMTAG_CONFLICTFLAGS:
    case RPMTAG_OBSOLETEFLAGS:
    case RPMTAG_PROVIDEFLAGS:
	tagflags = RPMSENSE_ANY;
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
    case RPMTAG_BUILDPLATFORMS:		/* XXX needs pattern parsing */
    case RPMTAG_EXCLUDEARCH:
    case RPMTAG_EXCLUSIVEARCH:
    case RPMTAG_EXCLUDEOS:
    case RPMTAG_EXCLUSIVEOS:
	addOrAppendListEntry(spec->sourceHeader, tag, field);
	break;
    case RPMTAG_BUILDARCHS:
	if ((rc = poptParseArgvString(field,
				      &(spec->BACount),
				      &(spec->BANames)))) {
	    rpmlog(RPMLOG_ERR,
		     _("line %d: Bad BuildArchitecture format: %s\n"),
		     spec->lineNum, spec->line);
	    return RPMRC_FAIL;
	}
	if (!spec->BACount)
	    spec->BANames = _free(spec->BANames);
	break;

    default:
	macro = 0;
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
    }

    if (macro)
	addMacro(spec->macros, macro, NULL, field, RMIL_SPEC);
    
    return 0;
}
/*@=boundswrite@*/

static rpmTag generateArbitraryTagNum(const char *s)
{
    const char *se;
    rpmTag tag = 0;

   for (se = s; *se && *se != ':'; se++)
	;

    if (se > s && *se == ':') {
	DIGEST_CTX ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);
	const char * digest = NULL;
	size_t digestlen = 0;
	rpmDigestUpdate(ctx, s, (se-s));
	rpmDigestFinal(ctx, &digest, &digestlen, 0);
	if (digest && digestlen > 4) {
	    memcpy(&tag, digest + (digestlen - 4), 4);
	    tag &= 0x3fffffff;
	    tag |= 0x40000000;
	}
	digest = _free(digest);
    }
    return tag;
}

/* This table has to be in a peculiar order.  If one tag is the */
/* same as another, plus a few letters, it must come first.     */

/**
 */
typedef struct PreambleRec_s {
    rpmTag tag;
    int multiLang;
    int obsolete;
/*@observer@*/ /*@null@*/
    const char * token;
} * PreambleRec;

/*@unchecked@*/
static struct PreambleRec_s preambleList[] = {
    {RPMTAG_NAME,		0, 0, "name"},
    {RPMTAG_VERSION,		0, 0, "version"},
    {RPMTAG_RELEASE,		0, 0, "release"},
    {RPMTAG_EPOCH,		0, 0, "epoch"},
    {RPMTAG_EPOCH,		0, 1, "serial"},
    {RPMTAG_SUMMARY,		1, 0, "summary"},
    {RPMTAG_LICENSE,		0, 0, "copyright"},
    {RPMTAG_LICENSE,		0, 0, "license"},
    {RPMTAG_DISTRIBUTION,	0, 0, "distribution"},
    {RPMTAG_DISTURL,		0, 0, "disturl"},
    {RPMTAG_VENDOR,		0, 0, "vendor"},
    {RPMTAG_GROUP,		1, 0, "group"},
    {RPMTAG_PACKAGER,		0, 0, "packager"},
    {RPMTAG_URL,		0, 0, "url"},
    {RPMTAG_SOURCE,		0, 0, "source"},
    {RPMTAG_PATCH,		0, 0, "patch"},
    {RPMTAG_NOSOURCE,		0, 0, "nosource"},
    {RPMTAG_NOPATCH,		0, 0, "nopatch"},
    {RPMTAG_EXCLUDEARCH,	0, 0, "excludearch"},
    {RPMTAG_EXCLUSIVEARCH,	0, 0, "exclusivearch"},
    {RPMTAG_EXCLUDEOS,		0, 0, "excludeos"},
    {RPMTAG_EXCLUSIVEOS,	0, 0, "exclusiveos"},
    {RPMTAG_ICON,		0, 0, "icon"},
    {RPMTAG_PROVIDEFLAGS,	0, 0, "provides"},
    {RPMTAG_REQUIREFLAGS,	1, 0, "requires"},
    {RPMTAG_PREREQ,		1, 0, "prereq"},
    {RPMTAG_CONFLICTFLAGS,	0, 0, "conflicts"},
    {RPMTAG_OBSOLETEFLAGS,	0, 0, "obsoletes"},
    {RPMTAG_PREFIXES,		0, 0, "prefixes"},
    {RPMTAG_PREFIXES,		0, 0, "prefix"},
    {RPMTAG_BUILDROOT,		0, 0, "buildroot"},
    {RPMTAG_BUILDARCHS,		0, 0, "buildarchitectures"},
    {RPMTAG_BUILDARCHS,		0, 0, "buildarch"},
    {RPMTAG_BUILDCONFLICTS,	0, 0, "buildconflicts"},
    {RPMTAG_BUILDOBSOLETES,	0, 0, "buildobsoletes"},
    {RPMTAG_BUILDPREREQ,	1, 0, "buildprereq"},
    {RPMTAG_BUILDPROVIDES,	0, 0, "buildprovides"},
    {RPMTAG_BUILDREQUIRES,	1, 0, "buildrequires"},
    {RPMTAG_AUTOREQPROV,	0, 0, "autoreqprov"},
    {RPMTAG_AUTOREQ,		0, 0, "autoreq"},
    {RPMTAG_AUTOPROV,		0, 0, "autoprov"},
    {RPMTAG_DOCDIR,		0, 0, "docdir"},
    {RPMTAG_DISTTAG,		0, 0, "disttag"},
    {RPMTAG_CVSID,		0, 0, "cvsid"},
    {RPMTAG_SVNID,		0, 0, "svnid"},
    {RPMTAG_SUGGESTSFLAGS,	0, 0, "suggests"},
    {RPMTAG_ENHANCESFLAGS,	0, 0, "enhances"},
    {RPMTAG_BUILDSUGGESTS,	0, 0, "buildsuggests"},
    {RPMTAG_BUILDENHANCES,	0, 0, "buildenhances"},
    {RPMTAG_VARIANTS,		0, 0, "variants"},
    {RPMTAG_VARIANTS,		0, 0, "variant"},
    {RPMTAG_XMAJOR,		0, 0, "xmajor"},
    {RPMTAG_XMINOR,		0, 0, "xminor"},
    {RPMTAG_REPOTAG,		0, 0, "repotag"},
    {RPMTAG_KEYWORDS,		0, 0, "keywords"},
    {RPMTAG_KEYWORDS,		0, 0, "keyword"},
    {RPMTAG_BUILDPLATFORMS,	0, 0, "buildplatforms"},
    /*@-nullassign@*/	/* LCL: can't add null annotation */
    {0, 0, 0, 0}
    /*@=nullassign@*/
};

/**
 */
/*@-boundswrite@*/
static int findPreambleTag(Spec spec, /*@out@*/rpmTag * tag,
		/*@null@*/ /*@out@*/ const char ** macro, /*@out@*/ char * lang)
	/*@modifies *tag, *macro, *lang @*/
{
    PreambleRec p;
    char *s;
    size_t len = 0;

    for (p = preambleList; p->token != NULL; p++) {
	len = strlen(p->token);
	if (!(p->token && !xstrncasecmp(spec->line, p->token, len)))
	    continue;
	if (p->obsolete) {
	    rpmlog(RPMLOG_ERR, _("Legacy syntax is unsupported: %s\n"),
			p->token);
	    p = NULL;
	}
	break;
    }
    if (p == NULL)
	return 1;
    if (p->token == NULL) {
	if (tag && (*tag = generateArbitraryTagNum(spec->line)) )
	    return 0;
	return 1;
    }

    s = spec->line + len;
    SKIPSPACE(s);

    switch (p->multiLang) {
    default:
    case 0:
	/* Unless this is a source or a patch, a ':' better be next */
	if (p->tag != RPMTAG_SOURCE && p->tag != RPMTAG_PATCH) {
	    if (*s != ':') return 1;
	}
	*lang = '\0';
	break;
    case 1:	/* Parse optional ( <token> ). */
	if (*s == ':') {
	    strcpy(lang, RPMBUILD_DEFAULT_LANG);
	    break;
	}
	if (*s != '(') return 1;
	s++;
	SKIPSPACE(s);
	while (!xisspace(*s) && *s != ')')
	    *lang++ = *s++;
	*lang = '\0';
	SKIPSPACE(s);
	if (*s != ')') return 1;
	s++;
	SKIPSPACE(s);
	if (*s != ':') return 1;
	break;
    }

    *tag = p->tag;
    if (macro)
	/*@-onlytrans -observertrans -dependenttrans@*/	/* FIX: double indirection. */
	*macro = p->token;
	/*@=onlytrans =observertrans =dependenttrans@*/
    return 0;
}
/*@=boundswrite@*/

/*@-boundswrite@*/
/* XXX should return rpmParseState, but RPMRC_FAIL forces int return. */
int parsePreamble(Spec spec, int initialPackage)
{
    rpmParseState nextPart;
    int rc, xx;
    char *name, *linep;
    Package pkg;
    char NVR[BUFSIZ];
    char lang[BUFSIZ];

    strcpy(NVR, "(main package)");

    pkg = newPackage(spec);
	
    if (! initialPackage) {
	rpmParseState flag;
	/* There is one option to %package: <pkg> or -n <pkg> */
	flag = PART_NONE;
	if (parseSimplePart(spec->line, &name, &flag)) {
	    rpmlog(RPMLOG_ERR, _("Bad package specification: %s\n"),
			spec->line);
	    return RPMRC_FAIL;
	}
	
	if (!lookupPackage(spec, name, flag, NULL)) {
	    rpmlog(RPMLOG_ERR, _("Package already exists: %s\n"),
			spec->line);
	    return RPMRC_FAIL;
	}
	
	/* Construct the package */
	if (flag == PART_SUBNAME) {
	    const char * mainName;
	    xx = headerGetEntry(spec->packages->header, RPMTAG_NAME,
			NULL, &mainName, NULL);
	    sprintf(NVR, "%s-%s", mainName, name);
	} else
	    strcpy(NVR, name);
	xx = headerAddEntry(pkg->header, RPMTAG_NAME, RPM_STRING_TYPE, NVR, 1);
    }

    if ((rc = readLine(spec, STRIP_TRAILINGSPACE | STRIP_COMMENTS)) > 0) {
	nextPart = PART_NONE;
    } else {
	if (rc)
	    return rc;
	while (! (nextPart = isPart(spec->line))) {
	    const char * macro = NULL;
	    rpmTag tag;

	    /* Skip blank lines */
	    linep = spec->line;
	    SKIPSPACE(linep);
	    if (*linep != '\0') {
		if (findPreambleTag(spec, &tag, &macro, lang)) {
		    rpmlog(RPMLOG_ERR, _("line %d: Unknown tag: %s\n"),
				spec->lineNum, spec->line);
		    return RPMRC_FAIL;
		}
		if (handlePreambleTag(spec, pkg, tag, macro, lang))
		    return RPMRC_FAIL;
		if (spec->BANames && !spec->recursing)
		    return PART_BUILDARCHITECTURES;
	    }
	    if ((rc =
		 readLine(spec, STRIP_TRAILINGSPACE | STRIP_COMMENTS)) > 0) {
		nextPart = PART_NONE;
		break;
	    }
	    if (rc)
		return rc;
	}
    }

    /* Do some final processing on the header */
    
    /* XXX Skip valid arch check if not building binary package */
    if (!spec->anyarch && checkForValidArchitectures(spec))
	return RPMRC_FAIL;

    if (pkg == spec->packages)
	fillOutMainPackage(pkg->header);

    if (checkForDuplicates(pkg->header, NVR))
	return RPMRC_FAIL;

    if (pkg != spec->packages)
	headerCopyTags(spec->packages->header, pkg->header,
			(int_32 *)copyTagsDuringParse);

    if (checkForRequired(pkg->header, NVR))
	return RPMRC_FAIL;

    return nextPart;
}
/*@=boundswrite@*/

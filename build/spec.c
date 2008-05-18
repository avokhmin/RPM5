/** \ingroup rpmbuild
 * \file build/spec.c
 * Handle spec data structure.
 */

#include "system.h"

#include <rpmio.h>
#include "buildio.h"
#include "rpmds.h"
#include "rpmfi.h"
#include "rpmts.h"

#include "rpmlua.h"

#include "debug.h"

/*@-redecl@*/
extern int specedit;
/*@=redecl@*/

#define SKIPWHITE(_x)	{while(*(_x) && (xisspace(*_x) || *(_x) == ',')) (_x)++;}
#define SKIPNONWHITE(_x){while(*(_x) &&!(xisspace(*_x) || *(_x) == ',')) (_x)++;}

/*@access rpmluav @*/

/**
 * @param p		trigger entry chain
 * @return		NULL always
 */
static inline
/*@null@*/ struct TriggerFileEntry * freeTriggerFiles(/*@only@*/ /*@null@*/ struct TriggerFileEntry * p)
	/*@modifies p @*/
{
    struct TriggerFileEntry *o, *q = p;
    
    while (q != NULL) {
	o = q;
	q = q->next;
	o->fileName = _free(o->fileName);
	o->script = _free(o->script);
	o->prog = _free(o->prog);
	o = _free(o);
    }
    return NULL;
}

/**
 * Destroy source component chain.
 * @param s		source component chain
 * @return		NULL always
 */
static inline
/*@null@*/ struct Source * freeSources(/*@only@*/ /*@null@*/ struct Source * s)
	/*@modifies s @*/
{
    struct Source *r, *t = s;

    while (t != NULL) {
	r = t;
	t = t->next;
	r->fullSource = _free(r->fullSource);
	r = _free(r);
    }
    return NULL;
}

rpmRC lookupPackage(Spec spec, const char *name, int flag, /*@out@*/Package *pkg)
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    const char *fullName;
    Package p;
    int xx;
    
    /* "main" package */
    if (name == NULL) {
	if (pkg)
	    *pkg = spec->packages;
	return RPMRC_OK;
    }

    /* Construct package name */
  { char *n;
    if (flag == PART_SUBNAME) {
	he->tag = RPMTAG_NAME;
	xx = headerGet(spec->packages->header, he, 0);
	fullName = n = alloca(strlen(he->p.str) + 1 + strlen(name) + 1);
	n = stpcpy(n, he->p.str);
	he->p.ptr = _free(he->p.ptr);
	*n++ = '-';
	*n = '\0';
    } else {
	fullName = n = alloca(strlen(name)+1);
    }
    /*@-mayaliasunique@*/
    strcpy(n, name);
    /*@=mayaliasunique@*/
  }

    /* Locate package with fullName */
    for (p = spec->packages; p != NULL; p = p->next) {
	he->tag = RPMTAG_NAME;
	xx = headerGet(p->header, he, 0);
	if (he->p.str && !strcmp(fullName, he->p.str)) {
	    he->p.ptr = _free(he->p.ptr);
	    break;
	}
	he->p.ptr = _free(he->p.ptr);
    }

    if (pkg)
	/*@-dependenttrans@*/ *pkg = p; /*@=dependenttrans@*/
    return ((p == NULL) ? RPMRC_FAIL : RPMRC_OK);
}

Package newPackage(Spec spec)
{
    Package p;
    Package pp;

    p = xcalloc(1, sizeof(*p));

    p->header = headerNew();
    p->ds = NULL;

    p->autoProv = ((_rpmbuildFlags & 0x1) != 0);
    p->autoReq = ((_rpmbuildFlags & 0x2) != 0);
    
#if 0    
    p->reqProv = NULL;
    p->triggers = NULL;
    p->triggerScripts = NULL;
#endif

    p->triggerFiles = NULL;
    
    p->fileFile = NULL;
    p->fileList = NULL;

    p->cpioList = NULL;

    p->preInFile = NULL;
    p->postInFile = NULL;
    p->preUnFile = NULL;
    p->postUnFile = NULL;
    p->verifyFile = NULL;
    p->sanityCheckFile = NULL;

    p->specialDoc = NULL;

    if (spec->packages == NULL) {
	spec->packages = p;
    } else {
	/* Always add package to end of list */
	for (pp = spec->packages; pp->next != NULL; pp = pp->next)
	    {};
	pp->next = p;
    }
    p->next = NULL;

    return p;
}

Package freePackage(Package pkg)
{
    if (pkg == NULL) return NULL;
    
    pkg->preInFile = _free(pkg->preInFile);
    pkg->postInFile = _free(pkg->postInFile);
    pkg->preUnFile = _free(pkg->preUnFile);
    pkg->postUnFile = _free(pkg->postUnFile);
    pkg->verifyFile = _free(pkg->verifyFile);
    pkg->sanityCheckFile = _free(pkg->sanityCheckFile);

    pkg->header = headerFree(pkg->header);
    pkg->ds = rpmdsFree(pkg->ds);
    pkg->fileList = freeStringBuf(pkg->fileList);
    pkg->fileFile = _free(pkg->fileFile);
    if (pkg->cpioList != NULL) {
	rpmfi fi = pkg->cpioList;
	pkg->cpioList = NULL;
	fi = rpmfiFree(fi);
    }

    pkg->specialDoc = freeStringBuf(pkg->specialDoc);
    pkg->triggerFiles = freeTriggerFiles(pkg->triggerFiles);

    pkg = _free(pkg);
    return NULL;
}

Package freePackages(Package packages)
{
    Package p;

    while ((p = packages) != NULL) {
	packages = p->next;
	p->next = NULL;
	p = freePackage(p);
    }
    return NULL;
}

/**
 */
static inline /*@owned@*/ struct Source *findSource(Spec spec, uint32_t num, int flag)
	/*@*/
{
    struct Source *p;

    for (p = spec->sources; p != NULL; p = p->next)
	if ((num == p->num) && (p->flags & flag)) return p;

    return NULL;
}

/**
 */
int SpecSourceCount(Spec spec)
{
    return spec->numSources;
}

/**
 */
SpecSource getSource(Spec spec, int num)
    /* @ */
{
    struct Source *p = spec->sources;
    int i;

    for (i = 0; i < num; i++)
	if ((p = p->next) == NULL) return NULL;

    return p;
}

/**
 */
const char * specSourceName(SpecSource source)
{
    return source->source;
}

/**
 */
const char * specFullSourceName(SpecSource source)
{
    return source->fullSource;
}

/**
 */
int specSourceNum(SpecSource source)
{
    return source->num;
}

/**
 */
int specSourceFlags(SpecSource source)
{
    return source->flags;
}

int parseNoSource(Spec spec, const char * field, rpmTag tag)
{
    const char *f, *fe;
    const char *name;
    uint32_t num, flag;

    if (tag == RPMTAG_NOSOURCE) {
	flag = RPMFILE_SOURCE;
	name = "source";
    } else {
	flag = RPMFILE_PATCH;
	name = "patch";
    }
    
    fe = field;
    for (f = fe; *f != '\0'; f = fe) {
	struct Source *p;

	SKIPWHITE(f);
	if (*f == '\0')
	    break;
	fe = f;
	SKIPNONWHITE(fe);
	if (*fe != '\0') fe++;

	if (parseNum(f, &num)) {
	    rpmlog(RPMLOG_ERR, _("line %d: Bad number: %s\n"),
		     spec->lineNum, f);
	    return RPMRC_FAIL;
	}

	if (! (p = findSource(spec, num, flag))) {
	    rpmlog(RPMLOG_ERR, _("line %d: Bad no%s number: %d\n"),
		     spec->lineNum, name, num);
	    return RPMRC_FAIL;
	}

	p->flags |= RPMFILE_GHOST;

    }

    return RPMRC_OK;
}

int addSource(Spec spec, /*@unused@*/ Package pkg,
		const char *field, rpmTag tag)
{
    struct Source *p;
#if defined(RPM_VENDOR_OPENPKG) /* regular-ordered-sources */
    struct Source *p_last;
#endif
    int flag = 0;
    const char *name = NULL;
    const char *mdir = NULL;
    char *nump;
    const char *fieldp = NULL;
    char buf[BUFSIZ];
    uint32_t num = 0;

    buf[0] = '\0';
    switch (tag) {
    case RPMTAG_SOURCE:
	flag = RPMFILE_SOURCE;
	name = "source";
	fieldp = spec->line + (sizeof("Source")-1);
	break;
    case RPMTAG_PATCH:
	flag = RPMFILE_PATCH;
	name = "patch";
	fieldp = spec->line + (sizeof("Patch")-1);
	break;
    case RPMTAG_ICON:
	flag = RPMFILE_ICON;
	name = "icon";
	fieldp = NULL;
	break;
    default:
assert(0);
	/*@notreached@*/ break;
    }
#if !defined(RPM_VENDOR_OPENPKG) /* splitted-source-directory */
    mdir = getSourceDir(flag);
assert(mdir != NULL);
#endif

    /* Get the number */
    if (fieldp != NULL) {
	/* We already know that a ':' exists, and that there */
	/* are no spaces before it.                          */
	/* This also now allows for spaces and tabs between  */
	/* the number and the ':'                            */

	nump = buf;
	while ((*fieldp != ':') && (*fieldp != ' ') && (*fieldp != '\t'))
	    *nump++ = *fieldp++;
	*nump = '\0';

	nump = buf;
	SKIPSPACE(nump);
	if (nump == NULL || *nump == '\0')
	    num = 0;
	else if (parseNum(buf, &num)) {
	    rpmlog(RPMLOG_ERR, _("line %d: Bad %s number: %s\n"),
			 spec->lineNum, name, spec->line);
	    return RPMRC_FAIL;
	}
    }

    /* Create the entry and link it in */
    p = xmalloc(sizeof(*p));
    p->num = num;
    p->fullSource = xstrdup(field);
    p->flags = flag;
    p->source = strrchr(p->fullSource, '/');
    if (p->source)
	p->source++;
    else
	p->source = p->fullSource;

#if defined(RPM_VENDOR_OPENPKG) /* regular-ordered-sources */
    p->next = NULL;
    p_last = spec->sources;
    while (p_last != NULL && p_last->next != NULL)
        p_last = p_last->next;
    if (p_last != NULL)
        p_last->next = p;
    else
        spec->sources = p;
#else
    p->next = spec->sources;
    spec->sources = p;
#endif

    spec->numSources++;

    /* XXX FIXME: need to add ICON* macros. */
#if defined(RPM_VENDOR_OPENPKG) /* splitted-source-directory */
    mdir = getSourceDir(flag, p->source);
#endif
    if (tag != RPMTAG_ICON) {
	const char *body = rpmGenPath(NULL, mdir, p->source);

	sprintf(buf, "%s%d",
		(flag & RPMFILE_PATCH) ? "PATCH" : "SOURCE", num);
	addMacro(spec->macros, buf, NULL, body, RMIL_SPEC);
	sprintf(buf, "%sURL%d",
		(flag & RPMFILE_PATCH) ? "PATCH" : "SOURCE", num);
	addMacro(spec->macros, buf, NULL, p->fullSource, RMIL_SPEC);
#ifdef WITH_LUA
    {	rpmlua lua = NULL; /* global state */
	const char * what = (flag & RPMFILE_PATCH) ? "patches" : "sources";
	rpmluav var = rpmluavNew();

	rpmluaPushTable(lua, what);
	rpmluavSetListMode(var, 1);
	rpmluavSetValue(var, RPMLUAV_STRING, body);
	rpmluaSetVar(lua, var);
/*@-moduncon@*/
	var = (rpmluav) rpmluavFree(var);
/*@=moduncon@*/
	rpmluaPop(lua);
    }
#endif
	body = _free(body);
    }
    
    return RPMRC_OK;
}

/**
 */
static inline /*@only@*/ /*@null@*/ speclines newSl(void)
	/*@*/
{
    speclines sl = NULL;
    if (specedit) {
	sl = xmalloc(sizeof(*sl));
	sl->sl_lines = NULL;
	sl->sl_nalloc = 0;
	sl->sl_nlines = 0;
    }
    return sl;
}

/**
 */
static inline /*@null@*/ speclines freeSl(/*@only@*/ /*@null@*/ speclines sl)
	/*@modifies sl @*/
{
    int i;
    if (sl == NULL) return NULL;
    for (i = 0; i < sl->sl_nlines; i++)
	/*@-unqualifiedtrans@*/
	sl->sl_lines[i] = _free(sl->sl_lines[i]);
	/*@=unqualifiedtrans@*/
    sl->sl_lines = _free(sl->sl_lines);
    return _free(sl);
}

/**
 */
static inline /*@only@*/ /*@null@*/ spectags newSt(void)
	/*@*/
{
    spectags st = NULL;
    if (specedit) {
	st = xmalloc(sizeof(*st));
	st->st_t = NULL;
	st->st_nalloc = 0;
	st->st_ntags = 0;
    }
    return st;
}

/**
 */
static inline /*@null@*/ spectags freeSt(/*@only@*/ /*@null@*/ spectags st)
	/*@modifies st @*/
{
    int i;
    if (st == NULL) return NULL;
    for (i = 0; i < st->st_ntags; i++) {
	spectag t = st->st_t + i;
	t->t_lang = _free(t->t_lang);
	t->t_msgid = _free(t->t_msgid);
    }
    st->st_t = _free(st->st_t);
    return _free(st);
}

Spec newSpec(void)
{
    Spec spec = xcalloc(1, sizeof(*spec));
    
    spec->specFile = NULL;

    spec->sl = newSl();
    spec->st = newSt();

    spec->fileStack = NULL;
    spec->lbuf_len = (size_t)rpmExpandNumeric("%{?_spec_line_buffer_size}%{!?_spec_line_buffer_size:100000}");
    spec->lbuf = (char *)xcalloc(1, spec->lbuf_len);
    spec->line = spec->lbuf;
    spec->nextline = NULL;
    spec->nextpeekc = '\0';
    spec->lineNum = 0;
    spec->readStack = xcalloc(1, sizeof(*spec->readStack));
    spec->readStack->next = NULL;
    spec->readStack->reading = 1;

    spec->rootURL = NULL;
    spec->prep = NULL;
    spec->build = NULL;
    spec->install = NULL;
    spec->check = NULL;
    spec->clean = NULL;
    spec->foo = NULL;
    spec->nfoo = 0;

    spec->sources = NULL;
    spec->packages = NULL;
    spec->noSource = 0;
    spec->numSources = 0;

    spec->sourceRpmName = NULL;
    spec->sourcePkgId = NULL;
    spec->sourceHeader = headerNew();
    spec->sourceCpioList = NULL;
    
    spec->buildSubdir = NULL;

    spec->passPhrase = NULL;
    spec->timeCheck = 0;
    spec->cookie = NULL;

    spec->BANames = NULL;
    spec->BACount = 0;
    spec->recursing = 0;
    spec->toplevel = 1;
    spec->BASpecs = NULL;

    spec->force = 0;
    spec->anyarch = 0;

/*@i@*/	spec->macros = rpmGlobalMacroContext;

    spec->_parseRCPOT = parseRCPOT;	/* XXX hack around backward linkage. */
    
    return spec;
}

Spec freeSpec(Spec spec)
{
    struct ReadLevelEntry *rl;

    if (spec == NULL) return NULL;

    spec->lbuf = _free(spec->lbuf);

    spec->sl = freeSl(spec->sl);
    spec->st = freeSt(spec->st);

    spec->prep = freeStringBuf(spec->prep);
    spec->build = freeStringBuf(spec->build);
    spec->install = freeStringBuf(spec->install);
    spec->check = freeStringBuf(spec->check);
    spec->clean = freeStringBuf(spec->clean);
    spec->foo = tagStoreFree(spec->foo, spec->nfoo);
    spec->nfoo = 0;

    spec->buildSubdir = _free(spec->buildSubdir);
    spec->rootURL = _free(spec->rootURL);
    spec->specFile = _free(spec->specFile);

    closeSpec(spec);

    while (spec->readStack) {
	rl = spec->readStack;
	/*@-dependenttrans@*/
	spec->readStack = rl->next;
	/*@=dependenttrans@*/
	rl->next = NULL;
	rl = _free(rl);
    }
    
    spec->sourceRpmName = _free(spec->sourceRpmName);
    spec->sourcePkgId = _free(spec->sourcePkgId);
    spec->sourceHeader = headerFree(spec->sourceHeader);

    if (spec->sourceCpioList != NULL) {
	rpmfi fi = spec->sourceCpioList;
	spec->sourceCpioList = NULL;
	fi = rpmfiFree(fi);
    }
    
    if (!spec->recursing) {
	if (spec->BASpecs != NULL)
	while (spec->BACount--) {
	    /*@-unqualifiedtrans@*/
	    spec->BASpecs[spec->BACount] =
			freeSpec(spec->BASpecs[spec->BACount]);
	    /*@=unqualifiedtrans@*/
	}
	/*@-compdef@*/
	spec->BASpecs = _free(spec->BASpecs);
	/*@=compdef@*/
    }
    spec->BANames = _free(spec->BANames);

    spec->passPhrase = _free(spec->passPhrase);
    spec->cookie = _free(spec->cookie);

#ifdef WITH_LUA
    {	rpmlua lua = NULL; /* global state */
	rpmluaDelVar(lua, "patches");
	rpmluaDelVar(lua, "sources");	
    }
#endif

    spec->sources = freeSources(spec->sources);
    spec->packages = freePackages(spec->packages);
    
    spec = _free(spec);

    return spec;
}

/*@only@*/
struct OpenFileInfo * newOpenFileInfo(void)
{
    struct OpenFileInfo *ofi;

    ofi = xmalloc(sizeof(*ofi));
    ofi->fd = NULL;
    ofi->fileName = NULL;
    ofi->lineNum = 0;
    ofi->readBuf[0] = '\0';
    ofi->readPtr = NULL;
    ofi->next = NULL;

    return ofi;
}

/**
 * Print copy of spec file, filling in Group/Description/Summary from specspo.
 * @param spec		spec file control structure
 */
static void
printNewSpecfile(Spec spec)
	/*@globals fileSystem @*/
	/*@modifies spec->sl->sl_lines[], spec->packages->header, fileSystem @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    Header h;
    speclines sl = spec->sl;
    spectags st = spec->st;
    const char * msgstr = NULL;
    int i, j;
    int xx;

    if (sl == NULL || st == NULL)
	return;

    for (i = 0; i < st->st_ntags; i++) {
	spectag t = st->st_t + i;
	const char * tn = tagName(t->t_tag);
	const char * errstr;
	char fmt[1024];

	fmt[0] = '\0';
	if (t->t_msgid == NULL)
	    h = spec->packages->header;
	else {
	    Package pkg;
	    char *fe;

	    strcpy(fmt, t->t_msgid);
	    for (fe = fmt; *fe && *fe != '('; fe++)
		{} ;
	    if (*fe == '(') *fe = '\0';
	    h = NULL;
	    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
		h = pkg->header;
		he->tag = RPMTAG_NAME;
		xx = headerGet(h, he, 0);
		if (!strcmp(he->p.str, fmt)) {
		    he->p.ptr = _free(he->p.ptr);
		    /*@innerbreak@*/ break;
		}
		he->p.ptr = _free(he->p.ptr);
	    }
	    if (pkg == NULL || h == NULL)
		h = spec->packages->header;
	}

	if (h == NULL)
	    continue;

	fmt[0] = '\0';
	(void) stpcpy( stpcpy( stpcpy( fmt, "%{"), tn), "}");
	msgstr = _free(msgstr);

	/* XXX this should use queryHeader(), but prints out tn as well. */
	msgstr = headerSprintf(h, fmt, NULL, rpmHeaderFormats, &errstr);
	if (msgstr == NULL) {
	    rpmlog(RPMLOG_ERR, _("can't query %s: %s\n"), tn, errstr);
	    return;
	}

	switch(t->t_tag) {
	case RPMTAG_SUMMARY:
	case RPMTAG_GROUP:
	    /*@-unqualifiedtrans@*/
	    sl->sl_lines[t->t_startx] = _free(sl->sl_lines[t->t_startx]);
	    /*@=unqualifiedtrans@*/
	    if (t->t_lang && strcmp(t->t_lang, RPMBUILD_DEFAULT_LANG))
		continue;
	    {   char *buf = xmalloc(strlen(tn) + sizeof(": ") + strlen(msgstr));
		(void) stpcpy( stpcpy( stpcpy(buf, tn), ": "), msgstr);
		sl->sl_lines[t->t_startx] = buf;
	    }
	    /*@switchbreak@*/ break;
	case RPMTAG_DESCRIPTION:
	    for (j = 1; j < t->t_nlines; j++) {
		if (*sl->sl_lines[t->t_startx + j] == '%')
		    /*@innercontinue@*/ continue;
		/*@-unqualifiedtrans@*/
		sl->sl_lines[t->t_startx + j] =
			_free(sl->sl_lines[t->t_startx + j]);
		/*@=unqualifiedtrans@*/
	    }
	    if (t->t_lang && strcmp(t->t_lang, RPMBUILD_DEFAULT_LANG)) {
		sl->sl_lines[t->t_startx] = _free(sl->sl_lines[t->t_startx]);
		continue;
	    }
	    sl->sl_lines[t->t_startx + 1] = xstrdup(msgstr);
	    if (t->t_nlines > 2)
		sl->sl_lines[t->t_startx + 2] = xstrdup("\n\n");
	    /*@switchbreak@*/ break;
	}
    }
    msgstr = _free(msgstr);

    for (i = 0; i < sl->sl_nlines; i++) {
	const char * s = sl->sl_lines[i];
	if (s == NULL)
	    continue;
	printf("%s", s);
	if (strchr(s, '\n') == NULL && s[strlen(s)-1] != '\n')
	    printf("\n");
    }
}

/**
 * Parse a spec file, and query the resultant header.
 * @param ts		rpm transaction
 * @param qva		query args
 * @param specName	specfile to parse
 * @param target	cpu-vender-os platform for query (NULL is current)
 * @return              0 on success
 */
static int _specQuery(rpmts ts, QVA_t qva, const char *specName,
		/*@null@*/ const char *target) 
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
    Spec spec = NULL;
    Package pkg;
    int res = 1;	/* assume error */
    int anyarch = (target == NULL) ? 1 : 0;
    char * passPhrase = "";
    int recursing = 0;
    char *cookie = NULL;
    int verify = 0;
    int xx;

    /*@-mods@*/ /* FIX: make spec abstract */
    if (parseSpec(ts, specName, "/", recursing, passPhrase,
		cookie, anyarch, 1, verify)
      || (spec = rpmtsSetSpec(ts, NULL)) == NULL)
    {
	rpmlog(RPMLOG_ERR,
	    _("query of specfile %s failed, can't parse\n"), 
	    specName);
	goto exit;
    }
    /*@=mods@*/

    res = 0;
    if (specedit) {
	printNewSpecfile(spec);
	goto exit;
    }

    switch (qva->qva_source) {
    case RPMQV_SPECSRPM:
	xx = initSourceHeader(spec, NULL);
	xx = qva->qva_showPackage(qva, ts, spec->sourceHeader);
	break;
    default:
    case RPMQV_SPECFILE:
	for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	    /* If no target was specified, display all packages.
	     * Packages with empty file lists are not produced.
	     */
	    /* XXX DIEDIEDIE: this logic looks flawed. */
	    if (target == NULL || pkg->fileList != NULL) 
		xx = qva->qva_showPackage(qva, ts, pkg->header);
	}
	break;
    }

exit:
    spec = freeSpec(spec);
    return res;
}

int rpmspecQuery(rpmts ts, QVA_t qva, const char * arg)
{
    int res = 1;
    const char * targets = rpmcliTargets;
    char *target;
    const char * t;
    const char * te;
    int nqueries = 0;

    if (qva->qva_showPackage == NULL)
	goto exit;

    if (targets == NULL) {
	res = _specQuery(ts, qva, arg, NULL); 
	nqueries++;
	goto exit;
    }

    rpmlog(RPMLOG_DEBUG, 
	_("Query specfile for platform(s): %s\n"), targets);
    for (t = targets; *t != '\0'; t = te) {
	/* Parse out next target platform. */ 
	if ((te = strchr(t, ',')) == NULL)
	    te = t + strlen(t);
	target = alloca(te-t+1);
	strncpy(target, t, (te-t));
	target[te-t] = '\0';
	if (*te != '\0')
	    te++;

	/* Query spec for this target platform. */
	rpmlog(RPMLOG_DEBUG, _("    target platform: %s\n"), target);
	/* Read in configuration for target. */
	if (t != targets) {
	    rpmFreeMacros(NULL);
	    rpmFreeRpmrc();
	    (void) rpmReadConfigFiles(NULL, target);
	}
	res = _specQuery(ts, qva, arg, target); 
	nqueries++;
	if (res) break;	
    }
    
exit:
    /* Restore original configuration. */
    if (nqueries > 1) {
	t = targets;
	if ((te = strchr(t, ',')) == NULL)
	    te = t + strlen(t);
	target = alloca(te-t+1);
	strncpy(target, t, (te-t));
	target[te-t] = '\0';
	if (*te != '\0')
	    te++;
	rpmFreeMacros(NULL);
	rpmFreeRpmrc();
	(void) rpmReadConfigFiles(NULL, target);
    }
    return res;
}

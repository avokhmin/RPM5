#include "system.h"

#include <assert.h>

#define	isblank(_c)	((_c) == ' ' || (_c) == '\t')
#define	STREQ(_t, _f, _fn)	((_fn) == (sizeof(_t)-1) && !strncmp((_t), (_f), (_fn)))
#define	FREE(_x)	{ if (_x) free(_x); ((void *)(_x)) = NULL; }

#ifdef DEBUG_MACROS
#define rpmError fprintf
#define RPMERR_BADSPEC stderr
#define	_(x)	x
#else
#include "rpmlib.h"
#endif

#include "rpmmacro.h"

typedef struct MacroBuf {
	const char *s;		/* text to expand */
	char *t;		/* expansion buffer */
	size_t nb;		/* no. bytes remaining in expansion buffer */
	int depth;		/* current expansion depth */
	int macro_trace;	/* pre-print macro to expand? */
	int expand_trace;	/* post-print macro expansion? */
	void *spec;		/* (future) %file expansion info */
	MacroContext *mc;
} MacroBuf;

#define SAVECHAR(_mb, _c) { *(_mb)->t = (_c), (_mb)->t++, (_mb)->nb--; }
#define	DELECHAR(_mb, _c) { if ((_mb)->t[-1] == (_c)) *((_mb)->t--) = '\0', (_mb)->nb++; }

static int expandMacro(MacroBuf *mb);

#define	MAX_MACRO_DEPTH	16
int max_macro_depth = MAX_MACRO_DEPTH;

#ifdef	DEBUG_MACROS
int print_macro_trace = 0;
int print_expand_trace = 0;
#else
int print_macro_trace = 0;
int print_expand_trace = 0;
#endif

#define	MACRO_CHUNK_SIZE	16

/* =============================================================== */

static int
compareMacroName(const void *ap, const void *bp)
{
	MacroEntry *ame = *((MacroEntry **)ap);
	MacroEntry *bme = *((MacroEntry **)bp);

	if (ame == NULL && bme == NULL) {
		return 0;
	}
	if (ame == NULL) {
		return 1;
	}
	if (bme == NULL) {
		return -1;
	}
	return strcmp(ame->name, bme->name);
}

static void
expandMacroTable(MacroContext *mc)
{
	if (mc->macroTable == NULL) {
		mc->macrosAllocated = MACRO_CHUNK_SIZE;
		mc->macroTable = (MacroEntry **)malloc(sizeof(*(mc->macroTable)) *
				mc->macrosAllocated);
		mc->firstFree = 0;
	} else {
		mc->macrosAllocated += MACRO_CHUNK_SIZE;
		mc->macroTable = (MacroEntry **)realloc(mc->macroTable, sizeof(*(mc->macroTable)) *
				mc->macrosAllocated);
	}
	memset(&mc->macroTable[mc->firstFree], 0, MACRO_CHUNK_SIZE * sizeof(*(mc->macroTable)));
}

static void
sortMacroTable(MacroContext *mc)
{
	qsort(mc->macroTable, mc->firstFree, sizeof(*(mc->macroTable)),
		compareMacroName);
}

void
dumpMacroTable(MacroContext *mc)
{
	int i;
	int nempty = 0;
	int nactive = 0;
    
	fprintf(stderr, "========================\n");
	for (i = 0; i < mc->firstFree; i++) {
		MacroEntry *me;
		if ((me = mc->macroTable[i]) == NULL) {
			nempty++;
			continue;
		}
		fprintf(stderr, "%3d%c %s", me->level,
			(me->used > 0 ? '=' : ':'), me->name);
		if (me->opts)
			fprintf(stderr, "(%s)", me->opts);
		if (me->body)
			fprintf(stderr, "\t%s", me->body);
		fprintf(stderr, "\n");
		nactive++;
	}
	fprintf(stderr, _("======================== active %d empty %d\n"),
		nactive, nempty);
}

static MacroEntry **
findEntry(MacroContext *mc, const char *name, size_t namelen)
{
	MacroEntry keybuf, *key, **ret;
	char namebuf[1024];

	if (! mc->firstFree)
		return NULL;

	if (namelen > 0) {
		strncpy(namebuf, name, namelen);
		namebuf[namelen] = '\0';
		name = namebuf;
	}
    
	key = &keybuf;
	memset(key, 0, sizeof(*key));
	key->name = (char *)name;
	ret = (MacroEntry **)bsearch(&key, mc->macroTable, mc->firstFree,
			sizeof(*(mc->macroTable)), compareMacroName);
	/* XXX TODO: find 1st empty slot and return that */
	return ret;
}

/* =============================================================== */

/* fgets analogue that reads \ continuations. Last newline always trimmed. */

static char *
rdcl(char *buf, size_t size, FILE *fp, int escapes)
{
	char *q = buf;
	size_t nb = 0;

	do {
		*q = '\0';			/* next char in buf */
		nb = 0;
		if (fgets(q, size, fp) == NULL)	/* read next line */
			break;
		nb = strlen(q);
		q += nb - 1;			/* last char in buf */
		*q-- = '\0';			/* trim newline */
		if (nb < 2 || *q != '\\')	/* continue? */
			break;
		if (escapes)			/* copy escape too */
			q++;
		else
			nb--;
		*q++ = '\n';			/* next char in buf */
		size -= nb;
	} while (size > 0);
	return (nb > 0 ? buf : NULL);
}

/* Return text between pl and matching pr */

static const char *
matchchar(const char *p, char pl, char pr)
{
	int lvl = 0;
	char c;

	while ((c = *p++) != '\0') {
		if (c == '\\') {		/* Ignore escaped chars */
			p++;
			continue;
		}
		if (c == pr) {
			if (--lvl <= 0)	return --p;
		} else if (c == pl)
			lvl++;
	}
	return (const char *)NULL;
}

static void
printMacro(MacroBuf *mb, const char *s, const char *se)
{
	const char *senl;
	const char *ellipsis;
	int choplen;

	if (s >= se) {	/* XXX just in case */
		fprintf(stderr, _("%3d>%*s(empty)"), mb->depth,
			(2 * mb->depth + 1), "");
		return;
	}

	if (s[-1] == '{')
		s--;

	/* If not end-of-string, print only to newline or end-of-string ... */
	if (*(senl = se) != '\0' && (senl = strchr(senl, '\n')) == NULL)
		senl = se + strlen(se);

	/* Limit trailing non-trace output */
	choplen = 61 - (2 * mb->depth);
	if ((senl - s) > choplen) {
		senl = s + choplen;
		ellipsis = "...";
	} else
		ellipsis = "";

	/* Substitute caret at end-of-macro position */
	fprintf(stderr, "%3d>%*s%%%.*s^", mb->depth,
		(2 * mb->depth + 1), "", (int)(se - s), s);
	if (se[1] != '\0' && (senl - (se+1)) > 0)
		fprintf(stderr, "%-.*s%s", (int)(senl - (se+1)), se+1, ellipsis);
	fprintf(stderr, "\n");
}

static void
printExpansion(MacroBuf *mb, const char *t, const char *te)
{
	const char *ellipsis;
	int choplen;

	if (!(te > t)) {
		fprintf(stderr, _("%3d<%*s(empty)\n"), mb->depth, (2 * mb->depth + 1), "");
		return;
	}

	/* Shorten output which contains newlines */
	while (te > t && te[-1] == '\n')
		te--;
	ellipsis = "";
	if (mb->depth > 0) {
		const char *tenl;
		while ((tenl = strchr(t, '\n')) && tenl < te)
			t = ++tenl;

		/* Limit expand output */
		choplen = 61 - (2 * mb->depth);
		if ((te - t) > choplen) {
			te = t + choplen;
			ellipsis = "...";
		}
	}

	fprintf(stderr, "%3d<%*s", mb->depth, (2 * mb->depth + 1), "");
	if (te > t)
		fprintf(stderr, "%.*s%s", (int)(te - t), t, ellipsis);
	fprintf(stderr, "\n");
}

#define	SKIPBLANK(_s, _c)	\
	while (((_c) = *(_s)) && isblank(_c)) \
		(_s)++;

#define	SKIPNONBLANK(_s, _c)	\
	while (((_c) = *(_s)) && !(isblank(_c) || c == '\n')) \
		(_s)++;

#define	COPYNAME(_ne, _s, _c)	\
    {	SKIPBLANK(_s,_c);	\
	while(((_c) = *(_s)) && (isalnum(_c) || (_c) == '_')) \
		*(_ne)++ = *(_s)++; \
	*(_ne) = '\0';		\
    }

#define	COPYOPTS(_oe, _s, _c)	\
    {	while(((_c) = *(_s)) && (_c) != ')') \
		*(_oe)++ = *(_s)++; \
	*(_oe) = '\0';		\
    }

#define	COPYBODY(_be, _s, _c)	\
    {	while(((_c) = *(_s)) && (_c) != '\n') { \
		if ((_c) == '\\') \
			(_s)++;	\
		*(_be)++ = *(_s)++; \
	}			\
	*(_be) = '\0';		\
    }

/* Save source and expand field into target */
static int
expandT(MacroBuf *mb, const char *f, size_t flen)
{
	char *sbuf;
	const char *s = mb->s;
	int rc;

	sbuf = alloca(flen + 1);
	memset(sbuf, 0, (flen + 1));

	strncpy(sbuf, f, flen);
	sbuf[flen] = '\0';
	mb->s = sbuf;
	rc = expandMacro(mb);
	mb->s = s;
	return rc;
}

#if 0
/* Save target and expand sbuf into target */
static int
expandS(MacroBuf *mb, char *tbuf, size_t tbuflen)
{
	const char *t = mb->t;
	size_t nb = mb->nb;
	int rc;

	mb->t = tbuf;
	mb->nb = tbuflen;
	rc = expandMacro(mb);
	mb->t = t;
	mb->nb = nb;
	return rc;
}
#endif

static int
expandU(MacroBuf *mb, char *u, size_t ulen)
{
	const char *s = mb->s;
	char *t = mb->t;
	size_t nb = mb->nb;
	char *tbuf;
	int rc;

	tbuf = alloca(ulen + 1);
	memset(tbuf, 0, (ulen + 1));

	mb->s = u;
	mb->t = tbuf;
	mb->nb = ulen;
	rc = expandMacro(mb);

	tbuf[ulen] = '\0';	/* XXX just in case */
	if (ulen > mb->nb)
		strncpy(u, tbuf, (ulen - mb->nb + 1));

	mb->s = s;
	mb->t = t;
	mb->nb = nb;

	return rc;
}

static int
doShellEscape(MacroBuf *mb, const char *cmd, size_t clen)
{
	char pcmd[BUFSIZ];
	FILE *shf;
	int rc;
	int c;

	strncpy(pcmd, cmd, clen);
	pcmd[clen] = '\0';
	rc = expandU(mb, pcmd, sizeof(pcmd));
	if (rc)
		return rc;

	if ((shf = popen(pcmd, "r")) == NULL)
		return 1;
	while(mb->nb > 0 && (c = fgetc(shf)) != EOF)
		SAVECHAR(mb, c);
	pclose(shf);

	DELECHAR(mb, '\n');	/* XXX delete trailing newline (if any) */
	return 0;
}

static const char *
doDefine(MacroBuf *mb, const char *se, int level, int expandbody)
{
	const char *s = se;
	char buf[BUFSIZ], *n = buf, *ne = n;
	char *o = NULL, *oe;
	char *b, *be;
	int c;
	int oc = ')';

	/* Copy name */
	COPYNAME(ne, s, c);

	/* Copy opts (if present) */
	oe = ne + 1;
	if (*s == '(') {
		s++;	/* skip ( */
		o = oe;
		COPYOPTS(oe, s, oc);
		s++;	/* skip ) */
	}

	/* Copy body, skipping over escaped newlines */
	b = be = oe + 1;
	SKIPBLANK(s, c);
	if (c == '{') {	/* XXX permit silent {...} grouping */
		if ((se = matchchar(s, c, '}')) == NULL) {
			rpmError(RPMERR_BADSPEC, _("Macro %%%s has unterminated body"), n);
			se = s;	/* XXX W2DO? */
			return se;
		}
		s++;	/* XXX skip { */
		strncpy(b, s, (se - s));
		b[se - s] = '\0';
		be += strlen(b);
		se++;	/* XXX skip } */
		s = se;	/* move scan forward */
	} else {	/* otherwise free-field */
		COPYBODY(be, s, c);

		/* Trim trailing blanks/newlines */
		while (--be >= b && (c = *be) && (isblank(c) || c == '\n'))
			;
		*(++be) = '\0';	/* one too far */
	}

	/* Move scan over body */
	if (*s == '\n')
		s++;
	se = s;

	/* Names must start with alphabetic or _ and be at least 3 chars */
	if (!((c = *n) && (isalpha(c) || c == '_') && (ne - n) > 2)) {
		rpmError(RPMERR_BADSPEC, _("Macro %%%s has illegal name (%%define)"), n);
		return se;
	}

	/* Options must be terminated with ')' */
	if (o && oc != ')') {
		rpmError(RPMERR_BADSPEC, _("Macro %%%s has unterminated opts"), n);
		return se;
	}

	if ((be - b) < 1) {
		rpmError(RPMERR_BADSPEC, _("Macro %%%s has empty body"), n);
		return se;
	}

	if (expandbody && expandU(mb, b, (&buf[sizeof(buf)] - b))) {
		rpmError(RPMERR_BADSPEC, _("Macro %%%s failed to expand"), n);
		return se;
	}

	addMacro(mb->mc, n, o, b, (level - 1));

	return se;
}

static const char *
doUndefine(MacroContext *mc, const char *se)
{
	const char *s = se;
	char buf[BUFSIZ], *n = buf, *ne = n;
	int c;

	COPYNAME(ne, s, c);

	/* Move scan over body */
	if (*s == '\n')
		s++;
	se = s;

	/* Names must start with alphabetic or _ and be at least 3 chars */
	if (!((c = *n) && (isalpha(c) || c == '_') && (ne - n) > 2)) {
		rpmError(RPMERR_BADSPEC, _("Macro %%%s has illegal name (%%undefine)"), n);
		return se;
	}

	delMacro(mc, n);

	return se;
}

#ifdef	DYING
static void
dumpME(const char *msg, MacroEntry *me)
{
	if (msg)
		fprintf(stderr, "%s", msg);
	fprintf(stderr, "\tme %p", me);
	if (me)
		fprintf(stderr,"\tname %p(%s) prev %p",
			me->name, me->name, me->prev);
	fprintf(stderr, "\n");
}
#endif

static void
pushMacro(MacroEntry **mep, const char *n, const char *o, const char *b, int level)
{
	MacroEntry *prev = (*mep ? *mep : NULL);
	MacroEntry *me = malloc(sizeof(*me));

	me->prev = prev;
	me->name = (prev ? prev->name : strdup(n));
	me->opts = (o ? strdup(o) : NULL);
	me->body = (b ? strdup(b) : NULL);
	me->used = 0;
	me->level = level;
	*mep = me;
}

static void
popMacro(MacroEntry **mep)
{
	MacroEntry *me = (*mep ? *mep : NULL);

	if (me) {
		/* XXX cast to workaround const */
		if ((*mep = me->prev) == NULL)
			FREE((char *)me->name);
		FREE((char *)me->opts);
		FREE((char *)me->body);
		FREE(me);
	}
}

static void
freeArgs(MacroBuf *mb)
{
	MacroContext *mc = mb->mc;
	int c;

	/* Delete dynamic macro definitions */
	for (c = 0; c < mc->firstFree; c++) {
		MacroEntry *me;
		int skiptest = 0;
		if ((me = mc->macroTable[c]) == NULL)
			continue;
		if (me->level < mb->depth)
			continue;
		if (strlen(me->name) == 1 && strchr("#*0", *me->name)) {
			if (*me->name == '*' && me->used > 0)
				skiptest = 1;
			; /* XXX skip test for %# %* %0 */
		} else if (!skiptest && me->used <= 0) {
#if NOTYET
			rpmError(RPMERR_BADSPEC, _("Macro %%%s (%s) was not used below level %d"),
				me->name, me->body, me->level);
#endif
		}
		popMacro(&mc->macroTable[c]);
	}
}

static const char *
grabArgs(MacroBuf *mb, const MacroEntry *me, const char *se)
{
    char buf[BUFSIZ], *b, *be;
    char aname[16];
    const char *opts, *o;
    int argc = 0;
    const char **argv;
    int optc = 0;
    const char **optv;
    int opte;
    int c;

    /* Copy macro name as argv[0] */
    argc = 0;
    b = be = buf;
    strcpy(b, me->name);
    be += strlen(b);
    *be = '\0';
    argc++;	/* XXX count argv[0] */

    addMacro(mb->mc, "0", NULL, b, mb->depth);
    
    /* Copy args into buf until newline */
    *be++ = ' ';
    b = be;	/* Save beginning of args */
    while ((c = *se) != 0) {
	char *a;
	se++;
	if (c == '\n')
		break;
	if (isblank(c))
		continue;
	if (argc > 1)
		*be++ = ' ';
	a = be;
	while (!(isblank(c) || c == '\n')) {
		*be++ = c;
		if ((c = *se) == '\0')
			break;
		se++;
	}
	*be = '\0';
	argc++;
    }

    /* Add unexpanded args as macro */
    addMacro(mb->mc, "*", NULL, b, mb->depth);

#ifdef NOTYET
    /* XXX if macros can be passed as args ... */
    expandU(mb, buf, sizeof(buf));
#endif

    /* Build argv array */
    argv = (const char **)alloca((argc + 1) * sizeof(char *));
    b = be = buf;
    for (c = 0; c < argc; c++) {
	b = be;
	if ((be = strchr(b, ' ')) == NULL)
		be = b + strlen(b);
	*be++ = '\0';
	argv[c] = b;
    }
    argv[argc] = NULL;

    opts = me->opts;

    /* First count number of options ... */
    optind = 0;
    optc = 0;
    optc++;	/* XXX count argv[0] too */
    while((c = getopt(argc, (char **)argv, opts)) != -1) {
	if (!(c != '?' && (o = strchr(opts, c)))) {
		rpmError(RPMERR_BADSPEC, _("Unknown option %c in %s(%s)"),
			c, me->name, opts);
		return se;
	}
	optc++;
    }

    /* ... then allocate storage ... */
    opte = optc + (argc - optind);
    optv = (const char **)alloca((opte + 1) * sizeof(char *));
    optv[0] = me->name;
    optv[opte] = NULL;

    /* ... and finally copy the options */
    optind = 0;
    optc = 0;
    optc++;	/* XXX count optv[0] */
    while((c = getopt(argc, (char **)argv, opts)) != -1) {
	o = strchr(opts, c);
	b = be;
	*be++ = '-';
	*be++ = c;
	if (o[1] == ':') {
		*be++ = ' ';
		strcpy(be, optarg);
		be += strlen(be);
	}
	*be++ = '\0';
	sprintf(aname, "-%c", c);
	addMacro(mb->mc, aname, NULL, b, mb->depth);
	if (o[1] == ':') {
		sprintf(aname, "-%c*", c);
		addMacro(mb->mc, aname, NULL, optarg, mb->depth);
	}
	optv[optc] = b;
	optc++;
    }

    for (c = optind; c < argc; c++) {
	sprintf(aname, "%d", (c - optind + 1));
	addMacro(mb->mc, aname, NULL, argv[c], mb->depth);
	optv[optc] = argv[c];
	optc++;
    }
    sprintf(aname, "%d", (argc - optind + 1));
    addMacro(mb->mc, "#", NULL, aname, mb->depth);

    return se;
}

static void
doOutput(MacroBuf *mb, int waserror, const char *msg, size_t msglen)
{
	char buf[BUFSIZ];

	strncpy(buf, msg, msglen);
	buf[msglen] = '\0';
	expandU(mb, buf, sizeof(buf));
	if (waserror)
		rpmError(RPMERR_BADSPEC, "%s", buf);
	else
		fprintf(stderr, "%s", buf);
}

static void
doFoo(MacroBuf *mb, const char *f, size_t fn, const char *g, size_t glen)
{
	char buf[BUFSIZ], *b = NULL, *be;
	int c;

	buf[0] = '\0';
	if (g) {
		strncpy(buf, g, glen);
		buf[glen] = '\0';
		expandU(mb, buf, sizeof(buf));
	}
	if (STREQ("basename", f, fn)) {
		if ((b = strrchr(buf, '/')) == NULL)
			b = buf;
#if NOTYET
	/* XXX watchout for conflict with %dir */
	} else if (STREQ("dirname", f, fn)) {
		if ((b = strrchr(buf, '/')) != NULL)
			*b = '\0';
		b = buf;
#endif
	} else if (STREQ("suffix", f, fn)) {
		if ((b = strrchr(buf, '.')) != NULL)
			b++;
	} else if (STREQ("expand", f, fn)) {
		b = buf;
	} else if (STREQ("uncompress", f, fn)) {
		int compressed = 1;
		for (b = buf; (c = *b) && isblank(c);)
			b++;
		for (be = b; (c = *be) && !isblank(c);)
			be++;
		*be++ = '\0';
#ifndef	DEBUG_MACROS
		isCompressed(b, &compressed);
#endif
		switch(compressed) {
		default:
		case 0:	/* COMPRESSED_NOT */
			sprintf(be, "%%_cat %s", b);
			break;
		case 1:	/* COMPRESSED_OTHER */
			sprintf(be, "%%_gzip -dc %s", b);
			break;
		case 2:	/* COMPRESSED_BZIP2 */
			sprintf(be, "%%_bzip2 %s", b);
			break;
		}
		b = be;
	} else if (STREQ("S", f, fn)) {
		for (b = buf; (c = *b) && isdigit(c);)
			b++;
		if (!c) {	/* digit index */
			b++;
			sprintf(b, "%%SOURCE%s", buf);
		} else
			b = buf;
	} else if (STREQ("P", f, fn)) {
		for (b = buf; (c = *b) && isdigit(c);)
			b++;
		if (!c) {	/* digit index */
			b++;
			sprintf(b, "%%PATCH%s", buf);
		} else
			b = buf;
	} else if (STREQ("F", f, fn)) {
		b = buf + strlen(buf) + 1;
		sprintf(b, "file%s.file", buf);
#if DEAD
fprintf(stderr, "FILE: \"%s\"\n", b);
#endif
	}

	if (b) {
		expandT(mb, b, strlen(b));
	}
}

/* The main recursion engine */

static int
expandMacro(MacroBuf *mb)
{
    MacroEntry **mep;
    MacroEntry *me;
    const char *s = mb->s, *se;
    const char *f, *fe;
    const char *g, *ge;
    size_t fn, gn;
    char *t = mb->t;	/* save expansion pointer for printExpand */
    int c;
    int rc = 0;
    int negate;
    int grab;
    int chkexist;

    if (++mb->depth > max_macro_depth) {
	rpmError(RPMERR_BADSPEC, _("Recursion depth(%d) greater than max(%d)"),
		mb->depth, max_macro_depth);
	mb->depth--;
	mb->expand_trace = 1;
	return 1;
    }

    while (rc == 0 && mb->nb > 0 && (c = *s) != '\0') {
	s++;
	/* Copy text until next macro */
	switch(c) {
	case '%':
		if (*s != '%')
			break;
		s++;	/* skip first % in %% */
		/* fall thru */
	default:
		SAVECHAR(mb, c);
		continue;
		break;
	}

	/* Expand next macro */
	f = fe = NULL;
	g = ge = NULL;
	if (mb->depth > 1)	/* XXX full expansion for outermost level */
		t = mb->t;	/* save expansion pointer for printExpand */
	negate = 0;
	grab = 0;
	chkexist = 0;
	switch ((c = *s)) {
	default:		/* %name substitution */
		while (strchr("!?", *s) != NULL) {
			switch(*s++) {
			case '!':
				negate = (++negate % 2);
				break;
			case '?':
				chkexist++;
				break;
			}
		}
		f = se = s;
		if (*se == '-')
			se++;
		while((c = *se) && (isalnum(c) || c == '_'))
			se++;
		if (*se == '*')
			se++;
		fe = se;
		/* For "%name " macros ... */
		if ((c = *fe) && isblank(c))
			grab = 1;
		break;
	case '(':		/* %(...) shell escape */
		if ((se = matchchar(s, c, ')')) == NULL) {
			rpmError(RPMERR_BADSPEC, _("Unterminated %c: %s"), c, s);
			rc = 1;
			continue;
		}
		if (mb->macro_trace)
			printMacro(mb, s, se+1);

		s++;	/* skip ( */
		rc = doShellEscape(mb, s, (se - s));
		se++;	/* skip ) */

		s = se;
		continue;
		break;
	case '{':		/* %{...}/%{...:...} substitution */
		if ((se = matchchar(s, c, '}')) == NULL) {
			rpmError(RPMERR_BADSPEC, _("Unterminated %c: %s"), c, s);
			rc = 1;
			continue;
		}
		f = s+1;/* skip { */
		se++;	/* skip } */
		while (strchr("!?", *f) != NULL) {
			switch(*f++) {
			case '!':
				negate = (++negate % 2);
				break;
			case '?':
				chkexist++;
				break;
			}
		}
		for (fe = f; (c = *fe) && !strchr(":}", c);)
			fe++;
		if (c == ':') {
			g = fe + 1;
			ge = se - 1;
		}
		break;
	}

	/* XXX Everything below expects fe > f */
	fn = (fe - f);
	gn = (ge - g);
	if (fn <= 0) {
		rpmError(RPMERR_BADSPEC, _("Empty token"));
		s = se;
		continue;
	}

	if (mb->macro_trace)
		printMacro(mb, s, se);

	/* Expand builtin macros */
	if (STREQ("global", f, fn)) {
		s = doDefine(mb, se, RMIL_GLOBAL, 1);
		continue;
	}
	if (STREQ("define", f, fn)) {
		s = doDefine(mb, se, mb->depth, 0);
		continue;
	}
	if (STREQ("undefine", f, fn)) {
		s = doUndefine(mb->mc, se);
		continue;
	}

	if (STREQ("echo", f, fn) ||
	    STREQ("warn", f, fn) ||
	    STREQ("error", f, fn)) {
		int waserror = 0;
		if (STREQ("error", f, fn))
			waserror = 1;
		if (g < ge)
			doOutput(mb, waserror, g, gn);
		else
			doOutput(mb, waserror, f, fn);
		s = se;
		continue;
	}

	if (STREQ("trace", f, fn)) {
		/* XXX TODO restore expand_trace/macro_trace to 0 on return */
		mb->expand_trace = mb->macro_trace = (negate ? 0 : mb->depth);
		if (mb->depth == 1) {
			print_macro_trace = mb->macro_trace;
			print_expand_trace = mb->expand_trace;
		}
		s = se;
		continue;
	}

	if (STREQ("dump", f, fn)) {
		dumpMacroTable(mb->mc);
		if (*se == '\n')
			se++;
		s = se;
		continue;
	}

	/* XXX necessary but clunky */
	if (STREQ("basename", f, fn) ||
	    STREQ("suffix", f, fn) ||
	    STREQ("expand", f, fn) ||
	    STREQ("uncompress", f, fn) ||
	    STREQ("S", f, fn) ||
	    STREQ("P", f, fn) ||
	    STREQ("F", f, fn)) {
		doFoo(mb, f, fn, g, gn);
		s = se;
		continue;
	}

	/* Expand defined macros */
	mep = findEntry(mb->mc, f, fn);
	me = (mep ? *mep : NULL);

	/* XXX Special processing for flags */
	if (*f == '-') {
		if (me)
			me->used++;	/* Mark macro as used */
		if ((me == NULL && !negate) ||	/* Without -f, skip %{-f...} */
		    (me != NULL && negate)) {	/* With -f, skip %{!-f...} */
			s = se;
			continue;
		}

		if (g && g < ge) {		/* Expand X in %{-f:X} */
			rc = expandT(mb, g, gn);
		} else
		if (me->body && *me->body) {	/* Expand %{-f}/%{-f*} */
			rc = expandT(mb, me->body, strlen(me->body));
		}
		s = se;
		continue;
	}

	/* XXX Special processing for macro existence */
	if (chkexist) {
		if ((me == NULL && !negate) ||	/* Without -f, skip %{?f...} */
		    (me != NULL && negate)) {	/* With -f, skip %{!?f...} */
			s = se;
			continue;
		}
		if (g && g < ge) {		/* Expand X in %{?f:X} */
			rc = expandT(mb, g, gn);
		} else
		if (me->body && *me->body) {	/* Expand %{?f}/%{?f*} */
			rc = expandT(mb, me->body, strlen(me->body));
		}
		s = se;
		continue;
	}
	
	if (me == NULL) {	/* leave unknown %... as is */
#ifndef HACK
#if DEAD
		/* XXX hack to skip over empty arg list */
		if (fn == 1 && *f == '*') {
			s = se;
			continue;
		}
#endif
		/* XXX hack to permit non-overloaded %foo to be passed */
		c = '%';	/* XXX only need to save % */
		SAVECHAR(mb, c);
#else
		rpmError(RPMERR_BADSPEC, _("Macro %%%.*s not found, skipping"), fn, f);
		s = se;
#endif
		continue;
	}

	/* Setup args for "%name " macros with opts */
	if (me && me->opts != NULL) {
		if (grab)
			se = grabArgs(mb, me, fe);
		else {
		    addMacro(mb->mc, "*", NULL, "", mb->depth);
		}
	}

	/* Recursively expand body of macro */
	mb->s = me->body;
	rc = expandMacro(mb);
	if (rc == 0)
		me->used++;	/* Mark macro as used */

	/* Free args for "%name " macros with opts */
	if (me->opts != NULL)
		freeArgs(mb);

	s = se;
    }

    *mb->t = '\0';
    mb->s = s;
    mb->depth--;
    if (rc != 0 || mb->expand_trace)
	printExpansion(mb, t, mb->t);
    return rc;
}

/* =============================================================== */
const char *
getMacroBody(MacroContext *mc, const char *name)
{
    MacroEntry **mep = findEntry(mc, name, 0);
    MacroEntry *me = (mep ? *mep : NULL);
    return ( me ? me->body : (const char *)NULL );
}

/* =============================================================== */
int
expandMacros(void *spec, MacroContext *mc, char *s, size_t slen)
{
	MacroBuf macrobuf, *mb = &macrobuf;
	char *tbuf;
	int rc;

	if (s == NULL || slen <= 0)
		return 0;

	tbuf = alloca(slen + 1);
	memset(tbuf, 0, (slen + 1));

	mb->s = s;
	mb->t = tbuf;
	mb->nb = slen;
	mb->depth = 0;
	mb->macro_trace = print_macro_trace;
	mb->expand_trace = print_expand_trace;

	mb->spec = spec;	/* (future) %file expansion info */
	mb->mc = mc;

	rc = expandMacro(mb);

	if (mb->nb <= 0)
		rpmError(RPMERR_BADSPEC, _("Target buffer overflow"));

	tbuf[slen] = '\0';	/* XXX just in case */
	strncpy(s, tbuf, (slen - mb->nb + 1));

	return rc;
}

void
addMacro(MacroContext *mc, const char *n, const char *o, const char *b, int level)
{
	MacroEntry **mep;

	/* If new name, expand macro table */
	if ((mep = findEntry(mc, n, 0)) == NULL) {
		if (mc->firstFree == mc->macrosAllocated)
			expandMacroTable(mc);
		mep = mc->macroTable + mc->firstFree++;
	}

	/* Push macro over previous definition */
	pushMacro(mep, n, o, b, level);

	/* If new name, sort macro table */
	if ((*mep)->prev == NULL)
		sortMacroTable(mc);
}

void
delMacro(MacroContext *mc, const char *name)
{
	MacroEntry **mep = findEntry(mc, name, 0);

	/* If name exists, pop entry */
	if ((mep = findEntry(mc, name, 0)) != NULL)
		popMacro(mep);
}

void
initMacros(MacroContext *mc, const char *macrofile)
{
	char *m, *mfile, *me;
	static int first = 1;

	/* XXX initialization should be per macro context, not per execution */
	if (first) {
		mc->macroTable = NULL;
		expandMacroTable(mc);
		max_macro_depth = MAX_MACRO_DEPTH;	/* XXX Assume good ol' macro expansion */
		first = 0;
	}

	if (macrofile == NULL)
		return;

	for (mfile = m = strdup(macrofile); *mfile; mfile = me) {
		FILE *fp;
		char buf[BUFSIZ];
		MacroBuf macrobuf, *mb = &macrobuf;

		if ((me = strchr(mfile, ':')) != NULL)
			*me++ = '\0';
		else
			me = mfile + strlen(mfile);

		if ((fp=fopen(mfile, "r")) == NULL)
			continue;

		/* XXX Assume new fangled macro expansion */
		max_macro_depth = 16;

		while(rdcl(buf, sizeof(buf), fp, 1) != NULL) {
			char c, *n;

			n = buf;
			SKIPBLANK(n, c);

			if (c != '%')
				continue;
			n++;
			mb->mc = mc;	/* XXX just enough to get by */
			(void)doDefine(mb, n, RMIL_MACROFILES, 0);
		}
		fclose(fp);
	}
	if (m)
		free(m);
}

void
freeMacros(MacroContext *mc)
{
	int i;
    
	for (i = 0; i < mc->firstFree; i++) {
		MacroEntry *me;
		while ((me = mc->macroTable[i]) != NULL) {
			/* XXX cast to workaround const */
			if ((mc->macroTable[i] = me->prev) == NULL)
				FREE((char *)me->name);
			FREE((char *)me->opts);
			FREE((char *)me->body);
			FREE(me);
		}
	}
	FREE(mc->macroTable);
}

/* =============================================================== */

int isCompressed(const char *file, int *compressed)
{
    FD_t fd;
    ssize_t nb;
    int rderrno;
    unsigned char magic[4];

    *compressed = COMPRESSED_NOT;

    if (fdFileno(fd = fdOpen(file, O_RDONLY, 0)) < 0) {
	rpmError(RPMERR_BADSPEC, _("File %s: %s"), file, strerror(errno));
	return 1;
    }
    nb = fdRead(fd, magic, sizeof(magic));
    rderrno = errno;
    fdClose(fd);

    if (nb < 0) {
	rpmError(RPMERR_BADSPEC, _("File %s: %s"), file, strerror(rderrno));
	return 1;
    } else if (nb < sizeof(magic)) {
	rpmError(RPMERR_BADSPEC, _("File %s is smaller than %d bytes"),
		file, sizeof(magic));
	return 0;
    }

    if (((magic[0] == 0037) && (magic[1] == 0213)) ||  /* gzip */
	((magic[0] == 0037) && (magic[1] == 0236)) ||  /* old gzip */
	((magic[0] == 0037) && (magic[1] == 0036)) ||  /* pack */
	((magic[0] == 0037) && (magic[1] == 0240)) ||  /* SCO lzh */
	((magic[0] == 0037) && (magic[1] == 0235)) ||  /* compress */
	((magic[0] == 0120) && (magic[1] == 0113) &&
	 (magic[2] == 0003) && (magic[3] == 0004))     /* pkzip */
	) {
	*compressed = COMPRESSED_OTHER;
    } else if ((magic[0] == 'B') && (magic[1] == 'Z')) {
	*compressed = COMPRESSED_BZIP2;
    }

    return 0;
}

/* =============================================================== */

#ifdef DEBUG_MACROS

MacroContext mc = { NULL, 0, 0};
char *macrofile = "./paths:./environment:./macros";
char *testfile = "./test";

int
main(int argc, char *argv[])
{
	char buf[BUFSIZ];
	FILE *fp;
	int x;

	initMacros(&mc, macrofile);
	dumpMacroTable(&mc);

	if ((fp = fopen(testfile, "r")) != NULL) {
		while(fgets(buf, sizeof(buf), fp)) {
			buf[strlen(buf)-1] = '\0';
			x = expandMacros(NULL, &mc, buf, sizeof(buf));
			fprintf(stderr, "%d->%s\n", x, buf);
			memset(buf, 0, sizeof(buf));
		}
		fclose(fp);
	}

	while(fgets(buf, sizeof(buf), stdin)) {
		buf[strlen(buf)-1] = '\0';
		x = expandMacros(NULL, &mc, buf, sizeof(buf));
		fprintf(stderr, "%d->%s\n <-\n", x, buf);
		memset(buf, 0, sizeof(buf));
	}

	return 0;
}
#endif	/* DEBUG_MACROS */

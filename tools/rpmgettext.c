/* rpmheader: spit out the header portion of a package */

#define	_GNU_SOURCE	1

#include "system.h"

#include "../build/rpmbuild.h"
#include "../build/buildio.h"

#include "rpmlead.h"
#include "signature.h"
#include "header.h"

#define	MYDEBUG	2

#ifdef	MYDEBUG
#include <stdarg.h>
static void dpf(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fflush(stderr);
}

#define	DPRINTF(_lvl, _fmt)	if ((_lvl) <= debug) dpf _fmt
#else
#define	DPRINTF(_lvl, _fmt)
#endif

const char *progname = NULL;
int debug = MYDEBUG;
int verbose = 0;
char *inputdir = "/mnt/redhat/comps/dist/5.2";
char *outputdir = "/tmp/OUT";
char *onlylang = NULL;

int gentran = 0;

static inline char *
basename(const char *file)
{
	char *fn = strrchr(file, '/');
	return fn ? fn+1 : (char *)file;
}

static const char *
getTagString(int tval)
{
    const struct headerTagTableEntry *t;
    static char buf[128];

    for (t = rpmTagTable; t->name != NULL; t++) {
	if (t->val == tval)
	    return t->name;
    }
    sprintf(buf, "<RPMTAG_%d>", tval);
    return buf;
}

static int
getTagVal(const char *tname)
{
    const struct headerTagTableEntry *t;
    int tval;

    for (t = rpmTagTable; t->name != NULL; t++) {
	if (!strncmp(tname, t->name, strlen(t->name)))
	    return t->val;
    }
    sscanf(tname, "%d", &tval);
    return tval;
}

const struct headerTypeTableEntry {
    char *name;
    int val;
} rpmTypeTable[] = {
    {"RPM_NULL_TYPE",	0},
    {"RPM_CHAR_TYPE",	1},
    {"RPM_INT8_TYPE",	2},
    {"RPM_INT16_TYPE",	3},
    {"RPM_INT32_TYPE",	4},
    {"RPM_INT64_TYPE",	5},
    {"RPM_STRING_TYPE",	6},
    {"RPM_BIN_TYPE",	7},
    {"RPM_STRING_ARRAY_TYPE",	8},
    {"RPM_I18NSTRING_TYPE",	9},
    {NULL,	0}
};

static char *
getTypeString(int tval)
{
    const struct headerTypeTableEntry *t;
    static char buf[128];

    for (t = rpmTypeTable; t->name != NULL; t++) {
	if (t->val == tval)
	    return t->name;
    }
    sprintf(buf, "<RPM_%d_TYPE>", tval);
    return buf;
}

static char **
headerGetLangs(Header h)
{
    char **s, *e, **table;
    int i, type, count;

    if (!headerGetRawEntry(h, HEADER_I18NTABLE, &type, (void **)&s, &count))
	return NULL;

    if ((table = (char **)calloc((count+1), sizeof(char *))) == NULL)
	return NULL;

    for (i = 0, e = *s; i < count > 0; i++, e += strlen(e)+1) {
	table[i] = e;
    }
    table[count] = NULL;

    return table;
}

/* ================================================================== */

static const char *
genSrpmFileName(Header h)
{
    char *name, *version, *release, *sourcerpm;
    char sfn[BUFSIZ];

    headerGetEntry(h, RPMTAG_NAME, NULL, (void **)&name, NULL);
    headerGetEntry(h, RPMTAG_VERSION, NULL, (void **)&version, NULL);
    headerGetEntry(h, RPMTAG_RELEASE, NULL, (void **)&release, NULL);
    sprintf(sfn, "%s-%s-%s.src.rpm", name, version, release);

    headerGetEntry(h, RPMTAG_SOURCERPM, NULL, (void **)&sourcerpm, NULL);

#if 0
    if (strcmp(sourcerpm, sfn))
	return strdup(sourcerpm);

    return NULL;
#else
    return strdup(sourcerpm);
#endif

}

static const char *
hasLang(const char *onlylang, char **langs, char **s)
{
	const char *e = *s;
	int i = 0;

	while(langs[i] && strcmp(langs[i], onlylang)) {
		i++;
		e += strlen(e) + 1;
	}
#if 0
	if (langs[i] && *e)
		return e;
	return NULL;
#else
	return onlylang;
#endif
}

/* ================================================================== */
/* XXX stripped down gettext environment */

#define	xstrdup		strdup
#define	xmalloc		malloc
#define	xrealloc	realloc

#if !defined(PARAMS)
#define	PARAMS(_x)	_x
#endif

/* Length from which starting on warnings about too long strings are given.
   Several systems have limits for strings itself, more have problems with
   strings in their tools (important here: gencat).  1024 bytes is a
   conservative limit.  Because many translation let the message size grow
   (German translations are always bigger) choose a length < 1024.  */
#if !defined(WARN_ID_LEN)
#define WARN_ID_LEN 900
#endif

/* This is the page width for the message_print function.  It should
   not be set to more than 79 characters (Emacs users will appreciate
   it).  It is used to wrap the msgid and msgstr strings, and also to
   wrap the file position (#:) comments.  */
#if !defined(PAGE_WIDTH)
#define PAGE_WIDTH 79
#endif

#include "fstrcmp.c"

#include "str-list.c"

#define	_LIBGETTEXT_H	1	/* XXX WTFO? _LIBINTL_H is undef? */
#undef	_			/* XXX WTFO? */

#include "message.c"

/* ================================================================== */

/* XXX cribbed from gettext/src/message.c */
static const char escapes[] = "\b\f\n\r\t";
static const char escape_names[] = "bfnrt";

static void
expandRpmPO(char *t, const char *s)
{
    const char *esc;
    int c;

    *t++ = '"';
    while((c = *s++) != '\0') {
	esc = strchr(escapes, c);
	if (esc == NULL && (!escape || isprint(c))) {
		if (c == '\\' || c == '"')
			*t++ = '\\';
		*t++ = c;
	} else if (esc != NULL) {
		*t++ = '\\';
		*t++ = escape_names[esc - escapes];
		if (c == '\n') {
			strcpy(t, "\"\n\"");
			t += strlen(t);
		}
	} else {
		*t++ = '\\';
		sprintf(t, "%3.3o", (c & 0377));
		t += strlen(t);
	}
    }
    *t++ = '"';
    *t = '\0';
}

static void
contractRpmPO(char *t, const char *s)
{
    int instring = 0;
    const char *esc;
    int c;

    while((c = *s++) != '\0') {
	if (!(c == '"' || !(instring & 1)))
		continue;
	switch(c) {
	case '\\':
		if (strchr("0123467", *s)) {
			char *se;
			*t++ = strtol(s, &se, 8);
			s = se;
		} else if ((esc = strchr(escape_names, *s)) != NULL) {
			*t++ = escapes[esc - escape_names];
			s++;
		} else {
			*t++ = *s++;
		}
		break;
	case '"':
		instring++;
		break;
	default:
		*t++ = c;
		break;
	}
    }
    *t = '\0';
}

static int poTags[] = {
    RPMTAG_DESCRIPTION,
    RPMTAG_GROUP,
    RPMTAG_SUMMARY,
#ifdef NOTYET
    RPMTAG_DISTRIBUTION,
    RPMTAG_VENDOR,
    RPMTAG_LICENSE,
    RPMTAG_PACKAGER,
#endif
    0
};

static int
gettextfile(int fd, const char *file, FILE *fp, int *poTags)
{
    struct rpmlead lead;
    Header h;
    int *tp;
    char **langs;
    const char *sourcerpm;
    char buf[BUFSIZ];
    
    DPRINTF(99, ("gettextfile(%d,\"%s\",%p,%p)\n", fd, file, fp, poTags));

    fprintf(fp, "\n#========================================================");

    readLead(fd, &lead);
    rpmReadSignature(fd, NULL, lead.signature_type);
    h = headerRead(fd, (lead.major >= 3) ?
		    HEADER_MAGIC_YES : HEADER_MAGIC_NO);

    if ((langs = headerGetLangs(h)) == NULL)
	return 1;

    sourcerpm = genSrpmFileName(h);

    for (tp = poTags; *tp != 0; tp++) {
	const char *onlymsgstr;
	char **s, *e;
	int i, type, count, nmsgstrs;

	if (!headerGetRawEntry(h, *tp, &type, (void **)&s, &count))
	    continue;

	/* XXX skip untranslated RPMTAG_GROUP for now ... */
	switch (*tp) {
	case RPMTAG_GROUP:
	    if (count <= 1)
		continue;
	    break;
	default:
	    break;
	}

	/* XXX generate catalog for single language */
	onlymsgstr = NULL;
	if (onlylang != NULL &&
	   (onlymsgstr = hasLang(onlylang, langs, s)) == NULL)
		continue;
	

	/* Print xref comment */
	fprintf(fp, "\n#%s\n", getTagString(*tp));
	fprintf(fp, "#: %s:%d\n", basename(file), *tp);
	if (sourcerpm)
	    fprintf(fp, "#: %s:%d\n", sourcerpm, *tp);

	/* Print msgid */
	e = *s;
	expandRpmPO(buf, e);
	fprintf(fp, "msgid %s\n", buf);

	nmsgstrs = 0;
	for (i = 1, e += strlen(e)+1; i < count && e != NULL; i++, e += strlen(e)+1) {
		if (!(onlymsgstr == NULL || onlymsgstr == e))
			continue;
		nmsgstrs++;
		expandRpmPO(buf, e);
		fprintf(fp, "msgstr");
		if (onlymsgstr == NULL)
			fprintf(fp, "(%s)", langs[i]);
		fprintf(fp, " %s\n", buf);
	}

	if (nmsgstrs < 1)
	    fprintf(fp, "msgstr \"\"\n");

	if (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE)
	    FREE(s)
    }
    
    FREE((char *)sourcerpm);
    FREE(langs);

    return 0;
}

/* ================================================================== */

static int
slurp(const char *file, char **ibufp, size_t *nbp)
{
    struct stat sb;
    char *ibuf;
    size_t nb;
    int fd;

    DPRINTF(99, ("slurp(\"%s\",%p,%p)\n", file, ibufp, nbp));

    if (ibufp)
	*ibufp = NULL;
    if (nbp)
	*nbp = 0;

    if (stat(file, &sb) < 0) {
	fprintf(stderr, _("stat(%s): %s\n"), file, strerror(errno));
	return 1;
    }

    nb = sb.st_size + 1;
    if ((ibuf = (char *)malloc(nb)) == NULL) {
	fprintf(stderr, _("malloc(%d)\n"), nb);
	return 2;
    }

    if ((fd = open(file, O_RDONLY)) < 0) {
	fprintf(stderr, _("open(%s): %s\n"), file, strerror(errno));
	return 3;
    }
    if ((nb = read(fd, ibuf, nb)) != sb.st_size) {
	fprintf(stderr, _("read(%s): %s\n"), file, strerror(errno));
	return 4;
    }
    close(fd);
    ibuf[nb] = '\0';

    if (ibufp)
	*ibufp = ibuf;
    if (nbp)
	*nbp = nb;

    return 0;
}

static char *
matchchar(const char *p, char pl, char pr)
{
	int lvl = 0;
	char c;

	while ((c = *p++) != '\0') {
		if (c == '\\') {	/* escaped chars */
			p++;
			continue;
		}
		if (pl == pr && c == pl && *p == pr) {	/* doubled quotes */
			p++;
			continue;
		}
		if (c == pr) {
			if (--lvl <= 0) return (char *)--p;
		} else if (c == pl)
			lvl++;
	}
	return NULL;
}

typedef struct {
	char *name;
	int len;
	int haslang;
} KW_t;

KW_t keywords[] = {
	{ "domain",	6, 0 },
	{ "msgid",	5, 0 },
	{ "msgstr",	6, 1 },
	{ NULL, 0, 0 }
};

#define	SKIPWHITE {while ((c = *se) && strchr("\b\f\n\r\t ", c)) se++;}
#define	NEXTLINE  {state = 0; while ((c = *se) && c != '\n') se++; if (c == '\n') se++;}

static int
parsepofile(const char *file, message_list_ty **mlpp, string_list_ty **flpp)
{
    char tbuf[BUFSIZ];
    string_list_ty *flp;
    message_list_ty *mlp;
    message_ty *mp;
    KW_t *kw = NULL;
    char *lang = NULL;
    char *buf, *s, *se, *t;
    size_t nb;
    int c, rc, tval;
    int state = 1;

    DPRINTF(99, ("parsepofile(\"%s\",%p,%p)\n", file, mlpp, flpp));

DPRINTF(100, ("================ %s\n", file));

    if ((rc = slurp(file, &buf, &nb)) != 0)
	return rc;

    flp = string_list_alloc();
    mlp = message_list_alloc();
    mp = NULL;
    s = buf;
    while ((c = *s) != '\0') {
	se = s;
	switch (state) {
	case 0:		/* free wheeling */
	case 1:		/* comment "domain" "msgid" "msgstr" */
		state = 1;
		SKIPWHITE;
		s = se;
		if (!(isalpha(c) || c == '#')) {
			fprintf(stderr, _("non-alpha char at \"%.20s\"\n"), se);
			NEXTLINE;
			break;
		}

		if (mp == NULL) {
			mp = message_alloc(NULL);
		}

		if (c == '#') {
			while ((c = *se) && c != '\n') se++;
	/* === s:se has comment */
DPRINTF(100, ("%.*s\n", (int)(se-s), s));

			if (c)
				*se = '\0';	/* zap \n */
			switch (s[1]) {
			case '~':	/* archival translations */
				break;
			case ':':	/* file cross reference */
			{   char *f, *fe, *g, *ge;
			    for (f = s+2; f < se; f = ge) {
				while (*f && strchr(" \t", *f)) f++;
				fe = f;
				while (*fe && !strchr(": \t", *fe)) fe++;
				if (*fe != ':') {
					fprintf(stderr, _("malformed #: xref at \"%.60s\"\n"), s);
					break;
				}
				*fe++ = '\0';
				g = ge = fe;
				while (*ge && !strchr(" \t", *ge)) ge++;
				*ge++ = '\0';
				tval = getTagVal(g);
				string_list_append_unique(flp, f);
				message_comment_filepos(mp, f, tval);
			    }
			}	break;
			case '.':	/* automatic comments */
				if (*s == '#') {
					s++;
					if (*s == '.') s++;
				}
				while (*s && strchr(" \t", *s)) s++;
				message_comment_dot_append(mp, xstrdup(s));
				break;
			case ',':	/* flag... */
				if (strstr (s, "fuzzy") != NULL)
					mp->is_fuzzy = 1;
				mp->is_c_format = parse_c_format_description_string(s);
				mp->do_wrap = parse_c_width_description_string(s);
				break;
			default:
				/* XXX might want to fix and/or warn here */
			case ' ':	/* comment...*/
				if (*s == '#') s++;
				while (*s && strchr(" \t", *s)) s++;
				message_comment_append(mp, xstrdup(s));
				break;
			}
			if (c)
				se++;	/* skip \n */
			break;
		}

		for (kw = keywords; kw->name != NULL; kw++) {
			if (!strncmp(s, kw->name, kw->len)) {
				se += kw->len;
				break;
			}
		}
		if (kw == NULL || kw->name == NULL) {
			fprintf(stderr, _("unknown keyword at \"%.20s\"\n"), se);
			NEXTLINE;
			break;
		}
	/* === s:se has keyword */
DPRINTF(100, ("%.*s", (int)(se-s), s));

		SKIPWHITE;
		s = se;
		if (kw->haslang && *se == '(') {
			while ((c = *se) && c != ')') se++;
			if (c != ')') {
				fprintf(stderr, _("unclosed paren at \"%.20s\"\n"), s);
				se = s;
				NEXTLINE;
				break;
			}
			s++;	/* skip ( */
	/* === s:se has lang */
DPRINTF(100, ("(%.*s)", (int)(se-s), s));
			*se = '\0';
			lang = s;
			if (c)
				se++;	/* skip ) */
		} else
			lang = "C";
DPRINTF(100, ("\n"));

		SKIPWHITE;
		if (*se != '"') {
			fprintf(stderr, _("missing string at \"%.20s\"\n"), s);
			se = s;
			NEXTLINE;
			break;
		}
		state = 2;
		break;
	case 2:		/* "...." */
		SKIPWHITE;
		if (c != '"') {
			fprintf(stderr, _("not a string at \"%.20s\"\n"), s);
			NEXTLINE;
			break;
		}

		t = tbuf;
		*t = '\0';
		do {
			s = se;
			s++;	/* skip open quote */
			if ((se = matchchar(s, c, c)) == NULL) {
				fprintf(stderr, _("missing close %c at \"%.20s\"\n"), c, s);
				se = s;
				NEXTLINE;
				break;
			}

	/* === s:se has next part of string */
			*se = '\0';
			contractRpmPO(t, s);
			t += strlen(t);
			*t = '\0';

			se++;	/* skip close quote */
			SKIPWHITE;
		} while (c == '"');

		if (!strcmp(kw->name, "msgid")) {
			FREE(((char *)mp->msgid));
			mp->msgid = xstrdup(tbuf);
		} else if (!strcmp(kw->name, "msgstr")) {
			static lex_pos_ty pos = { __FILE__, __LINE__ };
			message_variant_append(mp, xstrdup(lang), xstrdup(tbuf), &pos);
			lang = NULL;
		}
		/* XXX Peek to see if next item is comment */
		SKIPWHITE;
		if (*se == '#' || *se == '\0') {
			message_list_append(mlp, mp);
			mp = NULL;
		}
		state = 1;
		break;
	}
	s = se;
    }

    if (mlpp)
	*mlpp = mlp;
    else
	message_list_free(mlp);

    if (flpp)
	*flpp = flp;
    else
	string_list_free(flp);

    FREE(buf);
    return rc;
}

/* ================================================================== */

static int
readRPM(char *fileName, Spec *specp, struct rpmlead *lead, Header *sigs, CSA_t *csa)
{
    int fdi = 0;
    Spec spec;
    int rc;

    DPRINTF(99, ("readRPM(\"%s\",%p,%p,%p,%p)\n", fileName, specp, lead, sigs, csa));

    if (fileName != NULL && (fdi = open(fileName, O_RDONLY, 0644)) < 0) {
	fprintf(stderr, _("readRPM: open %s: %s\n"), fileName, strerror(errno));
	exit(1);
    }

    /* Get copy of lead */
    if ((rc = read(fdi, lead, sizeof(*lead))) != sizeof(*lead)) {
	fprintf(stderr, _("readRPM: read %s: %s\n"), fileName, strerror(errno));
	exit(1);
    }
    lseek(fdi, 0, SEEK_SET);	/* XXX FIXME: EPIPE */

    /* Reallocate build data structures */
    spec = newSpec();
    spec->packages = newPackage(spec);

    /* XXX the header just allocated will be allocated again */
    if (spec->packages->header) {
	headerFree(spec->packages->header);
	spec->packages->header = NULL;
    }

   /* Read the rpm lead and header */
    rc = rpmReadPackageInfo(fdi, sigs, &spec->packages->header);
    switch (rc) {
    case 1:
	fprintf(stderr, _("error: %s is not an RPM package\n"), fileName);
	exit(1);
    case 0:
	break;
    default:
	fprintf(stderr, _("error: reading header from %s\n"), fileName);
	exit(1);
	break;
    }

    if (specp)
	*specp = spec;

    if (csa) {
	csa->cpioFdIn = fdi;
    } else if (fdi != 0) {
	close(fdi);
    }

    return 0;
}

/* For all poTags in h, if msgid is in msg list, then substitute msgstr's */
static int
headerInject(Header h, int *poTags, message_list_ty *mlp)
{
    message_ty *mp;
    message_variant_ty *mvp;
    int *tp;
    char **langs;
    
    DPRINTF(99, ("headerInject(%p,%p,%p)\n", h, poTags, mlp));

    if ((langs = headerGetLangs(h)) == NULL)
	return 1;

    for (tp = poTags; *tp != 0; tp++) {
	char **s, *e;
	int i, type, count;

	if (!headerGetRawEntry(h, *tp, &type, (void **)&s, &count))
	    continue;

	/* Search for the msgid */
	e = *s;
	if ((mp = message_list_search(mlp, e)) != NULL) {
DPRINTF(1, ("%s\n\tmsgid\n", getTagString(*tp)));
	    for (i = 0; i < count; i++) {
		if ((mvp = message_variant_search(mp, langs[i])) == NULL)
		    continue;
DPRINTF(1, ("\tmsgstr(%s)\n", langs[i]));
		headerAddI18NString(h, *tp, (char *)mvp->msgstr, langs[i]);
	    }
	}

	if (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE)
	    FREE(s)
    }

    FREE(langs);

    return 0;
}

static int
rewriteBinaryRPM(char *fni, char *fno, message_list_ty *mlp)
{
    struct rpmlead lead;	/* XXX FIXME: exorcize lead/arch/os */
    Header sigs;
    Spec spec;
    CSA_t csabuf, *csa = &csabuf;
    int rc;

    DPRINTF(99, ("rewriteBinaryRPM(\"%s\",\"%s\",%p)\n", fni, fno, mlp));

    csa->cpioArchiveSize = 0;
    csa->cpioFdIn = -1;
    csa->cpioList = NULL;
    csa->cpioCount = 0;
    csa->lead = &lead;		/* XXX FIXME: exorcize lead/arch/os */

    /* Read rpm and (partially) recreate spec/pkg control structures */
    if ((rc = readRPM(fni, &spec, &lead, &sigs, csa)) != 0)
	return rc;

    /* Inject new strings into header tags */
    if ((rc = headerInject(spec->packages->header, poTags, mlp)) != 0)
	return rc;

    /* Rewrite the rpm */
    if (lead.type == RPMLEAD_SOURCE) {
	return writeRPM(spec->packages->header, fno, (int)lead.type,
		csa, spec->passPhrase, &(spec->cookie));
    } else {
	return writeRPM(spec->packages->header, fno, (int)lead.type,
		csa, spec->passPhrase, NULL);
    }
}

/* ================================================================== */

#define	STDINFN	"<stdin>"

#define	RPMGETTEXT	"rpmgettext"
static int
rpmgettext(int fd, const char *file, FILE *ofp)
{
	char fni[BUFSIZ], fno[BUFSIZ];
	const char *fn;

	DPRINTF(99, ("rpmgettext(%d,\"%s\",%p)\n", fd, file, ofp));

	if (file == NULL)
	    file = STDINFN;

	if (!strcmp(file, STDINFN))
	    return gettextfile(fd, file, ofp, poTags);

	fni[0] = '\0';
	if (inputdir && *file != '/') {
	    strcpy(fni, inputdir);
	    strcat(fni, "/");
	}
	strcat(fni, file);

	if (gentran) {
	    char *op;
	    fno[0] = '\0';
	    fn = file;
	    if (outputdir) {
		strcpy(fno, outputdir);
		strcat(fno, "/");
		fn = basename(file);
	    }
	    strcat(fno, fn);

	    if ((op = strrchr(fno, '-')) != NULL &&
		(op = strchr(op, '.')) != NULL)
		    strcpy(op, ".tran");

	    if ((ofp = fopen(fno, "w")) == NULL) {
		fprintf(stderr, _("Can't open %s\n"), fno);
		return 4;
	    }
	}

	if ((fd = open(fni, O_RDONLY, 0644)) < 0) {
	    fprintf(stderr, _("rpmgettext: open %s: %s\n"), fni, strerror(errno));
	    return 2;
	}

	if (gettextfile(fd, fni, ofp, poTags)) {
	    return 3;
	}

	if (ofp != stdout)
	    fclose(ofp);
	if (fd != 0)
	    close(fd);

	return 0;
}

static char *archs[] = {
	"noarch",
	"i386",
	"alpha",
	"sparc",
	NULL
};

#define	RPMPUTTEXT	"rpmputtext"
static int
rpmputtext(int fd, const char *file, FILE *ofp)
{
	string_list_ty *flp;
	message_list_ty *mlp;
	char fn[BUFSIZ], fni[BUFSIZ], fno[BUFSIZ];
	int j, rc;

	DPRINTF(99, ("rpmputtext(%d,\"%s\",%p)\n", fd, file, ofp));

	if ((rc = parsepofile(file, &mlp, &flp)) != 0)
		return rc;

	for (j = 0; j < flp->nitems && rc == 0; j++) {
	    char *f, *fe;

	    f = fn;
	    strcpy(f, flp->item[j]);

	    /* Find the text after the name-version-release */
	    if ((fe = strrchr(f, '-')) == NULL ||
		(fe = strchr(fe, '.')) == NULL) {
		fprintf(stderr, _("skipping malformed xref \"%s\"\n"), fn);
		continue;
	    }
	    fe++;	/* skip . */

	    if ( !strcmp(fe, "src.rpm") ) {
	    } else {
		char **a, *arch;

		for (a = archs; (arch = *a) != NULL && rc == 0; a++) {

		/* Append ".arch.rpm" */
		    *fe = '\0';
		    strcat(fe, arch);
		    strcat(fe, ".rpm");

		/* Build input "inputdir/arch/fn" */
		    fni[0] = '\0';
		    if (inputdir) {
			strcat(fni, inputdir);
			strcat(fni, "/");
			strcat(fni, arch);
			strcat(fni, "/");
		    }
		    strcat(fni, f);
	
		/* Build output "outputdir/arch/fn" */
		    fno[0] = '\0';
		    if (outputdir) {
			strcat(fno, outputdir);
			strcat(fno, "/");
			strcat(fno, arch);
			strcat(fno, "/");
		    }
		    strcat(fno, f);

		/* XXX skip over noarch/exclusivearch */
		    if (!access(fni, R_OK))
			rc = rewriteBinaryRPM(fni, fno, mlp);
		}
	    }
	}

	if (mlp)
		message_list_free(mlp);

	return rc;
}

#define	RPMCHKTEXT	"rpmchktext"
static int
rpmchktext(int fd, const char *file, FILE *ofp)
{
	DPRINTF(99, ("rpmchktext(%d,\"%s\",%p)\n", fd, file, ofp));
	return parsepofile(file, NULL, NULL);
}

int
main(int argc, char **argv)
{
    int rc = 0;
    int c;
    extern char *optarg;
    extern int optind;
    int errflg = 0;

    progname = basename(argv[0]);

    while((c = getopt(argc, argv, "deEl:I:O:Tv")) != EOF)
    switch (c) {
    case 'd':
	debug++;
	break;
    case 'e':
	message_print_style_escape(0);
	break;
    case 'E':
	message_print_style_escape(1);
	break;
    case 'l':
	onlylang = optarg;
	break;
    case 'I':
	inputdir = optarg;
	break;
    case 'O':
	outputdir = optarg;
	break;
    case 'T':
	gentran++;
    case 'v':
	verbose++;
	break;
    case '?':
    default:
	errflg++;
	break;
    }

    if (errflg) {
	exit(1);
    }

    /* XXX I don't want to read rpmrc yet */
    rpmSetVar(RPMVAR_TMPPATH, "/tmp");

    if (!strcmp(progname, RPMGETTEXT)) {
	if (optind == argc) {
	    rc = rpmgettext(0, STDINFN, stdout);
	} else {
	    for ( ; optind < argc; optind++ ) {
		if ((rc = rpmgettext(0, argv[optind], stdout)) != 0)
		    break;
	    }
	}
    } else if (!strcmp(progname, RPMPUTTEXT)) {
	if (optind == argc) {
	    rc = rpmputtext(0, STDINFN, stdout);
	} else {
	    for ( ; optind < argc; optind++ ) {
		if ((rc = rpmputtext(0, argv[optind], stdout)) != 0)
		    break;
	    }
	}
    } else if (!strcmp(progname, RPMCHKTEXT)) {
	if (optind == argc) {
	    rc = rpmchktext(0, STDINFN, stdout);
	} else {
	    for ( ; optind < argc; optind++ ) {
		if ((rc = rpmchktext(0, argv[optind], stdout)) != 0)
		    break;
	    }
	}
    } else {
	rc = 1;
    } 

    return rc;
}

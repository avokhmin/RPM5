/** \ingroup header
 * \file lib/formats.c
 */

#include "system.h"

#include "rpmio_internal.h"
#include <rpmlib.h>
#include <rpmmacro.h>	/* XXX for %_i18ndomains */

#include <rpmds.h>
#include <rpmfi.h>

#include "legacy.h"
#include "manifest.h"
#include "argv.h"
#include "misc.h"
#include "fs.h"

#include "debug.h"

/*@access pgpDig @*/
/*@access pgpDigParams @*/

/**
 * Identify type of trigger.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix	(unused)
 * @param padding	(unused)
 * @param element	(unused)
 * @return		formatted string
 */
static /*@only@*/ char * triggertypeFormat(int_32 type, const void * data,
		/*@unused@*/ char * formatPrefix, /*@unused@*/ int padding,
		/*@unused@*/ int element)
	/*@requires maxRead(data) >= 0 @*/
{
    const int_32 * item = data;
    char * val;

    if (type != RPM_INT32_TYPE)
	val = xstrdup(_("(invalid type)"));
    else if (*item & RPMSENSE_TRIGGERPREIN)
	val = xstrdup("prein");
    else if (*item & RPMSENSE_TRIGGERIN)
	val = xstrdup("in");
    else if (*item & RPMSENSE_TRIGGERUN)
	val = xstrdup("un");
    else if (*item & RPMSENSE_TRIGGERPOSTUN)
	val = xstrdup("postun");
    else
	val = xstrdup("");
    return val;
}

/**
 * Format file permissions for display.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix
 * @param padding
 * @param element	(unused)
 * @return		formatted string
 */
static /*@only@*/ char * permsFormat(int_32 type, const void * data,
		char * formatPrefix, int padding, /*@unused@*/ int element)
	/*@modifies formatPrefix @*/
	/*@requires maxRead(data) >= 0 @*/
{
    char * val;
    char * buf;

    if (type != RPM_INT32_TYPE) {
	val = xstrdup(_("(invalid type)"));
    } else {
	val = xmalloc(15 + padding);
/*@-boundswrite@*/
	strcat(formatPrefix, "s");
/*@=boundswrite@*/
	buf = rpmPermsString(*((int_32 *) data));
	/*@-formatconst@*/
	sprintf(val, formatPrefix, buf);
	/*@=formatconst@*/
	buf = _free(buf);
    }

    return val;
}

/**
 * Format file flags for display.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix
 * @param padding
 * @param element	(unused)
 * @return		formatted string
 */
static /*@only@*/ char * fflagsFormat(int_32 type, const void * data,
		char * formatPrefix, int padding, /*@unused@*/ int element)
	/*@modifies formatPrefix @*/
	/*@requires maxRead(data) >= 0 @*/
{
    char * val;
    char buf[15];
    int anint = *((int_32 *) data);

    if (type != RPM_INT32_TYPE) {
	val = xstrdup(_("(invalid type)"));
    } else {
	buf[0] = '\0';
/*@-boundswrite@*/
	if (anint & RPMFILE_DOC)
	    strcat(buf, "d");
	if (anint & RPMFILE_CONFIG)
	    strcat(buf, "c");
	if (anint & RPMFILE_SPECFILE)
	    strcat(buf, "s");
	if (anint & RPMFILE_MISSINGOK)
	    strcat(buf, "m");
	if (anint & RPMFILE_NOREPLACE)
	    strcat(buf, "n");
	if (anint & RPMFILE_GHOST)
	    strcat(buf, "g");
	if (anint & RPMFILE_LICENSE)
	    strcat(buf, "l");
	if (anint & RPMFILE_README)
	    strcat(buf, "r");
/*@=boundswrite@*/

	val = xmalloc(5 + padding);
/*@-boundswrite@*/
	strcat(formatPrefix, "s");
/*@=boundswrite@*/
	/*@-formatconst@*/
	sprintf(val, formatPrefix, buf);
	/*@=formatconst@*/
    }

    return val;
}

/**
 * Wrap a pubkey in ascii armor for display.
 * @todo Permit selectable display formats (i.e. binary).
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix	(unused)
 * @param padding	(unused)
 * @param element	no. bytes of binary data
 * @return		formatted string
 */
static /*@only@*/ char * armorFormat(int_32 type, const void * data,
		/*@unused@*/ char * formatPrefix, /*@unused@*/ int padding,
		int element)
	/*@*/
{
    const char * enc;
    const unsigned char * s;
    size_t ns;
    int atype;

    switch (type) {
    case RPM_OPENPGP_TYPE:
    case RPM_ASN1_TYPE:		/* XXX WRONG */
    case RPM_BIN_TYPE:
	s = data;
	/* XXX HACK ALERT: element field abused as no. bytes of binary data. */
	ns = element;
	atype = PGPARMOR_SIGNATURE;	/* XXX check pkt for signature */
	break;
    case RPM_STRING_TYPE:
    case RPM_STRING_ARRAY_TYPE:
	enc = data;
	if (b64decode(enc, (void **)&s, &ns))
	    return xstrdup(_("(not base64)"));
	atype = PGPARMOR_PUBKEY;	/* XXX check pkt for pubkey */
	break;
    case RPM_NULL_TYPE:
    case RPM_CHAR_TYPE:
    case RPM_INT8_TYPE:
    case RPM_INT16_TYPE:
    case RPM_INT32_TYPE:
    case RPM_INT64_TYPE:
    case RPM_I18NSTRING_TYPE:
    default:
	return xstrdup(_("(invalid type)"));
	/*@notreached@*/ break;
    }

    /* XXX this doesn't use padding directly, assumes enough slop in retval. */
    return pgpArmorWrap(atype, s, ns);
}

/**
 * Encode binary data in base64 for display.
 * @todo Permit selectable display formats (i.e. binary).
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix	(unused)
 * @param padding
 * @param element
 * @return		formatted string
 */
static /*@only@*/ char * base64Format(int_32 type, const void * data,
		/*@unused@*/ char * formatPrefix, int padding, int element)
	/*@*/
{
    char * val;

    if (!(type == RPM_BIN_TYPE || type == RPM_ASN1_TYPE || type == RPM_OPENPGP_TYPE)) {
	val = xstrdup(_("(not a blob)"));
    } else {
	const char * enc;
	char * t;
	int lc;
	/* XXX HACK ALERT: element field abused as no. bytes of binary data. */
	size_t ns = element;
	size_t nt = ((ns + 2) / 3) * 4;

/*@-boundswrite@*/
	/*@-globs@*/
	/* Add additional bytes necessary for eol string(s). */
	if (b64encode_chars_per_line > 0 && b64encode_eolstr != NULL) {
	    lc = (nt + b64encode_chars_per_line - 1) / b64encode_chars_per_line;
	if (((nt + b64encode_chars_per_line - 1) % b64encode_chars_per_line) != 0)
	    ++lc;
	    nt += lc * strlen(b64encode_eolstr);
	}
	/*@=globs@*/

	val = t = xcalloc(1, nt + padding + 1);
	*t = '\0';

    /* XXX b64encode accesses uninitialized memory. */
    { 	unsigned char * _data = xcalloc(1, ns+1);
	memcpy(_data, data, ns);
	if ((enc = b64encode(_data, ns)) != NULL) {
	    t = stpcpy(t, enc);
	    enc = _free(enc);
	}
	_data = _free(_data);
    }
/*@=boundswrite@*/
    }

    return val;
}

/**
 * Return length of string represented with xml characters substituted.
 * @param s		string
 * @return		length of xml string
 */
static size_t xmlstrlen(const char * s)
	/*@*/
{
    size_t len = 0;
    int c;

/*@-boundsread@*/
    while ((c = *s++) != '\0')
/*@=boundsread@*/
    {
	switch (c) {
	case '<':
	case '>':	len += sizeof("&lt;") - 1;	/*@switchbreak@*/ break;
	case '&':	len += sizeof("&amp;") - 1;	/*@switchbreak@*/ break;
	default:	len += 1;			/*@switchbreak@*/ break;
	}
    }
    return len;
}

/**
 * Copy source string to target, substituting for  xml characters.
 * @param t		target xml string
 * @param s		source string
 * @return		target xml string
 */
static char * xmlstrcpy(/*@returned@*/ char * t, const char * s)
	/*@modifies t @*/
{
    char * te = t;
    int c;

/*@-bounds@*/
    while ((c = *s++) != '\0') {
	switch (c) {
	case '<':	te = stpcpy(te, "&lt;");	/*@switchbreak@*/ break;
	case '>':	te = stpcpy(te, "&gt;");	/*@switchbreak@*/ break;
	case '&':	te = stpcpy(te, "&amp;");	/*@switchbreak@*/ break;
	default:	*te++ = c;			/*@switchbreak@*/ break;
	}
    }
    *te = '\0';
/*@=bounds@*/
    return t;
}

/**
 * Wrap tag data in simple header xml markup.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix
 * @param padding
 * @param element	(unused)
 * @return		formatted string
 */
/*@-bounds@*/
static /*@only@*/ char * xmlFormat(int_32 type, const void * data,
		char * formatPrefix, int padding,
		/*@unused@*/ int element)
	/*@modifies formatPrefix @*/
{
    const char * xtag = NULL;
    size_t nb;
    char * val;
    const char * s = NULL;
    char * t, * te;
    unsigned long long anint = 0;
    int freeit = 0;
    int xx;

/*@-branchstate@*/
    switch (type) {
    case RPM_I18NSTRING_TYPE:
    case RPM_STRING_TYPE:
	s = data;
	xtag = "string";
	/* XXX Force utf8 strings. */
	s = xstrdup(s);
	s = xstrtolocale(s);
	freeit = 1;
	break;
    case RPM_OPENPGP_TYPE:
    case RPM_ASN1_TYPE:
    case RPM_BIN_TYPE:
    {	int cpl = b64encode_chars_per_line;
/*@-mods@*/
	b64encode_chars_per_line = 0;
/*@=mods@*/
/*@-formatconst@*/
	s = base64Format(type, data, formatPrefix, padding, element);
/*@=formatconst@*/
/*@-mods@*/
	b64encode_chars_per_line = cpl;
/*@=mods@*/
	xtag = "base64";
	freeit = 1;
    }	break;
    case RPM_CHAR_TYPE:
    case RPM_INT8_TYPE:
	anint = *((uint_8 *) data);
	break;
    case RPM_INT16_TYPE:
	anint = *((uint_16 *) data);
	break;
    case RPM_INT32_TYPE:
	anint = *((uint_32 *) data);
	break;
    case RPM_INT64_TYPE:
	anint = *((uint_64 *) data);
	break;
    case RPM_NULL_TYPE:
    case RPM_STRING_ARRAY_TYPE:
    default:
	return xstrdup(_("(invalid xml type)"));
	/*@notreached@*/ break;
    }
/*@=branchstate@*/

/*@-branchstate@*/
    if (s == NULL) {
	int tlen = 64;
	t = memset(alloca(tlen+1), 0, tlen+1);
/*@-duplicatequals@*/
	if (anint != 0)
	    xx = snprintf(t, tlen, "%llu", anint);
/*@=duplicatequals@*/
	s = t;
	xtag = "integer";
    }
/*@=branchstate@*/

    nb = xmlstrlen(s);
    if (nb == 0) {
	nb += strlen(xtag) + sizeof("\t</>");
	te = t = alloca(nb);
	te = stpcpy( stpcpy( stpcpy(te, "\t<"), xtag), "/>");
    } else {
	nb += 2 * strlen(xtag) + sizeof("\t<></>");
	te = t = alloca(nb);
	te = stpcpy( stpcpy( stpcpy(te, "\t<"), xtag), ">");
	te = xmlstrcpy(te, s);
	te += strlen(te);
	te = stpcpy( stpcpy( stpcpy(te, "</"), xtag), ">");
    }

/*@-branchstate@*/
    if (freeit)
	s = _free(s);
/*@=branchstate@*/

    nb += padding;
    val = xmalloc(nb+1);
/*@-boundswrite@*/
    strcat(formatPrefix, "s");
/*@=boundswrite@*/
/*@-formatconst@*/
    xx = snprintf(val, nb, formatPrefix, t);
/*@=formatconst@*/
    val[nb] = '\0';

    return val;
}
/*@=bounds@*/

/**
 * Return length of string represented with yaml indentation.
 * @param s		string
 * @param lvl		indentation level
 * @return		length of yaml string
 */
static size_t yamlstrlen(const char * s, int lvl)
	/*@*/
{
    size_t len = 0;
    int indent = (lvl > 0);
    int c;

/*@-boundsread@*/
    while ((c = *s++) != '\0')
/*@=boundsread@*/
    {
	if (indent) {
	    len += 2 * lvl;
	    indent = 0;
	}
	if (c == '\n')
	    indent = (lvl > 0);
	len++;
    }
    return len;
}

/**
 * Copy source string to target, indenting for yaml.
 * @param t		target yaml string
 * @param s		source string
 * @param lvl		indentation level
 * @return		target yaml string
 */
static char * yamlstrcpy(/*@out@*/ /*@returned@*/ char * t, const char * s, int lvl)
	/*@modifies t @*/
{
    char * te = t;
    int indent = (lvl > 0);
    int c;

/*@-bounds@*/
    while ((c = *s++) != '\0') {
	if (indent) {
	    int i;
	    for (i = 0; i < lvl; i++) {
		*te++ = ' ';
		*te++ = ' ';
	    }
	    indent = 0;
	}
	if (c == '\n')
	    indent = (lvl > 0);
	*te++ = c;
    }
    *te = '\0';
/*@=bounds@*/
    return t;
}

/**
 * Wrap tag data in simple header yaml markup.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix
 * @param padding
 * @param element	element index (or -1 for non-array).
 * @return		formatted string
 */
/*@-bounds@*/
static /*@only@*/ char * yamlFormat(int_32 type, const void * data,
		char * formatPrefix, int padding,
		int element)
	/*@modifies formatPrefix @*/
{
    const char * xtag = NULL;
    const char * ytag = NULL;
    size_t nb;
    char * val;
    const char * s = NULL;
    char * t, * te;
    unsigned long long anint = 0;
    int freeit = 0;
    int lvl = 0;
    int xx;
    int c;

/*@-branchstate@*/
    switch (type) {
    case RPM_I18NSTRING_TYPE:
    case RPM_STRING_TYPE:
	xx = 0;
	s = data;
	if (strchr("[", s[0]))	/* leading [ */
	    xx = 1;
	if (xx == 0)
	while ((c = *s++) != '\0') {
	    switch (c) {
	    default:
		continue;
	    case '\n':	/* multiline */
		xx = 1;
		/*@switchbreak@*/ break;
	    case '-':	/* leading "- \"" */
	    case ':':	/* embedded ": " or ":" at EOL */
		if (s[0] != ' ' && s[0] != '\0' && s[1] != '"')
		    continue;
		xx = 1;
		/*@switchbreak@*/ break;
	    }
	    /*@loopbreak@*/ break;
	}
	if (xx) {
	    if (element >= 0) {
		xtag = "- |-\n";
		lvl = 3;
	    } else {
		xtag = "|-\n";
		lvl = 2;
	    }
	} else {
	    xtag = (element >= 0 ? "- " : NULL);
	}

	/* XXX Force utf8 strings. */
	s = xstrdup(data);
	s = xstrtolocale(s);
	freeit = 1;
	break;
    case RPM_OPENPGP_TYPE:
    case RPM_ASN1_TYPE:
    case RPM_BIN_TYPE:
    {	int cpl = b64encode_chars_per_line;
/*@-mods@*/
	b64encode_chars_per_line = 0;
/*@=mods@*/
/*@-formatconst@*/
	s = base64Format(type, data, formatPrefix, padding, element);
	element = -element;	/* XXX skip "    " indent. */
/*@=formatconst@*/
/*@-mods@*/
	b64encode_chars_per_line = cpl;
/*@=mods@*/
	xtag = "!!binary ";
	freeit = 1;
    }	break;
    case RPM_CHAR_TYPE:
    case RPM_INT8_TYPE:
	anint = *((uint_8 *) data);
	break;
    case RPM_INT16_TYPE:
	anint = *((uint_16 *) data);
	break;
    case RPM_INT32_TYPE:
	anint = *((uint_32 *) data);
	break;
    case RPM_INT64_TYPE:
	anint = *((uint_64 *) data);
	break;
    case RPM_NULL_TYPE:
    case RPM_STRING_ARRAY_TYPE:
    default:
	return xstrdup(_("(invalid yaml type)"));
	/*@notreached@*/ break;
    }
/*@=branchstate@*/

/*@-branchstate@*/
    if (s == NULL) {
	int tlen = 64;
	t = memset(alloca(tlen+1), 0, tlen+1);
/*@-duplicatequals@*/
	xx = snprintf(t, tlen, "%llu", anint);
/*@=duplicatequals@*/
	s = t;
	xtag = (element >= 0 ? "- " : NULL);
    }
/*@=branchstate@*/

    nb = yamlstrlen(s, lvl);
    if (nb == 0) {
	if (element >= 0)
	    nb += sizeof("    ") - 1;
	nb += sizeof("- ~") - 1;
	nb++;
	te = t = alloca(nb);
	if (element >= 0)
	    te = stpcpy(te, "    ");
	te = stpcpy(te, "- ~");
    } else {
	if (element >= 0)
	    nb += sizeof("    ") - 1;
	if (xtag)
	    nb += strlen(xtag);
	if (ytag)
	    nb += strlen(ytag);
	nb++;
	te = t = alloca(nb);
	if (element >= 0)
	    te = stpcpy(te, "    ");
	if (xtag)
	    te = stpcpy(te, xtag);
	te = yamlstrcpy(te, s, lvl);
	te += strlen(te);
	if (ytag)
	    te = stpcpy(te, ytag);
    }

    /* XXX s was malloc'd */
/*@-branchstate@*/
    if (freeit)
	s = _free(s);
/*@=branchstate@*/

    nb += padding;
    val = xmalloc(nb+1);
/*@-boundswrite@*/
    strcat(formatPrefix, "s");
/*@=boundswrite@*/
/*@-formatconst@*/
    xx = snprintf(val, nb, formatPrefix, t);
/*@=formatconst@*/
    val[nb] = '\0';

    return val;
}
/*@=bounds@*/

/**
 * Display signature fingerprint and time.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix	(unused)
 * @param padding
 * @param element	(unused)
 * @return		formatted string
 */
static /*@only@*/ char * pgpsigFormat(int_32 type, const void * data,
		/*@unused@*/ char * formatPrefix, /*@unused@*/ int padding,
		/*@unused@*/ int element)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    char * val, * t;

    if (!(type == RPM_BIN_TYPE || type == RPM_ASN1_TYPE || type == RPM_OPENPGP_TYPE)) {
	val = xstrdup(_("(not a blob)"));
    } else {
	unsigned char * pkt = (byte *) data;
	unsigned int pktlen = 0;
/*@-boundsread@*/
	unsigned int v = *pkt;
/*@=boundsread@*/
	pgpTag tag = 0;
	unsigned int plen;
	unsigned int hlen = 0;

	if (v & 0x80) {
	    if (v & 0x40) {
		tag = (v & 0x3f);
		plen = pgpLen(pkt+1, &hlen);
	    } else {
		tag = (v >> 2) & 0xf;
		plen = (1 << (v & 0x3));
		hlen = pgpGrab(pkt+1, plen);
	    }
	
	    pktlen = 1 + plen + hlen;
	}

	if (pktlen == 0 || tag != PGPTAG_SIGNATURE) {
	    val = xstrdup(_("(not an OpenPGP signature)"));
	} else {
	    pgpDig dig = pgpNewDig();
	    pgpDigParams sigp = &dig->signature;
	    size_t nb = 0;
	    const char *tempstr;

	    (void) pgpPrtPkts(pkt, pktlen, dig, 0);

	    val = NULL;
	again:
	    nb += 100;
	    val = t = xrealloc(val, nb + 1);

/*@-boundswrite@*/
	    switch (sigp->pubkey_algo) {
	    case PGPPUBKEYALGO_DSA:
		t = stpcpy(t, "DSA");
		break;
	    case PGPPUBKEYALGO_RSA:
		t = stpcpy(t, "RSA");
		break;
	    default:
		(void) snprintf(t, nb - (t - val), "%d", sigp->pubkey_algo);
		t += strlen(t);
		break;
	    }
	    if (t + 5 >= val + nb)
		goto again;
	    *t++ = '/';
	    switch (sigp->hash_algo) {
	    case PGPHASHALGO_MD5:
		t = stpcpy(t, "MD5");
		break;
	    case PGPHASHALGO_SHA1:
		t = stpcpy(t, "SHA1");
		break;
	    default:
		(void) snprintf(t, nb - (t - val), "%d", sigp->hash_algo);
		t += strlen(t);
		break;
	    }
	    if (t + strlen (", ") + 1 >= val + nb)
		goto again;

	    t = stpcpy(t, ", ");

	    /* this is important if sizeof(int_32) ! sizeof(time_t) */
	    {	time_t dateint = pgpGrab(sigp->time, sizeof(sigp->time));
		struct tm * tstruct = localtime(&dateint);
		if (tstruct)
 		    (void) strftime(t, (nb - (t - val)), "%c", tstruct);
	    }
	    t += strlen(t);
	    if (t + strlen (", Key ID ") + 1 >= val + nb)
		goto again;
	    t = stpcpy(t, ", Key ID ");
	    tempstr = pgpHexStr(sigp->signid, sizeof(sigp->signid));
	    if (t + strlen (tempstr) > val + nb)
		goto again;
	    t = stpcpy(t, tempstr);
/*@=boundswrite@*/

	    dig = pgpFreeDig(dig);
	}
    }

    return val;
}

/**
 * Format dependency flags for display.
 * @param type		tag type
 * @param data		tag value
 * @param formatPrefix
 * @param padding
 * @param element	(unused)
 * @return		formatted string
 */
static /*@only@*/ char * depflagsFormat(int_32 type, const void * data,
		char * formatPrefix, int padding, /*@unused@*/ int element)
	/*@modifies formatPrefix @*/
	/*@requires maxRead(data) >= 0 @*/
{
    char * val;

    if (type != RPM_INT32_TYPE) {
	val = xstrdup(_("(invalid type)"));
    } else {
	int anint = *((int_32 *) data);
	char *t, *buf;

	t = buf = alloca(32);
	*t = '\0';

/*@-boundswrite@*/
#ifdef	NOTYET	/* XXX appending markers breaks :depflags format. */
	if (anint & RPMSENSE_SCRIPT_PRE)
	    t = stpcpy(t, "(pre)");
	if (anint & RPMSENSE_SCRIPT_POST)
	    t = stpcpy(t, "(post)");
	if (anint & RPMSENSE_SCRIPT_PREUN)
	    t = stpcpy(t, "(preun)");
	if (anint & RPMSENSE_SCRIPT_POSTUN)
	    t = stpcpy(t, "(postun)");
#endif
	if (anint & RPMSENSE_SENSEMASK)
	    *t++ = ' ';
	if (anint & RPMSENSE_LESS)
	    *t++ = '<';
	if (anint & RPMSENSE_GREATER)
	    *t++ = '>';
	if (anint & RPMSENSE_EQUAL)
	    *t++ = '=';
	if (anint & RPMSENSE_SENSEMASK)
	    *t++ = ' ';
	*t = '\0';
/*@=boundswrite@*/

	val = xmalloc(5 + padding);
/*@-boundswrite@*/
	strcat(formatPrefix, "s");
/*@=boundswrite@*/
	/*@-formatconst@*/
	sprintf(val, formatPrefix, buf);
	/*@=formatconst@*/
    }

    return val;
}

/**
 * Retrieve mounted file system paths.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int fsnamesTag( /*@unused@*/ Header h, /*@out@*/ int_32 * type,
		/*@out@*/ void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals fileSystem, internalState @*/
	/*@modifies *type, *data, *count, *freeData,
		fileSystem, internalState @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    const char ** list;

/*@-boundswrite@*/
    if (rpmGetFilesystemList(&list, count))
	return 1;
/*@=boundswrite@*/

    if (type) *type = RPM_STRING_ARRAY_TYPE;
    if (data) *((const char ***) data) = list;
    if (freeData) *freeData = 0;

    return 0;
}

/**
 * Retrieve install prefixes.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int instprefixTag(Header h, /*@null@*/ /*@out@*/ rpmTagType * type,
		/*@null@*/ /*@out@*/ const void ** data,
		/*@null@*/ /*@out@*/ int_32 * count,
		/*@null@*/ /*@out@*/ int * freeData)
	/*@modifies *type, *data, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType ipt;
    char ** array;

    if (hge(h, RPMTAG_INSTALLPREFIX, type, (void **)data, count)) {
	if (freeData) *freeData = 0;
	return 0;
    } else if (hge(h, RPMTAG_INSTPREFIXES, &ipt, (void **) &array, count)) {
	if (type) *type = RPM_STRING_TYPE;
/*@-boundsread@*/
	if (data) *data = xstrdup(array[0]);
/*@=boundsread@*/
	if (freeData) *freeData = 1;
	array = hfd(array, ipt);
	return 0;
    }

    return 1;
}

/**
 * Retrieve mounted file system space.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int fssizesTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies *type, *data, *count, *freeData, rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char ** filenames;
    int_32 * filesizes;
    uint_64 * usages;
    int numFiles;

    if (!hge(h, RPMTAG_FILESIZES, NULL, (void **) &filesizes, &numFiles)) {
	filesizes = NULL;
	numFiles = 0;
	filenames = NULL;
    } else {
	rpmfiBuildFNames(h, RPMTAG_BASENAMES, &filenames, &numFiles);
    }

/*@-boundswrite@*/
    if (rpmGetFilesystemList(NULL, count))
	return 1;
/*@=boundswrite@*/

    *type = RPM_INT64_TYPE;
    *freeData = 1;

    if (filenames == NULL) {
	usages = xcalloc((*count), sizeof(*usages));
	*data = usages;

	return 0;
    }

/*@-boundswrite@*/
    if (rpmGetFilesystemUsage(filenames, filesizes, numFiles, &usages, 0))	
	return 1;
/*@=boundswrite@*/

    *data = usages;

    filenames = _free(filenames);

    return 0;
}

/**
 * Retrieve trigger info.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int triggercondsTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType tnt, tvt, tst;
    int_32 * indices, * flags;
    char ** names, ** versions;
    int numNames, numScripts;
    char ** conds, ** s;
    char * item, * flagsStr;
    char * chptr;
    int i, j, xx;
    char buf[5];

    if (!hge(h, RPMTAG_TRIGGERNAME, &tnt, (void **) &names, &numNames)) {
	*freeData = 0;
	return 0;
    }

    xx = hge(h, RPMTAG_TRIGGERINDEX, NULL, (void **) &indices, NULL);
    xx = hge(h, RPMTAG_TRIGGERFLAGS, NULL, (void **) &flags, NULL);
    xx = hge(h, RPMTAG_TRIGGERVERSION, &tvt, (void **) &versions, NULL);
    xx = hge(h, RPMTAG_TRIGGERSCRIPTS, &tst, (void **) &s, &numScripts);
    s = hfd(s, tst);

    *freeData = 1;
    *data = conds = xmalloc(sizeof(*conds) * numScripts);
    *count = numScripts;
    *type = RPM_STRING_ARRAY_TYPE;
/*@-bounds@*/
    for (i = 0; i < numScripts; i++) {
	chptr = xstrdup("");

	for (j = 0; j < numNames; j++) {
	    if (indices[j] != i)
		/*@innercontinue@*/ continue;

	    item = xmalloc(strlen(names[j]) + strlen(versions[j]) + 20);
	    if (flags[j] & RPMSENSE_SENSEMASK) {
		buf[0] = '%', buf[1] = '\0';
		flagsStr = depflagsFormat(RPM_INT32_TYPE, flags, buf, 0, j);
		sprintf(item, "%s %s %s", names[j], flagsStr, versions[j]);
		flagsStr = _free(flagsStr);
	    } else {
		strcpy(item, names[j]);
	    }

	    chptr = xrealloc(chptr, strlen(chptr) + strlen(item) + 5);
	    if (*chptr != '\0') strcat(chptr, ", ");
	    strcat(chptr, item);
	    item = _free(item);
	}

	conds[i] = chptr;
    }
/*@=bounds@*/

    names = hfd(names, tnt);
    versions = hfd(versions, tvt);

    return 0;
}

/**
 * Retrieve trigger type info.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int triggertypeTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType tst;
    int_32 * indices, * flags;
    const char ** conds;
    const char ** s;
    int i, j, xx;
    int numScripts, numNames;

    if (!hge(h, RPMTAG_TRIGGERINDEX, NULL, (void **) &indices, &numNames)) {
	*freeData = 0;
	return 1;
    }

    xx = hge(h, RPMTAG_TRIGGERFLAGS, NULL, (void **) &flags, NULL);
    xx = hge(h, RPMTAG_TRIGGERSCRIPTS, &tst, (void **) &s, &numScripts);
    s = hfd(s, tst);

    *freeData = 1;
    *data = conds = xmalloc(sizeof(*conds) * numScripts);
    *count = numScripts;
    *type = RPM_STRING_ARRAY_TYPE;
/*@-bounds@*/
    for (i = 0; i < numScripts; i++) {
	for (j = 0; j < numNames; j++) {
	    if (indices[j] != i)
		/*@innercontinue@*/ continue;

	    if (flags[j] & RPMSENSE_TRIGGERPREIN)
		conds[i] = xstrdup("prein");
	    else if (flags[j] & RPMSENSE_TRIGGERIN)
		conds[i] = xstrdup("in");
	    else if (flags[j] & RPMSENSE_TRIGGERUN)
		conds[i] = xstrdup("un");
	    else if (flags[j] & RPMSENSE_TRIGGERPOSTUN)
		conds[i] = xstrdup("postun");
	    else
		conds[i] = xstrdup("");
	    /*@innerbreak@*/ break;
	}
    }
/*@=bounds@*/

    return 0;
}

/**
 * Retrieve file paths.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int filenamesTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFNames(h, RPMTAG_BASENAMES, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file classes.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int fileclassTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFClasses(h, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file contexts from header.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int filecontextsTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFContexts(h, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file contexts from file system.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int fscontextsTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFSContexts(h, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file contexts from policy RE's.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int recontextsTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildREContexts(h, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file provides.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int fileprovideTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFDeps(h, RPMTAG_PROVIDENAME, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve file requires.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int filerequireTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    *type = RPM_STRING_ARRAY_TYPE;
    rpmfiBuildFDeps(h, RPMTAG_REQUIRENAME, (const char ***) data, count);
    *freeData = 1;
    return 0;
}

/**
 * Retrieve Requires(missingok): array for Suggests: or Enhances:.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int missingokTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *type, *data, *count, *freeData,
		rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    rpmds ds = rpmdsNew(h, RPMTAG_REQUIRENAME, 0);
    ARGV_t av = NULL;
    ARGV_t argv;
    int argc = 0;
    char * t;
    size_t nb = 0;
    int i;

assert(ds != NULL);
    /* Collect dependencies marked as hints. */
    ds = rpmdsInit(ds);
    if (ds != NULL)
    while (rpmdsNext(ds) >= 0) {
	int Flags = rpmdsFlags(ds);
	const char * DNEVR;
	if (!(Flags & RPMSENSE_MISSINGOK))
	    continue;
	DNEVR = rpmdsDNEVR(ds);
	if (DNEVR == NULL)
	    continue;
	nb += sizeof(*argv) + strlen(DNEVR+2) + 1;
	(void) argvAdd(&av, DNEVR+2);
	argc++;
    }
    nb += sizeof(*argv);	/* final argv NULL */

    /* Create contiguous header string array. */
    argv = (ARGV_t) xcalloc(nb, 1);
    t = (char *)(argv + argc);
    for (i = 0; i < argc; i++) {
	argv[i] = t;
	t = stpcpy(t, av[i]);
	*t++ = '\0';
    }
    av = argvFree(av);
    ds = rpmdsFree(ds);

    /* XXX perhaps return "(none)" inband if no suggests/enhances <shrug>. */

    *type = RPM_STRING_ARRAY_TYPE;
    *data = argv;
    *count = argc;
    *freeData = 1;
    return 0;
}

/* I18N look aside diversions */

#if defined(ENABLE_NLS)
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
extern int _nl_msg_cat_cntr;	/* XXX GNU gettext voodoo */
/*@=exportlocal =exportheadervar@*/
#endif
/*@observer@*/ /*@unchecked@*/
static const char * language = "LANGUAGE";

/*@observer@*/ /*@unchecked@*/
static const char * _macro_i18ndomains = "%{?_i18ndomains}";

/**
 * Retrieve i18n text.
 * @param h		header
 * @param tag		tag
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int i18nTag(Header h, int_32 tag, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *type, *data, *count, *freeData, rpmGlobalMacroContext @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    char * dstring = rpmExpand(_macro_i18ndomains, NULL);
    int rc;

    *type = RPM_STRING_TYPE;
    *data = NULL;
    *count = 0;
    *freeData = 0;

    if (dstring && *dstring) {
	char *domain, *de;
	const char * langval;
	const char * msgkey;
	const char * msgid;

	{   const char * tn = tagName(tag);
	    const char * n;
	    char * mk;
	    (void) headerNVR(h, &n, NULL, NULL);
	    mk = alloca(strlen(n) + strlen(tn) + sizeof("()"));
	    sprintf(mk, "%s(%s)", n, tn);
	    msgkey = mk;
	}

	/* change to en_US for msgkey -> msgid resolution */
	langval = getenv(language);
	(void) setenv(language, "en_US", 1);
#if defined(ENABLE_NLS)
/*@i@*/	++_nl_msg_cat_cntr;
#endif

	msgid = NULL;
	/*@-branchstate@*/
	for (domain = dstring; domain != NULL; domain = de) {
	    de = strchr(domain, ':');
	    if (de) *de++ = '\0';
	    msgid = /*@-unrecog@*/ dgettext(domain, msgkey) /*@=unrecog@*/;
	    if (msgid != msgkey) break;
	}
	/*@=branchstate@*/

	/* restore previous environment for msgid -> msgstr resolution */
	if (langval)
	    (void) setenv(language, langval, 1);
	else
	    unsetenv(language);
#if defined(ENABLE_NLS)
/*@i@*/	++_nl_msg_cat_cntr;
#endif

	if (domain && msgid) {
	    *data = /*@-unrecog@*/ dgettext(domain, msgid) /*@=unrecog@*/;
	    *data = xstrdup(*data);	/* XXX xstrdup has side effects. */
	    *count = 1;
	    *freeData = 1;
	}
	dstring = _free(dstring);
	if (*data)
	    return 0;
    }

    dstring = _free(dstring);

    rc = hge(h, tag, type, (void **)data, count);

    if (rc && (*data) != NULL) {
	*data = xstrdup(*data);
	*data = xstrtolocale(*data);
	*freeData = 1;
	return 0;
    }

    *freeData = 0;
    *data = NULL;
    *count = 0;
    return 1;
}

/**
 * Retrieve text and convert to locale.
 */
static int localeTag(Header h, int_32 tag, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    rpmTagType t;
    char **d, **d2, *dp;
    int rc, i, l;

    rc = hge(h, tag, &t, (void **)&d, count);
    if (!rc || d == NULL || *count == 0) {
	*freeData = 0;
	*data = NULL;
	*count = 0;
	return 1;
    }
    if (type)
	*type = t;
    if (t == RPM_STRING_TYPE) {
	d = (char **)xstrdup((char *)d);
	d = (char **)xstrtolocale((char *)d);
	*freeData = 1;
    } else if (t == RPM_STRING_ARRAY_TYPE) {
	l = 0;
	for (i = 0; i < *count; i++) {
	    d[i] = xstrdup(d[i]);
	    d[i] = (char *)xstrtolocale(d[i]);
assert(d[i] != NULL);
	    l += strlen(d[i]) + 1;
	}
	d2 = xmalloc(*count * sizeof(*d2) + l);
	dp = (char *)(d2 + *count);
	for (i = 0; i < *count; i++) {
	    d2[i] = dp;
	    strcpy(dp, d[i]);
	    dp += strlen(dp) + 1;
	    d[i] = _free(d[i]);
	}
	d = _free(d);
	d = d2;
	*freeData = 1;
    } else
	*freeData = 0;
    *data = (void **)d;
    return 0;
}

/**
 * Retrieve summary text.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int summaryTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *type, *data, *count, *freeData, rpmGlobalMacroContext @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    return i18nTag(h, RPMTAG_SUMMARY, type, data, count, freeData);
}

/**
 * Retrieve description text.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int descriptionTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *type, *data, *count, *freeData, rpmGlobalMacroContext @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    return i18nTag(h, RPMTAG_DESCRIPTION, type, data, count, freeData);
}

static int changelognameTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    return localeTag(h, RPMTAG_CHANGELOGNAME, type, data, count, freeData);
}

static int changelogtextTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@modifies *type, *data, *count, *freeData @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    return localeTag(h, RPMTAG_CHANGELOGTEXT, type, data, count, freeData);
}

/**
 * Retrieve group text.
 * @param h		header
 * @retval *type	tag type
 * @retval *data	tag value
 * @retval *count	no. of data items
 * @retval *freeData	data-was-malloc'ed indicator
 * @return		0 on success
 */
static int groupTag(Header h, /*@out@*/ rpmTagType * type,
		/*@out@*/ const void ** data, /*@out@*/ int_32 * count,
		/*@out@*/ int * freeData)
	/*@globals rpmGlobalMacroContext, h_errno @*/
	/*@modifies *type, *data, *count, *freeData, rpmGlobalMacroContext @*/
	/*@requires maxSet(type) >= 0 /\ maxSet(data) >= 0
		/\ maxSet(count) >= 0 /\ maxSet(freeData) >= 0 @*/
{
    return i18nTag(h, RPMTAG_GROUP, type, data, count, freeData);
}

/*@-type@*/ /* FIX: cast? */
const struct headerSprintfExtension_s rpmHeaderFormats[] = {
    { HEADER_EXT_TAG, "RPMTAG_CHANGELOGNAME",	{ changelognameTag } },
    { HEADER_EXT_TAG, "RPMTAG_CHANGELOGTEXT",	{ changelogtextTag } },
    { HEADER_EXT_TAG, "RPMTAG_DESCRIPTION",	{ descriptionTag } },
    { HEADER_EXT_TAG, "RPMTAG_ENHANCES",	{ missingokTag } },
    { HEADER_EXT_TAG, "RPMTAG_FILECLASS",	{ fileclassTag } },
    { HEADER_EXT_TAG, "RPMTAG_FILECONTEXTS",	{ filecontextsTag } },
    { HEADER_EXT_TAG, "RPMTAG_FILENAMES",	{ filenamesTag } },
    { HEADER_EXT_TAG, "RPMTAG_FILEPROVIDE",	{ fileprovideTag } },
    { HEADER_EXT_TAG, "RPMTAG_FILEREQUIRE",	{ filerequireTag } },
    { HEADER_EXT_TAG, "RPMTAG_FSCONTEXTS",	{ fscontextsTag } },
    { HEADER_EXT_TAG, "RPMTAG_FSNAMES",		{ fsnamesTag } },
    { HEADER_EXT_TAG, "RPMTAG_FSSIZES",		{ fssizesTag } },
    { HEADER_EXT_TAG, "RPMTAG_GROUP",		{ groupTag } },
    { HEADER_EXT_TAG, "RPMTAG_INSTALLPREFIX",	{ instprefixTag } },
    { HEADER_EXT_TAG, "RPMTAG_RECONTEXTS",	{ recontextsTag } },
    { HEADER_EXT_TAG, "RPMTAG_SUGGESTS",	{ missingokTag } },
    { HEADER_EXT_TAG, "RPMTAG_SUMMARY",		{ summaryTag } },
    { HEADER_EXT_TAG, "RPMTAG_TRIGGERCONDS",	{ triggercondsTag } },
    { HEADER_EXT_TAG, "RPMTAG_TRIGGERTYPE",	{ triggertypeTag } },
    { HEADER_EXT_FORMAT, "armor",		{ armorFormat } },
    { HEADER_EXT_FORMAT, "base64",		{ base64Format } },
    { HEADER_EXT_FORMAT, "depflags",		{ depflagsFormat } },
    { HEADER_EXT_FORMAT, "fflags",		{ fflagsFormat } },
    { HEADER_EXT_FORMAT, "perms",		{ permsFormat } },
    { HEADER_EXT_FORMAT, "permissions",		{ permsFormat } },
    { HEADER_EXT_FORMAT, "pgpsig",		{ pgpsigFormat } },
    { HEADER_EXT_FORMAT, "triggertype",		{ triggertypeFormat } },
    { HEADER_EXT_FORMAT, "xml",			{ xmlFormat } },
    { HEADER_EXT_FORMAT, "yaml",		{ yamlFormat } },
    { HEADER_EXT_MORE, NULL,		{ (void *) headerDefaultFormats } }
} ;
/*@=type@*/

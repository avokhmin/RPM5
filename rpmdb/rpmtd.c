#include "system.h"

#define	_RPMTD_INTERNAL
#define _RPMTAG_INTERNAL	/* XXX rpmHeaderFormatFuncByValue() */
#include <rpmtd.h>
#include <rpmpgp.h>

#include "debug.h"

#define RPM_NULL_TYPE		0
#define RPM_INT8_TYPE		RPM_UINT8_TYPE
#define RPM_INT16_TYPE		RPM_UINT16_TYPE
#define RPM_INT32_TYPE		RPM_UINT32_TYPE
#define RPM_INT64_TYPE		RPM_UINT64_TYPE

#define	rpmTagGetType(_tag)	tagType(_tag)

#define	rpmHeaderFormats	headerCompoundFormats

static void *rpmHeaderFormatFuncByName(const char *fmt)
{
    headerSprintfExtension ext;
    void *func = NULL;

    for (ext = rpmHeaderFormats; ext->name != NULL; ext++) {
	if (ext->name == NULL || ext->type != HEADER_EXT_FORMAT)
	    continue;
        if (!strcmp(ext->name, fmt)) {
            func = ext->u.fmtFunction;
            break;
        }
    }
    return func;
}

struct key_s {
    const char * str;
    rpmtdFormats fmt;
} keyFormats[] = {
    { "armor",		RPMTD_FORMAT_ARMOR },
#ifdef	NOTYET
    { "arraysize",	RPMTD_FORMAT_ARRAYSIZE },
#endif
    { "base64",		RPMTD_FORMAT_BASE64 },
    { "date",		RPMTD_FORMAT_DATE },
    { "day",		RPMTD_FORMAT_DAY },
    { "depflags",	RPMTD_FORMAT_DEPFLAGS },
    { "fflags",		RPMTD_FORMAT_FFLAGS },
    { "hex",		RPMTD_FORMAT_HEX },
    { "octal",		RPMTD_FORMAT_OCTAL },
    { "permissions",	RPMTD_FORMAT_PERMS },
    { "perms",		RPMTD_FORMAT_PERMS },
    { "pgpsig",		RPMTD_FORMAT_PGPSIG },
    { "shescape",	RPMTD_FORMAT_SHESCAPE },
#ifdef	NOTYET
    { "string",		RPMTD_FORMAT_STRING },
#endif
    { "triggertype",	RPMTD_FORMAT_TRIGGERTYPE },
    { "xml",		RPMTD_FORMAT_XML },
};
/*@unchecked@*/
static size_t nKeyFormats = sizeof(keyFormats) / sizeof(keyFormats[0]);

static const char * fmt2name(rpmtdFormats fmt)
{
    const char * str = NULL;
    int i;

    for (i = 0; i < (int)nKeyFormats; i++) {
	if (fmt != keyFormats[i].fmt)
	    continue;
	str = keyFormats[i].str;
	break;
    }
    return str;
}

static void *rpmHeaderFormatFuncByValue(rpmtdFormats fmt)
{
    return rpmHeaderFormatFuncByName(fmt2name(fmt));
}

rpmtd rpmtdNew(void)
{
    rpmtd td = xmalloc(sizeof(*td));
    rpmtdReset(td);
    return td;
}

rpmtd rpmtdFree(rpmtd td)
{
    /* permit free on NULL td */
    if (td != NULL) {
	/* XXX should we free data too - a flag maybe? */
	free(td);
    }
    return NULL;
}

void rpmtdReset(rpmtd td)
{
assert(td != NULL);

    memset(td, 0, sizeof(*td));
    td->ix = -1;
}

void rpmtdFreeData(rpmtd td)
{
    int i;
assert(td != NULL);

    if (td->flags & RPMTD_ALLOCED) {
	if (td->flags & RPMTD_PTR_ALLOCED) {
	    assert(td->data != NULL);
	    char **data = td->data;
	    for (i = 0; i < (int)td->count; i++) {
		free(data[i]);
	    }
	}
	free(td->data);
    }
    rpmtdReset(td);
}

rpm_count_t rpmtdCount(rpmtd td)
{
assert(td != NULL);
    /* fix up for binary type abusing count as data length */
    return (td->type == RPM_BIN_TYPE) ? 1 : td->count;
}

rpmTag rpmtdTag(rpmtd td)
{
assert(td != NULL);
    return td->tag;
}

rpmTagType rpmtdType(rpmtd td)
{
assert(td != NULL);
    return td->type;
}

int rpmtdGetIndex(rpmtd td)
{
assert(td != NULL);
    return td->ix;
}

int rpmtdSetIndex(rpmtd td, int index)
{
assert(td != NULL);

    if (index < 0 || index >= (int)rpmtdCount(td)) {
	return -1;
    }
    td->ix = index;
    return td->ix;
}

int rpmtdInit(rpmtd td)
{
assert(td != NULL);

    /* XXX check that this is an array type? */
    td->ix = -1;
    return 0;
}

int rpmtdNext(rpmtd td)
{
assert(td != NULL);

    int i = -1;
    
    if (++td->ix >= 0) {
	if (td->ix < (int)rpmtdCount(td)) {
	    i = td->ix;
	} else {
	    td->ix = i;
	}
    }
    return i;
}

uint32_t *rpmtdNextUint32(rpmtd td)
{
    uint32_t *res = NULL;
assert(td != NULL);
    if (rpmtdNext(td) >= 0) {
	res = rpmtdGetUint32(td);
    }
    return res;
}

uint64_t *rpmtdNextUint64(rpmtd td)
{
    uint64_t *res = NULL;
assert(td != NULL);
    if (rpmtdNext(td) >= 0) {
	res = rpmtdGetUint64(td);
    }
    return res;
}

const char *rpmtdNextString(rpmtd td)
{
    const char *res = NULL;
assert(td != NULL);
    if (rpmtdNext(td) >= 0) {
	res = rpmtdGetString(td);
    }
    return res;
}

char * rpmtdGetChar(rpmtd td)
{
    char *res = NULL;

assert(td != NULL);
    if (td->type == RPM_INT8_TYPE) {
	int ix = (td->ix >= 0 ? td->ix : 0);
	res = (char *) td->data + ix;
    } 
    return res;
}

uint16_t * rpmtdGetUint16(rpmtd td)
{
    uint16_t *res = NULL;

assert(td != NULL);
    if (td->type == RPM_INT16_TYPE) {
	int ix = (td->ix >= 0 ? td->ix : 0);
	res = (uint16_t *) td->data + ix;
    } 
    return res;
}

uint32_t * rpmtdGetUint32(rpmtd td)
{
    uint32_t *res = NULL;

assert(td != NULL);
    if (td->type == RPM_INT32_TYPE) {
	int ix = (td->ix >= 0 ? td->ix : 0);
	res = (uint32_t *) td->data + ix;
    } 
    return res;
}

uint64_t * rpmtdGetUint64(rpmtd td)
{
    uint64_t *res = NULL;

assert(td != NULL);
    if (td->type == RPM_INT64_TYPE) {
	int ix = (td->ix >= 0 ? td->ix : 0);
	res = (uint64_t *) td->data + ix;
    } 
    return res;
}

const char * rpmtdGetString(rpmtd td)
{
    const char *str = NULL;

assert(td != NULL);
    if (td->type == RPM_STRING_TYPE) {
	str = (const char *) td->data;
    } else if (td->type == RPM_STRING_ARRAY_TYPE ||
	       td->type == RPM_I18NSTRING_TYPE) {
	/* XXX TODO: check for array bounds */
	int ix = (td->ix >= 0 ? td->ix : 0);
	str = *((const char**) td->data + ix);
    } 
    return str;
}

char *rpmtdFormat(rpmtd td, rpmtdFormats fmt, const char *errmsg)
{
    headerTagFormatFunction func = rpmHeaderFormatFuncByValue(fmt);
    const char *err = NULL;
    char *str = NULL;

    if (func) {
#ifdef	NOTYET	/* XXX different prototypes, wait for rpmhe coercion. */
	char fmtbuf[50]; /* yuck, get rid of this */
	strcpy(fmtbuf, "%");
	str = func(td, fmtbuf);
#else
	err = _("Unknown format");
#endif
    } else {
	err = _("Unknown format");
    }
    
    if (err && errmsg) {
	errmsg = err;
    }

    return str;
}

int rpmtdSetTag(rpmtd td, rpmTag tag)
{
    rpmTagType newtype = rpmTagGetType(tag);
    int rc = 0;

assert(td != NULL);

    /* 
     * Sanity checks: 
     * - is the new tag valid at all
     * - if changing tag of non-empty container, require matching type 
     */
    if (newtype == RPM_NULL_TYPE)
	goto exit;

    if (td->data || td->count > 0) {
	if (rpmTagGetType(td->tag) != rpmTagGetType(tag)) {
	    goto exit;
	}
    } 

    td->tag = tag;
    td->type = newtype & RPM_MASK_TYPE;
    rc = 1;
    
exit:
    return rc;
}

static inline int rpmtdSet(rpmtd td, rpmTag tag, rpmTagType type, 
			    rpm_constdata_t data, rpm_count_t count)
{
    rpmtdReset(td);
    td->tag = tag;
    td->type = type;
    td->count = count;
    /* 
     * Discards const, but we won't touch the data (even rpmtdFreeData()
     * wont free it as allocation flags aren't set) so it's "ok". 
     * XXX: Should there be a separate RPMTD_FOO flag for "user data"?
     */
    td->data = (void *) data;
    return 1;
}

int rpmtdFromUint8(rpmtd td, rpmTag tag, uint8_t *data, rpm_count_t count)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    rpmTagReturnType retype = rpmTagGetType(tag) & RPM_MASK_RETURN_TYPE;
    
    if (count < 1)
	return 0;

    /*
     * BIN type is really just an uint8_t array internally, it's just
     * treated specially otherwise.
     */
    switch (type) {
    case RPM_INT8_TYPE:
	if (retype != RPM_ARRAY_RETURN_TYPE && count > 1) 
	    return 0;
	/* fallthrough */
    case RPM_BIN_TYPE:
	break;
    default:
	return 0;
    }
    
    return rpmtdSet(td, tag, type, data, count);
}

int rpmtdFromUint16(rpmtd td, rpmTag tag, uint16_t *data, rpm_count_t count)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    rpmTagReturnType retype = rpmTagGetType(tag) & RPM_MASK_RETURN_TYPE;
    if (type != RPM_INT16_TYPE || count < 1) 
	return 0;
    if (retype != RPM_ARRAY_RETURN_TYPE && count > 1) 
	return 0;
    
    return rpmtdSet(td, tag, type, data, count);
}

int rpmtdFromUint32(rpmtd td, rpmTag tag, uint32_t *data, rpm_count_t count)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    rpmTagReturnType retype = rpmTagGetType(tag) & RPM_MASK_RETURN_TYPE;
    if (type != RPM_INT32_TYPE || count < 1) 
	return 0;
    if (retype != RPM_ARRAY_RETURN_TYPE && count > 1) 
	return 0;
    
    return rpmtdSet(td, tag, type, data, count);
}

int rpmtdFromUint64(rpmtd td, rpmTag tag, uint64_t *data, rpm_count_t count)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    rpmTagReturnType retype = rpmTagGetType(tag) & RPM_MASK_RETURN_TYPE;
    if (type != RPM_INT64_TYPE || count < 1) 
	return 0;
    if (retype != RPM_ARRAY_RETURN_TYPE && count > 1) 
	return 0;
    
    return rpmtdSet(td, tag, type, data, count);
}

int rpmtdFromString(rpmtd td, rpmTag tag, const char *data)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    int rc = 0;

    if (type == RPM_STRING_TYPE) {
	rc = rpmtdSet(td, tag, type, data, 1);
    } else if (type == RPM_STRING_ARRAY_TYPE) {
	rc = rpmtdSet(td, tag, type, &data, 1);
    }

    return rc;
}

int rpmtdFromStringArray(rpmtd td, rpmTag tag, const char **data, rpm_count_t count)
{
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    if (type != RPM_STRING_ARRAY_TYPE || count < 1)
	return 0;
    if (type == RPM_STRING_TYPE && count != 1)
	return 0;

    return rpmtdSet(td, tag, type, data, count);
}

int rpmtdFromArgv(rpmtd td, rpmTag tag, const char ** argv)
{
    int count = argvCount(argv);
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;

    if (type != RPM_STRING_ARRAY_TYPE || count < 1)
	return 0;

    return rpmtdSet(td, tag, type, argv, count);
}

int rpmtdFromArgi(rpmtd td, rpmTag tag, ARGI_t argi)
{
    int count = argiCount(argi);
    rpmTagType type = rpmTagGetType(tag) & RPM_MASK_TYPE;
    rpmTagReturnType retype = rpmTagGetType(tag) & RPM_MASK_RETURN_TYPE;

    if (type != RPM_INT32_TYPE || retype != RPM_ARRAY_RETURN_TYPE || count < 1)
	return 0;

    return rpmtdSet(td, tag, type, argiData(argi), count);
}

rpmtd rpmtdDup(rpmtd td)
{
    rpmtd newtd = NULL;
    char **data = NULL;
    int i;
    
assert(td != NULL);
    /* TODO: permit other types too */
    if (td->type != RPM_STRING_ARRAY_TYPE && td->type != RPM_I18NSTRING_TYPE) {
	return NULL;
    }

    /* deep-copy container and data, drop immutable flag */
    newtd = rpmtdNew();
    memcpy(newtd, td, sizeof(*td));
    newtd->flags &= ~(RPMTD_IMMUTABLE);

    newtd->flags |= (RPMTD_ALLOCED | RPMTD_PTR_ALLOCED);
    newtd->data = data = xmalloc(td->count * sizeof(*data));
    while ((i = rpmtdNext(td)) >= 0) {
	data[i] = xstrdup(rpmtdGetString(td));
    }

    return newtd;
}

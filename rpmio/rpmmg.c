/** \ingroup rpmio
 * \file rpmio/rpmmg.c
 */

#include "system.h"

#if defined(HAVE_MAGIC_H)
#include "magic.h"
#endif

#include <rpmiotypes.h>
#include <rpmlog.h>
#define	_RPMMG_INTERNAL
#include <rpmmg.h>

#include "debug.h"

/*@unchecked@*/
int _rpmmg_debug = 0;

rpmmg rpmmgFree(rpmmg mg)
{
if (_rpmmg_debug)
fprintf(stderr, "--> rpmmgFree(%p)\n", mg);
    if (mg) {
#if defined(HAVE_MAGIC_H)
	if (mg->ms) {
	    magic_close(mg->ms);
	    mg->ms = NULL;
	}
#endif
	mg->fn = _free(mg->fn);
	mg = _free(mg);
    }
    return NULL;
}

rpmmg rpmmgNew(const char * fn, int flags)
{
    rpmmg mg = xcalloc(1, sizeof(*mg));
    int xx;

    if (fn)
	mg->fn = xstrdup(fn);
#if defined(HAVE_MAGIC_H)
    mg->flags = (flags ? flags : MAGIC_CHECK);/* XXX MAGIC_COMPRESS flag? */
    mg->ms = magic_open(flags);
    if (mg->ms == NULL) {
	rpmlog(RPMLOG_ERR, _("magic_open(0x%x) failed: %s\n"),
		flags, strerror(errno));
	return rpmmgFree(mg);
    }
    xx = magic_load(mg->ms, mg->fn);
    if (xx == -1) {
        rpmlog(RPMLOG_ERR, _("magic_load(ms, %s) failed: %s\n"),
                (fn ? fn : "(nil)"), magic_error(mg->ms));
	return rpmmgFree(mg);
    }
#endif

if (_rpmmg_debug)
fprintf(stderr, "--> rpmmgNew(%s, 0x%x) mg %p\n", (fn ? fn : "(nil)"), flags, mg);
    return mg;
}

const char * rpmmgFile(rpmmg mg, const char *fn)
{
    const char * t = NULL;

#if defined(HAVE_MAGIC_H)
    if (mg->ms) {
	t = magic_file(mg->ms, fn);
	/* XXX HACK: libmagic compiled without <pcreposix.h> spews here. */
	if (t == NULL) {
	    const char * msg = magic_error(mg->ms);
	    if (strstr(msg, "regexec error 17, (match failed)") == NULL)
		rpmlog(RPMLOG_ERR, _("magic_file(ms, %s) failed: %s\n"),
			(fn ? fn : "(nil)"), msg);
	}
    }
#endif

    if (t == NULL) t = "";
    t = xstrdup(t);

if (_rpmmg_debug)
fprintf(stderr, "--> rpmmgFile(%p, %s) %s\n", mg, (fn ? fn : "(nil)"), t);
    return t;
}

const char * rpmmgBuffer(rpmmg mg, const char * b, size_t nb)
{
    const char * t = NULL;

#if defined(HAVE_MAGIC_H)
    if (mg->ms) {
	t = magic_buffer(mg->ms, b, nb);
	/* XXX HACK: libmagic compiled without <pcreposix.h> spews here. */
	if (t == NULL) {
	    const char * msg = magic_error(mg->ms);
	    if (strstr(msg, "regexec error 17, (match failed)") == NULL)
		rpmlog(RPMLOG_ERR, _("magic_buffer(ms, %p[%u]) failed: %s\n"),
			b, (unsigned)nb, msg);
	}
    }
#endif

    if (t == NULL) t = "";
    t = xstrdup(t);

if (_rpmmg_debug)
fprintf(stderr, "--> rpmmgBuffer(%p, %p[%d]) %s\n", mg, b, (int)nb, t);
    return t;
}

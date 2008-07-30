#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#undef Fflush
#undef Mkdir
#undef Stat
#undef Fstat

#include <stdio.h>
#include <string.h>
#include <utime.h>

#include <rpmio.h>
#include "rpmcb.h"

#include "rpmtypes.h"
#include "rpmtag.h"
#include "rpmdb.h"

#include "rpmte.h"
#include "rpmts.h"

#include "misc.h"

#include "rpmcli.h"

#include "rpmxs.h"

MODULE = RPM::PackageIterator		PACKAGE = RPM::PackageIterator

PROTOTYPES: ENABLE

void
new(class, sv_tagname = NULL, sv_tagvalue = NULL, keylen = 0)
    char * class
    SV * sv_tagname
    SV * sv_tagvalue
    int keylen
    PREINIT:
    rpmts ts = rpmtsCreate();
    PPCODE:
    PUTBACK;
    _newiterator(ts, sv_tagname, sv_tagvalue, keylen);
    SPAGAIN;
    ts = rpmtsFree(ts);

void
prune(mi, ...)
    rpmdbMatchIterator mi
    PREINIT:
    int * exclude = NULL;
    int exclude_count = 0;
    int i = 0;
    CODE:
    exclude_count = items - 1;
    exclude = malloc(exclude_count * sizeof(int));
    for (i = 1; i < items; i++) {
        if (!SvIOK(ST(i))) { /* TODO: */ }
        exclude[i - 1] = SvIV(ST(i));
    }
    rpmdbPruneIterator(mi, exclude, exclude_count, 0);
    _free(exclude);
    
unsigned int
getoffset(mi)
    rpmdbMatchIterator mi
    CODE:
    RETVAL = rpmdbGetIteratorOffset(mi);
    OUTPUT:
    RETVAL

int
count(mi)
    rpmdbMatchIterator mi
    CODE:
    RETVAL = rpmdbGetIteratorCount(mi);
    OUTPUT:
    RETVAL

void
next(mi)
    rpmdbMatchIterator mi
    PREINIT:
    Header header = NULL;
    PPCODE:
    header = rpmdbNextIterator(mi);
    if (header) {
        XPUSHs(sv_2mortal(sv_setref_pv(newSVpv("", 0), "RPM::Header", headerLink(header))));
    }
    

#include <string.h>

#include "spec.h"
#include "rpmlib.h"
#include "reqprov.h"

static struct ReqComp {
    char *token;
    int sense;
} ReqComparisons[] = {
    { "<=", RPMSENSE_LESS | RPMSENSE_EQUAL},
    { "<=S", RPMSENSE_LESS | RPMSENSE_EQUAL | RPMSENSE_SERIAL},
    { "=<", RPMSENSE_LESS | RPMSENSE_EQUAL},
    { "=<S", RPMSENSE_LESS | RPMSENSE_EQUAL | RPMSENSE_SERIAL},
    { "<", RPMSENSE_LESS},
    { "<S", RPMSENSE_LESS | RPMSENSE_SERIAL},

    { "=", RPMSENSE_EQUAL},
    { "=S", RPMSENSE_EQUAL | RPMSENSE_SERIAL},
    
    { ">=", RPMSENSE_GREATER | RPMSENSE_EQUAL},
    { ">=S", RPMSENSE_GREATER | RPMSENSE_EQUAL | RPMSENSE_SERIAL},
    { "=>", RPMSENSE_GREATER | RPMSENSE_EQUAL},
    { "=>S", RPMSENSE_GREATER | RPMSENSE_EQUAL | RPMSENSE_SERIAL},
    { ">", RPMSENSE_GREATER},
    { ">S", RPMSENSE_GREATER | RPMSENSE_SERIAL},
    { NULL, 0 },
};

int parseRequiresConflicts(Spec spec, Package pkg, char *field,
			   int tag, int index)
{
    char buf[BUFSIZ], *bufp, *version, *name;
    int flags;
    char *req = NULL;
    struct ReqComp *rc;

    strcpy(buf, field);
    bufp = buf;

    while (req || (req = strtok(bufp, " ,\t\n"))) {
	bufp = NULL;
	
	if (tag == RPMTAG_CONFLICTFLAGS) {
	    if (req[0] == '/') {
		rpmError(RPMERR_BADSPEC,
			 "line %d: No file names in Conflicts: %s",
			 spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	    flags = RPMSENSE_CONFLICTS;
	    name = "Conflicts";
	} else if (tag == RPMTAG_PREREQ) {
	    flags = RPMSENSE_PREREQ;
	    name = "PreReq";
	} else if (tag == RPMTAG_TRIGGERIN) {
	    flags = RPMSENSE_TRIGGERIN;
	    name = "%triggerin";
	} else if (tag == RPMTAG_TRIGGERPOSTUN) {
	    flags = RPMSENSE_TRIGGERUN;
	    name = "%triggerpostun";
	} else if (tag == RPMTAG_TRIGGERUN) {
	    flags = RPMSENSE_TRIGGERUN;
	    name = "%triggerun";
	} else {
	    flags = RPMSENSE_ANY;
	    name = "Requires";
	}

	if ((version = strtok(NULL, " ,\t\n"))) {
	    rc = ReqComparisons;
	    while (rc->token && strcmp(version, rc->token)) {
		rc++;
	    }
	    if (rc->token) {
		if (req[0] == '/') {
		    rpmError(RPMERR_BADSPEC,
			     "line %d: No versions on file names in %s: %s",
			     spec->lineNum, name, spec->line);
		    return RPMERR_BADSPEC;
		}
		if (tag == RPMTAG_PREREQ) {
		    rpmError(RPMERR_BADSPEC,
			     "line %d: No versions in PreReq: %s",
			     spec->lineNum, spec->line);
		    return RPMERR_BADSPEC;
		}
		/* read a version */
		flags |= rc->sense;
		version = strtok(NULL, " ,\t\n");
	    }
	}

	if ((flags & RPMSENSE_SENSEMASK) && !version) {
	    rpmError(RPMERR_BADSPEC,
		     "line %d: Version required in %s: %s",
		     spec->lineNum, name, spec->line);
	    return RPMERR_BADSPEC;
	}

	addReqProv(spec, pkg, flags, req,
		   (flags & RPMSENSE_SENSEMASK) ? version : NULL, index);

	/* If there is no sense, we just read the next token */
	req = (flags & RPMSENSE_SENSEMASK) ? NULL : version;
    }

    return 0;
}

int parseProvidesObsoletes(Spec spec, Package pkg, char *field, int tag)
{
    char *prov, buf[BUFSIZ], *line;
    int flags;

    flags = (tag == RPMTAG_PROVIDES) ? RPMSENSE_PROVIDES : RPMSENSE_OBSOLETES;

    strcpy(buf, field);
    line = buf;
    
    while ((prov = strtok(line, " ,\t\n"))) {
	if (prov[0] == '/') {
	    rpmError(RPMERR_BADSPEC,
		     "line %d: No file names in %s: %s",
		     spec->lineNum,
		     (tag == RPMTAG_PROVIDES) ? "Provides" : "Obsoletes",
		     spec->line);
	    return RPMERR_BADSPEC;
	}
	addReqProv(spec, pkg, flags, prov, NULL, 0);
	line = NULL;
    }

    return 0;
}

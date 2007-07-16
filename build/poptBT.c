/** \ingroup rpmcli
 * \file build/poptBT.c
 *  Popt tables for build modes.
 */

#include "system.h"

#include <rpmio.h>
#include <rpmcli.h>
#include <rpmbuild.h>

#include "build.h"
#include "debug.h"

/*@unchecked@*/
struct rpmBuildArguments_s         rpmBTArgs;

#define	POPT_USECATALOG		-1011
#define	POPT_NOLANG		-1012
#define	POPT_RMSOURCE		-1013
#define	POPT_RMBUILD		-1014
	/* XXX was POPT_BUILDROOT -1015 */

#define	POPT_NOBUILD		-1017
#define	POPT_SHORTCIRCUIT	-1018
#define	POPT_RMSPEC		-1019
#define	POPT_SIGN		-1020

#define	POPT_REBUILD		0x4220
#define	POPT_RECOMPILE		0x4320
#define	POPT_BA			0x6261
#define	POPT_BB			0x6262
#define	POPT_BC			0x6263
#define	POPT_BI			0x6269
#define	POPT_BL			0x626c
#define	POPT_BP			0x6270
#define	POPT_BS			0x6273
#define	POPT_TA			0x7461
#define	POPT_TB			0x7462
#define	POPT_TC			0x7463
#define	POPT_TI			0x7469
#define	POPT_TL			0x746c
#define	POPT_TP			0x7470
#define	POPT_TS			0x7473

/*@-redecl@*/
/*@unchecked@*/
extern int _fsm_debug;
/*@=redecl@*/

/*@-exportlocal@*/
/*@unchecked@*/
int noLang = 0;
/*@=exportlocal@*/

/*@unchecked@*/
static int noBuild = 0;

/*@unchecked@*/
static int signIt = 0;

/*@unchecked@*/
static int useCatalog = 0;

/**
 */
/*@-boundswrite@*/
static void buildArgCallback( /*@unused@*/ poptContext con,
		/*@unused@*/ enum poptCallbackReason reason,
		const struct poptOption * opt,
		/*@unused@*/ const char * arg,
		/*@unused@*/ const void * data)
{
    BTA_t rba = &rpmBTArgs;

    switch (opt->val) {
    case POPT_REBUILD:
    case POPT_RECOMPILE:
    case POPT_BA:
    case POPT_BB:
    case POPT_BC:
    case POPT_BI:
    case POPT_BL:
    case POPT_BP:
    case POPT_BS:
    case POPT_TA:
    case POPT_TB:
    case POPT_TC:
    case POPT_TI:
    case POPT_TL:
    case POPT_TP:
    case POPT_TS:
	if (rba->buildMode == '\0' && rba->buildChar == '\0') {
	    rba->buildMode = (((unsigned)opt->val) >> 8) & 0xff;
	    rba->buildChar = (opt->val     ) & 0xff;
	}
	break;

    case POPT_NOBUILD: rba->noBuild = 1; break;
    case POPT_NOLANG: rba->noLang = 1; break;
    case POPT_SHORTCIRCUIT: rba->shortCircuit = 1; break;
    case POPT_SIGN: rba->sign = 1; break;
    case POPT_USECATALOG: rba->useCatalog = 1; break;
    case POPT_RMSOURCE: rba->buildAmount |= RPMBUILD_RMSOURCE; break;
    case POPT_RMSPEC: rba->buildAmount |= RPMBUILD_RMSPEC; break;
    case POPT_RMBUILD: rba->buildAmount |= RPMBUILD_RMBUILD; break;

    case RPMCLI_POPT_NODIGEST:
	rba->qva_flags |= VERIFY_DIGEST;
	break;

    case RPMCLI_POPT_NOSIGNATURE:
	rba->qva_flags |= VERIFY_SIGNATURE;
	break;

    case RPMCLI_POPT_NOHDRCHK:
	rba->qva_flags |= VERIFY_HDRCHK;
	break;

    case RPMCLI_POPT_NODEPS:
	rba->noDeps = 1;
	break;

    case RPMCLI_POPT_FORCE:
	rba->force = 1;
	break;

    }
}
/*@=boundswrite@*/

/**
 */
/*@-bitwisesigned -compmempass @*/
/*@unchecked@*/
struct poptOption rpmBuildPoptTable[] = {
/*@-type@*/ /* FIX: cast? */
 { NULL, '\0', POPT_ARG_CALLBACK | POPT_CBFLAG_INC_DATA | POPT_CBFLAG_CONTINUE,
	buildArgCallback, 0, NULL, NULL },
/*@=type@*/

 { "bp", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BP,
	N_("build through %prep (unpack sources and apply patches) from <specfile>"),
	N_("<specfile>") },
 { "bc", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BC,
	N_("build through %build (%prep, then compile) from <specfile>"),
	N_("<specfile>") },
 { "bi", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BI,
	N_("build through %install (%prep, %build, then install) from <specfile>"),
	N_("<specfile>") },
 { "bl", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BL,
	N_("verify %files section from <specfile>"),
	N_("<specfile>") },
 { "ba", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BA,
	N_("build source and binary packages from <specfile>"),
	N_("<specfile>") },
 { "bb", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BB,
	N_("build binary package only from <specfile>"),
	N_("<specfile>") },
 { "bs", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_BS,
	N_("build source package only from <specfile>"),
	N_("<specfile>") },

 { "tp", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TP,
	N_("build through %prep (unpack sources and apply patches) from <tarball>"),
	N_("<tarball>") },
 { "tc", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TC,
	N_("build through %build (%prep, then compile) from <tarball>"),
	N_("<tarball>") },
 { "ti", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TI,
	N_("build through %install (%prep, %build, then install) from <tarball>"),
	N_("<tarball>") },
 { "tl", 0, POPT_ARGFLAG_ONEDASH|POPT_ARGFLAG_DOC_HIDDEN, 0, POPT_TL,
	N_("verify %files section from <tarball>"),
	N_("<tarball>") },
 { "ta", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TA,
	N_("build source and binary packages from <tarball>"),
	N_("<tarball>") },
 { "tb", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TB,
	N_("build binary package only from <tarball>"),
	N_("<tarball>") },
 { "ts", 0, POPT_ARGFLAG_ONEDASH, 0, POPT_TS,
	N_("build source package only from <tarball>"),
	N_("<tarball>") },

 { "rebuild", '\0', 0, 0, POPT_REBUILD,
	N_("build binary package from <source package>"),
	N_("<source package>") },
 { "recompile", '\0', 0, 0, POPT_RECOMPILE,
	N_("build through %install (%prep, %build, then install) from <source package>"),
	N_("<source package>") },

 { "clean", '\0', 0, 0, POPT_RMBUILD,
	N_("remove build tree when done"), NULL},
 { "force", '\0', POPT_ARGFLAG_DOC_HIDDEN, &rpmBTArgs.force, RPMCLI_POPT_FORCE,
        N_("ignore ExcludeArch: directives from spec file"), NULL},
 { "fsmdebug", '\0', (POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN), &_fsm_debug, -1,
	N_("debug file state machine"), NULL},
 { "nobuild", '\0', 0, &noBuild,  POPT_NOBUILD,
	N_("do not execute any stages of the build"), NULL },
 { "nodeps", '\0', 0, NULL, RPMCLI_POPT_NODEPS,
	N_("do not verify build dependencies"), NULL },

 { "nodigest", '\0', POPT_ARGFLAG_DOC_HIDDEN, 0, RPMCLI_POPT_NODIGEST,
        N_("don't verify package digest(s)"), NULL },
 { "nohdrchk", '\0', POPT_ARGFLAG_DOC_HIDDEN, 0, RPMCLI_POPT_NOHDRCHK,
        N_("don't verify database header(s) when retrieved"), NULL },
 { "nosignature", '\0', POPT_ARGFLAG_DOC_HIDDEN, 0, RPMCLI_POPT_NOSIGNATURE,
        N_("don't verify package signature(s)"), NULL },

 { "nolang", '\0', POPT_ARGFLAG_DOC_HIDDEN, &noLang, POPT_NOLANG,
	N_("do not accept i18N msgstr's from specfile"), NULL},
 { "rmsource", '\0', 0, 0, POPT_RMSOURCE,
	N_("remove sources when done"), NULL},
 { "rmspec", '\0', 0, 0, POPT_RMSPEC,
	N_("remove specfile when done"), NULL},
 { "short-circuit", '\0', 0, 0,  POPT_SHORTCIRCUIT,
	N_("skip straight to specified stage (only for c,i)"), NULL },
 { "sign", '\0', POPT_ARGFLAG_DOC_HIDDEN, &signIt, POPT_SIGN,
	N_("generate PGP/GPG signature"), NULL },
 { "target", '\0', POPT_ARG_STRING, 0,  RPMCLI_POPT_TARGETPLATFORM,
	N_("override target platform"), N_("CPU-VENDOR-OS") },
 { "usecatalog", '\0', POPT_ARGFLAG_DOC_HIDDEN, &useCatalog, POPT_USECATALOG,
	N_("lookup i18N strings in specfile catalog"), NULL},

   POPT_TABLEEND
};
/*@=bitwisesigned =compmempass @*/

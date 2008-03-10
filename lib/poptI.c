/** \ingroup rpmcli
 * \file lib/poptI.c
 *  Popt tables for install modes.
 */

#include "system.h"

#include <rpmio.h>
#include <rpmcli.h>

#include "debug.h"

/*@-redecl@*/
extern time_t get_date(const char * p, void * now);	/* XXX expedient lies */
/*@=redecl@*/

/*@-fullinitblock@*/
/*@unchecked@*/
struct rpmQVKArguments_s rpmIArgs = {
    .probFilter = (RPMPROB_FILTER_REPLACEOLDFILES | RPMPROB_FILTER_REPLACENEWFILES),
};
/*@=fullinitblock@*/

#define	POPT_RELOCATE		-1021
#define	POPT_EXCLUDEPATH	-1022
#define	POPT_ROLLBACK		-1023
#define	POPT_ROLLBACK_EXCLUDE	-1024
/* -1025 thrugh -1033 are common in rpmcli.h. */
#define	POPT_AUTOROLLBACK_GOAL	-1036

#define	alloca_strdup(_s)	strcpy(alloca(strlen(_s)+1), (_s))

/**
 * Print a message and exit.
 * @todo (CLI embedding) Use rpmlog/rpmlog instead of fprintf, remove exit.
 * @param desc		message	
 */
/*@exits@*/
static void argerror(const char * desc)
	/*@globals stderr, fileSystem @*/
	/*@modifies stderr, fileSystem @*/
{
    /*@-modfilesys -globs @*/
    fprintf(stderr, _("%s: %s\n"), __progname, desc);
    /*@=modfilesys =globs @*/
    exit(EXIT_FAILURE);
}

/**
 */
static void installArgCallback(/*@unused@*/ UNUSED(poptContext con),
		/*@unused@*/ UNUSED(enum poptCallbackReason reason),
		const struct poptOption * opt, const char * arg,
		/*@unused@*/ UNUSED(const void * data))
	/*@globals rpmIArgs, stderr, fileSystem @*/
	/*@modifies rpmIArgs, stderr, fileSystem @*/
{
    QVA_t ia = &rpmIArgs;

    /* XXX avoid accidental collisions with POPT_BIT_SET for flags */
    if (opt->arg == NULL)
    switch (opt->val) {

    case 'i':
	ia->installInterfaceFlags |= INSTALL_INSTALL;
	break;

    case POPT_EXCLUDEPATH:
	if (arg == NULL || *arg != '/') 
	    argerror(_("exclude paths must begin with a /"));
	ia->relocations = xrealloc(ia->relocations, 
			sizeof(*ia->relocations) * (ia->numRelocations + 1));
/*@-temptrans@*/
	ia->relocations[ia->numRelocations].oldPath = xstrdup(arg);
/*@=temptrans@*/
	ia->relocations[ia->numRelocations].newPath = NULL;
	ia->numRelocations++;
	break;
    case POPT_RELOCATE:
      { char * oldPath = NULL;
	char * newPath = NULL;
	
	if (arg == NULL) 
	    argerror(_("Option --relocate needs /old/path=/new/path argument"));
	if (*arg != '/') 
	    argerror(_("relocations must begin with a /"));
	oldPath = xstrdup(arg);
	if (!(newPath = strchr(oldPath, '=')))
	    argerror(_("relocations must contain a ="));
	*newPath++ = '\0';
	if (*newPath != '/') 
	    argerror(_("relocations must have a / following the ="));
	ia->relocations = xrealloc(ia->relocations, 
			sizeof(*ia->relocations) * (ia->numRelocations + 1));
/*@-temptrans@*/
	ia->relocations[ia->numRelocations].oldPath = oldPath;
/*@=temptrans@*/
/*@-kepttrans -usereleased @*/
	ia->relocations[ia->numRelocations].newPath = newPath;
/*@=kepttrans =usereleased @*/
	ia->numRelocations++;
      }	break;

    case POPT_ROLLBACK_EXCLUDE:
    {	uint32_t tid;
	char *t, *te;

	/* Make sure we were given the proper number of args */
	if (arg == NULL)
	    argerror(_("Option --rbexclude needs transaction id argument(s)"));

	te = alloca_strdup(arg);
	while (*te != '\0' && strchr(" \t\n,", *te) != NULL)
	    *te++ = '\0';
	while ((t = te++) != NULL && *t != '\0') {
	    /* Find next tid. */
	    while (*te != '\0' && strchr(" \t\n,", *te) == NULL)
		te++;
	    while (*te != '\0' && strchr(" \t\n,", *te) != NULL)
		*te++ = '\0';

	    /* Convert arg to TID which happens to be time_t */
	    /* XXX: Need check for arg to be an integer      */
	    tid = (uint32_t) strtol(t, NULL, 0);

	    /* Allocate space for new exclude tid */
	    ia->rbtidExcludes = xrealloc(ia->rbtidExcludes, 
		sizeof(*ia->rbtidExcludes) * (ia->numrbtidExcludes + 1));

	    /* Add it to the list and iterate count*/
/*@-temptrans@*/
	    ia->rbtidExcludes[ia->numrbtidExcludes] = tid;
/*@=temptrans@*/
	    ia->numrbtidExcludes++;
	}
    } break;

    case POPT_ROLLBACK:
      {	time_t tid;
	if (arg == NULL)
	    argerror(_("Option --rollback needs a time/date stamp argument"));

	/*@-moduncon@*/
	tid = get_date(arg, NULL);
	rpmlog(RPMLOG_INFO, _("Rollback goal:  %-24.24s (0x%08x)\n"), ctime(&tid), (int)tid);
	/*@=moduncon@*/

	if (tid == (time_t)-1 || tid == (time_t)0)
	    argerror(_("malformed rollback time/date stamp argument"));
	ia->rbtid = tid;
      }	break;
    
    case POPT_AUTOROLLBACK_GOAL:
      {	time_t tid;
	if (arg == NULL)
	    argerror(_("arbgoal takes a time/date stamp argument"));

	/*@-moduncon@*/
	tid = get_date(arg, NULL);
	/*@=moduncon@*/

	if (tid == (time_t)-1 || tid == (time_t)0)
	    argerror(_("malformed arbgoal time/date stamp argument"));
	ia->arbtid = tid;
      }	break;

    case RPMCLI_POPT_NODIGEST:
	ia->qva_flags |= VERIFY_DIGEST;
	break;

    case RPMCLI_POPT_NOSIGNATURE:
	ia->qva_flags |= VERIFY_SIGNATURE;
	break;

    case RPMCLI_POPT_NOHDRCHK:
	ia->qva_flags |= VERIFY_HDRCHK;
	break;

    case RPMCLI_POPT_NODEPS:
	ia->noDeps = 1;
	break;

    case RPMCLI_POPT_NOFDIGESTS:
	ia->transFlags |= RPMTRANS_FLAG_NOFDIGESTS;
	break;

    case RPMCLI_POPT_NOCONTEXTS:
	ia->transFlags |= RPMTRANS_FLAG_NOCONTEXTS;
	break;

    case RPMCLI_POPT_NOSCRIPTS:
	ia->transFlags |= (_noTransScripts | _noTransTriggers);
	break;

    }
}

/**
 */
/*@-bitwisesigned -compmempass @*/
/*@unchecked@*/
struct poptOption rpmInstallPoptTable[] = {
/*@-type@*/ /* FIX: cast? */
 { NULL, '\0', POPT_ARG_CALLBACK | POPT_CBFLAG_INC_DATA | POPT_CBFLAG_CONTINUE,
	installArgCallback, 0, NULL, NULL },
/*@=type@*/

#if 0
 { "aid", '\0', POPT_BIT_SET, &rpmIArgs.depFlags, RPMDEPS_FLAG_ADDINDEPS,
	N_("add suggested packages to transaction"), NULL },
 { "anaconda", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
 	&rpmIArgs.depFlags, RPMDEPS_FLAG_ANACONDA|RPMDEPS_FLAG_DEPLOOPS,
	N_("use anaconda \"presentation order\""), NULL},
 { "deploops", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
 	&rpmIArgs.depFlags, RPMDEPS_FLAG_DEPLOOPS,
	N_("print dependency loops as warning"), NULL},
 { "nosuggest", '\0', POPT_BIT_SET,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOSUGGEST,
	N_("do not suggest missing dependency resolution(s)"), NULL},
 { "noconflicts", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOCONFLICTS,
	N_("do not check added package conflicts"), NULL},
 { "nolinktos", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOLINKTOS,
	N_("ignore added package requires on symlink targets"), NULL},
 { "noobsoletes", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOOBSOLETES,
	N_("ignore added package obsoletes"), NULL},
 { "noparentdirs", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOPARENTDIRS,
	N_("ignore added package requires on file parent directory"), NULL},
 { "norequires", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOREQUIRES,
	N_("do not check added package requires"), NULL},
 { "noupgrade", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.depFlags, RPMDEPS_FLAG_NOUPGRADE,
	N_("ignore added package upgrades"), NULL},
#endif

 { "allfiles", '\0', POPT_BIT_SET,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_ALLFILES,
  N_("install all files, even configurations which might otherwise be skipped"),
	NULL},
 { "allmatches", '\0', POPT_BIT_SET,
	&rpmIArgs.installInterfaceFlags, INSTALL_ALLMATCHES,
	N_("remove all packages which match <package> (normally an error is generated if <package> specified multiple packages)"),
	NULL},

 { "apply", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.transFlags,
	(_noTransScripts|_noTransTriggers|
		RPMTRANS_FLAG_APPLYONLY|RPMTRANS_FLAG_PKGCOMMIT),
	N_("do not execute package scriptlet(s)"), NULL },

 { "badreloc", '\0', POPT_BIT_SET,
	&rpmIArgs.probFilter, RPMPROB_FILTER_FORCERELOCATE,
	N_("relocate files in non-relocatable package"), NULL},

 { "dirstash", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_DIRSTASH,
	N_("save erased package files by renaming into sub-directory"), NULL},
 { "erase", 'e', POPT_BIT_SET,
	&rpmIArgs.installInterfaceFlags, INSTALL_ERASE,
	N_("erase (uninstall) package"), N_("<package>+") },
 { "excludeconfigs", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOCONFIGS,
	N_("do not install configuration files"), NULL},
 { "excludedocs", '\0', POPT_BIT_SET,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NODOCS,
	N_("do not install documentation"), NULL},
 { "excludepath", '\0', POPT_ARG_STRING, NULL, POPT_EXCLUDEPATH,
	N_("skip files with leading component <path> "),
	N_("<path>") },

 { "fileconflicts", '\0', POPT_BIT_CLR, &rpmIArgs.probFilter,
	(RPMPROB_FILTER_REPLACEOLDFILES | RPMPROB_FILTER_REPLACENEWFILES),
	N_("detect file conflicts between packages"), NULL},

 { "freshen", 'F', POPT_BIT_SET, &rpmIArgs.installInterfaceFlags,
	(INSTALL_UPGRADE|INSTALL_FRESHEN|INSTALL_INSTALL),
	N_("upgrade package(s) if already installed"),
	N_("<packagefile>+") },
 { "hash", 'h', POPT_BIT_SET, &rpmIArgs.installInterfaceFlags, INSTALL_HASH,
	N_("print hash marks as package installs (good with -v)"), NULL},
#ifndef	DIEDIEDIE
 { "ignorearch", '\0', POPT_BIT_SET,
	&rpmIArgs.probFilter, RPMPROB_FILTER_IGNOREARCH,
	N_("don't verify package architecture"), NULL},
 { "ignoreos", '\0', POPT_BIT_SET,
	&rpmIArgs.probFilter, RPMPROB_FILTER_IGNOREOS,
	N_("don't verify package operating system"), NULL},
#endif
 { "ignoresize", '\0', POPT_BIT_SET, &rpmIArgs.probFilter,
	(RPMPROB_FILTER_DISKSPACE|RPMPROB_FILTER_DISKNODES),
	N_("don't check disk space before installing"), NULL},
 { "includedocs", '\0', POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.incldocs, 0,
	N_("install documentation"), NULL},

 { "install", 'i', 0, NULL, 'i',
	N_("install package(s)"), N_("<packagefile>+") },

 { "justdb", '\0', POPT_BIT_SET, &rpmIArgs.transFlags, RPMTRANS_FLAG_JUSTDB,
	N_("update the database, but do not modify the filesystem"), NULL},

 { "noconfigs", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOCONFIGS,
	N_("do not install configuration files"), NULL},
 { "nodeps", '\0', 0, NULL, RPMCLI_POPT_NODEPS,
	N_("do not verify package dependencies"), NULL },
 { "nodocs", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NODOCS,
	N_("do not install documentation"), NULL},

 { "nomd5", '\0', POPT_ARGFLAG_DOC_HIDDEN, NULL, RPMCLI_POPT_NOFDIGESTS,
	N_("don't verify file digests"), NULL },
 { "nofdigests", '\0', 0, NULL, RPMCLI_POPT_NOFDIGESTS,
	N_("don't verify file digests"), NULL },
 { "nocontexts", '\0',0,  NULL, RPMCLI_POPT_NOCONTEXTS,
	N_("don't install file security contexts"), NULL},

 { "noorder", '\0', POPT_BIT_SET,
	&rpmIArgs.installInterfaceFlags, INSTALL_NOORDER,
	N_("do not reorder package installation to satisfy dependencies"),
	NULL},

 { "noscripts", '\0', 0, NULL, RPMCLI_POPT_NOSCRIPTS,
	N_("do not execute package scriptlet(s)"), NULL },

 { "nopre", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.transFlags,
	RPMTRANS_FLAG_NOPRE,
	N_("do not execute %%pre scriptlet (if any)"), NULL },
 { "nopost", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.transFlags,
	RPMTRANS_FLAG_NOPOST,
	N_("do not execute %%post scriptlet (if any)"), NULL },
 { "nopreun", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.transFlags,
	RPMTRANS_FLAG_NOPREUN,
	N_("do not execute %%preun scriptlet (if any)"), NULL },
 { "nopostun", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN, &rpmIArgs.transFlags,
	RPMTRANS_FLAG_NOPOSTUN,
	N_("do not execute %%postun scriptlet (if any)"), NULL },

 { "nodigest", '\0', POPT_ARGFLAG_DOC_HIDDEN, NULL, RPMCLI_POPT_NODIGEST,
        N_("don't verify package digest(s)"), NULL },
 { "nohdrchk", '\0', POPT_ARGFLAG_DOC_HIDDEN, NULL, RPMCLI_POPT_NOHDRCHK,
        N_("don't verify database header(s) when retrieved"), NULL },
 { "nosignature", '\0', POPT_ARGFLAG_DOC_HIDDEN, NULL, RPMCLI_POPT_NOSIGNATURE,
        N_("don't verify package signature(s)"), NULL },

 { "notriggers", '\0', POPT_BIT_SET, &rpmIArgs.transFlags, _noTransTriggers,
	N_("do not execute any scriptlet(s) triggered by this package"), NULL},
 { "notriggerprein", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOTRIGGERPREIN,
	N_("do not execute any %%triggerprein scriptlet(s)"), NULL},
 { "notriggerin", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOTRIGGERIN,
	N_("do not execute any %%triggerin scriptlet(s)"), NULL},
 { "notriggerun", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOTRIGGERUN,
	N_("do not execute any %%triggerun scriptlet(s)"), NULL},
 { "notriggerpostun", '\0', POPT_BIT_SET|POPT_ARGFLAG_DOC_HIDDEN,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_NOTRIGGERPOSTUN,
	N_("do not execute any %%triggerpostun scriptlet(s)"), NULL},

 { "oldpackage", '\0', POPT_BIT_SET,
	&rpmIArgs.probFilter, RPMPROB_FILTER_OLDPACKAGE,
	N_("upgrade to an old version of the package (--force on upgrades does this automatically)"),
	NULL},
 { "percent", '\0', POPT_BIT_SET,
	&rpmIArgs.installInterfaceFlags, INSTALL_PERCENT,
	N_("print percentages as package installs"), NULL},
 { "prefix", '\0', POPT_ARG_STRING, &rpmIArgs.qva_prefix, 0,
	N_("relocate the package to <dir>, if relocatable"),
	N_("<dir>") },
 { "relocate", '\0', POPT_ARG_STRING, NULL, POPT_RELOCATE,
	N_("relocate files from path <old> to <new>"),
	N_("<old>=<new>") },
 { "repackage", '\0', POPT_BIT_SET,
	&rpmIArgs.transFlags, RPMTRANS_FLAG_REPACKAGE,
	N_("save erased package files by repackaging"), NULL},
 { "replacefiles", '\0', POPT_BIT_SET, &rpmIArgs.probFilter,
	(RPMPROB_FILTER_REPLACEOLDFILES | RPMPROB_FILTER_REPLACENEWFILES),
	N_("ignore file conflicts between packages"), NULL},
 { "replacepkgs", '\0', POPT_BIT_SET,
	&rpmIArgs.probFilter, RPMPROB_FILTER_REPLACEPKG,
	N_("reinstall if the package is already present"), NULL},
 { "rollback", '\0', POPT_ARG_STRING|POPT_ARGFLAG_DOC_HIDDEN, NULL, POPT_ROLLBACK,
	N_("deinstall new, reinstall old, package(s), back to <date>"),
	N_("<date>") },
 { "arbgoal", '\0', POPT_ARG_STRING|POPT_ARGFLAG_DOC_HIDDEN, NULL, POPT_AUTOROLLBACK_GOAL,
	N_("If transaction fails rollback to <date>"),
	N_("<date>") },
 { "rbexclude", '\0', POPT_ARG_STRING|POPT_ARGFLAG_DOC_HIDDEN, NULL, POPT_ROLLBACK_EXCLUDE,
	N_("Exclude Transaction I.D. from rollback"),
	N_("<tid>") },
 { "test", '\0', POPT_BIT_SET, &rpmIArgs.transFlags, RPMTRANS_FLAG_TEST,
	N_("don't install, but tell if it would work or not"), NULL},
 { "upgrade", 'U', POPT_BIT_SET,
	&rpmIArgs.installInterfaceFlags, (INSTALL_UPGRADE|INSTALL_INSTALL),
	N_("upgrade package(s)"),
	N_("<packagefile>+") },

   POPT_TABLEEND
};
/*@=bitwisesigned =compmempass @*/

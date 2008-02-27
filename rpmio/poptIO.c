/** \ingroup rpmio
 * \file rpmio/poptIO.c
 *  Popt tables for all rpmio-only executables.
 */

#include "system.h"

#include <poptIO.h>

#define _RPMPGP_INTERNAL
#if defined(WITH_BEECRYPT)
#define _RPMBC_INTERNAL
#include <rpmbc.h>
#endif
#if defined(WITH_GCRYPT)
#define _RPMGC_INTERNAL
#include <rpmgc.h>
#endif
#if defined(WITH_NSS)
#define _RPMNSS_INTERNAL
#include <rpmnss.h>
#endif
#if defined(WITH_SSL)
#define _RPMSSL_INTERNAL
#include <rpmssl.h>
#endif

#include "debug.h"

const char *__progname;

#define POPT_SHOWVERSION	-999
#define POPT_UNDEFINE		-994
#define	POPT_CRYPTO		-993

/*@unchecked@*/
int __debug = 0;

#ifdef	NOTYET
/*@unchecked@*/
extern int _cpio_debug;

/*@unchecked@*/
extern int _tar_debug;
#endif

/*@unchecked@*/
extern int _rpmsq_debug;

/*@-exportheadervar@*/
/*@-redecl@*/
/*@unchecked@*/
extern int _av_debug;
/*@unchecked@*/
extern int _dav_debug;
/*@unchecked@*/
extern int _ftp_debug;
/*@unchecked@*/
extern int noLibio;
/*@unchecked@*/
extern int _rpmio_debug;
/*@unchecked@*/
extern int _xar_debug;
/*@=redecl@*/
/*@=exportheadervar@*/

/*@unchecked@*/ /*@null@*/
const char * rpmioPipeOutput = NULL;

/*@unchecked@*/
const char * rpmioRootDir = "/";

/*@observer@*/ /*@unchecked@*/
const char *rpmioEVR = VERSION;

/*@unchecked@*/
static int rpmioInitialized = -1;

#if defined(RPM_VENDOR_OPENPKG) /* support-rpmlua-option */
#ifdef WITH_LUA
/*@unchecked@*/
extern const char *rpmluaFiles;
#endif
#endif

#ifdef	NOTYET
#if defined(RPM_VENDOR_OPENPKG) /* support-rpmpopt-option */
/*@unchecked@*/
static char *rpmpoptfiles = RPMPOPTFILES;
#endif
#endif

pgpHashAlgo rpmioDigestHashAlgo = -1;

/**
 * Digest options using popt.
 */
struct poptOption rpmioDigestPoptTable[] = {
 { "md2", '\0', POPT_ARG_VAL, 	&rpmioDigestHashAlgo, PGPHASHALGO_MD2,
	N_("MD2 digest (RFC-1319)"), NULL },
 { "md4", '\0', POPT_ARG_VAL, 	&rpmioDigestHashAlgo, PGPHASHALGO_MD4,
	N_("MD4 digest"), NULL },
 { "md5", '\0', POPT_ARG_VAL, 	&rpmioDigestHashAlgo, PGPHASHALGO_MD5,
	N_("MD5 digest (RFC-1321)"), NULL },
 { "sha1",'\0', POPT_ARG_VAL, 	&rpmioDigestHashAlgo, PGPHASHALGO_SHA1,
	N_("SHA-1 digest (FIPS-180-1)"), NULL },
 { "sha224",'\0', POPT_ARG_VAL, &rpmioDigestHashAlgo, PGPHASHALGO_SHA224,
	N_("SHA-224 digest (FIPS-180-2)"), NULL },
 { "sha256",'\0', POPT_ARG_VAL, &rpmioDigestHashAlgo, PGPHASHALGO_SHA256,
	N_("SHA-256 digest (FIPS-180-2)"), NULL },
 { "sha384",'\0', POPT_ARG_VAL, &rpmioDigestHashAlgo, PGPHASHALGO_SHA384,
	N_("SHA-384 digest (FIPS-180-2)"), NULL },
 { "sha512",'\0', POPT_ARG_VAL, &rpmioDigestHashAlgo, PGPHASHALGO_SHA512,
	N_("SHA-512 digest (FIPS-180-2)"), NULL },
 { "salsa10",'\0', POPT_ARG_VAL,&rpmioDigestHashAlgo, PGPHASHALGO_SALSA10,
	N_("SALSA-10 hash"), NULL },
 { "salsa20",'\0', POPT_ARG_VAL,&rpmioDigestHashAlgo, PGPHASHALGO_SALSA20,
	N_("SALSA-20 hash"), NULL },
 { "rmd128",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_RIPEMD128,
	N_("RIPEMD-128 digest"), NULL },
 { "rmd160",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_RIPEMD160,
	N_("RIPEMD-160 digest"), NULL },
 { "rmd256",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_RIPEMD256,
	N_("RIPEMD-256 digest"), NULL },
 { "rmd320",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_RIPEMD320,
	N_("RIPEMD-320 digest"), NULL },
 { "tiger",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_TIGER192,
	N_("TIGER digest"), NULL },
 { "crc32",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_CRC32,
	N_("CRC-32 checksum"), NULL },
 { "crc64",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_CRC64,
	N_("CRC-64 checksum"), NULL },
 { "adler32",'\0', POPT_ARG_VAL,&rpmioDigestHashAlgo, PGPHASHALGO_ADLER32,
	N_("ADLER-32 checksum"), NULL },
 { "jlu32",'\0', POPT_ARG_VAL,	&rpmioDigestHashAlgo, PGPHASHALGO_JLU32,
	N_("lookup3 hash"), NULL },
 { "none",'\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &rpmioDigestHashAlgo, PGPHASHALGO_NONE,
	N_("no hash algorithm"), NULL },
 { "all",'\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &rpmioDigestHashAlgo, 256,
	N_("all hash algorithm(s)"), NULL },
    POPT_TABLEEND
};

/**
 * Display rpm version.
 */
static void printVersion(FILE * fp)
	/*@globals rpmioEVR, fileSystem @*/
	/*@modifies *fp, fileSystem @*/
{
    fprintf(fp, _("%s (" RPM_NAME ") %s\n"), __progname, rpmioEVR);
}

void rpmioConfigured(void)
	/*@globals rpmioInitialized @*/
	/*@modifies rpmioInitialized @*/
{

    if (rpmioInitialized < 0) {
	/* XXX TODO: add initialization side-effects. */
	rpmioInitialized = 0;
    }
    if (rpmioInitialized)
	exit(EXIT_FAILURE);
}

/**
 */
static void rpmioAllArgCallback(poptContext con,
                /*@unused@*/ enum poptCallbackReason reason,
                const struct poptOption * opt, const char * arg,
                /*@unused@*/ const void * data)
	/*@globals pgpImplVecs,
 		rpmCLIMacroContext, rpmGlobalMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies con, pgpImplVecs,
 		rpmCLIMacroContext, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{

    /* XXX avoid accidental collisions with POPT_BIT_SET for flags */
    if (opt->arg == NULL)
    switch (opt->val) {
    case 'q':
	rpmSetVerbosity(RPMLOG_WARNING);
	break;
    case 'v':
	rpmIncreaseVerbosity();
	break;
    case 'D':
    {	char *s, *t;
	/* XXX Convert '-' in macro name to underscore, skip leading %. */
	s = t = xstrdup(arg);
	while (*t && !xisspace((int)*t)) {
	    if (*t == '-') *t = '_';
	    t++;
	}
	t = s;
	if (*t == '%') t++;
	rpmioConfigured();
/*@-type@*/
	/* XXX adding macro to global context isn't Right Thing Todo. */
	(void) rpmDefineMacro(NULL, t, RMIL_CMDLINE);
	(void) rpmDefineMacro(rpmCLIMacroContext, t, RMIL_CMDLINE);
/*@=type@*/
	s = _free(s);
    }	break;
    case POPT_UNDEFINE:
    {	char *s, *t;
	/* XXX Convert '-' in macro name to underscore, skip leading %. */
	s = t = xstrdup(arg);
	while (*t && !xisspace((int)*t)) {
	    if (*t == '-') *t = '_';
	    t++;
	}
	t = s;
	if (*t == '%') t++;
/*@-type@*/
	rpmioConfigured();
	(void) rpmUndefineMacro(NULL, t);
	(void) rpmUndefineMacro(rpmCLIMacroContext, t);
/*@=type@*/
	s = _free(s);
    }	break;
    case POPT_CRYPTO:
	rpmioConfigured();
	{   const char *val = rpmExpand(arg, NULL);
#if defined(WITH_BEECRYPT)
	    if (!xstrcasecmp(val, "beecrypt") || !xstrcasecmp(val, "bc"))
		pgpImplVecs = &rpmbcImplVecs;
#endif
#if defined(WITH_GCRYPT)
	    if (!xstrcasecmp(val, "gcrypt") || !xstrcasecmp(val, "gc"))
		pgpImplVecs = &rpmgcImplVecs;
#endif
#if defined(WITH_NSS)
	    if (!xstrcasecmp(val, "NSS"))
		pgpImplVecs = &rpmnssImplVecs;
#endif
#if defined(WITH_SSL)
	    if (!xstrcasecmp(val, "OpenSSL") || !xstrcasecmp(val, "ssl"))
		pgpImplVecs = &rpmsslImplVecs;
#endif
	    val = _free(val);
	}
	break;
    case 'E':
	rpmioConfigured();
	{   const char *val = rpmExpand(arg, NULL);
#if defined(RPM_VENDOR_OPENPKG) /* no-extra-terminating-newline-on-eval */
            size_t val_len;
            val_len = strlen(val);
            if (val[val_len - 1] == '\n')
                fwrite(val, val_len, 1, stdout);
            else
#endif
	    fprintf(stdout, "%s\n", val);
	    val = _free(val);
	}
	break;
    case POPT_SHOWVERSION:
	printVersion(stdout);
/*@i@*/	con = rpmioFini(con);
	exit(EXIT_SUCCESS);
	/*@notreached@*/ break;
    }
}

/*@unchecked@*/
int rpmioFtsOpts = 0;

/*@unchecked@*/
struct poptOption rpmioFtsPoptTable[] = {
 { "comfollow", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_COMFOLLOW,
	N_("FTS_COMFOLLOW: follow command line symlinks"), NULL },
 { "logical", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_LOGICAL,
	N_("FTS_LOGICAL: logical walk"), NULL },
 { "nochdir", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_NOCHDIR,
	N_("FTS_NOCHDIR: don't change directories"), NULL },
 { "nostat", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_NOSTAT,
	N_("FTS_NOSTAT: don't get stat info"), NULL },
 { "physical", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_PHYSICAL,
	N_("FTS_PHYSICAL: physical walk"), NULL },
 { "seedot", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_SEEDOT,
	N_("FTS_SEEDOT: return dot and dot-dot"), NULL },
 { "xdev", '\0', POPT_BIT_SET,		&rpmioFtsOpts, FTS_XDEV,
	N_("FTS_XDEV: don't cross devices"), NULL },
 { "whiteout", '\0', POPT_BIT_SET,	&rpmioFtsOpts, FTS_WHITEOUT,
	N_("FTS_WHITEOUT: return whiteout information"), NULL },
   POPT_TABLEEND
};

/*@-bitwisesigned -compmempass @*/
/*@unchecked@*/
struct poptOption rpmioAllPoptTable[] = {
/*@-type@*/ /* FIX: cast? */
 { NULL, '\0', POPT_ARG_CALLBACK | POPT_CBFLAG_INC_DATA | POPT_CBFLAG_CONTINUE,
        rpmioAllArgCallback, 0, NULL, NULL },
/*@=type@*/

 { "debug", 'd', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &__debug, -1,
        NULL, NULL },

 { "define", 'D', POPT_ARG_STRING, NULL, (int)'D',
	N_("define MACRO with value EXPR"),
	N_("'MACRO EXPR'") },
 { "undefine", '\0', POPT_ARG_STRING, NULL, POPT_UNDEFINE,
	N_("undefine MACRO"),
	N_("'MACRO'") },
 { "eval", 'E', POPT_ARG_STRING, NULL, (int)'E',
	N_("print macro expansion of EXPR"),
	N_("'EXPR'") },

#ifdef	NOTYET
 { "macros", '\0', POPT_ARG_STRING, &rpmMacrofiles, 0,
	N_("read <FILE:...> instead of default file(s)"),
	N_("<FILE:...>") },
#if defined(RPM_VENDOR_OPENPKG) /* support-rpmlua-option */
#ifdef WITH_LUA
 { "rpmlua", '\0', POPT_ARG_STRING, &rpmluaFiles, 0,
	N_("read <FILE:...> instead of default RPM Lua file(s)"),
	N_("<FILE:...>") },
#endif
#endif
#if defined(RPM_VENDOR_OPENPKG) /* support-rpmpopt-option */
 { "rpmpopt", '\0', POPT_ARG_STRING, NULL, 0,
	N_("read <FILE:...> instead of default POPT file(s)"),
	N_("<FILE:...>") },
#endif
#endif	/* NOTYET */

#if defined(HAVE_LIBIO_H) && defined(_G_IO_IO_FILE_VERSION)
 { "nolibio", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &noLibio, 1,
	N_("disable use of libio(3) API"), NULL},
#endif

 { "pipe", '\0', POPT_ARG_STRING|POPT_ARGFLAG_DOC_HIDDEN, &rpmioPipeOutput, 0,
	N_("send stdout to CMD"),
	N_("CMD") },
 { "root", 'r', POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &rpmioRootDir, 0,
	N_("use ROOT as top level directory"),
	N_("ROOT") },

 { "quiet", '\0', 0, NULL, (int)'q',
	N_("provide less detailed output"), NULL},
 { "verbose", 'v', 0, NULL, (int)'v',
	N_("provide more detailed output"), NULL},
 { "version", '\0', 0, NULL, POPT_SHOWVERSION,
	N_("print the version"), NULL },

#if defined(HAVE_LIBIO_H) && defined(_G_IO_IO_FILE_VERSION)
 { "nolibio", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &noLibio, 1,
       N_("disable use of libio(3) API"), NULL},
#endif

 { "usecrypto",'\0', POPT_ARG_STRING|POPT_ARGFLAG_DOC_HIDDEN, NULL, POPT_CRYPTO,
        N_("select cryptography implementation"),
	N_("CRYPTO") },

 { "avdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_av_debug, -1,
	N_("debug argv collections"), NULL},
#ifdef	NOTYET
 { "cpiodebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_cpio_debug, -1,
	N_("debug cpio payloads"), NULL},
#endif
 { "davdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_dav_debug, -1,
	N_("debug WebDAV data stream"), NULL},
 { "ftpdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_ftp_debug, -1,
	N_("debug FTP/HTTP data stream"), NULL},
 { "miredebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_mire_debug, -1,
	NULL, NULL},
#ifdef	DYING
 { "poptdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_popt_debug, -1,
	N_("debug option/argument processing"), NULL},
#endif
 { "rpmiodebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_rpmio_debug, -1,
	N_("debug rpmio I/O"), NULL},
 { "rpmmgdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_rpmmg_debug, -1,
	NULL, NULL},
 { "rpmsqdebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_rpmsq_debug, -1,
	NULL, NULL},
 { "xardebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_xar_debug, -1,
	NULL, NULL},
#ifdef	NOTYET
 { "tardebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_tar_debug, -1,
	N_("debug tar payloads"), NULL},
#endif
 { "stats", '\0', POPT_ARG_VAL,				&_rpmsw_stats, -1,
	N_("display operation statistics"), NULL},
 { "urldebug", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &_url_debug, -1,
	N_("debug URL cache handling"), NULL},

   POPT_TABLEEND
};
/*@=bitwisesigned =compmempass @*/

poptContext
rpmioFini(poptContext optCon)
{
    /* XXX this should be done in the rpmioClean() wrapper. */
    /* keeps memory leak checkers quiet */
    rpmFreeMacros(NULL);
/*@i@*/	rpmFreeMacros(rpmCLIMacroContext);

    rpmioClean();

    optCon = poptFreeContext(optCon);

#if defined(HAVE_MCHECK_H) && defined(HAVE_MTRACE)
    /*@-noeffect@*/
    muntrace();   /* Trace malloc only if MALLOC_TRACE=mtrace-output-file. */
    /*@=noeffect@*/
#endif

    return NULL;
}

static inline int checkfd(const char * devnull, int fdno, int flags)
{
    struct stat sb;
    int ret = 0;

    if (fstat(fdno, &sb) == -1 && errno == EBADF)
	ret = (open(devnull, flags) == fdno) ? 1 : 2;
    return ret;
}

/*@-globstate@*/
poptContext
rpmioInit(int argc, char *const argv[], struct poptOption * optionsTable)
{
    poptContext optCon;
#ifdef	NOTYET
    char *path_buf, *path, *path_next;
#endif
    int rc;
#ifdef	NOTYET
#if defined(RPM_VENDOR_OPENPKG) /* support-rpmpopt-option */
    int i;
#endif
#endif

#if defined(HAVE_MCHECK_H) && defined(HAVE_MTRACE)
    /*@-noeffect@*/
    mtrace();   /* Trace malloc only if MALLOC_TRACE=mtrace-output-file. */
    /*@=noeffect@*/
#endif
/*@-globs -mods@*/
    setprogname(argv[0]);       /* Retrofit glibc __progname */

    /* XXX glibc churn sanity */
    if (__progname == NULL) {
	if ((__progname = strrchr(argv[0], '/')) != NULL) __progname++;
	else __progname = argv[0];
    }
/*@=globs =mods@*/

    /* Insure that stdin/stdout/stderr are open, lest stderr end up in rpmdb. */
   {	static const char _devnull[] = "/dev/null";
#if defined(STDIN_FILENO)
	(void) checkfd(_devnull, STDIN_FILENO, O_RDONLY);
#endif
#if defined(STDOUT_FILENO)
	(void) checkfd(_devnull, STDOUT_FILENO, O_WRONLY);
#endif
#if defined(STDERR_FILENO)
	(void) checkfd(_devnull, STDERR_FILENO, O_WRONLY);
#endif
   }

#if defined(ENABLE_NLS) && !defined(__LCLINT__)
    (void) setlocale(LC_ALL, "" );
    (void) bindtextdomain(PACKAGE, LOCALEDIR);
    (void) textdomain(PACKAGE);
#endif

    rpmSetVerbosity(RPMLOG_NOTICE);

    if (optionsTable == NULL) {
	/* Read rpm configuration (if not already read). */
	rpmioConfigured();
	return NULL;
    }

/*@-nullpass -temptrans@*/
    optCon = poptGetContext(__progname, argc, (const char **)argv, optionsTable, 0);
/*@=nullpass =temptrans@*/

#ifdef	NOTYET
    /* read all RPM POPT configuration files */
#if defined(RPM_VENDOR_OPENPKG) /* support-rpmpopt-option */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rpmpopt") == 0 && i+1 < argc) {
            rpmpoptfiles = argv[i+1];
            break;
        }
        else if (strncmp(argv[i], "--rpmpopt=", 10) == 0) {
            rpmpoptfiles = argv[i]+10;
            break;
        }
    }
    path_buf = xstrdup(rpmpoptfiles);
#else
    path_buf = xstrdup(RPMPOPTFILES);
#endif
    for (path = path_buf; path != NULL && *path != '\0'; path = path_next) {
        const char **av;
        int ac, i;

        /* locate start of next path element */
        path_next = strchr(path, ':');
        if (path_next != NULL && *path_next == ':')
            *path_next++ = '\0';
        else
            path_next = path + strlen(path);

        /* glob-expand the path element */
        ac = 0;
        av = NULL;
        if ((i = rpmGlob(path, &ac, &av)) != 0)
            continue;

        /* work-off each resulting file from the path element */
        for (i = 0; i < ac; i++) {
            const char *fn = av[i];
#if defined(RPM_VENDOR_OPENPKG) /* security-sanity-check-rpmpopt-and-rpmmacros */
            if (fn[0] == '@' /* attention */) {
                fn++;
                if (!rpmSecuritySaneFile(fn)) {
                    rpmlog(RPMLOG_WARNING, "existing POPT configuration file \"%s\" considered INSECURE -- not loaded\n", fn);
                    continue;
                }
            }
#endif
            (void)poptReadConfigFile(optCon, fn);
            av[i] = _free(av[i]);
        }
        av = _free(av);
    }
    path_buf = _free(path_buf);

    /* read standard POPT configuration files */
    (void) poptReadDefaultConfig(optCon, 1);

    poptSetExecPath(optCon, USRLIBRPM, 1);
#endif	/* NOTYET */

    /* Process all options, whine if unknown. */
    while ((rc = poptGetNextOpt(optCon)) > 0) {
	const char * optArg = poptGetOptArg(optCon);
/*@-dependenttrans -modobserver -observertrans @*/
	optArg = _free(optArg);
/*@=dependenttrans =modobserver =observertrans @*/
	switch (rc) {
	default:
/*@-nullpass@*/
	    fprintf(stderr, _("%s: option table misconfigured (%d)\n"),
		__progname, rc);
/*@=nullpass@*/
	    exit(EXIT_FAILURE);

	    /*@notreached@*/ /*@switchbreak@*/ break;
        }
    }

    if (rc < -1) {
/*@-nullpass@*/
	fprintf(stderr, "%s: %s: %s\n", __progname,
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
/*@=nullpass@*/
	exit(EXIT_FAILURE);
    }

    /* Read rpm configuration (if not already read). */
    rpmioConfigured();

    if (__debug) {
	rpmIncreaseVerbosity();
	rpmIncreaseVerbosity();
    }

    return optCon;
}
/*@=globstate@*/

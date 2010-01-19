#include "system.h"

#include <rpmio.h>
#include <rpmlog.h>
#include <rpmcb.h>
#include <argv.h>

#define	_RPMJS_INTERNAL
#include <rpmjs.h>

#include <rpmcli.h>

#include "debug.h"

/*@unchecked@*/
static int _debug = 0;

/*@unchecked@*/
static int _loglvl = 0;

/*@unchecked@*/
static int _test = 1;

typedef struct rpmjsClassTable_s {
/*@observer@*/
    const char *name;
    int ix;
} * rpmjsClassTable;

/*@unchecked@*/ /*@observer@*/
static struct rpmjsClassTable_s classTable[] = {
    { "Aug",		-25 },
    { "Bc",		-40 },	/* todo++ */
    { "Bf",		-26 },

    { "Cudf",		-50 },	/* todo++ */

    { "Db",		-45 },
    { "Dbc",		-46 },
    { "Dbe",		-44 },
    { "Mpf",		-48 },
    { "Seq",		-49 },
    { "Txn",		-47 },

    { "Dc",		-28 },
    { "Dig",		-37 },	/* todo++ */
    { "Dir",		-29 },
    { "Ds",		-13 },
    { "Fc",		-34 },	/* todo++ */
    { "Fi",		-14 },
    { "Fts",		-30 },
    { "Gi",		-35 },	/* todo++ */
    { "Hdr",		-12 },
    { "Io",		-32 },
    { "Iob",		-36 },	/* todo++ */
    { "Mc",		-24 },
    { "Mg",		-31 },
    { "Mi",		-11 },
    { "Mpw",		-41 },
    { "Ps",		-16 },
    { "Sm",		-43 },	/* todo++ */
    { "Sp",		-42 },	/* todo++ */
    { "St",		27 },
    { "Sw",		-38 },	/* todo++ */
    { "Sx",		-39 },
#if defined(WITH_SYCK)
    { "Syck",		 -3 },	/* todo++ */
#endif
    { "Sys",		33 },
    { "Te",		-15 },
    { "Ts",		-10 },
    { "Xar",		-51 },	/* todo++ */
#if defined(WITH_UUID)
    { "Uuid",		 -2 },
#endif
};

/*@unchecked@*/
static size_t nclassTable = sizeof(classTable) / sizeof(classTable[0]);

/*@unchecked@*/
static const char tscripts[] = "./tscripts/";

/*@unchecked@*/
static const char * _acknack = "\
function ack(cmd, expected) {\n\
  try {\n\
    actual = eval(cmd);\n\
  } catch(e) {\n\
    print(\"NACK:  ack(\"+cmd+\")\tcaught '\"+e+\"'\");\n\
    return;\n\
  }\n\
  if (actual != expected && expected != undefined)\n\
    print(\"NACK:  ack(\"+cmd+\")\tgot '\"+actual+\"' not '\"+expected+\"'\");\n\
  else if (loglvl)\n\
    print(\"       ack(\"+cmd+\")\tgot '\"+actual+\"'\");\n\
}\n\
function nack(cmd, expected) {\n\
  try {\n\
    actual = eval(cmd);\n\
  } catch(e) {\n\
    print(\" ACK: nack(\"+cmd+\")\tcaught '\"+e+\"'\");\n\
    return;\n\
  }\n\
  if (actual == expected)\n\
    print(\" ACK: nack(\"+cmd+\")\tgot '\"+actual+\"' not '\"+expected+\"'\");\n\
  else if (loglvl)\n\
    print(\"      nack(\"+cmd+\")\tgot '\"+actual+\"'\");\n\
}\n\
";

static rpmRC
rpmjsLoadFile(const char * pre, const char * fn, int bingo)
{
    rpmjs js = _rpmjsI;
    rpmRC ret = RPMRC_FAIL;
    const char * result = NULL;
    char * str;

    if (bingo || pre == NULL) pre = "";

    str = rpmExpand(pre, "\ntry { require('system').include('", fn, "'); } catch(e) { print(e); print(e.stack); throw e; }", NULL);
if (_debug)
fprintf(stderr, "\trunning:\n%s\n", str);

    result = NULL;
    ret = rpmjsRun(NULL, str, &result);

    if (result != NULL && *result != '\0') {
	fprintf(stdout, "%s\n", result);
	fflush(stdout);
    }
    str = _free(str);

    return ret;
}

static void
rpmjsLoadClasses(void)
{
    const char * pre = NULL;
    int * order = NULL;
    size_t norder = 64;
    rpmjsClassTable tbl;
    rpmjs js;
    const char * result;
    int ix;
    size_t i;
    int bingo = 0;

    i = norder * sizeof(*order);
    order = memset(alloca(i), 0, i);

    /* Inject _debug and _loglvl into the interpreter context. */
    {	char dstr[32];
	char lstr[32];
	sprintf(dstr, "%d", _debug);
	sprintf(lstr, "%d", _loglvl);
	pre = rpmExpand("var debug = ", dstr, ";\n"
			"var loglvl = ", lstr, ";\n",
			_acknack, NULL);
    }

    /* Load requested classes and initialize the test order. */
    /* XXX FIXME: resultp != NULL to actually execute?!? */
    (void) rpmjsRun(NULL, "print(\"loading RPM classes.\");", &result);
    js = _rpmjsI;

    for (i = 0, tbl = classTable; i < nclassTable; i++, tbl++) {
	if (tbl->ix <= 0)
	    continue;
	order[tbl->ix & (norder - 1)] = i + 1;
    }

    /* Test requested classes in order. */
    for (i = 0; i < norder; i++) {
	const char * fn;
	struct stat sb;

	if (order[i] <= 0)
	    continue;
	ix = order[i] - 1;
	tbl = &classTable[ix];
	fn = rpmGetPath(tscripts, "/", tbl->name, ".js", NULL);
	if (Stat(fn, &sb) == 0) {
	    (void) rpmjsLoadFile(pre, fn, bingo);
	    bingo = 1;
	}
	fn = _free(fn);
    }

    pre = _free(pre);
    return;
}

static struct poptOption optionsTable[] = {
 { "debug", 'd', POPT_ARG_VAL,	&_debug, -1,		NULL, NULL },
 { "test", 't', POPT_ARG_VAL,	&_test, -1,		NULL, NULL },
 { "zeal", 'z', POPT_ARG_INT,	&_rpmjs_zeal, 0,	NULL, NULL },

 { NULL, '\0', POPT_ARG_INCLUDE_TABLE, rpmcliAllPoptTable, 0,
	N_("Common options for all rpm executables:"), NULL },

  POPT_AUTOHELP
  POPT_TABLEEND
};

int
main(int argc, char *argv[])
{
    poptContext optCon;
    ARGV_t av = poptGetArgs(optCon);
    int ac = argvCount(av);
    const char * fn;
    int rc = 1;		/* assume failure */

_rpmjs_zeal = 2;
    optCon = rpmcliInit(argc, argv, optionsTable);
    if (!_test && ac < 1) {
	poptPrintUsage(optCon, stderr, 0);
	goto exit;
    }

if (_debug) {
rpmSetVerbosity(RPMLOG_DEBUG);
} else {
rpmSetVerbosity(RPMLOG_NOTICE);
}

_rpmjs_debug = 0;
    if (_debug && !_loglvl) _loglvl = 1;
    rpmjsLoadClasses();
_rpmjs_debug = 1;

    if (av != NULL)
    while ((fn = *av++) != NULL) {
	rpmRC ret = rpmjsLoadFile(NULL, fn, 1);
	if (ret != RPMRC_OK)
	    goto exit;
    }

    rc = 0;

exit:
_rpmjs_debug = 0;
    optCon = rpmcliFini(optCon);

    return rc;
}

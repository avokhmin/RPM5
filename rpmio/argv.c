/** \ingroup rpmio
 * \file rpmio/argv.c
 */

#include "system.h"

#include "rpmio_internal.h"	/* XXX fdGetFILE() */
#include <argv.h>

#include "debug.h"

/*@-bounds@*/

void argvPrint(const char * msg, ARGV_t argv, FILE * fp)
{
    ARGV_t av;

    if (fp == NULL) fp = stderr;

    if (msg)
	fprintf(fp, "===================================== %s\n", msg);

    if (argv)
    for (av = argv; *av; av++)
	fprintf(fp, "%s\n", *av);

}

ARGI_t argiFree(ARGI_t argi)
{
    if (argi) {
	argi->nvals = 0;
	argi->vals = _free(argi->vals);
    }
    argi = _free(argi);
    return NULL;
}

ARGV_t argvFree(/*@only@*/ /*@null@*/ ARGV_t argv)
{
    ARGV_t av;
    
/*@-branchstate@*/
    if (argv)
    for (av = argv; *av; av++)
	*av = _free(*av);
/*@=branchstate@*/
    argv = _free(argv);
    return NULL;
}

int argiCount(ARGI_t argi)
{
    int nvals = 0;
    if (argi)
	nvals = argi->nvals;
    return nvals;
}

ARGint_t argiData(ARGI_t argi)
{
    ARGint_t vals = NULL;
    if (argi && argi->nvals > 0)
	vals = argi->vals;
    return vals;
}

int argvCount(const ARGV_t argv)
{
    int argc = 0;
    if (argv)
    while (argv[argc] != NULL)
	argc++;
    return argc;
}

ARGV_t argvData(ARGV_t argv)
{
/*@-retalias -temptrans @*/
    return argv;
/*@=retalias =temptrans @*/
}

int argvCmp(const void * a, const void * b)
{
/*@-boundsread@*/
    ARGstr_t astr = *(ARGV_t)a;
    ARGstr_t bstr = *(ARGV_t)b;
/*@=boundsread@*/
    return strcmp(astr, bstr);
}

int argvSort(ARGV_t argv, int (*compar)(const void *, const void *))
{
    if (compar == NULL)
	compar = argvCmp;
    qsort(argv, argvCount(argv), sizeof(*argv), compar);
    return 0;
}

ARGV_t argvSearch(ARGV_t argv, ARGstr_t val,
		int (*compar)(const void *, const void *))
{
    if (argv == NULL)
	return NULL;
    if (compar == NULL)
	compar = argvCmp;
    return bsearch(&val, argv, argvCount(argv), sizeof(*argv), compar);
}

int argiAdd(/*@out@*/ ARGI_t * argip, int ix, int val)
{
    ARGI_t argi;

    if (argip == NULL)
	return -1;
    if (*argip == NULL)
	*argip = xcalloc(1, sizeof(**argip));
    argi = *argip;
    if (ix < 0)
	ix = argi->nvals;
    if (ix >= argi->nvals) {
	argi->vals = xrealloc(argi->vals, (ix + 1) * sizeof(*argi->vals));
	memset(argi->vals + argi->nvals, 0,
		(ix - argi->nvals) * sizeof(*argi->vals));
	argi->nvals = ix + 1;
    }
    argi->vals[ix] = val;
    return 0;
}

int argvAdd(/*@out@*/ ARGV_t * argvp, ARGstr_t val)
{
    ARGV_t argv;
    int argc;

    if (argvp == NULL)
	return -1;
    argc = argvCount(*argvp);
/*@-unqualifiedtrans@*/
    *argvp = xrealloc(*argvp, (argc + 1 + 1) * sizeof(**argvp));
/*@=unqualifiedtrans@*/
    argv = *argvp;
    argv[argc++] = xstrdup(val);
    argv[argc  ] = NULL;
    return 0;
}

int argvAppend(/*@out@*/ ARGV_t * argvp, const ARGV_t av)
{
    ARGV_t argv = *argvp;
    int argc = argvCount(argv);
    int ac = argvCount(av);
    int i;

    argv = xrealloc(argv, (argc + ac + 1) * sizeof(*argv));
    for (i = 0; i < ac; i++)
	argv[argc + i] = xstrdup(av[i]);
    argv[argc + ac] = NULL;
    *argvp = argv;
    return 0;
}

int argvSplit(ARGV_t * argvp, const char * str, const char * seps)
{
    static char whitespace[] = " \f\n\r\t\v";
    char * dest = xmalloc(strlen(str) + 1);
    ARGV_t argv;
    int argc = 1;
    const char * s;
    char * t;
    int c;

    if (seps == NULL)
	seps = whitespace;

    for (argc = 1, s = str, t = dest; (c = *s); s++, t++) {
	if (strchr(seps, c)) {
	    argc++;
	    c = '\0';
	}
	*t = c;
    }
    *t = '\0';

    argv = xmalloc( (argc + 1) * sizeof(*argv));

    for (c = 0, s = dest; s < t; s+= strlen(s) + 1) {
	if (*s == '\0')
	    continue;
	argv[c] = xstrdup(s);
	c++;
    }
    argv[c] = NULL;
    *argvp = argv;
/*@-nullstate@*/
    return 0;
/*@=nullstate@*/
}

char * argvJoin(ARGV_t argv)
{
    size_t nb = 0;
    int argc;
    char *t, *te;

    for (argc = 0; argv[argc] != NULL; argc++) {
	if (argc != 0)
	    nb++;
	nb += strlen(argv[argc]);
    }
    nb++;

    te = t = xmalloc(nb);
    *te = '\0';
    for (argc = 0; argv[argc] != NULL; argc++) {
	if (argc != 0)
	    *te++ = ' ';
	te = stpcpy(te, argv[argc]);
    }
    *te = '\0';
    return t;
}

int argvFgets(ARGV_t * argvp, void * fd)
{
    FILE * fp = (fd ? fdGetFILE(fd) : stdin);
    ARGV_t av = NULL;
    char buf[BUFSIZ];
    char * b, * be;
    int rc = 0;

    if (fp == NULL)
	return -2;
    while (!rc && (b = fgets(buf, sizeof(buf), fp)) != NULL) {
	buf[sizeof(buf)-1] = '\0';
	be = b + strlen(buf);
	if (be > b) be--;
	while (strchr("\r\n", *be) != NULL)
	    *be-- = '\0';
	rc = argvAdd(&av, b);
    }

    if (!rc)
	rc = ferror(fp);
    if (!rc)
	rc = (feof(fp) ? 0 : 1);
    if (!rc && argvp)
	*argvp = av;
    else
	av = argvFree(av);
    
    return rc;
}

/*@=bounds@*/

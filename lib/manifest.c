/** \ingroup rpmcli
 * \file lib/manifest.c
 */

#include "system.h"

#include <rpmio_internal.h>
#include "stringbuf.h"
#include "manifest.h"
#include "misc.h"
#include "debug.h"

/*@access StringBuf @*/

/**
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param this		memory to free
 * @retval		NULL always
 */
static /*@null@*/ void * _free(/*@only@*/ /*@null@*/ const void * this) {
    if (this)   free((void *)this);
    return NULL;
}

char * rpmPermsString(int mode)
{
    char *perms = xstrdup("----------");
   
    if (S_ISDIR(mode)) 
	perms[0] = 'd';
    else if (S_ISLNK(mode))
	perms[0] = 'l';
    else if (S_ISFIFO(mode)) 
	perms[0] = 'p';
    else if (S_ISSOCK(mode)) 
	perms[0] = 's';
    else if (S_ISCHR(mode))
	perms[0] = 'c';
    else if (S_ISBLK(mode))
	perms[0] = 'b';

    /*@-unrecog@*/
    if (mode & S_IRUSR) perms[1] = 'r';
    if (mode & S_IWUSR) perms[2] = 'w';
    if (mode & S_IXUSR) perms[3] = 'x';
 
    if (mode & S_IRGRP) perms[4] = 'r';
    if (mode & S_IWGRP) perms[5] = 'w';
    if (mode & S_IXGRP) perms[6] = 'x';

    if (mode & S_IROTH) perms[7] = 'r';
    if (mode & S_IWOTH) perms[8] = 'w';
    if (mode & S_IXOTH) perms[9] = 'x';

    if (mode & S_ISUID)
	perms[3] = ((mode & S_IXUSR) ? 's' : 'S'); 

    if (mode & S_ISGID)
	perms[6] = ((mode & S_IXGRP) ? 's' : 'S'); 

    if (mode & S_ISVTX)
	perms[9] = ((mode & S_IXOTH) ? 't' : 'T');
    /*@=unrecog@*/

    return perms;
}

int rpmReadPackageManifest(FD_t fd, int * argcPtr, const char *** argvPtr)
{
    StringBuf sb = newStringBuf();
    char * s, *se;
    int ac = 0;
    const char ** av = NULL;
    int argc = (argcPtr ? *argcPtr : 0);
    const char ** argv = (argvPtr ? *argvPtr : NULL);
    int rc = 0;
    int i;

    while (1) {
	char line[BUFSIZ];

	/* Read next line. */
	s = fgets(line, sizeof(line) - 1, fdGetFp(fd));
	if (s == NULL) {
	    /* XXX Ferror check needed */
	    break;
	}

	/* Skip comments. */
	if ((se = strchr(s, '#')) != NULL) *se = '\0';

	/* Trim white space. */
	se = s + strlen(s);
	while (se > s && (se[-1] == '\n' || se[-1] == '\r'))
	    *(--se) = '\0';
	while (*s && strchr(" \f\n\r\t\v", *s) != NULL)
	    s++;
	if (*s == '\0') continue;

	/* Insure that file contains only ASCII */
	if (*s < 32) {
	    rc = 1;
	    goto exit;
	}

	/* Concatenate next line in buffer. */
	*se++ = ' ';
	*se = '\0';
	appendStringBuf(sb, s);
    }

    if (s == NULL)		/* XXX always true */
	s = getStringBuf(sb);

    if (!(s && *s)) {
	rc = 1;
	goto exit;
    }

    /* Glob manifest items. */
    rc = rpmGlob(s, &ac, &av);
    if (rc) goto exit;

    /* Find 1st existing unprocessed arg. */
    for (i = 0; i < argc; i++)
	if (argv && argv[i]) break;

    /* Concatenate existing unprocessed args after manifest contents. */
    if (argv && i < argc) {
	int nac = (ac - 1) + (argc - i - 1) + 1;
	const char ** nav = xcalloc(nac, sizeof(*nav));

	if (ac)
	    memcpy(nav, av, (ac - 1) * sizeof(*nav));
	if ((argc - i - 1) > 0)
	    memcpy(nav + (ac - 1), argv + i, (argc - i - 1) * sizeof(*nav));
	nav[nac - 1] = NULL;

	*argvPtr = argv = _free(argv);
	av = _free(av);
	av = nav;
	ac = nac;
    }

    /* Save new argc/argv list. */
    if (argvPtr)
	*argvPtr = av;
    if (argcPtr)
	*argcPtr = ac;

exit:
    if (argvPtr == NULL || (rc != 0 && av)) {
	for (i = 0; i < ac; i++)
	    av[i] = _free(av[i]);
	av = _free(av);
    }
    freeStringBuf(sb);
    return rc;
}

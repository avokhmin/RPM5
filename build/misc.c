#include "system.h"

#include "rpmbuild.h"

#include "popt/popt.h"

int parseNum(char *line, int *res)
{
    char *s1;
    
    s1 = NULL;
    *res = strtoul(line, &s1, 10);
    if ((*s1) || (s1 == line) || (*res == ULONG_MAX)) {
	return 1;
    }

    return 0;
}

char *cleanFileName(const char *name)
{
    static char res[BUFSIZ];
    char *copyTo, copied;
    const char *copyFrom;

    /* Copy to fileName, eliminate duplicate "/" and trailing "/" */
    copyTo = res;
    copied = '\0';
    copyFrom = name;
    while (*copyFrom) {
	if (*copyFrom != '/' || copied != '/') {
	    *copyTo++ = copied = *copyFrom;
	}
	copyFrom++;
    }
    *copyTo = '\0';
    copyTo--;
    if ((copyTo != res) && (*copyTo == '/')) {
	*copyTo = '\0';
    }

    return res;
}

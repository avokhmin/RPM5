#include "system.h"

#include "rpmlib.h"

int main(int argc, char ** argv)
{
    Header h;
    FD_t fdi, fdo;

    if (argc == 1) {
	fdi = fdDup(0);
    } else {
	fdi = fdOpen(argv[1], O_RDONLY, 0644);
    }

    if (fdFileno(fdi) < 0) {
	fprintf(stderr, _("cannot open %s: %s\n"), argv[1], strerror(errno));
	exit(1);
    }

    h = headerRead(fdi, HEADER_MAGIC_YES);
    if (!h) {
	fprintf(stderr, _("headerRead error: %s\n"), strerror(errno));
	exit(1);
    }
    fdClose(fdi);
  
    fdo = fdDup(1);
    headerDump(h, stdout, fdo, rpmTagTable);
    headerFree(h);

    return 0;
}

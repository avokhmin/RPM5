/* rpmheader: spit out the header portion of a package */

#include "system.h"

#include "rpmlead.h"
#include "signature.h"
#include "header.h"

int main(int argc, char **argv)
{
    FD_t fdi, fdo;
    struct rpmlead lead;
    Header hd;
    int rc;
    
    setprogname(argv[0]);	/* Retrofit glibc __progname */
    if (argc == 1) {
	fdi = fdDup(STDIN_FILENO);
    } else {
	fdi = Fopen(argv[1], "r.ufdio");
	if (Ferror(fdi)) {
	    perror(argv[1]);
	    exit(EXIT_FAILURE);
	}
    }

    readLead(fdi, &lead);
    rpmReadSignature(fdi, NULL, lead.signature_type);
    hd = headerRead(fdi, (lead.major >= 3) ?
		    HEADER_MAGIC_YES : HEADER_MAGIC_NO);
    fdo = fdDup(STDOUT_FILENO);
    rc = headerWrite(fdo, hd, HEADER_MAGIC_YES);
    
    return rc;
}

/* rpmlead: spit out the lead portion of a package */

#include "system.h"

#include "rpmio.h"
#include "rpmlead.h"

int main(int argc, char **argv)
{
    FD_t fdi, fdo;
    struct rpmlead lead;
    
    if (argc == 1) {
	fdi = fdDup(STDIN_FILENO);
    } else {
	fdi = fdOpen(argv[1], O_RDONLY, 0644);
    }

    readLead(fdi, &lead);
    fdo = fdDup(STDOUT_FILENO);
    writeLead(fdo, &lead);
    
    return 0;
}

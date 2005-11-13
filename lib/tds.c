#include "system.h"

#include <rpmlib.h>

#include <rpmds.h>

#include "debug.h"

extern int _rpmds_debug;

int main(int argc, char *argv[])
{
    rpmTag tagN = RPMTAG_REQUIRENAME;
    rpmds ds = NULL;
    rpmds dsA;
    rpmds dsA1;
    rpmds dsA2;
    rpmds dsB;
    rpmds dsC;
    int rc = 0;
    int xx;

    _rpmds_debug = 0;
    dsA = rpmdsSingle(tagN, "A", "0", RPMSENSE_EQUAL);
    dsA1 = rpmdsSingle(tagN, "A", "0", RPMSENSE_LESS);
    dsA2 = rpmdsSingle(tagN, "A", "0", RPMSENSE_GREATER);
    dsB = rpmdsSingle(tagN, "B", "1", RPMSENSE_LESS);
    dsC = rpmdsSingle(tagN, "C", "2", RPMSENSE_GREATER);

    rpmdsMerge(&ds, dsA2);
    rpmdsMerge(&ds, dsA);
    rpmdsMerge(&ds, dsC);
    rpmdsMerge(&ds, dsB);
    rpmdsMerge(&ds, dsA1);

    xx = rpmdsPrint(ds, NULL);

    ds = rpmdsFree(ds);

    return 0;
}

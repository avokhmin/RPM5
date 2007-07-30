/** \ingroup rpmio
 * \file rpmio/trpmio.c
 */

#include <stdio.h>
#include "rpmio.h"
#include "debug.h"

int main (void)
{
    FD_t f1, f2, f3, f4, f5;
 
    fprintf (stderr, "open http://www.gnome.org/\n");
    f1 = Fopen ("http://www.gnome.org/", "r");
 
    fprintf (stderr, "open http://people.redhat.com/\n");
    f2 = Fopen ("http://people.redhat.com/", "r");
 
  if (f1) {
    fprintf (stderr, "close http://www.gnome.org/\n");
    Fclose (f1);
  }
 
    fprintf (stderr, "open http://www.redhat.com/\n");
    f3 = Fopen ("http://www.redhat.com/", "r");
 
  if (f2) {
    fprintf (stderr, "close http://people.redhat.com/\n");
    Fclose (f2);
  }
 
    fprintf (stderr, "open http://www.wraptastic.org/\n");
    f4 = Fopen ("http://www.wraptastic.org/", "r");
 
  if (f3) {
    fprintf (stderr, "close http://people.redhat.com/\n");
    Fclose (f3);
  }
 
    fprintf (stderr, "open http://people.redhat.com/\n");
    f5 = Fopen ("http://people.redhat.com/", "r");
 
  if (f4) {
    fprintf (stderr, "close http://www.wraptastic.org/\n");
    Fclose (f4);
  }
 
  if (f5) {
    fprintf (stderr, "close http://people.redhat.com/\n");
    Fclose (f5);
  }
 
    return 0;
}

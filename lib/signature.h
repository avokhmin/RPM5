#ifndef H_SIGNATURE

/* signature.h - generate and verify signatures */

#include "header.h"

/**************************************************/
/*                                                */
/* Signature types                                */
/*                                                */
/* These are what goes in the Lead                */
/*                                                */
/**************************************************/

#define RPMSIG_NONE         0  /* Do not change! */
#define RPMSIG_BAD          2  /* Returned for unknown types */
/* The following types are no longer generated */
#define RPMSIG_PGP262_1024  1  /* No longer generated */
#define RPMSIG_MD5          3
#define RPMSIG_MD5_PGP      4

/* These are the new-style signatures.  They are Header structures.    */
/* Inside them we can put any number of any type of signature we like. */

#define RPMSIG_HEADERSIG    5  /* New Header style signature */

/**************************************************/
/*                                                */
/* Prototypes                                     */
/*                                                */
/**************************************************/

Header rpmNewSignature(void);

/* If an old-style signature is found, we emulate a new style one */
int rpmReadSignature(int fd, Header *header, short sig_type);
int rpmWriteSignature(int fd, Header header);

/* Generate a signature of data in file, insert in header */
int rpmAddSignature(Header header, char *file,
		    int_32 sigTag, char *passPhrase);

/******************************************************************/

/* Return type of signature in effect for building */
int rpmLookupSignatureType(void);

/* Utility to read a pass phrase from the user */
char *rpmGetPassPhrase(char *prompt);

#endif

/* RPM - Copyright (C) 1995 Red Hat Software
 * 
 * header.h - routines for managing rpm tagged structures
 */

/* WARNING: 1 means success, 0 means failure (yes, this is backwards) */

#ifndef H_HEADER
#define H_HEADER
#include <stdio.h>

#if defined(__alpha__)
typedef long int int_64;
typedef int int_32;
typedef short int int_16;
typedef char int_8;

typedef unsigned int uint_32;
typedef unsigned short uint_16;

#else

typedef long long int int_64;
typedef int int_32;
typedef short int int_16;
typedef char int_8;

typedef unsigned int uint_32;
typedef unsigned short uint_16;
#endif

typedef struct headerToken *Header;
typedef struct headerIteratorS *HeaderIterator;

struct headerTagTableEntry {
    char * name;
    int val;
};

enum headerSprintfExtenstionType { HEADER_EXT_LAST = 0, HEADER_EXT_FORMAT,
				   HEADER_EXT_MORE, HEADER_EXT_TAG };

/* This will only ever be passed RPM_TYPE_INT32 or RPM_TYPE_STRING to
   help keep things simple */
typedef char * (*headerTagFormatFunction)(int_32 type, const void * data, 
					  char * formatPrefix,
					  int padding, int element);
/* This is allowed to fail, which indicates the tag doesn't exist */
typedef int (*headerTagTagFunction)(Header h, int_32 * type, void ** data,
				       int_32 * count, int * freeData);

struct headerSprintfExtension {
    enum headerSprintfExtenstionType type;
    char * name;
    union {
	void * generic;
	headerTagFormatFunction formatFunction;
	headerTagTagFunction tagFunction;
	struct headerSprintfExtension * more;
    } u;
};

/* This defines some basic conversions all header users would probably like
   to have */
extern const struct headerSprintfExtension headerDefaultFormats[];

/* read and write a header from a file */
Header headerRead(int fd, int magicp);
void headerWrite(int fd, Header h, int magicp);
unsigned int headerSizeof(Header h, int magicp);

#define HEADER_MAGIC_NO   0
#define HEADER_MAGIC_YES  1

/* load and unload a header from a chunk of memory */
Header headerLoad(void *p);
void *headerUnload(Header h);

Header headerNew(void);
void headerFree(Header h);

/* dump a header to a file, in human readable format */
void headerDump(Header h, FILE *f, int flags, 
		const struct headerTagTableEntry * tags);

/* the returned string must be free()d */
char * headerSprintf(Header h, const char * fmt, 
		     const struct headerTagTableEntry * tags,
		     const struct headerSprintfExtension * extentions,
		     char ** error);

#define HEADER_DUMP_INLINE   1

/* Duplicate tags are okay, but only defined for iteration (with the 
   exceptions noted below). While you are allowed to add i18n string
   arrays through this function, you probably don't mean to. See
   headerAddI18NString() instead */
int headerAddEntry(Header h, int_32 tag, int_32 type, void *p, int_32 c);
/* if there are multiple entries with this tag, the first one gets replaced */
int headerModifyEntry(Header h, int_32 tag, int_32 type, void *p, int_32 c);

/* A NULL lang is interpreted as the C locale.  Here are the rules:
   
	1) If the tag isn't in the Header, it's added with the passed string
	   as a version.
	2) If the tag occurs multiple times in entry, which tag is affected
	   by the operation is undefined.
	2) If the tag is in the header w/ this language, the entry is
	   *replaced* (like headerModifyEntry()).

   This function is intended to just "do the right thing". If you need
   more fine grained control use headerAddEntry() and headerModifyEntry()
   but be careful!
*/
int headerAddI18NString(Header h, int_32 tag, char * string, char * lang);

/* Appends item p to entry w/ tag and type as passed. Won't work on
   RPM_STRING_TYPE. Any pointers from headerGetEntry() for this entry
   are invalid after this call has been made! */
int headerAppendEntry(Header h, int_32 tag, int_32 type, void * p, int_32 c);
int headerAddOrAppendEntry(Header h, int_32 tag, int_32 type,
			   void * p, int_32 c);

/* Will never return RPM_I18NSTRING_TYPE! RPM_STRING_TYPE elements w/
   RPM_I18NSTRING_TYPE equivalent enreies are translated (if HEADER_I18NTABLE
   entry is present). */
int headerGetEntry(Header h, int_32 tag, int_32 *type, void **p, int_32 *c);

/* If *type is RPM_NULL_TYPE any type will match, otherwise only *type will
   match. */
int headerGetRawEntry(Header h, int_32 tag, int_32 *type, void **p, int_32 *c);

int headerIsEntry(Header h, int_32 tag);
/* removes all entries of type tag from the header, returns 1 if none were
   found */
int headerRemoveEntry(Header h, int_32 tag);

HeaderIterator headerInitIterator(Header h);
int headerNextIterator(HeaderIterator iter,
		       int_32 *tag, int_32 *type, void **p, int_32 *c);
void headerFreeIterator(HeaderIterator iter);

/* reexamines LANGUAGE and LANG settings to set a new language for the
   header; this only needs to be called if LANGUAGE or LANG may have changed
   since the first headerGetEntry() on the header */
void headerResetLang(Header h);
/* sets the language path for the header; this doesn't need to be done if
   the LANGUAGE or LANG enivronment variable are correct */
void headerSetLangPath(Header h, char * lang);

Header headerCopy(Header h);
void headerSort(Header h);

/* Entry Types */

#define RPM_NULL_TYPE		0
#define RPM_CHAR_TYPE		1
#define RPM_INT8_TYPE		2
#define RPM_INT16_TYPE		3
#define RPM_INT32_TYPE		4
/* #define RPM_INT64_TYPE	5   ---- These aren't supported (yet) */
#define RPM_STRING_TYPE		6
#define RPM_BIN_TYPE		7
#define RPM_STRING_ARRAY_TYPE	8
#define RPM_I18NSTRING_TYPE	9

/* Tags -- general use tags should start at 1000 (RPM's tag space starts 
   there) */

#define HEADER_I18NTABLE	100

#endif H_HEADER

#ifndef H_MIRE
#define H_MIRE

/** \ingroup rpmtrans
 * \file rpmio/mire.h
 * RPM pattern matching.
 */

/*@-noparams@*/
#include <fnmatch.h>
/*@=noparams@*/
#if defined(__LCLINT__)
/*@-declundef -exportheader -redecl @*/ /* LCL: missing annotation */
extern int fnmatch (const char *__pattern, const char *__name, int __flags)
	/*@*/;
/*@=declundef =exportheader =redecl @*/
#endif

#if defined(WITH_PCRE) && defined(HAVE_PCREPOSIX_H)
#include <pcreposix.h>
#else
#include <regex.h>
#endif

#if defined(__LCLINT__)
/*@-declundef -exportheader @*/ /* LCL: missing modifies (only is bogus) */
extern void regfree (/*@only@*/ regex_t *preg)
	/*@modifies *preg @*/;
/*@=declundef =exportheader @*/
#endif

/**
 */
/*@-exportlocal@*/
/*@unchecked@*/
extern int _mire_debug;
/*@=exportlocal@*/

/**
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct miRE_s * miRE;

/**
 * Tag value pattern match mode.
 */
typedef enum rpmMireMode_e {
    RPMMIRE_DEFAULT	= 0,	/*!< regex with \., .* and ^...$ added */
    RPMMIRE_STRCMP	= 1,	/*!< strings  using strcmp(3) */
    RPMMIRE_REGEX	= 2,	/*!< regex(7) patterns through regcomp(3) */
    RPMMIRE_GLOB	= 3	/*!< glob(7) patterns through fnmatch(3) */
} rpmMireMode;

#if defined(_MIRE_INTERNAL)
/**
 */
struct miRE_s {
    rpmMireMode	mode;		/*!< pattern match mode */
/*@only@*/ /*@relnull@*/
    const char *pattern;	/*!< pattern string */
/*@only@*/ /*@relnull@*/
    regex_t *preg;		/*!< regex compiled pattern buffer */
    int	fnflags;	/*!< fnmatch(3) flags (0 uses FNM_PATHNAME|FNM_PERIOD)*/
    int	cflags;		/*!< regcomp(3) flags (0 uses REG_EXTENDED|REG_NOSUB) */
    int	eflags;		/*!< regexec(3) flags */
    int notmatch;		/*!< non-zero: negative match, like "grep -v" */
    int tag;			/*!< sort identifier (e.g. an rpmTag) */
    int nrefs;			/*!< Reference count. */
};
#endif	/* defined(_MIRE_INTERNAL) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deallocate pattern match memory.
 * @param mire		pattern container
 * @return		0 on success
 */
int mireClean(miRE mire)
	/*@modifies mire @*/;

/*@-exportlocal@*/
/*@null@*/
miRE XmireUnlink (/*@killref@*/ /*@only@*/ /*@null@*/ miRE mire,
		/*@null@*/ const char * msg, const char * fn, unsigned ln)
	/*@modifies mire @*/;
/*@=exportlocal@*/
#define	mireUnlink(_mire, _msg)	XmireUnlink(_mire, _msg, __FILE__, __LINE__)

/**
 * Reference a pattern container instance.
 * @param mire		pattern container
 * @param msg
 * @return		new pattern container reference
 */
/*@unused@*/ /*@newref@*/ /*@null@*/
miRE mireLink (/*@null@*/ miRE mire, /*@null@*/ const char * msg)
	/*@modifies mire @*/;

/** @todo Remove debugging entry from the ABI. */
/*@newref@*/ /*@null@*/
miRE XmireLink (/*@null@*/ miRE mire, /*@null@*/ const char * msg,
		const char * fn, unsigned ln)
        /*@modifies mire @*/;
#define	mireLink(_mire, _msg)	XmireLink(_mire, _msg, __FILE__, __LINE__)

/**
 * Free pattern container.
 * @param mire		pattern container
 * @return		NULL always
 */
/*@null@*/
miRE mireFree(/*@killref@*/ /*@only@*/ /*@null@*/ miRE mire)
	/*@modifies mire @*/;

/**
 * Create pattern container.
 * @param mode		type of pattern match
 * @param tag		identifier (e.g. an rpmTag)
 * @return		NULL always
 */
miRE mireNew(rpmMireMode mode, int tag)
	/*@*/;

/**
 * Execute pattern match.
 * @param mire		pattern container
 * @param val		value to match
 * @return		0 if pattern matches, >0 on nomatch, <0 on error
 */
int mireRegexec(miRE mire, const char * val)
	/*@modifies mire @*/;

/**
 * Compile pattern match.
 *
 * @param mire		pattern container
 * @param val		pattern to compile
 * @return		0 on success
 */
int mireRegcomp(miRE mire, const char * pattern)
	/*@modifies mire @*/;

#ifdef __cplusplus
}
#endif

#endif

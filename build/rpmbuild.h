#ifndef	_H_RPMBUILD_
#define	_H_RPMBUILD_

/** \ingroup rpmbuild
 * \file build/rpmbuild.h
 *  This is the *only* module users of librpmbuild should need to include.
 */

#include "rpmlib.h"

/* and it shouldn't need these :-( */
#include "stringbuf.h"
#include "misc.h"

/* but this will be needed */
#include "rpmspec.h"

/** \ingroup rpmbuild
 * Bit(s) to control buildSpec() operation.
 */
typedef enum rpmBuildFlags_e {
    RPMBUILD_NONE	= 0,
    RPMBUILD_PREP	= (1 << 0),	/*!< Execute %%prep. */
    RPMBUILD_BUILD	= (1 << 1),	/*!< Execute %%build. */
    RPMBUILD_INSTALL	= (1 << 2),	/*!< Execute %%install. */
    RPMBUILD_CLEAN	= (1 << 3),	/*!< Execute %%clean. */
    RPMBUILD_FILECHECK	= (1 << 4),	/*!< Check %%files manifest. */
    RPMBUILD_PACKAGESOURCE = (1 << 5),	/*!< Create source package. */
    RPMBUILD_PACKAGEBINARY = (1 << 6),	/*!< Create binary package(s). */
    RPMBUILD_RMSOURCE	= (1 << 7),	/*!< Remove source(s) and patch(s). */
    RPMBUILD_RMBUILD	= (1 << 8),	/*!< Remove build sub-tree. */
    RPMBUILD_STRINGBUF	= (1 << 9),	/*!< only for doScript() */
    RPMBUILD_RMSPEC	= (1 << 10)	/*!< Remove spec file. */
} rpmBuildFlags;

#include <ctype.h>

#define SKIPSPACE(s) { while (*(s) && xisspace(*(s))) (s)++; }
#define SKIPNONSPACE(s) { while (*(s) && !xisspace(*(s))) (s)++; }

#define PART_SUBNAME  0
#define PART_NAME     1

/** \ingroup rpmbuild
 * Spec file parser states.
 */
typedef enum rpmParseState_e {
    PART_NONE		= 0,	/*!< */
    PART_PREAMBLE	= 1,	/*!< */
    PART_PREP		= 2,	/*!< */
    PART_BUILD		= 3,	/*!< */
    PART_INSTALL	= 4,	/*!< */
    PART_CLEAN		= 5,	/*!< */
    PART_FILES		= 6,	/*!< */
    PART_PRE		= 7,	/*!< */
    PART_POST		= 8,	/*!< */
    PART_PREUN		= 9,	/*!< */
    PART_POSTUN		= 10,	/*!< */
    PART_DESCRIPTION	= 11,	/*!< */
    PART_CHANGELOG	= 12,	/*!< */
    PART_TRIGGERIN	= 13,	/*!< */
    PART_TRIGGERUN	= 14,	/*!< */
    PART_VERIFYSCRIPT	= 15,	/*!< */
    PART_BUILDARCHITECTURES= 16,/*!< */
    PART_TRIGGERPOSTUN	= 17,	/*!< */
    PART_LAST		= 18	/*!< */
} rpmParseState;

#define STRIP_NOTHING             0
#define STRIP_TRAILINGSPACE (1 << 0)
#define STRIP_COMMENTS      (1 << 1)

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmbuild
 * Destroy uid/gid caches.
 */
void freeNames(void);

/** \ingroup rpmbuild
 * Return cached user name from user id.
 * @todo Implement using hash.
 * @param		user id
 * @return		cached user name
 */
/*@observer@*/ const char *getUname(uid_t uid);

/** \ingroup rpmbuild
 * Return cached user name.
 * @todo Implement using hash.
 * @param		user name
 * @return		cached user name
 */
/*@observer@*/ const char *getUnameS(const char *uname);

/** \ingroup rpmbuild
 * Return cached group name from group id.
 * @todo Implement using hash.
 * @param		group id
 * @return		cached group name
 */
/*@observer@*/ const char *getGname(gid_t gid);

/** \ingroup rpmbuild
 * Return cached group name.
 * @todo Implement using hash.
 * @param		group name
 * @return		cached group name
 */
/*@observer@*/ const char *getGnameS(const char *gname);

/** \ingroup rpmbuild
 * Return build hostname.
 * @return		build hostname
 */
/*@observer@*/ const char *const buildHost(void);

/** \ingroup rpmbuild
 * Return build time stamp.
 * @return		build time stamp
 */
/*@observer@*/ time_t *const getBuildTime(void);

/** \ingroup rpmbuild
 * Read next line from spec file.
 * @param spec		spec file control structure
 * @param strip		truncate comments?
 * @return		0 on success, 1 on EOF, <0 on error
 */
int readLine(Spec spec, int strip);

/** \ingroup rpmbuild
 * Stop reading from spec file, freeing resources.
 * @param spec		spec file control structure
 */
void closeSpec(Spec spec);

/** \ingroup rpmbuild
 * Truncate comment lines.
 * @param s		skip white space, truncate line at '#'
 */
void handleComments(char *s);

/** \ingroup rpmbuild
 * Check line for section separator, return next parser state.
 * @param		line from spec file
 * @return		next parser state
 */
rpmParseState isPart(const char *line);

/** \ingroup rpmbuild
 * Parse a number.
 * @param		line from spec file
 * @retval res		pointer to int
 * @return		0 on success, 1 on failure
 */
int parseNum(const char *line, /*@out@*/int *res);

/** \ingroup rpmbuild
 * Add changelog entry to header.
 * @param h		header
 * @param time		time of change
 * @param name		person who made the change
 * @param text		description of change
 */
void addChangelogEntry(Header h, time_t time, const char *name, const char *text);

/** \ingroup rpmbuild
 * Parse %%build/%%install/%%clean section(s) of a spec file.
 * @param spec		spec file control structure
 * @param parsePart	current rpmParseState
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parseBuildInstallClean(Spec spec, rpmParseState parsePart);

/** \ingroup rpmbuild
 * Parse %%changelog section of a spec file.
 * @param spec		spec file control structure
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parseChangelog(Spec spec);

/** \ingroup rpmbuild
 * Parse %%description section of a spec file.
 * @param spec		spec file control structure
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parseDescription(Spec spec);

/** \ingroup rpmbuild
 * Parse %%files section of a spec file.
 * @param spec		spec file control structure
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parseFiles(Spec spec);

/** \ingroup rpmbuild
 * Parse tags from preamble of a spec file.
 * @param spec		spec file control structure
 * @param initialPackage
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parsePreamble(Spec spec, int initialPackage);

/** \ingroup rpmbuild
 * Parse %%prep section of a spec file.
 * @param spec		spec file control structure
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parsePrep(Spec spec);

/** \ingroup rpmbuild
 * Parse dependency relations from spec file and/or autogenerated output buffer.
 * @param spec		spec file control structure
 * @param pkg		package control structure
 * @param field		text to parse (e.g. "foo < 0:1.2-3, bar = 5:6.7")
 * @param tag		tag, identifies type of dependency
 * @param index		(0 always)
 * @param flags		dependency flags already known from context
 * @return		0 on success, RPMERR_BADSPEC on failure
 */
int parseRCPOT(Spec spec, Package pkg, const char *field, int tag, int index,
	       rpmsenseFlags flags);

/** \ingroup rpmbuild
 * Parse %%pre et al scriptlets from a spec file.
 * @param spec		spec file control structure
 * @param parsePart	current rpmParseState
 * @return		>= 0 next rpmParseState, < 0 on error
 */
int parseScript(Spec spec, int parsePart);

/** \ingroup rpmbuild
 * Parse %%trigger et al scriptlets from a spec file.
 * @param spec		spec file control structure
 * @param pkg		package control structure
 * @param field
 * @param tag
 * @return
 */
int parseTrigger(Spec spec, Package pkg, char *field, int tag);

/** \ingroup rpmbuild
 * Evaluate boolean expression.
 * @param spec		spec file control structure
 * @param expr		expression to parse
 * @return
 */
int parseExpressionBoolean(Spec spec, const char * expr);

/** \ingroup rpmbuild
 * Evaluate string expression.
 * @param spec		spec file control structure
 * @param expr		expression to parse
 * @return
 */
char *parseExpressionString(Spec spec, const char * expr);

/** \ingroup rpmbuild
 * Run a build script, assembled from spec file scriptlet section.
 *
 * @param spec		spec file control structure
 * @param what		type of script
 * @param name		name of scriptlet section
 * @param sb		lines that compose script body
 * @param test		don't execute scripts or package if testing
 * @return		0 on success, RPMERR_SCRIPT on failure
 */
int doScript(Spec spec, int what, const char *name, StringBuf sb, int test);

/** \ingroup rpmbuild
 * Find sub-package control structure by name.
 * @param spec		spec file control structure
 * @param name		(sub-)package name
 * @param flag		if PART_SUBNAME, then 1st package name is prepended
 * @retval pkg		package control structure
 * @return		0 on success, 1 on failure
 */
int lookupPackage(Spec spec, const char *name, int flag, /*@out@*/Package *pkg);

/** \ingroup rpmbuild
 * Create and initialize package control structure.
 * @param spec		spec file control structure
 * @return		package control structure
 */
/*@only@*/ Package newPackage(Spec spec);

/** \ingroup rpmbuild
 * Destroy all packages associated with spec file.
 * @param spec		spec file control structure
 */
void freePackages(Spec spec);

/** \ingroup rpmbuild
 * Destroy package control structure.
 * @param pkg		package control structure
 */
void freePackage(/*@only@*/ Package pkg);

/** \ingroup rpmbuild
 * Add dependency to header, filtering duplicates.
 * @param spec		spec file control structure
 * @param h		header
 * @param depFlags	(e.g. Requires: foo < 0:1.2-3, both "Requires:" and "<")
 * @param depName	(e.g. Requires: foo < 0:1.2-3, "foo")
 * @param depEVR	(e.g. Requires: foo < 0:1.2-3, "0:1.2-3")
 * @param index		(0 always)
 * @return		0 always
 */
int addReqProv(/*@unused@*/Spec spec, Header h,
		rpmsenseFlags flag, const char *depName, const char *depEVR,
		int index);

/** \ingroup rpmbuild
 * Add rpmlib feature dependency.
 * @param h		header
 * @param feature	rpm feature name (i.e. "rpmlib(Foo)" for feature Foo)
 * @param featureEVR	rpm feature epoch/version/release
 * @return		0 always
 */
int rpmlibNeedsFeature(Header h, const char * feature, const char * featureEVR);

/** \ingroup rpmbuild
 * Post-build processing for binary package(s).
 * @param spec		spec file control structure
 * @param installSpecialDoc
 * @param test		don't execute scripts or package if testing
 * @return		0 on success
 */
int processBinaryFiles(Spec spec, int installSpecialDoc, int test);

/** \ingroup rpmbuild
 * Create and initialize header for source package.
 * @param spec		spec file control structure
 */
void initSourceHeader(Spec spec);

/** \ingroup rpmbuild
 * Post-build processing for source package.
 * @param spec		spec file control structure
 * @return		0 on success
 */
int processSourceFiles(Spec spec);

/* global entry points */

/** \ingroup rpmbuild
 * Parse spec file into spec control structure.
 * @retval specp	spec file control structure
 * @param specFile
 * @param rootdir
 * @param buildRoot
 * @param inBuildArch
 * @param passPhrase
 * @param cookie
 * @param anyarch
 * @param force
 * @return
 */
int parseSpec(Spec *specp, const char *specFile, const char *rootdir,
		const char *buildRoot, int inBuildArch, const char *passPhrase,
		char *cookie, int anyarch, int force);

/** \ingroup rpmbuild
 * @retval specp	spec file control structure
 * @param specFile
 * @param rootdir
 * @param buildRoot
 * @param inBuildArch
 * @param passPhrase
 * @param cookie
 * @param anyarch
 * @param force
 * @return
 */
extern int (*parseSpecVec) (Spec *specp, const char *specFile,
		const char *rootdir, const char *buildRoot,
		int inBuildArch, const char *passPhrase,
		char *cookie, int anyarch, int force);	/* XXX FIXME */

/** \ingroup rpmbuild
 * Build stages state machine driver.
 * @param spec		spec file control structure
 * @param what		bit(s) to enable stages of build
 * @param test		don't execute scripts or package if testing
 * @return		0 on success
 */
int buildSpec(Spec spec, int what, int test);

/** \ingroup rpmbuild
 * Generate binary package(s).
 * @param spec		spec file control structure
 * @return		0 on success
 */
int packageBinaries(Spec spec);

/** \ingroup rpmbuild
 * Generate source package.
 * @param spec		spec file control structure
 * @return		0 on success
 */
int packageSources(Spec spec);

#ifdef __cplusplus
}
#endif

#endif	/* _H_RPMBUILD_ */

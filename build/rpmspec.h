#ifndef _H_SPEC_
#define _H_SPEC_

/** \file build/rpmspec.h
 *  The Spec and Package data structures used during build.
 */

typedef struct SpecStruct *Spec;
#include "rpmmacro.h"

#if 0
struct ReqProvTrigger {
    int flags;
    char *name;
    char *version;
    int index;      /* Only used for triggers */
    struct ReqProvTrigger *next;
};
#endif

/** */
struct TriggerFileEntry {
/*@{*/
    int index;
    /*@only@*/ char *fileName;
    /*@only@*/ char *script;
    /*@only@*/ char *prog;
    /*@owned@*/ struct TriggerFileEntry *next;
/*@}*/
};

#define RPMBUILD_ISSOURCE     1
#define RPMBUILD_ISPATCH     (1 << 1)
#define RPMBUILD_ISICON      (1 << 2)
#define RPMBUILD_ISNO        (1 << 3)

#define RPMBUILD_DEFAULT_LANG "C"

/** */
struct Source {
/*@{*/
    /*@owned@*/ char *fullSource;
    /*@dependent@*/ char *source;     /* Pointer into fullSource */
    int flags;
    int num;
    /*@owned@*/ struct Source *next;
/*@}*/
};

/** */
typedef struct ReadLevelEntry {
/*@{*/
    int reading;
    /*@dependent@*/ struct ReadLevelEntry *next;
/*@}*/
} RLE_t;

/** */
typedef struct OpenFileInfo {
/*@{*/
    /*@only@*/ char *fileName;
    FD_t fd;
    int lineNum;
    char readBuf[BUFSIZ];
    /*@dependent@*/ char *readPtr;
    /*@owned@*/ struct OpenFileInfo *next;
/*@}*/
} OFI_t;

/** */
struct spectag {
/*@{*/
    int t_tag;
    int t_startx;
    int t_nlines;
    /*@only@*/ const char *t_lang;
    /*@only@*/ const char *t_msgid;
/*@}*/
};

/** */
struct spectags {
/*@{*/
    /*@owned@*/ struct spectag *st_t;
    int st_nalloc;
    int st_ntags;
/*@}*/
};

/** */
struct speclines {
/*@{*/
    /*@only@*/ char **sl_lines;
    int sl_nalloc;
    int sl_nlines;
/*@}*/
};

/** The structure used to store values parsed from a spec file. */
struct SpecStruct {
/*@{*/
    /*@only@*/ const char *specFile;	/*!< Name of the spec file. */
    /*@only@*/ const char *sourceRpmName;

    /*@owned@*/ struct speclines *sl;
    /*@owned@*/ struct spectags *st;

    /*@owned@*/ struct OpenFileInfo *fileStack;
    char lbuf[BUFSIZ];
    char nextpeekc;
    /*@dependent@*/ char *nextline;
    /*@dependent@*/ char *line;
    int lineNum;

    /*@owned@*/ struct ReadLevelEntry *readStack;

    /*@refcounted@*/ Header buildRestrictions;
    /*@owned@*/ struct SpecStruct **buildArchitectureSpecs;
    /*@only@*/ const char ** buildArchitectures;
    int buildArchitectureCount;
    int inBuildArchitectures;

    int force;
    int anyarch;

    int gotBuildRootURL;
    /*@only@*/ const char *buildRootURL;
    /*@only@*/ const char *buildSubdir;

    char *passPhrase;
    int timeCheck;
    char *cookie;

    /*@owned@*/ struct Source *sources;
    int numSources;
    int noSource;

    /*@refcounted@*/ Header sourceHeader;
    int sourceCpioCount;
    /*@owned@*/ struct cpioFileMapping *sourceCpioList;

    /*@dependent@*/ struct MacroContext *macros;

    /*@only@*/ const char *rootURL;
    /*@only@*/ StringBuf prep;		/*!< %prep scriptlet. */
    /*@only@*/ StringBuf build;		/*!< %build scriptlet. */
    /*@only@*/ StringBuf install;	/*!< %install scriptlet. */
    /*@only@*/ StringBuf clean;		/*!< %clean scriptlet. */

    /*@owned@*/ struct PackageStruct *packages;	/*!< Package list. */
/*@}*/
};

/** The structure used to store values for a package. */
struct PackageStruct {
/*@{*/
    /*@refcounted@*/ Header header;

    int cpioCount;
    /*@owned@*/ struct cpioFileMapping *cpioList;

    /*@owned@*/ struct Source *icon;

    int autoReq;
    int autoProv;

    char *preInFile;		/*!< %pre scriptlet. */
    char *postInFile;		/*!< %post scriptlet. */
    char *preUnFile;		/*!< %preun scriptlet. */
    char *postUnFile;		/*!< %postun scriptlet. */
    char *verifyFile;		/*!< %verifyscript scriptlet. */

    /*@only@*/ StringBuf specialDoc;

#if 0
    struct ReqProvTrigger *triggers;
    char *triggerScripts;
#endif

    /*@only@*/ struct TriggerFileEntry *triggerFiles;

    /*@only@*/ const char *fileFile;
    /*@only@*/ StringBuf fileList; /* If NULL, package will not be written */

    /*@dependent@*/ struct PackageStruct *next;
/*@}*/
};

/** */
typedef struct PackageStruct *Package;

#ifdef __cplusplus
extern "C" {
#endif

/*@only@*/ Spec newSpec(void);
void freeSpec(/*@only@*/ Spec spec);
extern void (*freeSpecVec) (Spec spec);	/* XXX FIXME */

struct OpenFileInfo * newOpenFileInfo(void);
struct spectag *stashSt(Spec spec, Header h, int tag, const char *lang);

int addSource(Spec spec, Package pkg, const char *field, int tag);
int parseNoSource(Spec spec, const char *field, int tag);

#ifdef __cplusplus
}
#endif

#endif /* _H_SPEC_ */

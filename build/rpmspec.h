#ifndef _H_SPEC_
#define _H_SPEC_

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

struct TriggerFileEntry {
    int index;
    /*@only@*/ char *fileName;
    /*@only@*/ char *script;
    /*@only@*/ char *prog;
    /*@owned@*/ struct TriggerFileEntry *next;
};

#define RPMBUILD_ISSOURCE     1
#define RPMBUILD_ISPATCH     (1 << 1)
#define RPMBUILD_ISICON      (1 << 2)
#define RPMBUILD_ISNO        (1 << 3)

#define RPMBUILD_DEFAULT_LANG "C"

struct Source {
    /*@owned@*/ char *fullSource;
    /*@dependent@*/ char *source;     /* Pointer into fullSource */
    int flags;
    int num;
    /*@owned@*/ struct Source *next;
};

struct ReadLevelEntry {
    int reading;
    /*@dependent@*/ struct ReadLevelEntry *next;
};

struct OpenFileInfo {
    /*@only@*/ char *fileName;
    /*@dependent@*/ FILE *file;
    int lineNum;
    char readBuf[BUFSIZ];
    /*@dependent@*/ char *readPtr;
    /*@owned@*/ struct OpenFileInfo *next;
};

struct SpecStruct {
    /*@only@*/ char *specFile;
    /*@only@*/ char *sourceRpmName;

    /*@owned@*/ struct OpenFileInfo *fileStack;
    char line[BUFSIZ];
    int lineNum;

    /*@only@*/ struct ReadLevelEntry *readStack;

    Header buildRestrictions;
    /*@owned@*/ struct SpecStruct **buildArchitectureSpecs;
    char ** buildArchitectures;
    int buildArchitectureCount;
    int inBuildArchitectures;

    int force;
    int anyarch;

    int gotBuildRoot;
    /*@only@*/ const char *buildRoot;
    /*@only@*/ const char *buildSubdir;

    char *passPhrase;
    int timeCheck;
    char *cookie;

    /*@owned@*/ struct Source *sources;
    int numSources;
    int noSource;

    Header sourceHeader;
    int sourceCpioCount;
    /*@owned@*/ struct cpioFileMapping *sourceCpioList;

    /*@dependent@*/ struct MacroContext *macros;

    int autoReq;
    int autoProv;

    /*@only@*/ StringBuf prep;
    /*@only@*/ StringBuf build;
    /*@only@*/ StringBuf install;
    /*@only@*/ StringBuf clean;

    /*@owned@*/ struct PackageStruct *packages;
};

struct PackageStruct {
    /*@only@*/ Header header;

    int cpioCount;
    /*@only@*/ struct cpioFileMapping *cpioList;

    /*@owned@*/ struct Source *icon;

    int autoReqProv;

    char *preInFile;
    char *postInFile;
    char *preUnFile;
    char *postUnFile;
    char *verifyFile;

    /*@only@*/ StringBuf specialDoc;

#if 0
    struct ReqProvTrigger *triggers;
    char *triggerScripts;
#endif

    /*@only@*/ struct TriggerFileEntry *triggerFiles;

    /*@only@*/ char *fileFile;
    /*@only@*/ StringBuf fileList; /* If NULL, package will not be written */

    /*@keep@*/ struct PackageStruct *next;
};

typedef struct PackageStruct *Package;

#ifdef __cplusplus
extern "C" {
#endif

/*@only@*/ Spec newSpec(void);
void freeSpec(/*@only@*/ Spec spec);

struct OpenFileInfo * newOpenFileInfo(void);

int addSource(Spec spec, Package pkg, char *field, int tag);
int parseNoSource(Spec spec, char *field, int tag);

#ifdef __cplusplus
}
#endif

#endif /* _H_SPEC_ */

#ifndef H_PSM
#define H_PSM

/** \ingroup rpmtrans payload
 * \file lib/psm.h
 * Package state machine to handle a package from a transaction set.
 */

#include "fsm.h"
#include "depends.h"

/*@unchecked@*/
/*@-exportlocal@*/
extern int _fi_debug;
/*@=exportlocal@*/

/**
 */
struct sharedFileInfo {
    int pkgFileNum;
    int otherFileNum;
    int otherPkg;
    int isRemoved;
};

/**
 */
struct transactionFileInfo_s {
/*@null@*/ Header h;		/*!< Package header */

/*@owned@*/ const char * name;	/*!< Name: tag (malloc'd). */
/*@owned@*/ const char * version; /*!< Version: tag (malloc'd). */
/*@owned@*/ const char * release; /*!< Release: tag (malloc'd). */

#ifdef	NOTYET
/*@owned@*/ const char ** provides;	/*!< Provides: name strings. */
/*@owned@*/ const char ** providesEVR;	/*!< Provides: [epoch:]version[-release] strings. */
/*@dependent@*/ int * provideFlags;	/*!< Provides: logical range qualifiers. */
/*@owned@*//*@null@*/ const char ** requires;	/*!< Requires: name strings. */
/*@owned@*//*@null@*/ const char ** requiresEVR;/*!< Requires: [epoch:]version[-release] strings. */
/*@dependent@*//*@null@*/ int * requireFlags;	/*!< Requires: logical range qualifiers. */
/*@owned@*//*@null@*/ const char ** baseNames;	/*!< Header file basenames. */
/*@dependent@*//*@null@*/ int_32 * epoch;	/*!< Header epoch (if any). */
    int providesCount;			/*!< No. of Provide:'s in header. */
    int requiresCount;			/*!< No. of Require:'s in header. */
    int filesCount;			/*!< No. of files in header. */
    int npreds;				/*!< No. of predecessors. */
    int depth;				/*!< Max. depth in dependency tree. */
    struct tsortInfo_s tsi;		/*!< Dependency tsort data. */
#endif

    uint_32 multiLib;		/* MULTILIB */
/*@null@*/
    fnpyKey key;		/*!< Package notify key. */
/*@null@*/ /*@dependent@*/
    rpmRelocation * relocs;	/*!< Package file relocations. */
/*@null@*/
    FD_t fd;			/*!< Package file handle */

/*=============================*/

  /* for all packages */
    enum rpmTransactionType type;
    fileAction action;		/*!< File disposition default. */
/*@owned@*/ fileAction * actions;	/*!< File disposition(s) */
/*@owned@*/ struct fingerPrint_s * fps;	/*!< File fingerprint(s) */
    HGE_t hge;			/*!< Vector to headerGetEntry() */
    HAE_t hae;			/*!< Vector to headerAddEntry() */
    HME_t hme;			/*!< Vector to headerModifyEntry() */
    HRE_t hre;			/*!< Vector to headerRemoveEntry() */
    HFD_t hfd;			/*!< Vector to headerFreeData() */
    int_32 epoch;
    uint_32 flags;		/*!< File flag default. */
    const uint_32 * fflags;	/*!< File flag(s) (from header) */
    const uint_32 * fsizes;	/*!< File size(s) (from header) */
    const uint_32 * fmtimes;	/*!< File modification time(s) (from header) */
/*@owned@*/ const char ** bnl;	/*!< Base name(s) (from header) */
/*@owned@*/ const char ** dnl;	/*!< Directory name(s) (from header) */
    int_32 * dil;		/*!< Directory indice(s) (from header) */
/*@owned@*/ const char ** obnl;	/*!< Original base name(s) (from header) */
/*@owned@*/ const char ** odnl;	/*!< Original directory name(s) (from header) */
/*@unused@*/ int_32 * odil;	/*!< Original directory indice(s) (from header) */
/*@owned@*/ const char ** fmd5s;/*!< File MD5 sum(s) (from header) */
/*@owned@*/ const char ** flinks;	/*!< File link(s) (from header) */
/* XXX setuid/setgid bits are turned off if fuser/fgroup doesn't map. */
    uint_16 * fmodes;		/*!< File mode(s) (from header) */
    uint_16 * frdevs;		/*!< File rdev(s) (from header) */
/*@only@*/ /*@null@*/ char * fstates;	/*!< File state(s) (from header) */
/*@owned@*/ const char ** fuser;	/*!< File owner(s) */
/*@owned@*/ const char ** fgroup;	/*!< File group(s) */
/*@owned@*/ const char ** flangs;	/*!< File lang(s) */
    int fc;			/*!< No. of files. */
    int dc;			/*!< No. of directories. */
    int bnlmax;			/*!< Length (in bytes) of longest base name. */
    int dnlmax;			/*!< Length (in bytes) of longest dir name. */
    int astriplen;
    int striplen;
    unsigned int archiveSize;
    mode_t dperms;		/*!< Directory perms (0755) if not mapped. */
    mode_t fperms;		/*!< File perms (0644) if not mapped. */
/*@only@*/ /*@null@*/ const char ** apath;
    int mapflags;
/*@owned@*/ /*@null@*/ int * fmapflags;
    uid_t uid;
/*@owned@*/ /*@null@*/ uid_t * fuids;	/*!< File uid(s) */
    gid_t gid;
/*@owned@*/ /*@null@*/ gid_t * fgids;	/*!< File gid(s) */
    int magic;

#define	TFIMAGIC	0x09697923
/*@owned@*/ FSM_t fsm;		/*!< File state machine data. */

    int keep_header;		/*!< Keep header? */
/*@refs@*/ int nrefs;		/*!< Reference count. */

/*@owned@*/ struct sharedFileInfo * replaced;	/*!< (TR_ADDED) */
/*@owned@*/ uint_32 * replacedSizes;	/*!< (TR_ADDED) */

    unsigned int record;	/*!< (TR_REMOVED) */
};

/**
 */
#define	PSM_VERBOSE	0x8000
#define	PSM_INTERNAL	0x4000
#define	PSM_SYSCALL	0x2000
#define	PSM_DEAD	0x1000
#define	_fv(_a)		((_a) | PSM_VERBOSE)
#define	_fi(_a)		((_a) | PSM_INTERNAL)
#define	_fs(_a)		((_a) | (PSM_INTERNAL | PSM_SYSCALL))
#define	_fd(_a)		((_a) | (PSM_INTERNAL | PSM_DEAD))
typedef enum pkgStage_e {
    PSM_UNKNOWN		=  0,
    PSM_INIT		=  1,
    PSM_PRE		=  2,
    PSM_PROCESS		=  3,
    PSM_POST		=  4,
    PSM_UNDO		=  5,
    PSM_FINI		=  6,

    PSM_PKGINSTALL	=  7,
    PSM_PKGERASE	=  8,
    PSM_PKGCOMMIT	= 10,
    PSM_PKGSAVE		= 12,

    PSM_CREATE		= 17,
    PSM_NOTIFY		= 22,
    PSM_DESTROY		= 23,
    PSM_COMMIT		= 25,

    PSM_CHROOT_IN	= 51,
    PSM_CHROOT_OUT	= 52,
    PSM_SCRIPT		= 53,
    PSM_TRIGGERS	= 54,
    PSM_IMMED_TRIGGERS	= 55,
    PSM_RPMIO_FLAGS	= 56,

    PSM_RPMDB_LOAD	= 97,
    PSM_RPMDB_ADD	= 98,
    PSM_RPMDB_REMOVE	= 99,

} pkgStage;
#undef	_fv
#undef	_fi
#undef	_fs
#undef	_fd

/**
 */
struct psm_s {
/*@refcounted@*/
    rpmTransactionSet ts;	/*!< transaction set */
/*@refcounted@*/
    TFI_t fi;			/*!< transaction element file info */
    FD_t cfd;			/*!< Payload file handle. */
    FD_t fd;			/*!< Repackage file handle. */
    Header oh;			/*!< Repackage/multilib header. */
/*@null@*/ rpmdbMatchIterator mi;
/*@observer@*/ const char * stepName;
/*@only@*/ /*@null@*/ const char * rpmio_flags;
/*@only@*/ /*@null@*/ const char * failedFile;
/*@only@*/ /*@null@*/ const char * pkgURL;	/*!< Repackage URL. */
/*@dependent@*/ const char * pkgfn;	/*!< Repackage file name. */
    int scriptTag;		/*!< Scriptlet data tag. */
    int progTag;		/*!< Scriptlet interpreter tag. */
    int npkgs_installed;	/*!< No. of installed instances. */
    int scriptArg;		/*!< Scriptlet package arg. */
    int sense;			/*!< One of RPMSENSE_TRIGGER{IN,UN,POSTUN}. */
    int countCorrection;	/*!< 0 if installing, -1 if removing. */
    int chrootDone;		/*!< Was chroot(2) done by pkgStage? */
    rpmCallbackType what;	/*!< Callback type. */
    unsigned long amount;	/*!< Callback amount. */
    unsigned long total;	/*!< Callback total. */
    rpmRC rc;
    pkgStage goal;
/*@unused@*/ pkgStage stage;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return (malloc'd) transaction element name-version-release string.
 * @param fi		transaction element file info
 * @return		name-version-release string
 */
/*@only@*/ /*@null@*/
char * fiGetNEVR(/*@null@*/const TFI_t fi)
	/*@*/;

/**
 * Return file type from mode_t.
 * @param mode		file mode bits (from header)
 * @return		file type
 */
fileTypes whatis(uint_16 mode)
	/*@*/;

/**
 * Relocate files in header.
 * @todo multilib file dispositions need to be checked.
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @param origH		package header
 * @param actions	file dispositions
 * @return		header with relocated files
 */
Header relocateFileList(const rpmTransactionSet ts, TFI_t fi,
		Header origH, fileAction * actions)
	/*@modifies ts, fi, origH, actions @*/;

/**
 * Load data from header into transaction file element info.
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @param h		header
 * @param keep_header	use header memory?
 */
void loadFi(/*@null@*/ const rpmTransactionSet ts, TFI_t fi,
		Header h, int keep_header)
	/*@modifies ts, fi, h @*/;

/**
 * Destroy transaction element file info.
 * @param fi		transaction element file info
 */
void freeFi(TFI_t fi)
	/*@modifies fi @*/;

/**
 * Retrieve key from transaction element file info
 * @param fi		transaction element file info
 * @return		transaction element file info key
 */
/*@null@*/ const void * rpmfiGetKey(TFI_t fi)
	/*@*/;

/**
 * Return formatted string representation of package disposition.
 * @param a		package dispostion
 * @return		formatted string
 */
/*@observer@*/ const char *const fiTypeString(/*@partial@*/TFI_t fi)
	/*@*/;

/**
 * Package state machine driver.
 * @param psm		package state machine data
 * @param stage		next stage
 * @return		0 on success
 */
int psmStage(PSM_t psm, pkgStage stage)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/;

#ifdef __cplusplus
}
#endif

#endif	/* H_ROLLBACK */

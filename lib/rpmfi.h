#ifndef H_RPMFI
#define H_RPMFI

/** \ingroup rpmfi
 * \file lib/rpmfi.h
 * Structure(s) used for file info tag sets.
 */

/*@-exportlocal@*/
/*@unchecked@*/
extern int _rpmfi_debug;
/*@=exportlocal@*/

/** \rpmfi
 * File types.
 * These are the file types used internally by rpm. The file
 * type is determined by applying stat(2) macros like S_ISDIR to
 * the file mode tag from a header. The values are arbitrary,
 * but are identical to the linux stat(2) file types.
 */
typedef enum rpmFileTypes_e {
    PIPE	=  1,	/*!< pipe/fifo */
    CDEV	=  2,	/*!< character device */
    XDIR	=  4,	/*!< directory */
    BDEV	=  6,	/*!< block device */
    REG		=  8,	/*!< regular file */
    LINK	= 10,	/*!< hard link */
    SOCK	= 12	/*!< socket */
} rpmFileTypes;

/**
 * File disposition(s) during package install/erase transaction.
 */
typedef enum fileAction_e {
    FA_UNKNOWN = 0,	/*!< initial action for file ... */
    FA_CREATE,		/*!< ... copy in from payload. */
    FA_COPYIN,		/*!< ... copy in from payload. */
    FA_COPYOUT,		/*!< ... copy out to payload. */
    FA_BACKUP,		/*!< ... renamed with ".rpmorig" extension. */
    FA_SAVE,		/*!< ... renamed with ".rpmsave" extension. */
    FA_SKIP, 		/*!< ... already replaced, don't remove. */
    FA_ALTNAME,		/*!< ... create with ".rpmnew" extension. */
    FA_ERASE,		/*!< ... to be removed. */
    FA_SKIPNSTATE,	/*!< ... untouched, state "not installed". */
    FA_SKIPNETSHARED,	/*!< ... untouched, state "netshared". */
    FA_SKIPCOLOR	/*!< ... untouched, state "wrong color". */
} fileAction;

#if defined(_RPMFI_INTERNAL)
#define XFA_SKIPPING(_a)	\
    ((_a) == FA_SKIP || (_a) == FA_SKIPNSTATE || (_a) == FA_SKIPNETSHARED || (_a) == FA_SKIPCOLOR)

/** \ingroup rpmfi
 * A package filename set.
 */
struct rpmfi_s {
    int i;			/*!< Current file index. */
    int j;			/*!< Current directory index. */

/*@observer@*/
    const char * Type;		/*!< Tag name. */

    rpmTag tagN;		/*!< Header tag. */
/*@refcounted@*/ /*@null@*/
    Header h;			/*!< Header for file info set (or NULL) */

/*@only@*/ /*?null?*/
    const char ** bnl;		/*!< Base name(s) (from header) */
/*@only@*/ /*?null?*/
    const char ** dnl;		/*!< Directory name(s) (from header) */

/*@only@*/ /*@relnull@*/
    const char ** fdigests;	/*!< File digest(s) (from header) */
/*@only@*/ /*@null@*/
    uint32_t * fdigestalgos;	/*!< File digest algorithm(s) (from header) */
/*@only@*/ /*@relnull@*/
    const char ** flinks;	/*!< File link(s) (from header) */
/*@only@*/ /*@null@*/
    const char ** flangs;	/*!< File lang(s) (from header) */

/*@only@*/ /*@relnull@*/
          uint32_t * dil;	/*!< Directory indice(s) (from header) */
/*@only@*/ /*?null?*/
    const uint32_t * fflags;	/*!< File flag(s) (from header) */
/*@only@*/ /*?null?*/
    const uint32_t * fsizes;	/*!< File size(s) (from header) */
/*@only@*/ /*?null?*/
    const uint32_t * fmtimes;	/*!< File modification time(s) (from header) */
/*@only@*/ /*?null?*/
          uint16_t * fmodes;	/*!< File mode(s) (from header) */
/*@only@*/ /*?null?*/
    const uint16_t * frdevs;	/*!< File rdev(s) (from header) */
/*@only@*/ /*?null?*/
    const uint32_t * finodes;	/*!< File inodes(s) (from header) */

/*@only@*/ /*@null@*/
    const char ** fuser;	/*!< File owner(s) (from header) */
/*@only@*/ /*@null@*/
    const char ** fgroup;	/*!< File group(s) (from header) */

/*@only@*/ /*@null@*/
    uint8_t * fstates;		/*!< File state(s) (from header) */

/*@only@*/ /*@null@*/
    const uint32_t * fcolors;	/*!< File color bits (header) */

/*@only@*/ /*@null@*/
    const char ** fcontexts;	/*! FIle security contexts. */

/*@only@*/ /*@null@*/
    const char ** cdict;	/*!< File class dictionary (header) */
    uint32_t ncdict;		/*!< No. of class entries. */
/*@only@*/ /*@null@*/
    const uint32_t * fcdictx;	/*!< File class dictionary index (header) */

/*@only@*/ /*@null@*/
    const uint32_t * ddict;	/*!< File depends dictionary (header) */
    uint32_t nddict;		/*!< No. of depends entries. */
/*@only@*/ /*@null@*/
    const uint32_t * fddictx;	/*!< File depends dictionary start (header) */
/*@only@*/ /*@null@*/
    const uint32_t * fddictn;	/*!< File depends dictionary count (header) */

/*@only@*/ /*?null?*/
    const uint32_t * vflags;	/*!< File verify flag(s) (from header) */

    uint32_t dc;		/*!< No. of directories. */
    uint32_t fc;		/*!< No. of files. */

/*=============================*/
/*@dependent@*/ /*@relnull@*/
    rpmte te;

/*-----------------------------*/
    uid_t uid;			/*!< File uid (default). */
    gid_t gid;			/*!< File gid (default). */
    uint32_t flags;		/*!< File flags (default). */
    fileAction action;		/*!< File disposition (default). */
/*@owned@*/ /*@relnull@*/
    fileAction * actions;	/*!< File disposition(s). */
/*@owned@*/
    struct fingerPrint_s * fps;	/*!< File fingerprint(s). */
/*@owned@*/
    const char ** obnl;		/*!< Original basename(s) (from header) */
/*@owned@*/
    const char ** odnl;		/*!< Original dirname(s) (from header) */
/*@unused@*/
    uint32_t * odil;		/*!< Original dirindex(s) (from header) */

/*@only@*/ /*@relnull@*/
    unsigned char * digests;	/*!< File digest(s) in binary. */
    uint32_t digestalgo;	/*!< File digest algorithm. */
    uint32_t digestlen;		/*!< No. bytes in binary digest. */

/*@only@*/ /*@relnull@*/
    const char * pretrans;
/*@only@*/ /*@relnull@*/
    const char * pretransprog;
/*@only@*/ /*@relnull@*/
    const char * posttrans;
/*@only@*/ /*@relnull@*/
    const char * posttransprog;
/*@only@*/ /*@relnull@*/
    const char * verifyscript;
/*@only@*/ /*@relnull@*/
    const char * verifyscriptprog;

/*@only@*/ /*@null@*/
    char * fn;			/*!< File name buffer. */
    int fnlen;			/*!< File name buffer length. */

    int astriplen;
    int striplen;
    unsigned long long archivePos;
    unsigned long long archiveSize;
    mode_t dperms;		/*!< Directory perms (0755) if not mapped. */
    mode_t fperms;		/*!< File perms (0644) if not mapped. */
/*@only@*/ /*@null@*/
    const char ** apath;
    int mapflags;
/*@owned@*/ /*@null@*/
    int * fmapflags;
/*@owned@*/
    FSM_t fsm;			/*!< File state machine data. */
    uint32_t color;		/*!< Color bit(s) from file color union. */

    int isSource;		/*!< Is this a SRPM? */

/*@owned@*/
    uint32_t * replacedSizes;	/*!< (TR_ADDED) */

    unsigned int record;	/*!< (TR_REMOVED) */
    int magic;
#define	RPMFIMAGIC	0x09697923
/*=============================*/

/*@refs@*/ int nrefs;		/*!< Reference count. */
};

#endif	/* _RPMFI_INTERNAL */

#ifdef __cplusplus
extern "C" {
#endif

/** \name RPMFI */
/*@{*/

/**
 * Unreference a file info set instance.
 * @param fi		file info set
 * @param msg
 * @return		NULL always
 */
/*@unused@*/ /*@null@*/
rpmfi rpmfiUnlink (/*@killref@*/ /*@only@*/ /*@null@*/ rpmfi fi,
		/*@null@*/ const char * msg)
	/*@modifies fi @*/;

/** @todo Remove debugging entry from the ABI.
 * @param fi		file info set
 * @param msg
 * @param fn
 * @param ln
 * @return		NULL always
 */
/*@-exportlocal@*/
/*@null@*/
rpmfi XrpmfiUnlink (/*@killref@*/ /*@only@*/ /*@null@*/ rpmfi fi,
		/*@null@*/ const char * msg, const char * fn, unsigned ln)
	/*@modifies fi @*/;
/*@=exportlocal@*/
#define	rpmfiUnlink(_fi, _msg) XrpmfiUnlink(_fi, _msg, __FILE__, __LINE__)

/**
 * Reference a file info set instance.
 * @param fi		file info set
 * @param msg
 * @return		new file info set reference
 */
/*@unused@*/ /*@null@*/
rpmfi rpmfiLink (/*@null@*/ rpmfi fi, /*@null@*/ const char * msg)
	/*@modifies fi @*/;

/** @todo Remove debugging entry from the ABI.
 * @param fi		file info set
 * @param msg
 * @param fn
 * @param ln
 * @return		NULL always
 */
/*@null@*/
rpmfi XrpmfiLink (/*@null@*/ rpmfi fi, /*@null@*/ const char * msg,
		const char * fn, unsigned ln)
        /*@modifies fi @*/;
#define	rpmfiLink(_fi, _msg)	XrpmfiLink(_fi, _msg, __FILE__, __LINE__)

/**
 * Return file count from file info set.
 * @param fi		file info set
 * @return		current file count
 */
int rpmfiFC(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file index from file info set.
 * @param fi		file info set
 * @return		current file index
 */
/*@unused@*/
int rpmfiFX(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Set current file index in file info set.
 * @param fi		file info set
 * @param fx		new file index
 * @return		current file index
 */
/*@unused@*/
int rpmfiSetFX(/*@null@*/ rpmfi fi, int fx)
	/*@modifies fi @*/;

/**
 * Return directory count from file info set.
 * @param fi		file info set
 * @return		current directory count
 */
int rpmfiDC(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current directory index from file info set.
 * @param fi		file info set
 * @return		current directory index
 */
int rpmfiDX(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Set current directory index in file info set.
 * @param fi		file info set
 * @param dx		new directory index
 * @return		current directory index
 */
int rpmfiSetDX(/*@null@*/ rpmfi fi, int dx)
	/*@modifies fi @*/;

/**
 * Return source rpm marker from file info set.
 * @param fi		file info set
 * @return		source rpm?
 */
int rpmfiIsSource(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current base name from file info set.
 * @param fi		file info set
 * @return		current base name, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const char * rpmfiBN(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current directory name from file info set.
 * @param fi		file info set
 * @return		current directory, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const char * rpmfiDN(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file name from file info set.
 * @param fi		file info set
 * @return		current file name
 */
/*@observer@*/
extern const char * rpmfiFN(/*@null@*/ rpmfi fi)
	/*@modifies fi @*/;

/**
 * Return current file flags from file info set.
 * @param fi		file info set
 * @return		current file flags, 0 on invalid
 */
uint32_t rpmfiFFlags(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Set current file flags in file info set.
 * @param fi		file info set
 * @param FFlags	new file flags
 * @return		previous file flags, 0 on invalid
 */
uint32_t rpmfiSetFFlags(/*@null@*/ rpmfi fi, uint32_t FFlags)
	/*@modifies fi @*/;

/**
 * Return current file verify flags from file info set.
 * @param fi		file info set
 * @return		current file verify flags, 0 on invalid
 */
uint32_t rpmfiVFlags(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Set current file verify flags in file info set.
 * @param fi		file info set
 * @param VFlags	new file verify flags
 * @return		previous file verify flags, 0 on invalid
 */
uint32_t rpmfiSetVFlags(/*@null@*/ rpmfi fi, uint32_t VFlags)
	/*@modifies fi @*/;

/**
 * Return current file mode from file info set.
 * @param fi		file info set
 * @return		current file mode, 0 on invalid
 */
uint16_t rpmfiFMode(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file state from file info set.
 * @param fi		file info set
 * @return		current file state, 0 on invalid
 */
rpmfileState rpmfiFState(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Set current file state in file info set.
 * @param fi		file info set
 * @param fstate	new file state
 * @return		previous file state, 0 on invalid
 */
rpmfileState rpmfiSetFState(/*@null@*/ rpmfi fi, rpmfileState fstate)
	/*@modifies fi @*/;

/**
 * Return current file (binary) digest from file info set.
 * @param fi		file info set
 * @retval *algop	digest algorithm
 * @retval *lenp	digest length (in bytes)
 * @return		current file digest, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const unsigned char * rpmfiDigest(/*@null@*/ rpmfi fi,
		/*@out@*/ /*@null@*/ int * algop,
		/*@out@*/ /*@null@*/ size_t * lenp)
	/*@modifies *algop, *lenp @*/;

/**
 * Return current file linkto (i.e. symlink(2) target) from file info set.
 * @param fi		file info set
 * @return		current file linkto, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const char * rpmfiFLink(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file size from file info set.
 * @param fi		file info set
 * @return		current file size, 0 on invalid
 */
uint32_t rpmfiFSize(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file rdev from file info set.
 * @param fi		file info set
 * @return		current file rdev, 0 on invalid
 */
uint16_t rpmfiFRdev(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file inode from file info set.
 * @param fi		file info set
 * @return		current file inode, 0 on invalid
 */
uint32_t rpmfiFInode(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return union of all file color bits from file info set.
 * @param fi		file info set
 * @return		current color
 */
uint32_t rpmfiColor(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file color bits from file info set.
 * @param fi		file info set
 * @return		current file color
 */
uint32_t rpmfiFColor(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file class from file info set.
 * @param fi		file info set
 * @return		current file class, 0 on invalid
 */
/*@-exportlocal@*/
/*@observer@*/ /*@null@*/
extern const char * rpmfiFClass(/*@null@*/ rpmfi fi)
	/*@*/;
/*@=exportlocal@*/

/**
 * Return current file security context from file info set.
 * @param fi		file info set
 * @return		current file context, 0 on invalid
 */
/*@-exportlocal@*/
/*@observer@*/ /*@null@*/
extern const char * rpmfiFContext(/*@null@*/ rpmfi fi)
	/*@*/;
/*@=exportlocal@*/

/**
 * Return current file depends dictionary from file info set.
 * @param fi		file info set
 * @retval *fddictp	file depends dictionary array (or NULL)
 * @return		no. of file depends entries, 0 on invalid
 */
uint32_t rpmfiFDepends(/*@null@*/ rpmfi fi,
		/*@out@*/ /*@null@*/ const uint32_t ** fddictp)
	/*@modifies *fddictp @*/;

/**
 * Return (calculated) current file nlink count from file info set.
 * @param fi		file info set
 * @return		current file nlink count, 0 on invalid
 */
uint32_t rpmfiFNlink(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file modify time from file info set.
 * @param fi		file info set
 * @return		current file modify time, 0 on invalid
 */
uint32_t rpmfiFMtime(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file owner from file info set.
 * @param fi		file info set
 * @return		current file owner, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const char * rpmfiFUser(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return current file group from file info set.
 * @param fi		file info set
 * @return		current file group, NULL on invalid
 */
/*@observer@*/ /*@null@*/
extern const char * rpmfiFGroup(/*@null@*/ rpmfi fi)
	/*@*/;

/**
 * Return next file iterator index.
 * @param fi		file info set
 * @return		file iterator index, -1 on termination
 */
int rpmfiNext(/*@null@*/ rpmfi fi)
	/*@modifies fi @*/;

/**
 * Initialize file iterator index.
 * @param fi		file info set
 * @param fx		file iterator index
 * @return		file info set
 */
/*@null@*/
rpmfi rpmfiInit(/*@null@*/ rpmfi fi, int fx)
	/*@modifies fi @*/;

/**
 * Return next directory iterator index.
 * @param fi		file info set
 * @return		directory iterator index, -1 on termination
 */
/*@unused@*/
int rpmfiNextD(/*@null@*/ rpmfi fi)
	/*@modifies fi @*/;

/**
 * Initialize directory iterator index.
 * @param fi		file info set
 * @param dx		directory iterator index
 * @return		file info set, NULL if dx is out of range
 */
/*@unused@*/ /*@null@*/
rpmfi rpmfiInitD(/*@null@*/ rpmfi fi, int dx)
	/*@modifies fi @*/;

/**
 * Destroy a file info set.
 * @param fi		file info set
 * @return		NULL always
 */
/*@null@*/
rpmfi rpmfiFree(/*@killref@*/ /*@only@*/ /*@null@*/ rpmfi fi)
	/*@globals fileSystem @*/
	/*@modifies fi, fileSystem @*/;

/**
 * Create and load a file info set.
 * @param ts		transaction set (NULL skips path relocation)
 * @param h		header
 * @param tagN		RPMTAG_BASENAMES
 * @param flags		scareMem(0x1), nofilter(0x2)
 * @return		new file info set
 */
/*@null@*/
rpmfi rpmfiNew(/*@null@*/ const rpmts ts, Header h, rpmTag tagN, int flags)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, h, rpmGlobalMacroContext, fileSystem, internalState @*/;

/**
 * Retrieve file classes from header.
 *
 * This function is used to retrieve file classes from the header.
 * 
 * @param h		header
 * @retval *fclassp	array of file classes
 * @retval *fcp		number of files
 */
void rpmfiBuildFClasses(Header h,
		/*@out@*/ const char *** fclassp, /*@out@*/ uint32_t * fcp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fclassp, *fcp, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Retrieve file security contexts from header.
 *
 * This function is used to retrieve file contexts from the header.
 * 
 * @param h		header
 * @retval *fcontextp	array of file contexts
 * @retval *fcp		number of files
 */
void rpmfiBuildFContexts(Header h,
		/*@out@*/ const char *** fcontextp, /*@out@*/ uint32_t * fcp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fcontextp, *fcp, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Retrieve file security contexts from file system.
 *
 * This function is used to retrieve file contexts from the file system.
 * 
 * @param h		header
 * @retval *fcontextp	array of file contexts
 * @retval *fcp		number of files
 */
void rpmfiBuildFSContexts(Header h,
		/*@out@*/ const char *** fcontextp, /*@out@*/ uint32_t * fcp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fcontextp, *fcp, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Retrieve file security contexts from policy RE's.
 *
 * This function is used to retrieve file contexts from policy RE's.
 * 
 * @param h		header
 * @retval *fcontextp	array of file contexts
 * @retval *fcp		number of files
 */
void rpmfiBuildREContexts(Header h,
		/*@out@*/ const char *** fcontextp, /*@out@*/ uint32_t * fcp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fcontextp, *fcp, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Retrieve per-file dependencies from header.
 *
 * This function is used to retrieve per-file dependencies from the header.
 * 
 * @param h		header
 * @param tagN		RPMTAG_PROVIDENAME | RPMTAG_REQUIRENAME
 * @retval *fdepsp	array of file dependencies
 * @retval *fcp		number of files
 */
void rpmfiBuildFDeps(Header h, rpmTag tagN,
		/*@out@*/ const char *** fdepsp, /*@out@*/ uint32_t * fcp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies h, *fdepsp, *fcp,
		rpmGlobalMacroContext, fileSystem, internalState @*/;

/**
 * Return file info comparison.
 * @param afi		1st file info
 * @param bfi		2nd file info
 * @return		0 if identical
 */
int rpmfiCompare(const rpmfi afi, const rpmfi bfi)
	/*@*/;

/**
 * Return file disposition.
 * @param ofi		old file info
 * @param nfi		new file info
 * @param skipMissing	OK to skip missing files?
 * @return		file dispostion
 */
fileAction rpmfiDecideFate(const rpmfi ofi, rpmfi nfi, int skipMissing)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies nfi, fileSystem, internalState @*/;

/**
 * Return formatted string representation of package disposition.
 * @param fi		file info set
 * @return		formatted string
 */
/*@-redef@*/
/*@observer@*/
const char * rpmfiTypeString(rpmfi fi)
	/*@*/;
/*@=redef@*/

/*@}*/

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMDS */

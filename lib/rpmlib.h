#ifndef H_RPMLIB
#define	H_RPMLIB

/** \ingroup rpmcli rpmrc rpmts rpmte rpmds rpmfi rpmdb lead signature header payload dbi
 * \file lib/rpmlib.h
 *
 * In Memoriam: Steve Taylor <staylor@redhat.com> was here, now he's not.
 *
 */

#include "rpmmessages.h"
#include "rpmerr.h"
#include "header.h"
#include "popt.h"

#define RPM_FORMAT_VERSION 5
#define RPM_MAJOR_VERSION 0
#define RPM_MINOR_VERSION 0

/**
 * Package read return codes.
 */
typedef	enum rpmRC_e {
    RPMRC_OK		= 0,	/*!< Generic success code */
    RPMRC_NOTFOUND	= 1,	/*!< Generic not found code. */
    RPMRC_FAIL		= 2,	/*!< Generic failure code. */
    RPMRC_NOTTRUSTED	= 3,	/*!< Signature is OK, but key is not trusted. */
    RPMRC_NOKEY		= 4	/*!< Public key is unavailable. */
} rpmRC;

/*@-redecl@*/
/*@checked@*/
extern struct MacroContext_s * rpmGlobalMacroContext;

/*@checked@*/
extern struct MacroContext_s * rpmCLIMacroContext;

/*@unchecked@*/ /*@observer@*/
extern const char * RPMVERSION;

/*@unchecked@*/ /*@observer@*/
extern const char * rpmNAME;

/*@unchecked@*/ /*@observer@*/
extern const char * rpmEVR;

/*@unchecked@*/
extern int rpmFLAGS;
/*@=redecl@*/

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmts
 * The RPM Transaction Set.
 * Transaction sets are inherently unordered! RPM may reorder transaction
 * sets to reduce errors. In general, installs/upgrades are done before
 * strict removals, and prerequisite ordering is done on installs/upgrades.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmts_s * rpmts;

/** \ingroup rpmbuild
 */
typedef struct Spec_s * Spec;

/** \ingroup rpmts
 * An added/available package retrieval key.
 */
typedef /*@abstract@*/ void * alKey;
#define	RPMAL_NOMATCH	((alKey)-1L)

/** \ingroup rpmts
 * An added/available package retrieval index.
 */
/*@-mutrep@*/
typedef /*@abstract@*/ int alNum;
/*@=mutrep@*/

/** \ingroup rpmds 
 * Dependency tag sets from a header, so that a header can be discarded early.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmds_s * rpmds;

/** \ingroup rpmds 
 * Container for commonly extracted dependency set(s).
 */
typedef struct rpmPRCO_s * rpmPRCO;

/** \ingroup rpmfi
 * File info tag sets from a header, so that a header can be discarded early.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmfi_s * rpmfi;

/** \ingroup rpmte
 * An element of a transaction set, i.e. a TR_ADDED or TR_REMOVED package.
 */
typedef /*@abstract@*/ struct rpmte_s * rpmte;

/** \ingroup rpmdb
 * Database of headers and tag value indices.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmdb_s * rpmdb;

/** \ingroup rpmdb
 * Database iterator.
 */
typedef /*@abstract@*/ struct _rpmdbMatchIterator * rpmdbMatchIterator;

/** \ingroup rpmgi
 * Generalized iterator.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmgi_s * rpmgi;

/**
 * Automatically generated table of tag name/value pairs.
 */
/*@-redecl@*/
/*@observer@*/ /*@unchecked@*/
extern const struct headerTagTableEntry_s * rpmTagTable;
/*@=redecl@*/

/**
 * Number of entries in rpmTagTable.
 */
/*@-redecl@*/
/*@unchecked@*/
extern const int rpmTagTableSize;

/*@unchecked@*/
extern headerTagIndices rpmTags;
/*@=redecl@*/

/**
 * Table of query format extensions.
 * @note Chains to headerDefaultFormats[].
 */
/*@-redecl@*/
/*@unchecked@*/
extern const struct headerSprintfExtension_s rpmHeaderFormats[];
/*@=redecl@*/

/**
 * Pseudo-tags used by the rpmdb and rpmgi iterator API's.
 */
#define	RPMDBI_PACKAGES		0	/* Installed package headers. */
#define	RPMDBI_DEPENDS		1	/* Dependency resolution cache. */
#define	RPMDBI_LABEL		2	/* Fingerprint search marker. */
#define	RPMDBI_ADDED		3	/* Added package headers. */
#define	RPMDBI_REMOVED		4	/* Removed package headers. */
#define	RPMDBI_AVAILABLE	5	/* Available package headers. */
#define	RPMDBI_HDLIST		6	/* (rpmgi) Header list. */
#define	RPMDBI_ARGLIST		7	/* (rpmgi) Argument list. */
#define	RPMDBI_FTSWALK		8	/* (rpmgi) File tree  walk. */

/** \ingroup header
 * Tags identify data in package headers.
 * @note tags should not have value 0!
 */
/** @todo: Somehow supply type **/
typedef enum rpmTag_e {

    RPMTAG_HEADERIMAGE		= HEADER_IMAGE,		/*!< internal Current image. */
    RPMTAG_HEADERSIGNATURES	= HEADER_SIGNATURES,	/*!< internal Signatures. */
    RPMTAG_HEADERIMMUTABLE	= HEADER_IMMUTABLE,	/*!< x Original image. */
/*@-enummemuse@*/
    RPMTAG_HEADERREGIONS	= HEADER_REGIONS,	/*!< internal Regions. */

    RPMTAG_HEADERI18NTABLE	= HEADER_I18NTABLE, /*!< s[] I18N string locales. */
/*@=enummemuse@*/

/* Retrofit (and uniqify) signature tags for use by tagName() and rpmQuery. */
/* the md5 sum was broken *twice* on big endian machines */
/* XXX 2nd underscore prevents tagTable generation */
    RPMTAG_SIG_BASE		= HEADER_SIGBASE,
    RPMTAG_SIGSIZE		= RPMTAG_SIG_BASE+1,	/* i */
    RPMTAG_SIGLEMD5_1		= RPMTAG_SIG_BASE+2,	/* internal - obsolete */
    RPMTAG_SIGPGP		= RPMTAG_SIG_BASE+3,	/* x */
    RPMTAG_SIGLEMD5_2		= RPMTAG_SIG_BASE+4,	/* x internal - obsolete */
    RPMTAG_SIGMD5	        = RPMTAG_SIG_BASE+5,	/* x */
#define	RPMTAG_PKGID	RPMTAG_SIGMD5			/* x */
    RPMTAG_SIGGPG	        = RPMTAG_SIG_BASE+6,	/* x */
    RPMTAG_SIGPGP5	        = RPMTAG_SIG_BASE+7,	/* internal - obsolete */

    RPMTAG_BADSHA1_1		= RPMTAG_SIG_BASE+8,	/* internal - obsolete */
    RPMTAG_BADSHA1_2		= RPMTAG_SIG_BASE+9,	/* internal - obsolete */
    RPMTAG_PUBKEYS		= RPMTAG_SIG_BASE+10,	/* s[] */
    RPMTAG_DSAHEADER		= RPMTAG_SIG_BASE+11,	/* x */
    RPMTAG_RSAHEADER		= RPMTAG_SIG_BASE+12,	/* x */
    RPMTAG_SHA1HEADER		= RPMTAG_SIG_BASE+13,	/* s */
#define	RPMTAG_HDRID	RPMTAG_SHA1HEADER	/* s */

    RPMTAG_NAME  		= 1000,	/* s */
#define	RPMTAG_N	RPMTAG_NAME	/* s */
    RPMTAG_VERSION		= 1001,	/* s */
#define	RPMTAG_V	RPMTAG_VERSION	/* s */
    RPMTAG_RELEASE		= 1002,	/* s */
#define	RPMTAG_R	RPMTAG_RELEASE	/* s */
    RPMTAG_EPOCH   		= 1003,	/* i */
#define	RPMTAG_E	RPMTAG_EPOCH	/* i */
    RPMTAG_SUMMARY		= 1004,	/* s{} */
    RPMTAG_DESCRIPTION		= 1005,	/* s{} */
    RPMTAG_BUILDTIME		= 1006,	/* i */
    RPMTAG_BUILDHOST		= 1007,	/* s */
    RPMTAG_INSTALLTIME		= 1008,	/* i */
    RPMTAG_SIZE			= 1009,	/* i */
    RPMTAG_DISTRIBUTION		= 1010,	/* s */
    RPMTAG_VENDOR		= 1011,	/* s */
    RPMTAG_GIF			= 1012,	/* x */
    RPMTAG_XPM			= 1013,	/* x */
    RPMTAG_LICENSE		= 1014,	/* s */
    RPMTAG_PACKAGER		= 1015,	/* s */
    RPMTAG_GROUP		= 1016,	/* s{} */
/*@-enummemuse@*/
    RPMTAG_CHANGELOG		= 1017, /* s[] internal */
/*@=enummemuse@*/
    RPMTAG_SOURCE		= 1018,	/* s[] */
    RPMTAG_PATCH		= 1019,	/* s[] */
    RPMTAG_URL			= 1020,	/* s */
    RPMTAG_OS			= 1021,	/* s legacy used int */
    RPMTAG_ARCH			= 1022,	/* s legacy used int */
    RPMTAG_PREIN		= 1023,	/* s */
    RPMTAG_POSTIN		= 1024,	/* s */
    RPMTAG_PREUN		= 1025,	/* s */
    RPMTAG_POSTUN		= 1026,	/* s */
    RPMTAG_OLDFILENAMES		= 1027, /* s[] obsolete */
    RPMTAG_FILESIZES		= 1028,	/* i[] */
    RPMTAG_FILESTATES		= 1029, /* c[] */
    RPMTAG_FILEMODES		= 1030,	/* h[] */
    RPMTAG_FILEUIDS		= 1031, /* i[] internal */
    RPMTAG_FILEGIDS		= 1032, /* i[] internal */
    RPMTAG_FILERDEVS		= 1033,	/* h[] */
    RPMTAG_FILEMTIMES		= 1034, /* i[] */
    RPMTAG_FILEDIGESTS		= 1035,	/* s[] */
#define RPMTAG_FILEMD5S	RPMTAG_FILEDIGESTS /* s[] */
    RPMTAG_FILELINKTOS		= 1036,	/* s[] */
    RPMTAG_FILEFLAGS		= 1037,	/* i[] */
/*@-enummemuse@*/
    RPMTAG_ROOT			= 1038, /* internal - obsolete */
/*@=enummemuse@*/
    RPMTAG_FILEUSERNAME		= 1039,	/* s[] */
    RPMTAG_FILEGROUPNAME	= 1040,	/* s[] */
/*@-enummemuse@*/
    RPMTAG_EXCLUDE		= 1041, /* internal - obsolete */
    RPMTAG_EXCLUSIVE		= 1042, /* internal - obsolete */
/*@=enummemuse@*/
    RPMTAG_ICON			= 1043, /* x */
    RPMTAG_SOURCERPM		= 1044,	/* s */
    RPMTAG_FILEVERIFYFLAGS	= 1045,	/* i[] */
    RPMTAG_ARCHIVESIZE		= 1046,	/* i */
    RPMTAG_PROVIDENAME		= 1047,	/* s[] */
#define	RPMTAG_PROVIDES RPMTAG_PROVIDENAME	/* s[] */
#define	RPMTAG_P	RPMTAG_PROVIDENAME	/* s[] */
    RPMTAG_REQUIREFLAGS		= 1048,	/* i[] */
    RPMTAG_REQUIRENAME		= 1049,	/* s[] */
#define	RPMTAG_REQUIRES RPMTAG_REQUIRENAME	/* s[] */
    RPMTAG_REQUIREVERSION	= 1050,	/* s[] */
    RPMTAG_NOSOURCE		= 1051, /* i internal */
    RPMTAG_NOPATCH		= 1052, /* i internal */
    RPMTAG_CONFLICTFLAGS	= 1053, /* i[] */
    RPMTAG_CONFLICTNAME		= 1054,	/* s[] */
#define	RPMTAG_CONFLICTS RPMTAG_CONFLICTNAME	/* s[] */
#define	RPMTAG_C	RPMTAG_CONFLICTNAME	/* s[] */
    RPMTAG_CONFLICTVERSION	= 1055,	/* s[] */
    RPMTAG_DEFAULTPREFIX	= 1056, /* s internal - deprecated */
    RPMTAG_BUILDROOT		= 1057, /* s internal */
    RPMTAG_INSTALLPREFIX	= 1058, /* s internal - deprecated */
    RPMTAG_EXCLUDEARCH		= 1059, /* s[] */
    RPMTAG_EXCLUDEOS		= 1060, /* s[] */
    RPMTAG_EXCLUSIVEARCH	= 1061, /* s[] */
    RPMTAG_EXCLUSIVEOS		= 1062, /* s[] */
    RPMTAG_AUTOREQPROV		= 1063, /* s internal */
    RPMTAG_RPMVERSION		= 1064,	/* s */
    RPMTAG_TRIGGERSCRIPTS	= 1065,	/* s[] */
    RPMTAG_TRIGGERNAME		= 1066,	/* s[] */
    RPMTAG_TRIGGERVERSION	= 1067,	/* s[] */
    RPMTAG_TRIGGERFLAGS		= 1068,	/* i[] */
    RPMTAG_TRIGGERINDEX		= 1069,	/* i[] */
    RPMTAG_VERIFYSCRIPT		= 1079,	/* s */
    RPMTAG_CHANGELOGTIME	= 1080,	/* i[] */
    RPMTAG_CHANGELOGNAME	= 1081,	/* s[] */
    RPMTAG_CHANGELOGTEXT	= 1082,	/* s[] */
/*@-enummemuse@*/
    RPMTAG_BROKENMD5		= 1083, /* internal - obsolete */
/*@=enummemuse@*/
    RPMTAG_PREREQ		= 1084, /* internal */
    RPMTAG_PREINPROG		= 1085,	/* s */
    RPMTAG_POSTINPROG		= 1086,	/* s */
    RPMTAG_PREUNPROG		= 1087,	/* s */
    RPMTAG_POSTUNPROG		= 1088,	/* s */
    RPMTAG_BUILDARCHS		= 1089, /* s[] */
    RPMTAG_OBSOLETENAME		= 1090,	/* s[] */
#define	RPMTAG_OBSOLETES RPMTAG_OBSOLETENAME	/* s[] */
#define	RPMTAG_O	RPMTAG_OBSOLETENAME	/* s[] */
    RPMTAG_VERIFYSCRIPTPROG	= 1091,	/* s */
    RPMTAG_TRIGGERSCRIPTPROG	= 1092,	/* s[] */
    RPMTAG_DOCDIR		= 1093, /* internal */
    RPMTAG_COOKIE		= 1094,	/* s */
    RPMTAG_FILEDEVICES		= 1095,	/* i[] */
    RPMTAG_FILEINODES		= 1096,	/* i[] */
    RPMTAG_FILELANGS		= 1097,	/* s[] */
    RPMTAG_PREFIXES		= 1098,	/* s[] */
    RPMTAG_INSTPREFIXES		= 1099,	/* s[] */
    RPMTAG_TRIGGERIN		= 1100, /* internal */
    RPMTAG_TRIGGERUN		= 1101, /* internal */
    RPMTAG_TRIGGERPOSTUN	= 1102, /* internal */
    RPMTAG_AUTOREQ		= 1103, /* internal */
    RPMTAG_AUTOPROV		= 1104, /* internal */
/*@-enummemuse@*/
    RPMTAG_CAPABILITY		= 1105, /* i legacy - obsolete */
/*@=enummemuse@*/
    RPMTAG_SOURCEPACKAGE	= 1106, /* i legacy - obsolete */
/*@-enummemuse@*/
    RPMTAG_OLDORIGFILENAMES	= 1107, /* internal - obsolete */
/*@=enummemuse@*/
    RPMTAG_BUILDPREREQ		= 1108, /* internal */
    RPMTAG_BUILDREQUIRES	= 1109, /* internal */
    RPMTAG_BUILDCONFLICTS	= 1110, /* internal */
/*@-enummemuse@*/
    RPMTAG_BUILDMACROS		= 1111, /* internal - unused */
/*@=enummemuse@*/
    RPMTAG_PROVIDEFLAGS		= 1112,	/* i[] */
    RPMTAG_PROVIDEVERSION	= 1113,	/* s[] */
    RPMTAG_OBSOLETEFLAGS	= 1114,	/* i[] */
    RPMTAG_OBSOLETEVERSION	= 1115,	/* s[] */
    RPMTAG_DIRINDEXES		= 1116,	/* i[] */
    RPMTAG_BASENAMES		= 1117,	/* s[] */
    RPMTAG_DIRNAMES		= 1118,	/* s[] */
    RPMTAG_ORIGDIRINDEXES	= 1119, /* i[] relocation */
    RPMTAG_ORIGBASENAMES	= 1120, /* s[] relocation */
    RPMTAG_ORIGDIRNAMES		= 1121, /* s[] relocation */
    RPMTAG_OPTFLAGS		= 1122,	/* s */
    RPMTAG_DISTURL		= 1123,	/* s */
    RPMTAG_PAYLOADFORMAT	= 1124,	/* s */
    RPMTAG_PAYLOADCOMPRESSOR	= 1125,	/* s */
    RPMTAG_PAYLOADFLAGS		= 1126,	/* s */
    RPMTAG_INSTALLCOLOR		= 1127, /* i transaction color when installed */
    RPMTAG_INSTALLTID		= 1128,	/* i */
    RPMTAG_REMOVETID		= 1129,	/* i */
/*@-enummemuse@*/
    RPMTAG_SHA1RHN		= 1130, /* internal - obsolete */
/*@=enummemuse@*/
    RPMTAG_RHNPLATFORM		= 1131,	/* s deprecated */
    RPMTAG_PLATFORM		= 1132,	/* s */
    RPMTAG_PATCHESNAME		= 1133, /* s[] deprecated placeholder (SuSE) */
    RPMTAG_PATCHESFLAGS		= 1134, /* i[] deprecated placeholder (SuSE) */
    RPMTAG_PATCHESVERSION	= 1135, /* s[] deprecated placeholder (SuSE) */
    RPMTAG_CACHECTIME		= 1136,	/* i */
    RPMTAG_CACHEPKGPATH		= 1137,	/* s */
    RPMTAG_CACHEPKGSIZE		= 1138,	/* i */
    RPMTAG_CACHEPKGMTIME	= 1139,	/* i */
    RPMTAG_FILECOLORS		= 1140,	/* i[] */
    RPMTAG_FILECLASS		= 1141,	/* i[] */
    RPMTAG_CLASSDICT		= 1142,	/* s[] */
    RPMTAG_FILEDEPENDSX		= 1143,	/* i[] */
    RPMTAG_FILEDEPENDSN		= 1144,	/* i[] */
    RPMTAG_DEPENDSDICT		= 1145,	/* i[] */
    RPMTAG_SOURCEPKGID		= 1146,	/* x */
    RPMTAG_FILECONTEXTS		= 1147,	/* s[] */
    RPMTAG_FSCONTEXTS		= 1148,	/* s[] extension */
    RPMTAG_RECONTEXTS		= 1149,	/* s[] extension */
    RPMTAG_POLICIES		= 1150,	/* s[] selinux *.te policy file. */
    RPMTAG_PRETRANS		= 1151,	/* s */
    RPMTAG_POSTTRANS		= 1152,	/* s */
    RPMTAG_PRETRANSPROG		= 1153,	/* s */
    RPMTAG_POSTTRANSPROG	= 1154,	/* s */
    RPMTAG_DISTTAG		= 1155,	/* s */
    RPMTAG_SUGGESTSNAME		= 1156,	/* s[] extension */
#define	RPMTAG_SUGGESTS RPMTAG_SUGGESTSNAME	/* s[] */
    RPMTAG_SUGGESTSVERSION	= 1157,	/* s[] extension */
    RPMTAG_SUGGESTSFLAGS	= 1158,	/* i[] extension */
    RPMTAG_ENHANCESNAME		= 1159,	/* s[] extension placeholder */
#define	RPMTAG_ENHANCES RPMTAG_ENHANCESNAME	/* s[] */
    RPMTAG_ENHANCESVERSION	= 1160,	/* s[] extension placeholder */
    RPMTAG_ENHANCESFLAGS	= 1161,	/* i[] extension placeholder */
    RPMTAG_PRIORITY		= 1162, /* i[] extension placeholder */
    RPMTAG_CVSID		= 1163, /* s */
#define	RPMTAG_SVNID	RPMTAG_CVSID	/* s */
    RPMTAG_BLINKPKGID		= 1164, /* s[] */
    RPMTAG_BLINKHDRID		= 1165, /* s[] */
    RPMTAG_BLINKNEVRA		= 1166, /* s[] */
    RPMTAG_FLINKPKGID		= 1167, /* s[] */
    RPMTAG_FLINKHDRID		= 1168, /* s[] */
    RPMTAG_FLINKNEVRA		= 1169, /* s[] */
    RPMTAG_PACKAGEORIGIN	= 1170, /* s */
    RPMTAG_TRIGGERPREIN		= 1171, /* internal */
    RPMTAG_BUILDSUGGESTS	= 1172, /* internal */
    RPMTAG_BUILDENHANCES	= 1173, /* internal */
    RPMTAG_SCRIPTSTATES		= 1174, /* i[] scriptlet exit codes */
    RPMTAG_SCRIPTMETRICS	= 1175, /* i[] scriptlet execution times */
    RPMTAG_BUILDCPUCLOCK	= 1176, /* i */
    RPMTAG_FILEDIGESTALGOS	= 1177, /* i[] */
    RPMTAG_VARIANTS		= 1178, /* s[] */
    RPMTAG_XMAJOR		= 1179, /* i */
    RPMTAG_XMINOR		= 1180, /* i */
    RPMTAG_REPOTAG		= 1181,	/* s */
    RPMTAG_KEYWORDS		= 1182,	/* s[] */
    RPMTAG_BUILDPLATFORMS	= 1183,	/* s[] */
    RPMTAG_PACKAGECOLOR		= 1184, /* i */
    RPMTAG_PACKAGEPREFCOLOR	= 1185, /* i (unimplemented) */
    RPMTAG_XATTRSDICT		= 1186, /* s[] (unimplemented) */
    RPMTAG_FILEXATTRSX		= 1187, /* i[] (unimplemented) */
    RPMTAG_DEPATTRSDICT		= 1188, /* s[] (unimplemented) */
    RPMTAG_CONFLICTATTRSX	= 1189, /* i[] (unimplemented) */
    RPMTAG_OBSOLETEATTRSX	= 1190, /* i[] (unimplemented) */
    RPMTAG_PROVIDEATTRSX	= 1191, /* i[] (unimplemented) */
    RPMTAG_REQUIREATTRSX	= 1192, /* i[] (unimplemented) */
    RPMTAG_BUILDPROVIDES	= 1193, /* internal */
    RPMTAG_BUILDOBSOLETES	= 1194, /* internal */
    RPMTAG_DBINSTANCE		= 1195, /* i */
    RPMTAG_NVRA			= 1196, /* s */
    RPMTAG_FILEPATHS		= 1197, /* s[] */
    RPMTAG_ORIGPATHS		= 1198, /* s[] */

/*@-enummemuse@*/
    RPMTAG_FIRSTFREE_TAG	/*!< internal */
/*@=enummemuse@*/
} rpmTag;

#define	RPMTAG_EXTERNAL_TAG		1000000

/**
 * Scriptlet identifiers.
 */
typedef enum rpmScriptID_e {
    RPMSCRIPT_UNKNOWN		=  0,	/*!< unknown scriptlet */
    RPMSCRIPT_PRETRANS		=  1,	/*!< %pretrans scriptlet */
    RPMSCRIPT_TRIGGERPREIN	=  2,	/*!< %triggerprein scriptlet */
    RPMSCRIPT_PREIN		=  3,	/*!< %pre scriptlet */
    RPMSCRIPT_POSTIN		=  4,	/*!< %post scriptlet  */
    RPMSCRIPT_TRIGGERIN		=  5,	/*!< %triggerin scriptlet  */
    RPMSCRIPT_TRIGGERUN		=  6,	/*!< %triggerun scriptlet  */
    RPMSCRIPT_PREUN		=  7,	/*!< %preun scriptlet  */
    RPMSCRIPT_POSTUN		=  8,	/*!< %postun scriptlet  */
    RPMSCRIPT_TRIGGERPOSTUN	=  9,	/*!< %triggerpostun scriptlet  */
    RPMSCRIPT_POSTTRANS		= 10,	/*!< %posttrans scriptlet  */
	/* 11-15 unused */
    RPMSCRIPT_VERIFY		= 16,	/*!< %verify scriptlet  */
    RPMSCRIPT_MAX		= 32
} rpmScriptID;

/**
 * Scriptlet states (when installed).
 */
typedef enum rpmScriptState_e {
    RPMSCRIPT_STATE_UNKNOWN	= 0,
	/* 0-15 reserved for waitpid return. */
    RPMSCRIPT_STATE_EXEC	= (1 << 16), /*!< scriptlet was exec'd */
    RPMSCRIPT_STATE_REAPED	= (1 << 17), /*!< scriptlet was reaped */
	/* 18-23 unused */
    RPMSCRIPT_STATE_SELINUX	= (1 << 24), /*!< scriptlet exec by SELinux */
    RPMSCRIPT_STATE_EMULATOR	= (1 << 25), /*!< scriptlet exec in emulator */
    RPMSCRIPT_STATE_LUA		= (1 << 26)  /*!< scriptlet exec with lua */
} rpmScriptState;

/**
 * File States (when installed).
 */
typedef enum rpmfileState_e {
    RPMFILE_STATE_NORMAL 	= 0,
    RPMFILE_STATE_REPLACED 	= 1,
    RPMFILE_STATE_NOTINSTALLED	= 2,
    RPMFILE_STATE_NETSHARED	= 3,
    RPMFILE_STATE_WRONGCOLOR	= 4
} rpmfileState;
#define	RPMFILE_STATE_MISSING	-1	/* XXX used for unavailable data */

/**
 * File Attributes.
 */
typedef	enum rpmfileAttrs_e {
/*@-enummemuse@*/
    RPMFILE_NONE	= 0,
/*@=enummemuse@*/
    RPMFILE_CONFIG	= (1 <<  0),	/*!< from %%config */
    RPMFILE_DOC		= (1 <<  1),	/*!< from %%doc */
    RPMFILE_ICON	= (1 <<  2),	/*!< from Icon: */
    RPMFILE_MISSINGOK	= (1 <<  3),	/*!< from %%config(missingok) */
    RPMFILE_NOREPLACE	= (1 <<  4),	/*!< from %%config(noreplace) */
    RPMFILE_SPECFILE	= (1 <<  5),	/*!< the specfile (srpm only). */
    RPMFILE_GHOST	= (1 <<  6),	/*!< from %%ghost */
    RPMFILE_LICENSE	= (1 <<  7),	/*!< from %%license */
    RPMFILE_README	= (1 <<  8),	/*!< from %%readme */
    RPMFILE_EXCLUDE	= (1 <<  9),	/*!< from %%exclude, internal */
    RPMFILE_UNPATCHED	= (1 << 10),	/*!< (deprecated) placeholder (SuSE) */
    RPMFILE_PUBKEY	= (1 << 11),	/*!< from %%pubkey */
    RPMFILE_POLICY	= (1 << 12),	/*!< from %%policy */
    RPMFILE_EXISTS	= (1 << 13),	/*!< did lstat(fn, st) succeed? */
    RPMFILE_SPARSE	= (1 << 14),	/*!< was ((512*st->st_blocks) < st->st_size) ? */
    RPMFILE_TYPED	= (1 << 15),	/*!< (unimplemented) from %%spook */
    RPMFILE_SOURCE	= (1 << 16),	/*!< from SourceN: (srpm only). */
    RPMFILE_PATCH	= (1 << 17),	/*!< from PatchN: (srpm only). */
    RPMFILE_OPTIONAL	= (1 << 18)	/*!< from %%optional. */
} rpmfileAttrs;

#define	RPMFILE_SPOOK	(RPMFILE_GHOST|RPMFILE_TYPED)
#define	RPMFILE_ALL	~(RPMFILE_NONE)

/* ==================================================================== */
/** \name RPMRC */
/*@{*/

/** \ingroup rpmrc
 * Build and install arch/os table identifiers.
 * @deprecated Eliminate from API.
 */
enum rpm_machtable_e {
    RPM_MACHTABLE_INSTARCH	= 0,	/*!< Install platform architecture. */
    RPM_MACHTABLE_INSTOS	= 1,	/*!< Install platform operating system. */
    RPM_MACHTABLE_BUILDARCH	= 2,	/*!< Build platform architecture. */
    RPM_MACHTABLE_BUILDOS	= 3	/*!< Build platform operating system. */
};
#define	RPM_MACHTABLE_COUNT	4	/*!< No. of arch/os tables. */

/** \ingroup rpmrc
 * Read macro configuration file(s) for a target.
 * @param file		NULL always
 * @param target	target platform (NULL uses default)
 * @return		0 on success, -1 on error
 */
int rpmReadConfigFiles(/*@null@*/ const char * file,
		/*@null@*/ const char * target)
	/*@globals rpmGlobalMacroContext, rpmCLIMacroContext, h_errno,
		fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, rpmCLIMacroContext,
		fileSystem, internalState @*/;

/*@only@*/ /*@null@*/ /*@unchecked@*/
extern void * platpat;
/*@unchecked@*/
extern int nplatpat;

/** \ingroup rpmrc
 * Return score of a platform string.
 * A platform score measures the "nearness" of a platform string wrto
 * configured platform patterns. The returned score is the line number
 * of the 1st pattern in /etc/rpm/platform that matches the input string.
 *
 * @param platform	cpu-vendor-os platform string
 * @param mi_re		pattern array (NULL uses /etc/rpm/platform patterns)
 * @param mi_nre	no. of patterns
 * @return		platform score (0 is no match, lower is preferred)
 */
int rpmPlatformScore(const char * platform, /*@null@*/ void * mi_re, int mi_nre)
	/*@modifies mi_re @*/;

/** \ingroup rpmrc
 * Display current rpmrc (and macro) configuration.
 * @param fp		output file handle
 * @return		0 always
 */
int rpmShowRC(FILE * fp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies *fp, rpmGlobalMacroContext, fileSystem, internalState  @*/;

/** \ingroup rpmrc
 * @deprecated Use addMacro to set _target_* macros.
 * @todo Eliminate from API.
 # @note Only used by build code.
 * @param archTable
 * @param osTable
 */
void rpmSetTables(int archTable, int osTable)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/;

/** \ingroup rpmrc
 * Set current arch/os names.
 * NULL as argument is set to the default value (munged uname())
 * pushed through a translation table (if appropriate).
 * @deprecated Use addMacro to set _target_* macros.
 * @todo Eliminate from API.
 *
 * @param arch		arch name (or NULL)
 * @param os		os name (or NULL)
 */
void rpmSetMachine(/*@null@*/ const char * arch, /*@null@*/ const char * os)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/;

/** \ingroup rpmrc
 * Destroy rpmrc arch/os compatibility tables.
 * @todo Eliminate from API.
 */
void rpmFreeRpmrc(void)
	/*@globals platpat, nplatpat, internalState @*/
	/*@modifies platpat, nplatpat, internalState @*/;

/*@}*/
/* ==================================================================== */
/** \name RPMTS */
/*@{*/
/**
 * Prototype for headerFreeData() vector.
 *
 * @param data		address of data (or NULL)
 * @param type		type of data (or -1 to force free)
 * @return		NULL always
 */
typedef /*@null@*/
    void * (*HFD_t) (/*@only@*/ /*@null@*/ const void * data, rpmTagType type)
	/*@modifies data @*/;

/**
 * Prototype for headerGetEntry() vector.
 *
 * Will never return RPM_I18NSTRING_TYPE! RPM_STRING_TYPE elements with
 * RPM_I18NSTRING_TYPE equivalent entries are translated (if HEADER_I18NTABLE
 * entry is present).
 *
 * @param h		header
 * @param tag		tag
 * @retval *type	tag value data type (or NULL)
 * @retval *p		tag value(s) (or NULL)
 * @retval *c		number of values (or NULL)
 * @return		1 on success, 0 on failure
 */
typedef int (*HGE_t) (Header h, rpmTag tag,
			/*@null@*/ /*@out@*/ rpmTagType * type,
			/*@null@*/ /*@out@*/ void * p,
			/*@null@*/ /*@out@*/ int_32 * c)
	/*@modifies *type, *p, *c @*/;

/**
 * Prototype for headerAddEntry() vector.
 *
 * Duplicate tags are okay, but only defined for iteration (with the
 * exceptions noted below). While you are allowed to add i18n string
 * arrays through this function, you probably don't mean to. See
 * headerAddI18NString() instead.
 *
 * @param h             header
 * @param tag           tag
 * @param type          tag value data type
 * @param p             tag value(s)
 * @param c             number of values
 * @return              1 on success, 0 on failure
 */
typedef int (*HAE_t) (Header h, rpmTag tag, rpmTagType type,
			const void * p, int_32 c)
	/*@modifies h @*/;

/**
 * Prototype for headerModifyEntry() vector.
 * If there are multiple entries with this tag, the first one gets replaced.
 *
 * @param h		header
 * @param tag		tag
 * @param type		tag value data type
 * @param p		tag value(s)
 * @param c		number of values
 * @return		1 on success, 0 on failure
 */
typedef int (*HME_t) (Header h, rpmTag tag, rpmTagType type,
			const void * p, int_32 c)
	/*@modifies h @*/;

/**
 * Prototype for headerRemoveEntry() vector.
 * Delete tag in header.
 * Removes all entries of type tag from the header, returns 1 if none were
 * found.
 *
 * @param h		header
 * @param tag		tag
 * @return		0 on success, 1 on failure (INCONSISTENT)
 */
typedef int (*HRE_t) (Header h, int_32 tag)
	/*@modifies h @*/;

/**
 * @todo Generalize filter mechanism.
 */
typedef enum rpmprobFilterFlags_e {
    RPMPROB_FILTER_NONE		= 0,
    RPMPROB_FILTER_IGNOREOS	= (1 << 0),	/*!< from --ignoreos */
    RPMPROB_FILTER_IGNOREARCH	= (1 << 1),	/*!< from --ignorearch */
    RPMPROB_FILTER_REPLACEPKG	= (1 << 2),	/*!< from --replacepkgs */
    RPMPROB_FILTER_FORCERELOCATE= (1 << 3),	/*!< from --badreloc */
    RPMPROB_FILTER_REPLACENEWFILES= (1 << 4),	/*!< from --replacefiles */
    RPMPROB_FILTER_REPLACEOLDFILES= (1 << 5),	/*!< from --replacefiles */
    RPMPROB_FILTER_OLDPACKAGE	= (1 << 6),	/*!< from --oldpackage */
    RPMPROB_FILTER_DISKSPACE	= (1 << 7),	/*!< from --ignoresize */
    RPMPROB_FILTER_DISKNODES	= (1 << 8)	/*!< from --ignoresize */
} rpmprobFilterFlags;

/**
 * We pass these around as an array with a sentinel.
 */
typedef struct rpmRelocation_s * rpmRelocation;
#if !defined(SWIG)
struct rpmRelocation_s {
/*@only@*/ /*@null@*/
    const char * oldPath;	/*!< NULL here evals to RPMTAG_DEFAULTPREFIX, */
/*@only@*/ /*@null@*/
    const char * newPath;	/*!< NULL means to omit the file completely! */
};
#endif

/**
 * Compare headers to determine which header is "newer".
 * @deprecated Use rpmdsCompare instead.
 * @param first		1st header
 * @param second	2nd header
 * @return		result of comparison
 */
int rpmVersionCompare(Header first, Header second)
	/*@*/;

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

#define XFA_SKIPPING(_a)	\
    ((_a) == FA_SKIP || (_a) == FA_SKIPNSTATE || (_a) == FA_SKIPNETSHARED || (_a) == FA_SKIPCOLOR)

/** \ingroup payload
 * Iterator across package file info, forward on install, backward on erase.
 */
typedef /*@abstract@*/ struct fsmIterator_s * FSMI_t;

/** \ingroup payload
 * File state machine data.
 */
typedef /*@abstract@*/ struct fsm_s * FSM_t;

/** \ingroup rpmts
 * Package state machine data.
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct rpmpsm_s * rpmpsm;

/** 
 * Return checked and loaded header.
 * @param ts		transaction set
 * @param _fd		file handle
 * @retval hdrp		address of header (or NULL)
 * @retval *msg		verification error message (or NULL)
 * @return		RPMRC_OK on success
 */
rpmRC rpmReadHeader(rpmts ts, void * _fd, /*@out@*/ Header *hdrp,
		/*@out@*/ /*@null@*/ const char ** msg)
        /*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
        /*@modifies ts, *_fd, *hdrp, *msg, rpmGlobalMacroContext,
                fileSystem, internalState @*/;

/**
 * Return package header from file handle, verifying digests/signatures.
 * @param ts		transaction set
 * @param _fd		file handle
 * @param fn		file name
 * @retval hdrp		address of header (or NULL)
 * @return		RPMRC_OK on success
 */
rpmRC rpmReadPackageFile(rpmts ts, void * _fd,
		const char * fn, /*@null@*/ /*@out@*/ Header * hdrp)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, *_fd, *hdrp, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/**
 * Install source package.
 * @param ts		transaction set
 * @param _fd		file handle
 * @retval specFilePtr	address of spec file name (or NULL)
 * @retval cookie	address of cookie pointer (or NULL)
 * @return		rpmRC return code
 */
rpmRC rpmInstallSourcePackage(rpmts ts, void * _fd,
			/*@null@*/ /*@out@*/ const char ** specFilePtr,
			/*@null@*/ /*@out@*/ const char ** cookie)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, _fd, *specFilePtr, *cookie, rpmGlobalMacroContext,
		fileSystem, internalState @*/;

/** \ingroup rpmts
 * Bit(s) to control rpmtsCheck() and rpmtsOrder() operation.
 * @todo Move to rpmts.h.
 */
typedef enum rpmdepFlags_e {
    RPMDEPS_FLAG_NONE		= 0,
    RPMDEPS_FLAG_NOUPGRADE	= (1 <<  0),	/*!< from --noupgrade */
    RPMDEPS_FLAG_NOREQUIRES	= (1 <<  1),	/*!< from --norequires */
    RPMDEPS_FLAG_NOCONFLICTS	= (1 <<  2),	/*!< from --noconflicts */
    RPMDEPS_FLAG_NOOBSOLETES	= (1 <<  3),	/*!< from --noobsoletes */
    RPMDEPS_FLAG_NOPARENTDIRS	= (1 <<  4),	/*!< from --noparentdirs */
    RPMDEPS_FLAG_NOLINKTOS	= (1 <<  5),	/*!< from --nolinktos */
    RPMDEPS_FLAG_ANACONDA	= (1 <<  6),	/*!< from --anaconda */
    RPMDEPS_FLAG_NOSUGGEST	= (1 <<  7),	/*!< from --nosuggest */
    RPMDEPS_FLAG_ADDINDEPS	= (1 <<  8),	/*!< from --aid */
    RPMDEPS_FLAG_DEPLOOPS	= (1 <<  9)	/*!< from --deploops */
} rpmdepFlags;

/** \ingroup rpmts
 * Bit(s) to control rpmtsRun() operation.
 * @todo Move to rpmts.h.
 */
typedef enum rpmtransFlags_e {
    RPMTRANS_FLAG_NONE		= 0,
    RPMTRANS_FLAG_TEST		= (1 <<  0),	/*!< from --test */
    RPMTRANS_FLAG_BUILD_PROBS	= (1 <<  1),	/*!< don't process payload */
    RPMTRANS_FLAG_NOSCRIPTS	= (1 <<  2),	/*!< from --noscripts */
    RPMTRANS_FLAG_JUSTDB	= (1 <<  3),	/*!< from --justdb */
    RPMTRANS_FLAG_NOTRIGGERS	= (1 <<  4),	/*!< from --notriggers */
    RPMTRANS_FLAG_NODOCS	= (1 <<  5),	/*!< from --excludedocs */
    RPMTRANS_FLAG_ALLFILES	= (1 <<  6),	/*!< from --allfiles */
/*@-enummemuse@*/
    RPMTRANS_FLAG_KEEPOBSOLETE	= (1 <<  7),	/*!< @todo Document. */
/*@=enummemuse@*/
    RPMTRANS_FLAG_NOCONTEXTS	= (1 <<  8),	/*!< from --nocontexts */
    RPMTRANS_FLAG_DIRSTASH	= (1 <<  9),	/*!< from --dirstash */
    RPMTRANS_FLAG_REPACKAGE	= (1 << 10),	/*!< from --repackage */

    RPMTRANS_FLAG_PKGCOMMIT	= (1 << 11),
/*@-enummemuse@*/
    RPMTRANS_FLAG_PKGUNDO	= (1 << 12),
/*@=enummemuse@*/
    RPMTRANS_FLAG_COMMIT	= (1 << 13),
/*@-enummemuse@*/
    RPMTRANS_FLAG_UNDO		= (1 << 14),
/*@=enummemuse@*/
    /* 15 unused */

    RPMTRANS_FLAG_NOTRIGGERPREIN= (1 << 16),	/*!< from --notriggerprein */
    RPMTRANS_FLAG_NOPRE		= (1 << 17),	/*!< from --nopre */
    RPMTRANS_FLAG_NOPOST	= (1 << 18),	/*!< from --nopost */
    RPMTRANS_FLAG_NOTRIGGERIN	= (1 << 19),	/*!< from --notriggerin */
    RPMTRANS_FLAG_NOTRIGGERUN	= (1 << 20),	/*!< from --notriggerun */
    RPMTRANS_FLAG_NOPREUN	= (1 << 21),	/*!< from --nopreun */
    RPMTRANS_FLAG_NOPOSTUN	= (1 << 22),	/*!< from --nopostun */
    RPMTRANS_FLAG_NOTRIGGERPOSTUN = (1 << 23),	/*!< from --notriggerpostun */
/*@-enummemuse@*/
    RPMTRANS_FLAG_NOPAYLOAD	= (1 << 24),
/*@=enummemuse@*/
    RPMTRANS_FLAG_APPLYONLY	= (1 << 25),

    /* 26 unused */
    RPMTRANS_FLAG_NOFDIGESTS	= (1 << 27),	/*!< from --nofdigests */
    /* 28-29 unused */
    RPMTRANS_FLAG_NOCONFIGS	= (1 << 30),	/*!< from --noconfigs */
    /* 31 unused */
} rpmtransFlags;

#define	_noTransScripts		\
  ( RPMTRANS_FLAG_NOPRE |	\
    RPMTRANS_FLAG_NOPOST |	\
    RPMTRANS_FLAG_NOPREUN |	\
    RPMTRANS_FLAG_NOPOSTUN	\
  )

#define	_noTransTriggers	\
  ( RPMTRANS_FLAG_NOTRIGGERPREIN | \
    RPMTRANS_FLAG_NOTRIGGERIN |	\
    RPMTRANS_FLAG_NOTRIGGERUN |	\
    RPMTRANS_FLAG_NOTRIGGERPOSTUN \
  )

/*@}*/

#if !defined(SWIG)
/**
 * Return tag name from value.
 * @param tag		tag value
 * @return		tag name, "(unknown)" on not found
 */
/*@-redecl@*/
/*@unused@*/ static inline /*@observer@*/
const char * tagName(int tag)
	/*@*/
{
/*@-type@*/
    return ((*rpmTags->tagName)(tag));
/*@=type@*/
}
/*@=redecl@*/

/**
 * Return tag data type from value.
 * @param tag		tag value
 * @return		tag data type, RPM_NULL_TYPE on not found.
 */
/*@unused@*/ static inline
int tagType(int tag)
	/*@*/
{
/*@-type@*/
    return ((*rpmTags->tagType)(tag));
/*@=type@*/
}

/**
 * Return tag value from name.
 * @param tagstr	name of tag
 * @return		tag value, -1 on not found
 */
/*@unused@*/ static inline
int tagValue(const char * tagstr)
	/*@*/
{
/*@-type@*/
    return ((*rpmTags->tagValue)(tagstr));
/*@=type@*/
}
#endif

/* ==================================================================== */
/** \name RPMK */
/*@{*/

/** \ingroup signature
 * Tags found in signature header from package.
 */
enum rpmtagSignature {
    RPMSIGTAG_SIZE	= 1000,	/*!< internal Header+Payload size in bytes. */
    RPMSIGTAG_LEMD5_1	= 1001,	/*!< internal Broken MD5, take 1 @deprecated legacy. */
    RPMSIGTAG_PGP	= 1002,	/*!< internal PGP 2.6.3 signature. */
    RPMSIGTAG_LEMD5_2	= 1003,	/*!< internal Broken MD5, take 2 @deprecated legacy. */
    RPMSIGTAG_MD5	= 1004,	/*!< internal MD5 signature. */
    RPMSIGTAG_GPG	= 1005, /*!< internal GnuPG signature. */
    RPMSIGTAG_PGP5	= 1006,	/*!< internal PGP5 signature @deprecated legacy. */
    RPMSIGTAG_PAYLOADSIZE = 1007,/*!< internal uncompressed payload size in bytes. */
    RPMSIGTAG_BADSHA1_1	= RPMTAG_BADSHA1_1,	/*!< internal Broken SHA1, take 1. */
    RPMSIGTAG_BADSHA1_2	= RPMTAG_BADSHA1_2,	/*!< internal Broken SHA1, take 2. */
    RPMSIGTAG_SHA1	= RPMTAG_SHA1HEADER,	/*!< internal sha1 header digest. */
    RPMSIGTAG_DSA	= RPMTAG_DSAHEADER,	/*!< internal DSA header signature. */
    RPMSIGTAG_RSA	= RPMTAG_RSAHEADER	/*!< internal RSA header signature. */
};

/*@}*/

#ifdef __cplusplus
}
#endif

#endif	/* H_RPMLIB */

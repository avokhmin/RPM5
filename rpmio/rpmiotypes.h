#ifndef _H_RPMIOTYPES_
#define	_H_RPMIOTYPES_

/** \ingroup rpmio
 * \file rpmio/rpmiotypes.h
 */

/** \ingroup rpmio
 * RPM return codes.
 */
typedef	enum rpmRC_e {
    RPMRC_OK		= 0,	/*!< Generic success code */
    RPMRC_NOTFOUND	= 1,	/*!< Generic not found code. */
    RPMRC_FAIL		= 2,	/*!< Generic failure code. */
    RPMRC_NOTTRUSTED	= 3,	/*!< Signature is OK, but key is not trusted. */
    RPMRC_NOKEY		= 4	/*!< Public key is unavailable. */
} rpmRC;

/** \ingroup rpmio
 * Private int typedefs to avoid C99 portability issues.
 */
typedef	/*@unsignedintegraltype@*/	unsigned char		rpmuint8_t;
typedef /*@unsignedintegraltype@*/	unsigned short		rpmuint16_t;
typedef /*@unsignedintegraltype@*/	unsigned int		rpmuint32_t;
typedef /*@unsignedintegraltype@*/	unsigned long long	rpmuint64_t;

/** \ingroup rpmio
 */
typedef /*@signedintegraltype@*/	int			rpmint32_t;

/** \ingroup rpmio
 */
typedef /*@abstract@*/ struct rpmiob_s * rpmiob;

/** \ingroup rpmio
 */
/*@unchecked@*/
extern size_t _rpmiob_chunk;

/** \ingroup rpmpgp
 */
typedef /*@abstract@*/ struct DIGEST_CTX_s * DIGEST_CTX;

/** \ingroup rpmpgp
 */
typedef /*@abstract@*/ struct pgpPkt_s * pgpPkt;

/** \ingroup rpmpgp
 */
typedef /*@abstract@*/ /*@refcounted@*/ struct pgpDig_s * pgpDig;

/** \ingroup rpmpgp
 */
typedef /*@abstract@*/ struct pgpDigParams_s * pgpDigParams;

/** \ingroup rpmpgp
 */
typedef rpmuint8_t pgpKeyID_t[8];

/** \ingroup rpmpgp
 */
typedef rpmuint8_t pgpTime_t[4];

/** \ingroup rpmpgp
 * Bit(s) to control digest and signature verification.
 */
typedef enum pgpVSFlags_e {
    RPMVSF_DEFAULT	= 0,
    RPMVSF_NOHDRCHK	= (1 <<  0),
    RPMVSF_NEEDPAYLOAD	= (1 <<  1),
    /* bit(s) 2-7 unused */
    RPMVSF_NOSHA1HEADER	= (1 <<  8),
    RPMVSF_NOMD5HEADER	= (1 <<  9),	/* unimplemented */
    RPMVSF_NODSAHEADER	= (1 << 10),
    RPMVSF_NORSAHEADER	= (1 << 11),
    /* bit(s) 12-15 unused */
    RPMVSF_NOSHA1	= (1 << 16),	/* unimplemented */
    RPMVSF_NOMD5	= (1 << 17),
    RPMVSF_NODSA	= (1 << 18),
    RPMVSF_NORSA	= (1 << 19)
    /* bit(s) 20-31 unused */
} pgpVSFlags;

#define	_RPMVSF_NODIGESTS	\
  ( RPMVSF_NOSHA1HEADER |	\
    RPMVSF_NOMD5HEADER |	\
    RPMVSF_NOSHA1 |		\
    RPMVSF_NOMD5 )

#define	_RPMVSF_NOSIGNATURES	\
  ( RPMVSF_NODSAHEADER |	\
    RPMVSF_NORSAHEADER |	\
    RPMVSF_NODSA |		\
    RPMVSF_NORSA )

#define	_RPMVSF_NOHEADER	\
  ( RPMVSF_NOSHA1HEADER |	\
    RPMVSF_NOMD5HEADER |	\
    RPMVSF_NODSAHEADER |	\
    RPMVSF_NORSAHEADER )

#define	_RPMVSF_NOPAYLOAD	\
  ( RPMVSF_NOSHA1 |		\
    RPMVSF_NOMD5 |		\
    RPMVSF_NODSA |		\
    RPMVSF_NORSA )

/*@-redef@*/ /* LCL: ??? */
typedef /*@abstract@*/ const void * fnpyKey;
/*@=redef@*/

/**
 * Bit(s) to identify progress callbacks.
 */
typedef enum rpmCallbackType_e {
    RPMCALLBACK_UNKNOWN         = 0,
    RPMCALLBACK_INST_PROGRESS   = (1 <<  0),
    RPMCALLBACK_INST_START      = (1 <<  1),
    RPMCALLBACK_INST_OPEN_FILE  = (1 <<  2),
    RPMCALLBACK_INST_CLOSE_FILE = (1 <<  3),
    RPMCALLBACK_TRANS_PROGRESS  = (1 <<  4),
    RPMCALLBACK_TRANS_START     = (1 <<  5),
    RPMCALLBACK_TRANS_STOP      = (1 <<  6),
    RPMCALLBACK_UNINST_PROGRESS = (1 <<  7),
    RPMCALLBACK_UNINST_START    = (1 <<  8),
    RPMCALLBACK_UNINST_STOP     = (1 <<  9),
    RPMCALLBACK_REPACKAGE_PROGRESS = (1 << 10),
    RPMCALLBACK_REPACKAGE_START = (1 << 11),
    RPMCALLBACK_REPACKAGE_STOP  = (1 << 12),
    RPMCALLBACK_UNPACK_ERROR    = (1 << 13),
    RPMCALLBACK_CPIO_ERROR      = (1 << 14),
    RPMCALLBACK_SCRIPT_ERROR    = (1 << 15)
} rpmCallbackType;

/**
 */
typedef void * rpmCallbackData;

/** \ingroup rpmpgp
 * 9.4. Hash Algorithms
 *
\verbatim
       ID           Algorithm                              Text Name
       --           ---------                              ---- ----
       1          - MD5                                    "MD5"
       2          - SHA-1                                  "SHA1"
       3          - RIPE-MD/160                            "RIPEMD160"
       4          - Reserved for double-width SHA (experimental)
       5          - MD2                                    "MD2"
       6          - Reserved for TIGER/192                 "TIGER192"
       7          - Reserved for HAVAL (5 pass, 160-bit)   "HAVAL-5-160"
       100 to 110 - Private/Experimental algorithm.
\endverbatim
 *
 * Implementations MUST implement SHA-1. Implementations SHOULD
 * implement MD5.
 * @todo Add SHA256.
 */
typedef enum pgpHashAlgo_e {
    PGPHASHALGO_ERROR		=  -1,
    PGPHASHALGO_NONE		=  0,
    PGPHASHALGO_MD5		=  1,	/*!< MD5 */
    PGPHASHALGO_SHA1		=  2,	/*!< SHA-1 */
    PGPHASHALGO_RIPEMD160	=  3,	/*!< RIPEMD-160 */
    PGPHASHALGO_MD2		=  5,	/*!< MD2 */
    PGPHASHALGO_TIGER192	=  6,	/*!< TIGER-192 */
    PGPHASHALGO_HAVAL_5_160	=  7,	/*!< HAVAL-5-160 */
    PGPHASHALGO_SHA256		=  8,	/*!< SHA-256 */
    PGPHASHALGO_SHA384		=  9,	/*!< SHA-384 */
    PGPHASHALGO_SHA512		= 10,	/*!< SHA-512 */

    PGPHASHALGO_MD4		= 104,	/*!< (private) MD4 */
    PGPHASHALGO_RIPEMD128	= 105,	/*!< (private) RIPEMD-128 */
    PGPHASHALGO_CRC32		= 106,	/*!< (private) CRC-32 */
    PGPHASHALGO_ADLER32		= 107,	/*!< (private) ADLER-32 */
    PGPHASHALGO_CRC64		= 108,	/*!< (private) CRC-64 */
    PGPHASHALGO_JLU32		= 109,	/*!< (private) Jenkins lookup3.c */
    PGPHASHALGO_SHA224		= 110,	/*!< (private) SHA-224 */
    PGPHASHALGO_RIPEMD256	= 111,	/*!< (private) RIPEMD-256 */
    PGPHASHALGO_RIPEMD320	= 112,	/*!< (private) RIPEMD-320 */
    PGPHASHALGO_SALSA10		= 113,	/*!< (private) SALSA-10 */
    PGPHASHALGO_SALSA20		= 114,	/*!< (private) SALSA-20 */

} pgpHashAlgo;

/** \ingroup rpmpgp
 * Bit(s) to control digest operation.
 */
typedef enum rpmDigestFlags_e {
    RPMDIGEST_NONE	= 0
} rpmDigestFlags;

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmpgp
 * Duplicate a digest context.
 * @param octx		existing digest context
 * @return		duplicated digest context
 */
/*@only@*/
DIGEST_CTX rpmDigestDup(DIGEST_CTX octx)
	/*@*/;

/** \ingroup rpmpgp
 * Initialize digest.
 * Set bit count to 0 and buffer to mysterious initialization constants.
 * @param hashalgo	type of digest
 * @param flags		bit(s) to control digest operation
 * @return		digest context
 */
/*@only@*/ /*@null@*/
DIGEST_CTX rpmDigestInit(pgpHashAlgo hashalgo, rpmDigestFlags flags)
	/*@*/;

/** \ingroup rpmpgp
 * Update context with next plain text buffer.
 * @param ctx		digest context
 * @param data		next data buffer
 * @param len		no. bytes of data
 * @return		0 on success
 */
int rpmDigestUpdate(/*@null@*/ DIGEST_CTX ctx, const void * data, size_t len)
	/*@modifies ctx @*/;

/** \ingroup rpmpgp
 * Return digest and destroy context.
 * Final wrapup - pad to 64-byte boundary with the bit pattern 
 * 1 0* (64-bit count of bits processed, MSB-first)
 *
 * @param ctx		digest context
 * @retval *datap	digest
 * @retval *lenp	no. bytes of digest
 * @param asAscii	return digest as ascii string?
 * @return		0 on success
 */
int rpmDigestFinal(/*@only@*/ /*@null@*/ DIGEST_CTX ctx,
	/*@null@*/ /*@out@*/ void * datap,
	/*@null@*/ /*@out@*/ size_t * lenp, int asAscii)
		/*@modifies *datap, *lenp @*/;

/** \ingroup rpmio
 */
typedef void * (*rpmCallbackFunction)
                (/*@null@*/ const void * h,
                const rpmCallbackType what,
                const rpmuint64_t amount,
                const rpmuint64_t total,
                /*@null@*/ fnpyKey key,
                /*@null@*/ rpmCallbackData data)
        /*@globals internalState@*/
        /*@modifies internalState@*/;

#if !defined(SWIG)
/** \ingroup rpmio
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param p		memory to free
 * @return		NULL always
 */
#if defined(WITH_DMALLOC)
#define _free(p) ((p) != NULL ? free((void *)(p)) : (void)0, NULL)
#else
/*@unused@*/ static inline /*@null@*/
void * _free(/*@only@*/ /*@null@*/ /*@out@*/ const void * p)
	/*@modifies p @*/
{
    if (p != NULL)	free((void *)p);
    return NULL;
}
#endif
#endif

/*@unused@*/ static inline int xislower(int c) /*@*/ {
    return (c >= (int)'a' && c <= (int)'z');
}
/*@unused@*/ static inline int xisupper(int c) /*@*/ {
    return (c >= (int)'A' && c <= (int)'Z');
}
/*@unused@*/ static inline int xisalpha(int c) /*@*/ {
    return (xislower(c) || xisupper(c));
}
/*@unused@*/ static inline int xisdigit(int c) /*@*/ {
    return (c >= (int)'0' && c <= (int)'9');
}
/*@unused@*/ static inline int xisalnum(int c) /*@*/ {
    return (xisalpha(c) || xisdigit(c));
}
/*@unused@*/ static inline int xisblank(int c) /*@*/ {
    return (c == (int)' ' || c == (int)'\t');
}
/*@unused@*/ static inline int xisspace(int c) /*@*/ {
    return (xisblank(c) || c == (int)'\n' || c == (int)'\r' || c == (int)'\f' || c == (int)'\v');
}
/*@unused@*/ static inline int xiscntrl(int c) /*@*/ {
    return (c < (int)' ');
}
/*@unused@*/ static inline int xisascii(int c) /*@*/ {
    return ((c & 0x80) != 0x80);
}
/*@unused@*/ static inline int xisprint(int c) /*@*/ {
    return (c >= (int)' ' && xisascii(c));
}
/*@unused@*/ static inline int xisgraph(int c) /*@*/ {
    return (c > (int)' ' && xisascii(c));
}
/*@unused@*/ static inline int xispunct(int c) /*@*/ {
    return (xisgraph(c) && !xisalnum(c));
}

/*@unused@*/ static inline int xtolower(int c) /*@*/ {
    return ((xisupper(c)) ? (c | ('a' - 'A')) : c);
}
/*@unused@*/ static inline int xtoupper(int c) /*@*/ {
    return ((xislower(c)) ? (c & ~('a' - 'A')) : c);
}

/** \ingroup rpmio
 * Locale insensitive strcasecmp(3).
 */
int xstrcasecmp(const char * s1, const char * s2)		/*@*/;

/** \ingroup rpmio
 * Locale insensitive strncasecmp(3).
 */
int xstrncasecmp(const char *s1, const char * s2, size_t n)	/*@*/;

/** \ingroup rpmio
 * Force encoding of string.
 */
/*@only@*/ /*@null@*/
const char * xstrtolocale(/*@only@*/ const char *str)
	/*@modifies *str @*/;

/**
 * Destroy an I/O buffer.
 * @param iob		I/O buffer
 * @return		NULL always
 */
/*@null@*/
rpmiob rpmiobFree(/*@only@*/ /*@null@*/ rpmiob iob)
	/*@modifies iob @*/;

/**
 * Create an I/O buffer.
 * @param len		no. of octets to allocate
 * @return		new I/O buffer
 */
/*@only@*/
rpmiob rpmiobNew(size_t len)
	/*@*/;

/**
 * Empty an I/O buffer.
 * @param iob		I/O buffer
 * @return		I/O buffer
 */
rpmiob rpmiobEmpty(/*@returned@*/ rpmiob iob)
	/*@modifies iob @*/;

/**
 * Trim trailing white space.
 * @param iob		I/O buffer
 * @return		I/O buffer
 */
rpmiob rpmiobRTrim(/*@returned@*/ rpmiob iob)
	/*@modifies iob @*/;

/**
 * Append string to I/O buffer.
 * @param iob		I/O buffer
 * @param s		string
 * @param nl		append NL?
 * @return		I/O buffer
 */
rpmiob rpmiobAppend(/*@returned@*/ rpmiob iob, const char * s, size_t nl)
	/*@modifies iob @*/;

/**
 * Return I/O buffer.
 * @param iob		I/O buffer
 * @return		I/O buffer (as string)
 */
rpmuint8_t * rpmiobBuf(rpmiob iob)
	/*@*/;

/**
 * Return I/O buffer (as string).
 * @param iob		I/O buffer
 * @return		I/O buffer (as string)
 */
char * rpmiobStr(rpmiob iob)
	/*@*/;

/**
 * Return I/O buffer len.
 * @param iob		I/O buffer
 * @return		I/O buffer length
 */
size_t rpmiobLen(rpmiob iob)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif /* _H_RPMIOTYPES_ */

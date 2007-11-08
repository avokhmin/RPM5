/** \ingroup rpmio signature
 * \file rpmio/rpmpgp.c
 * Routines to handle RFC-2440 detached signatures.
 */

#include "system.h"
#include "rpmio_internal.h"
#include "debug.h"

/*@access pgpDig @*/
/*@access pgpDigParams @*/
/*@access pgpPkt @*/

/*@unchecked@*/
static int _debug = 0;

/*@unchecked@*/
static int _print = 0;

/*@unchecked@*/ /*@null@*/
static pgpDig _dig = NULL;

/*@unchecked@*/ /*@null@*/
static pgpDigParams _digp = NULL;

struct pgpPkt_s {
    pgpTag tag;
    unsigned int pktlen;
    const byte *h;
    unsigned int hlen;
};

struct pgpValTbl_s pgpSigTypeTbl[] = {
    { PGPSIGTYPE_BINARY,	"Binary document signature" },
    { PGPSIGTYPE_TEXT,		"Text document signature" },
    { PGPSIGTYPE_STANDALONE,	"Standalone signature" },
    { PGPSIGTYPE_GENERIC_CERT,	"Generic certification of a User ID and Public Key" },
    { PGPSIGTYPE_PERSONA_CERT,	"Personal certification of a User ID and Public Key" },
    { PGPSIGTYPE_CASUAL_CERT,	"Casual certification of a User ID and Public Key" },
    { PGPSIGTYPE_POSITIVE_CERT,	"Positive certification of a User ID and Public Key" },
    { PGPSIGTYPE_SUBKEY_BINDING,"Subkey Binding Signature" },
    { PGPSIGTYPE_SIGNED_KEY,	"Signature directly on a key" },
    { PGPSIGTYPE_KEY_REVOKE,	"Key revocation signature" },
    { PGPSIGTYPE_SUBKEY_REVOKE,	"Subkey revocation signature" },
    { PGPSIGTYPE_CERT_REVOKE,	"Certification revocation signature" },
    { PGPSIGTYPE_TIMESTAMP,	"Timestamp signature" },
    { -1,			"Unknown signature type" },
};

struct pgpValTbl_s pgpPubkeyTbl[] = {
    { PGPPUBKEYALGO_RSA,	"RSA" },
    { PGPPUBKEYALGO_RSA_ENCRYPT,"RSA(Encrypt-Only)" },
    { PGPPUBKEYALGO_RSA_SIGN,	"RSA(Sign-Only)" },
    { PGPPUBKEYALGO_ELGAMAL_ENCRYPT,"Elgamal(Encrypt-Only)" },
    { PGPPUBKEYALGO_DSA,	"DSA" },
    { PGPPUBKEYALGO_EC,		"Elliptic Curve" },
    { PGPPUBKEYALGO_ECDSA,	"ECDSA" },
    { PGPPUBKEYALGO_ELGAMAL,	"Elgamal" },
    { PGPPUBKEYALGO_DH,		"Diffie-Hellman (X9.42)" },
    { -1,			"Unknown public key algorithm" },
};

struct pgpValTbl_s pgpSymkeyTbl[] = {
    { PGPSYMKEYALGO_PLAINTEXT,	"Plaintext" },
    { PGPSYMKEYALGO_IDEA,	"IDEA" },
    { PGPSYMKEYALGO_TRIPLE_DES,	"3DES" },
    { PGPSYMKEYALGO_CAST5,	"CAST5" },
    { PGPSYMKEYALGO_BLOWFISH,	"BLOWFISH" },
    { PGPSYMKEYALGO_SAFER,	"SAFER" },
    { PGPSYMKEYALGO_DES_SK,	"DES/SK" },
    { PGPSYMKEYALGO_AES_128,	"AES(128-bit key)" },
    { PGPSYMKEYALGO_AES_192,	"AES(192-bit key)" },
    { PGPSYMKEYALGO_AES_256,	"AES(256-bit key)" },
    { PGPSYMKEYALGO_TWOFISH,	"TWOFISH(256-bit key)" },
    { PGPSYMKEYALGO_NOENCRYPT,	"no encryption" },
    { -1,			"Unknown symmetric key algorithm" },
};

struct pgpValTbl_s pgpCompressionTbl[] = {
    { PGPCOMPRESSALGO_NONE,	"Uncompressed" },
    { PGPCOMPRESSALGO_ZIP,	"ZIP" },
    { PGPCOMPRESSALGO_ZLIB, 	"ZLIB" },
    { PGPCOMPRESSALGO_BZIP2, 	"BZIP2" },
    { -1,			"Unknown compression algorithm" },
};

struct pgpValTbl_s pgpHashTbl[] = {
    { PGPHASHALGO_MD5,		"MD5" },
    { PGPHASHALGO_SHA1,		"SHA1" },
    { PGPHASHALGO_RIPEMD160,	"RIPEMD160" },
    { PGPHASHALGO_MD2,		"MD2" },
    { PGPHASHALGO_TIGER192,	"TIGER192" },
    { PGPHASHALGO_HAVAL_5_160,	"HAVAL-5-160" },
    { PGPHASHALGO_SHA256,	"SHA256" },
    { PGPHASHALGO_SHA384,	"SHA384" },
    { PGPHASHALGO_SHA512,	"SHA512" },
    { -1,			"Unknown hash algorithm" },
};

/*@-exportlocal -exportheadervar@*/
/*@observer@*/ /*@unchecked@*/
struct pgpValTbl_s pgpKeyServerPrefsTbl[] = {
    { 0x80,			"No-modify" },
    { -1,			"Unknown key server preference" },
};
/*@=exportlocal =exportheadervar@*/

struct pgpValTbl_s pgpSubTypeTbl[] = {
    { PGPSUBTYPE_SIG_CREATE_TIME,"signature creation time" },
    { PGPSUBTYPE_SIG_EXPIRE_TIME,"signature expiration time" },
    { PGPSUBTYPE_EXPORTABLE_CERT,"exportable certification" },
    { PGPSUBTYPE_TRUST_SIG,	"trust signature" },
    { PGPSUBTYPE_REGEX,		"regular expression" },
    { PGPSUBTYPE_REVOCABLE,	"revocable" },
    { PGPSUBTYPE_KEY_EXPIRE_TIME,"key expiration time" },
    { PGPSUBTYPE_ARR,		"additional recipient request" },
    { PGPSUBTYPE_PREFER_SYMKEY,	"preferred symmetric algorithms" },
    { PGPSUBTYPE_REVOKE_KEY,	"revocation key" },
    { PGPSUBTYPE_ISSUER_KEYID,	"issuer key ID" },
    { PGPSUBTYPE_NOTATION,	"notation data" },
    { PGPSUBTYPE_PREFER_HASH,	"preferred hash algorithms" },
    { PGPSUBTYPE_PREFER_COMPRESS,"preferred compression algorithms" },
    { PGPSUBTYPE_KEYSERVER_PREFERS,"key server preferences" },
    { PGPSUBTYPE_PREFER_KEYSERVER,"preferred key server" },
    { PGPSUBTYPE_PRIMARY_USERID,"primary user id" },
    { PGPSUBTYPE_POLICY_URL,	"policy URL" },
    { PGPSUBTYPE_KEY_FLAGS,	"key flags" },
    { PGPSUBTYPE_SIGNER_USERID,	"signer's user id" },
    { PGPSUBTYPE_REVOKE_REASON,	"reason for revocation" },
    { PGPSUBTYPE_FEATURES,	"features" },
    { PGPSUBTYPE_EMBEDDED_SIG,	"embedded signature" },

    { PGPSUBTYPE_INTERNAL_100,	"internal subpkt type 100" },
    { PGPSUBTYPE_INTERNAL_101,	"internal subpkt type 101" },
    { PGPSUBTYPE_INTERNAL_102,	"internal subpkt type 102" },
    { PGPSUBTYPE_INTERNAL_103,	"internal subpkt type 103" },
    { PGPSUBTYPE_INTERNAL_104,	"internal subpkt type 104" },
    { PGPSUBTYPE_INTERNAL_105,	"internal subpkt type 105" },
    { PGPSUBTYPE_INTERNAL_106,	"internal subpkt type 106" },
    { PGPSUBTYPE_INTERNAL_107,	"internal subpkt type 107" },
    { PGPSUBTYPE_INTERNAL_108,	"internal subpkt type 108" },
    { PGPSUBTYPE_INTERNAL_109,	"internal subpkt type 109" },
    { PGPSUBTYPE_INTERNAL_110,	"internal subpkt type 110" },
    { -1,			"Unknown signature subkey type" },
};

struct pgpValTbl_s pgpTagTbl[] = {
    { PGPTAG_PUBLIC_SESSION_KEY,"Public-Key Encrypted Session Key" },
    { PGPTAG_SIGNATURE,		"Signature" },
    { PGPTAG_SYMMETRIC_SESSION_KEY,"Symmetric-Key Encrypted Session Key" },
    { PGPTAG_ONEPASS_SIGNATURE,	"One-Pass Signature" },
    { PGPTAG_SECRET_KEY,	"Secret Key" },
    { PGPTAG_PUBLIC_KEY,	"Public Key" },
    { PGPTAG_SECRET_SUBKEY,	"Secret Subkey" },
    { PGPTAG_COMPRESSED_DATA,	"Compressed Data" },
    { PGPTAG_SYMMETRIC_DATA,	"Symmetrically Encrypted Data" },
    { PGPTAG_MARKER,		"Marker" },
    { PGPTAG_LITERAL_DATA,	"Literal Data" },
    { PGPTAG_TRUST,		"Trust" },
    { PGPTAG_USER_ID,		"User ID" },
    { PGPTAG_PUBLIC_SUBKEY,	"Public Subkey" },
    { PGPTAG_COMMENT_OLD,	"Comment (from OpenPGP draft)" },
    { PGPTAG_PHOTOID,		"PGP's photo ID" },
    { PGPTAG_ENCRYPTED_MDC,	"Integrity protected encrypted data" },
    { PGPTAG_MDC,		"Manipulaion detection code packet" },
    { PGPTAG_PRIVATE_60,	"Private #60" },
    { PGPTAG_COMMENT,		"Comment" },
    { PGPTAG_PRIVATE_62,	"Private #62" },
    { PGPTAG_CONTROL,		"Control (GPG)" },
    { -1,			"Unknown packet tag" },
};

struct pgpValTbl_s pgpArmorTbl[] = {
    { PGPARMOR_MESSAGE,		"MESSAGE" },
    { PGPARMOR_PUBKEY,		"PUBLIC KEY BLOCK" },
    { PGPARMOR_SIGNATURE,	"SIGNATURE" },
    { PGPARMOR_SIGNED_MESSAGE,	"SIGNED MESSAGE" },
    { PGPARMOR_FILE,		"ARMORED FILE" },
    { PGPARMOR_PRIVKEY,		"PRIVATE KEY BLOCK" },
    { PGPARMOR_SECKEY,		"SECRET KEY BLOCK" },
    { -1,			"Unknown armor block" }
};

struct pgpValTbl_s pgpArmorKeyTbl[] = {
    { PGPARMORKEY_VERSION,	"Version: " },
    { PGPARMORKEY_COMMENT,	"Comment: " },
    { PGPARMORKEY_MESSAGEID,	"MessageID: " },
    { PGPARMORKEY_HASH,		"Hash: " },
    { PGPARMORKEY_CHARSET,	"Charset: " },
    { -1,			"Unknown armor key" }
};

static void pgpPrtNL(void)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (!_print) return;
    fprintf(stderr, "\n");
}

static void pgpPrtInt(const char *pre, int i)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (!_print) return;
    if (pre && *pre)
	fprintf(stderr, "%s", pre);
    fprintf(stderr, " %d", i);
}

static void pgpPrtStr(const char *pre, const char *s)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (!_print) return;
    if (pre && *pre)
	fprintf(stderr, "%s", pre);
    fprintf(stderr, " %s", s);
}

static void pgpPrtHex(const char *pre, const byte *p, unsigned int plen)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (!_print) return;
    if (pre && *pre)
	fprintf(stderr, "%s", pre);
    fprintf(stderr, " %s", pgpHexStr(p, plen));
}

void pgpPrtVal(const char * pre, pgpValTbl vs, byte val)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (!_print) return;
    if (pre && *pre)
	fprintf(stderr, "%s", pre);
    fprintf(stderr, "%s(%u)", pgpValStr(vs, val), (unsigned)val);
}

/**
 */
/*@unused@*/ static /*@observer@*/
const char * pgpMpiHex(const byte *p)
        /*@*/
{
    static char prbuf[2048];
    char *t = prbuf;
    t = pgpHexCvt(t, p+2, pgpMpiLen(p)-2);
    return prbuf;
}

/**
 * @return		0 on success
 */
static int pgpHexSet(const char * pre, int lbits,
		/*@out@*/ mpnumber * mpn, const byte * p, const byte * pend)
	/*@globals fileSystem @*/
	/*@modifies mpn, fileSystem @*/
{
    unsigned int mbits = pgpMpiBits(p);
    unsigned int nbits;
    unsigned int nbytes;
    char * t;
    unsigned int ix;

    if ((p + ((mbits+7) >> 3)) > pend)
	return 1;

    nbits = (lbits > mbits ? lbits : mbits);
    nbytes = ((nbits + 7) >> 3);
    t = xmalloc(2*nbytes+1);
    ix = 2 * ((nbits - mbits) >> 3);

if (_debug)
fprintf(stderr, "*** mbits %u nbits %u nbytes %u t %p[%d] ix %u\n", mbits, nbits, nbytes, t, (2*nbytes+1), ix);
    if (ix > 0) memset(t, (int)'0', ix);
    strcpy(t+ix, pgpMpiHex(p));
if (_debug)
fprintf(stderr, "*** %s %s\n", pre, t);
    (void) mpnsethex(mpn, t);
    t = _free(t);
if (_debug && _print)
fprintf(stderr, "\t %s ", pre), mpfprintln(stderr, mpn->size, mpn->data);
    return 0;
}

int pgpPrtSubType(const byte *h, unsigned int hlen, pgpSigType sigtype)
{
    const byte *p = h;
    unsigned plen;
    int i;

    while (hlen > 0) {
	i = pgpLen(p, &plen);
	p += i;
	hlen -= i;

	pgpPrtVal("    ", pgpSubTypeTbl, (p[0]&(~PGPSUBTYPE_CRITICAL)));
	if (p[0] & PGPSUBTYPE_CRITICAL)
	    if (_print)
		fprintf(stderr, " *CRITICAL*");
	switch (*p) {
	case PGPSUBTYPE_PREFER_SYMKEY:	/* preferred symmetric algorithms */
	    for (i = 1; i < plen; i++)
		pgpPrtVal(" ", pgpSymkeyTbl, p[i]);
	    /*@switchbreak@*/ break;
	case PGPSUBTYPE_PREFER_HASH:	/* preferred hash algorithms */
	    for (i = 1; i < plen; i++)
		pgpPrtVal(" ", pgpHashTbl, p[i]);
	    /*@switchbreak@*/ break;
	case PGPSUBTYPE_PREFER_COMPRESS:/* preferred compression algorithms */
	    for (i = 1; i < plen; i++)
		pgpPrtVal(" ", pgpCompressionTbl, p[i]);
	    /*@switchbreak@*/ break;
	case PGPSUBTYPE_KEYSERVER_PREFERS:/* key server preferences */
	    for (i = 1; i < plen; i++)
		pgpPrtVal(" ", pgpKeyServerPrefsTbl, p[i]);
	    /*@switchbreak@*/ break;
	case PGPSUBTYPE_SIG_CREATE_TIME:
/*@-mods -mayaliasunique @*/
	    if (_digp && !(_digp->saved & PGPDIG_SAVED_TIME) &&
		(sigtype == PGPSIGTYPE_POSITIVE_CERT || sigtype == PGPSIGTYPE_BINARY || sigtype == PGPSIGTYPE_TEXT || sigtype == PGPSIGTYPE_STANDALONE))
	    {
		_digp->saved |= PGPDIG_SAVED_TIME;
		memcpy(_digp->time, p+1, sizeof(_digp->time));
	    }
/*@=mods =mayaliasunique @*/
	    /*@fallthrough@*/
	case PGPSUBTYPE_SIG_EXPIRE_TIME:
	case PGPSUBTYPE_KEY_EXPIRE_TIME:
	    if ((plen - 1) == 4) {
		time_t t = pgpGrab(p+1, plen-1);
		if (_print)
		   fprintf(stderr, " %-24.24s(0x%08x)", ctime(&t), (unsigned)t);
	    } else
		pgpPrtHex("", p+1, plen-1);
	    /*@switchbreak@*/ break;

	case PGPSUBTYPE_ISSUER_KEYID:	/* issuer key ID */
/*@-mods -mayaliasunique @*/
	    if (_digp && !(_digp->saved & PGPDIG_SAVED_ID) &&
		(sigtype == PGPSIGTYPE_POSITIVE_CERT || sigtype == PGPSIGTYPE_BINARY || sigtype == PGPSIGTYPE_TEXT || sigtype == PGPSIGTYPE_STANDALONE))
	    {
		_digp->saved |= PGPDIG_SAVED_ID;
		memcpy(_digp->signid, p+1, sizeof(_digp->signid));
	    }
/*@=mods =mayaliasunique @*/
	    /*@fallthrough@*/
	case PGPSUBTYPE_EXPORTABLE_CERT:
	case PGPSUBTYPE_TRUST_SIG:
	case PGPSUBTYPE_REGEX:
	case PGPSUBTYPE_REVOCABLE:
	case PGPSUBTYPE_ARR:
	case PGPSUBTYPE_REVOKE_KEY:
	case PGPSUBTYPE_NOTATION:
	case PGPSUBTYPE_PREFER_KEYSERVER:
	case PGPSUBTYPE_PRIMARY_USERID:
	case PGPSUBTYPE_POLICY_URL:
	case PGPSUBTYPE_KEY_FLAGS:
	case PGPSUBTYPE_SIGNER_USERID:
	case PGPSUBTYPE_REVOKE_REASON:
	case PGPSUBTYPE_FEATURES:
	case PGPSUBTYPE_EMBEDDED_SIG:
	case PGPSUBTYPE_INTERNAL_100:
	case PGPSUBTYPE_INTERNAL_101:
	case PGPSUBTYPE_INTERNAL_102:
	case PGPSUBTYPE_INTERNAL_103:
	case PGPSUBTYPE_INTERNAL_104:
	case PGPSUBTYPE_INTERNAL_105:
	case PGPSUBTYPE_INTERNAL_106:
	case PGPSUBTYPE_INTERNAL_107:
	case PGPSUBTYPE_INTERNAL_108:
	case PGPSUBTYPE_INTERNAL_109:
	case PGPSUBTYPE_INTERNAL_110:
	default:
	    pgpPrtHex("", p+1, plen-1);
	    /*@switchbreak@*/ break;
	}
	pgpPrtNL();
	p += plen;
	hlen -= plen;
    }
    return 0;
}

/*@-varuse =readonlytrans @*/
/*@observer@*/ /*@unchecked@*/
static const char * pgpSigRSA[] = {
    " m**d =",
    NULL,
};

/*@observer@*/ /*@unchecked@*/
static const char * pgpSigDSA[] = {
    "    r =",
    "    s =",
    NULL,
};
/*@=varuse =readonlytrans @*/

static int pgpPrtSigParams(const pgpPkt pp, byte pubkey_algo, byte sigtype,
		const byte *p)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    const byte * pend = pp->h + pp->hlen;
    int i;

    for (i = 0; p < pend; i++, p += pgpMpiLen(p)) {
	if (pubkey_algo == PGPPUBKEYALGO_RSA) {
	    if (i >= 1) break;
	    if (_dig &&
	(sigtype == PGPSIGTYPE_BINARY || sigtype == PGPSIGTYPE_TEXT))
	    {
		switch (i) {
		case 0:		/* m**d */
		    (void) mpnsethex(&_dig->c, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t  m**d = "),  mpfprintln(stderr, _dig->c.size, _dig->c.data);
		    /*@switchbreak@*/ break;
		default:
		    /*@switchbreak@*/ break;
		}
	    }
	    pgpPrtStr("", pgpSigRSA[i]);
	} else if (pubkey_algo == PGPPUBKEYALGO_DSA) {
	    if (i >= 2) break;
	    if (_dig &&
	(sigtype == PGPSIGTYPE_BINARY || sigtype == PGPSIGTYPE_TEXT))
	    {
		int xx;
		xx = 0;
		switch (i) {
		case 0:		/* r */
		    xx = pgpHexSet(pgpSigDSA[i], 160, &_dig->r, p, pend);
		    /*@switchbreak@*/ break;
		case 1:		/* s */
		    xx = pgpHexSet(pgpSigDSA[i], 160, &_dig->s, p, pend);
		    /*@switchbreak@*/ break;
		default:
		    xx = 1;
		    /*@switchbreak@*/ break;
		}
		if (xx) return xx;
	    }
	    pgpPrtStr("", pgpSigDSA[i]);
	} else {
	    if (_print)
		fprintf(stderr, "%7d", i);
	}
	pgpPrtStr("", pgpMpiStr(p));
	pgpPrtNL();
    }

    return 0;
}

int pgpPrtSig(const pgpPkt pp)
	/*@globals _digp @*/
	/*@modifies *_digp @*/
{
    byte version = pp->h[0];
    byte * p;
    unsigned plen;
    int rc;

    switch (version) {
    case 3:
    {   pgpPktSigV3 v = (pgpPktSigV3)pp->h;
	time_t t;

	if (v->hashlen != 5)
	    return 1;

	pgpPrtVal("V3 ", pgpTagTbl, pp->tag);
	pgpPrtVal(" ", pgpPubkeyTbl, v->pubkey_algo);
	pgpPrtVal(" ", pgpHashTbl, v->hash_algo);
	pgpPrtVal(" ", pgpSigTypeTbl, v->sigtype);
	pgpPrtNL();
	t = pgpGrab(v->time, sizeof(v->time));
	if (_print)
	    fprintf(stderr, " %-24.24s(0x%08x)", ctime(&t), (unsigned)t);
	pgpPrtNL();
	pgpPrtHex(" signer keyid", v->signid, sizeof(v->signid));
	plen = pgpGrab(v->signhash16, sizeof(v->signhash16));
	pgpPrtHex(" signhash16", v->signhash16, sizeof(v->signhash16));
	pgpPrtNL();

	if (_digp && _digp->pubkey_algo == 0) {
	    _digp->version = v->version;
	    _digp->hashlen = v->hashlen;
	    _digp->sigtype = v->sigtype;
	    _digp->hash = memcpy(xmalloc(v->hashlen), &v->sigtype, v->hashlen);
	    memcpy(_digp->time, v->time, sizeof(_digp->time));
	    memcpy(_digp->signid, v->signid, sizeof(_digp->signid));
	    _digp->pubkey_algo = v->pubkey_algo;
	    _digp->hash_algo = v->hash_algo;
	    memcpy(_digp->signhash16, v->signhash16, sizeof(_digp->signhash16));
	}

	p = ((byte *)v) + sizeof(*v);
	rc = pgpPrtSigParams(pp, v->pubkey_algo, v->sigtype, p);
    }	break;
    case 4:
    {   pgpPktSigV4 v = (pgpPktSigV4)pp->h;

	pgpPrtVal("V4 ", pgpTagTbl, pp->tag);
	pgpPrtVal(" ", pgpPubkeyTbl, v->pubkey_algo);
	pgpPrtVal(" ", pgpHashTbl, v->hash_algo);
	pgpPrtVal(" ", pgpSigTypeTbl, v->sigtype);
	pgpPrtNL();

	p = &v->hashlen[0];
	plen = pgpGrab(v->hashlen, sizeof(v->hashlen));
	p += sizeof(v->hashlen);

	if ((p + plen) > (pp->h + pp->hlen))
	    return 1;

if (_debug && _print)
fprintf(stderr, "   hash[%u] -- %s\n", plen, pgpHexStr(p, plen));
	if (_digp && _digp->pubkey_algo == 0) {
	    _digp->hashlen = sizeof(*v) + plen;
	    _digp->hash = memcpy(xmalloc(_digp->hashlen), v, _digp->hashlen);
	}
	(void) pgpPrtSubType(p, plen, v->sigtype);
	p += plen;

	plen = pgpGrab(p,2);
	p += 2;

	if ((p + plen) > (pp->h + pp->hlen))
	    return 1;

if (_debug && _print)
fprintf(stderr, " unhash[%u] -- %s\n", plen, pgpHexStr(p, plen));
	(void) pgpPrtSubType(p, plen, v->sigtype);
	p += plen;

	plen = pgpGrab(p,2);
	pgpPrtHex(" signhash16", p, 2);
	pgpPrtNL();

	if (_digp && _digp->pubkey_algo == 0) {
	    _digp->version = v->version;
	    _digp->sigtype = v->sigtype;
	    _digp->pubkey_algo = v->pubkey_algo;
	    _digp->hash_algo = v->hash_algo;
	    memcpy(_digp->signhash16, p, sizeof(_digp->signhash16));
	}

	p += 2;
	if (p > (pp->h + pp->hlen))
	    return 1;

	rc = pgpPrtSigParams(pp, v->pubkey_algo, v->sigtype, p);
    }	break;
    default:
	rc = 1;
	break;
    }
    return rc;
}

/*@-varuse =readonlytrans @*/
/*@observer@*/ /*@unchecked@*/
static const char * pgpPublicRSA[] = {
    "    n =",
    "    e =",
    NULL,
};

#ifdef NOTYET
/*@observer@*/ /*@unchecked@*/
static const char * pgpSecretRSA[] = {
    "    d =",
    "    p =",
    "    q =",
    "    u =",
    NULL,
};
#endif

/*@observer@*/ /*@unchecked@*/
static const char * pgpPublicDSA[] = {
    "    p =",
    "    q =",
    "    g =",
    "    y =",
    NULL,
};

#ifdef	NOTYET
/*@observer@*/ /*@unchecked@*/
static const char * pgpSecretDSA[] = {
    "    x =",
    NULL,
};
#endif

/*@observer@*/ /*@unchecked@*/
static const char * pgpPublicELGAMAL[] = {
    "    p =",
    "    g =",
    "    y =",
    NULL,
};

#ifdef	NOTYET
/*@observer@*/ /*@unchecked@*/
static const char * pgpSecretELGAMAL[] = {
    "    x =",
    NULL,
};
#endif
/*@=varuse =readonlytrans @*/

static const byte * pgpPrtPubkeyParams(const pgpPkt pp, byte pubkey_algo,
		/*@returned@*/ const byte *p)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    int i;

    for (i = 0; p < &pp->h[pp->hlen]; i++, p += pgpMpiLen(p)) {
	if (pubkey_algo == PGPPUBKEYALGO_RSA) {
	    if (i >= 2) break;
	    if (_dig) {
		switch (i) {
		case 0:		/* n */
		    (void) mpbsethex(&_dig->rsa_pk.n, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     n = "),  mpfprintln(stderr, _dig->rsa_pk.n.size, _dig->rsa_pk.n.modl);
		    /*@switchbreak@*/ break;
		case 1:		/* e */
		    (void) mpnsethex(&_dig->rsa_pk.e, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     e = "),  mpfprintln(stderr, _dig->rsa_pk.e.size, _dig->rsa_pk.e.data);
		    /*@switchbreak@*/ break;
		default:
		    /*@switchbreak@*/ break;
		}
	    }
	    pgpPrtStr("", pgpPublicRSA[i]);
	} else if (pubkey_algo == PGPPUBKEYALGO_DSA) {
	    if (i >= 4) break;
	    if (_dig) {
		switch (i) {
		case 0:		/* p */
		    (void) mpbsethex(&_dig->p, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     p = "),  mpfprintln(stderr, _dig->p.size, _dig->p.modl);
		    /*@switchbreak@*/ break;
		case 1:		/* q */
		    (void) mpbsethex(&_dig->q, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     q = "),  mpfprintln(stderr, _dig->q.size, _dig->q.modl);
		    /*@switchbreak@*/ break;
		case 2:		/* g */
		    (void) mpnsethex(&_dig->g, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     g = "),  mpfprintln(stderr, _dig->g.size, _dig->g.data);
		    /*@switchbreak@*/ break;
		case 3:		/* y */
		    (void) mpnsethex(&_dig->y, pgpMpiHex(p));
if (_debug && _print)
fprintf(stderr, "\t     y = "),  mpfprintln(stderr, _dig->y.size, _dig->y.data);
		    /*@switchbreak@*/ break;
		default:
		    /*@switchbreak@*/ break;
		}
	    }
	    pgpPrtStr("", pgpPublicDSA[i]);
	} else if (pubkey_algo == PGPPUBKEYALGO_ELGAMAL_ENCRYPT) {
	    if (i >= 3) break;
	    pgpPrtStr("", pgpPublicELGAMAL[i]);
	} else {
	    if (_print)
		fprintf(stderr, "%7d", i);
	}
	pgpPrtStr("", pgpMpiStr(p));
	pgpPrtNL();
    }

    return p;
}

static const byte * pgpPrtSeckeyParams(const pgpPkt pp, /*@unused@*/ byte pubkey_algo,
		/*@returned@*/ const byte *p)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    int i;

    switch (*p) {
    case 0:
	pgpPrtVal(" ", pgpSymkeyTbl, *p);
	break;
    case 255:
	p++;
	pgpPrtVal(" ", pgpSymkeyTbl, *p);
	switch (p[1]) {
	case 0x00:
	    pgpPrtVal(" simple ", pgpHashTbl, p[2]);
	    p += 2;
	    /*@innerbreak@*/ break;
	case 0x01:
	    pgpPrtVal(" salted ", pgpHashTbl, p[2]);
	    pgpPrtHex("", p+3, 8);
	    p += 10;
	    /*@innerbreak@*/ break;
	case 0x03:
	    pgpPrtVal(" iterated/salted ", pgpHashTbl, p[2]);
	    /*@-shiftnegative -shiftimplementation @*/ /* FIX: unsigned cast */
	    i = (16 + (p[11] & 0xf)) << ((p[11] >> 4) + 6);
	    /*@=shiftnegative =shiftimplementation @*/
	    pgpPrtHex("", p+3, 8);
	    pgpPrtInt(" iter", i);
	    p += 11;
	    /*@innerbreak@*/ break;
	}
	break;
    default:
	pgpPrtVal(" ", pgpSymkeyTbl, *p);
	pgpPrtHex(" IV", p+1, 8);
	p += 8;
	break;
    }
    pgpPrtNL();

    p++;

#ifdef	NOTYET	/* XXX encrypted MPI's need to be handled. */
    for (i = 0; p < &pp->h[pp->hlen]; i++, p += pgpMpiLen(p)) {
	if (pubkey_algo == PGPPUBKEYALGO_RSA) {
	    if (pgpSecretRSA[i] == NULL) break;
	    pgpPrtStr("", pgpSecretRSA[i]);
	} else if (pubkey_algo == PGPPUBKEYALGO_DSA) {
	    if (pgpSecretDSA[i] == NULL) break;
	    pgpPrtStr("", pgpSecretDSA[i]);
	} else if (pubkey_algo == PGPPUBKEYALGO_ELGAMAL_ENCRYPT) {
	    if (pgpSecretELGAMAL[i] == NULL) break;
	    pgpPrtStr("", pgpSecretELGAMAL[i]);
	} else {
	    if (_print)
		fprintf(stderr, "%7d", i);
	}
	pgpPrtStr("", pgpMpiStr(p));
	pgpPrtNL();
    }
#else
    pgpPrtHex(" secret", p, (pp->hlen - (p - pp->h) - 2));
    pgpPrtNL();
    p += (pp->hlen - (p - pp->h) - 2);
#endif
    pgpPrtHex(" checksum", p, 2);
    pgpPrtNL();

    return p;
}

int pgpPrtKey(const pgpPkt pp)
	/*@globals _digp @*/
	/*@modifies *_digp @*/
{
    byte version = pp->h[0];
    const byte * p;
    unsigned plen;
    time_t t;
    int rc;

    switch (version) {
    case 3:
    {   pgpPktKeyV3 v = (pgpPktKeyV3)pp->h;
	pgpPrtVal("V3 ", pgpTagTbl, pp->tag);
	pgpPrtVal(" ", pgpPubkeyTbl, v->pubkey_algo);
	t = pgpGrab(v->time, sizeof(v->time));
	if (_print)
	    fprintf(stderr, " %-24.24s(0x%08x)", ctime(&t), (unsigned)t);
	plen = pgpGrab(v->valid, sizeof(v->valid));
	if (plen != 0)
	    fprintf(stderr, " valid %u days", plen);
	pgpPrtNL();

	if (_digp && _digp->tag == pp->tag) {
	    _digp->version = v->version;
	    memcpy(_digp->time, v->time, sizeof(_digp->time));
	    _digp->pubkey_algo = v->pubkey_algo;
	}

	p = ((byte *)v) + sizeof(*v);
	p = pgpPrtPubkeyParams(pp, v->pubkey_algo, p);
	rc = 0;
    }	break;
    case 4:
    {   pgpPktKeyV4 v = (pgpPktKeyV4)pp->h;
	pgpPrtVal("V4 ", pgpTagTbl, pp->tag);
	pgpPrtVal(" ", pgpPubkeyTbl, v->pubkey_algo);
	t = pgpGrab(v->time, sizeof(v->time));
	if (_print)
	    fprintf(stderr, " %-24.24s(0x%08x)", ctime(&t), (unsigned)t);
	pgpPrtNL();

	if (_digp && _digp->tag == pp->tag) {
	    _digp->version = v->version;
	    memcpy(_digp->time, v->time, sizeof(_digp->time));
	    _digp->pubkey_algo = v->pubkey_algo;
	}

	p = ((byte *)v) + sizeof(*v);
	p = pgpPrtPubkeyParams(pp, v->pubkey_algo, p);
	if (!(pp->tag == PGPTAG_PUBLIC_KEY || pp->tag == PGPTAG_PUBLIC_SUBKEY))
	    p = pgpPrtSeckeyParams(pp, v->pubkey_algo, p);
	rc = 0;
    }	break;
    default:
	rc = 1;
	break;
    }
    return rc;
}

int pgpPrtUserID(const pgpPkt pp)
	/*@globals _digp @*/
	/*@modifies *_digp @*/
{
    pgpPrtVal("", pgpTagTbl, pp->tag);
    if (_print)
	fprintf(stderr, " \"%.*s\"", (int)pp->hlen, (const char *)pp->h);
    pgpPrtNL();
    if (_digp) {
	char * t = memcpy(xmalloc(pp->hlen+1), pp->h, pp->hlen);
	t[pp->hlen] = '\0';
	_digp->userid = _free(_digp->userid);
	_digp->userid = t;
    }
    return 0;
}

int pgpPrtComment(const pgpPkt pp)
{
    const byte * h = pp->h;
    int i = pp->hlen;

    pgpPrtVal("", pgpTagTbl, pp->tag);
    if (_print)
	fprintf(stderr, " ");
    while (i > 0) {
	int j;
	if (*h >= ' ' && *h <= 'z') {
	    j = 0;
	    while (j < i && h[j] != '\0')
		j++;
	    while (j < i && h[j] == '\0')
		j++;
	    if (_print && j)
		fprintf(stderr, "%.*s", (int)strlen((const char *)h), (const char *)h);
	} else {
	    pgpPrtHex("", h, i);
	    j = i;
	}
	i -= j;
	h += j;
    }
    pgpPrtNL();
    return 0;
}

int pgpPktLen(const byte *pkt, unsigned int pleft, pgpPkt pp)
{
    unsigned int val = *pkt;
    unsigned int plen;

    memset(pp, 0, sizeof(*pp));
    /* XXX can't deal with these. */
    if (!(val & 0x80))
	return -1;

    if (val & 0x40) {
	pp->tag = (val & 0x3f);
	plen = pgpLen(pkt+1, &pp->hlen);
    } else {
	pp->tag = (val >> 2) & 0xf;
	plen = (1 << (val & 0x3));
	pp->hlen = pgpGrab(pkt+1, plen);
    }

    pp->pktlen = 1 + plen + pp->hlen;
    if (pleft > 0 && pp->pktlen > pleft)
	return -1;

/*@-assignexpose -temptrans @*/
    pp->h = pkt + 1 + plen;
/*@=assignexpose =temptrans @*/

    return pp->pktlen;
}

int pgpPubkeyFingerprint(const byte * pkt, unsigned int pktlen, byte * keyid)
{
    pgpPkt pp = alloca(sizeof(*pp));
    int rc = pgpPktLen(pkt, pktlen, pp);
    const byte *se;
    int i;

    /* Pubkeys only please. */
    if (pp->tag != PGPTAG_PUBLIC_KEY)
	return -1;

    /* Choose the correct keyid. */
    switch (pp->h[0]) {
    default:	return -1;
    case 3:
      {	pgpPktKeyV3 v = (pgpPktKeyV3) (pp->h);
	se = (byte *)(v + 1);
	switch (v->pubkey_algo) {
	default:	return -1;
	case PGPPUBKEYALGO_RSA:
	    se += pgpMpiLen(se);
	    memmove(keyid, (se-8), 8);
	    /*@innerbreak@*/ break;
	}
      } break;
    case 4:
      {	pgpPktKeyV4 v = (pgpPktKeyV4) (pp->h);
	byte * d = NULL;
	size_t dlen = 0;

	se = (byte *)(v + 1);
	switch (v->pubkey_algo) {
	default:	return -1;
	case PGPPUBKEYALGO_RSA:
	    for (i = 0; i < 2; i++)
		se += pgpMpiLen(se);
	    /*@innerbreak@*/ break;
	case PGPPUBKEYALGO_DSA:
	    for (i = 0; i < 4; i++)
		se += pgpMpiLen(se);
	    /*@innerbreak@*/ break;
	}
	{   DIGEST_CTX ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);
	    (void) rpmDigestUpdate(ctx, pkt, (se-pkt));
	    (void) rpmDigestFinal(ctx, &d, &dlen, 0);
	}

	memmove(keyid, (d + (dlen-8)), 8);
	d = _free(d);
      } break;
    }
    rc = 0;
    return rc;
}

int pgpExtractPubkeyFingerprint(const char * b64pkt, byte * keyid)
{
    const byte * pkt;
    size_t pktlen;

    if (b64decode(b64pkt, (void **)&pkt, &pktlen))
	return -1;	/* on error */
    (void) pgpPubkeyFingerprint(pkt, (unsigned int)pktlen, keyid);
    pkt = _free(pkt);
    return 8;	/* no. of bytes of pubkey signid */
}

int pgpPrtPkt(const byte *pkt, unsigned int pleft)
{
    pgpPkt pp = alloca(sizeof(*pp));
    int rc = pgpPktLen(pkt, pleft, pp);

    if (rc < 0)
	return rc;

    switch (pp->tag) {
    case PGPTAG_SIGNATURE:
	rc = pgpPrtSig(pp);
	break;
    case PGPTAG_PUBLIC_KEY:
	/* Get the public key fingerprint. */
	if (_digp) {
/*@-mods@*/
	    if (!pgpPubkeyFingerprint(pkt, pp->pktlen, _digp->signid))
		_digp->saved |= PGPDIG_SAVED_ID;
	    else
		memset(_digp->signid, 0, sizeof(_digp->signid));
/*@=mods@*/
	}
	/*@fallthrough@*/
    case PGPTAG_PUBLIC_SUBKEY:
	rc = pgpPrtKey(pp);
	break;
    case PGPTAG_SECRET_KEY:
    case PGPTAG_SECRET_SUBKEY:
	rc = pgpPrtKey(pp);
	break;
    case PGPTAG_USER_ID:
	rc = pgpPrtUserID(pp);
	break;
    case PGPTAG_COMMENT:
    case PGPTAG_COMMENT_OLD:
	rc = pgpPrtComment(pp);
	break;

    case PGPTAG_RESERVED:
    case PGPTAG_PUBLIC_SESSION_KEY:
    case PGPTAG_SYMMETRIC_SESSION_KEY:
    case PGPTAG_COMPRESSED_DATA:
    case PGPTAG_SYMMETRIC_DATA:
    case PGPTAG_MARKER:
    case PGPTAG_LITERAL_DATA:
    case PGPTAG_TRUST:
    case PGPTAG_PHOTOID:
    case PGPTAG_ENCRYPTED_MDC:
    case PGPTAG_MDC:
    case PGPTAG_PRIVATE_60:
    case PGPTAG_PRIVATE_62:
    case PGPTAG_CONTROL:
    default:
	pgpPrtVal("", pgpTagTbl, pp->tag);
	pgpPrtHex("", pp->h, pp->hlen);
	pgpPrtNL();
	rc = 0;
	break;
    }

    return (rc ? -1 : pp->pktlen);
}

pgpDig pgpNewDig(pgpVSFlags vsflags)
{
    pgpDig dig = xcalloc(1, sizeof(*dig));
    dig->vsflags = vsflags;
    return dig;
}

void pgpCleanDig(pgpDig dig)
{
    if (dig != NULL) {
	int i;
	dig->signature.userid = _free(dig->signature.userid);
	dig->pubkey.userid = _free(dig->pubkey.userid);
	memset(&dig->dops, 0, sizeof(dig->dops));
	memset(&dig->sops, 0, sizeof(dig->sops));
	dig->ppkts = _free(dig->ppkts);
	dig->npkts = 0;
	dig->signature.hash = _free(dig->signature.hash);
	dig->pubkey.hash = _free(dig->pubkey.hash);
	/*@-unqualifiedtrans@*/ /* FIX: double indirection */
	for (i = 0; i < 4; i++) {
	    dig->signature.params[i] = _free(dig->signature.params[i]);
	    dig->pubkey.params[i] = _free(dig->pubkey.params[i]);
	}
	/*@=unqualifiedtrans@*/

	memset(&dig->signature, 0, sizeof(dig->signature));
	memset(&dig->pubkey, 0, sizeof(dig->pubkey));

	dig->md5 = _free(dig->md5);
	dig->sha1 = _free(dig->sha1);
	mpnfree(&dig->hm);
	mpnfree(&dig->r);
	mpnfree(&dig->s);

	(void) rsapkFree(&dig->rsa_pk);
	mpnfree(&dig->m);
	mpnfree(&dig->c);
	mpnfree(&dig->rsahm);
    }
/*@-nullstate@*/
    return;
/*@=nullstate@*/
}

pgpDig pgpFreeDig(/*@only@*/ /*@null@*/ pgpDig dig)
	/*@modifies dig @*/
{
    if (dig != NULL) {

	/* Lose the header tag data. */
	if (dig->sig)
	    dig->sig = _free(dig->sig);

	/* Dump the signature/pubkey data. */
	pgpCleanDig(dig);

	if (dig->hdrsha1ctx != NULL)
	    (void) rpmDigestFinal(dig->hdrsha1ctx, NULL, NULL, 0);
	dig->hdrsha1ctx = NULL;

	if (dig->sha1ctx != NULL)
	    (void) rpmDigestFinal(dig->sha1ctx, NULL, NULL, 0);
	dig->sha1ctx = NULL;

	mpbfree(&dig->p);
	mpbfree(&dig->q);
	mpnfree(&dig->g);
	mpnfree(&dig->y);
	mpnfree(&dig->hm);
	mpnfree(&dig->r);
	mpnfree(&dig->s);

#ifdef	NOTYET
	if (dig->hdrmd5ctx != NULL)
	    (void) rpmDigestFinal(dig->hdrmd5ctx, NULL, NULL, 0);
	dig->hdrmd5ctx = NULL;
#endif

	if (dig->md5ctx != NULL)
	    (void) rpmDigestFinal(dig->md5ctx, NULL, NULL, 0);
	dig->md5ctx = NULL;

	mpbfree(&dig->rsa_pk.n);
	mpnfree(&dig->rsa_pk.e);
	mpnfree(&dig->m);
	mpnfree(&dig->c);
	mpnfree(&dig->hm);

	dig = _free(dig);
    }
    return dig;
}

pgpDigParams pgpGetSignature(pgpDig dig)
{
    return (dig ? &dig->signature : NULL);
}

pgpDigParams pgpGetPubkey(pgpDig dig)
{
    return (dig ? &dig->pubkey : NULL);
}

uint32_t pgpGetSigtag(pgpDig dig)
{
    return (dig ? dig->sigtag : 0);
}

uint32_t pgpGetSigtype(pgpDig dig)
{
    return (dig ? dig->sigtype : 0);
}

const void * pgpGetSig(pgpDig dig)
{
    return (dig ? dig->sig : NULL);
}

uint32_t pgpGetSiglen(pgpDig dig)
{
    return (dig ? dig->siglen : 0);
}

int pgpSetSig(pgpDig dig,
	uint32_t sigtag, uint32_t sigtype, const void * sig, uint32_t siglen)
{
    if (dig != NULL) {
#if 0
	if (dig->sig)
	    dig->sig = _free(dig->sig);
#endif
	dig->sigtag = sigtag;
	dig->sigtype = (sig ? sigtype : 0);
/*@-assignexpose -kepttrans@*/
	dig->sig = sig;
/*@=assignexpose =kepttrans@*/
	dig->siglen = siglen;
    }
    return 0;
}

void * pgpStatsAccumulator(pgpDig dig, int opx)
{
    void * sw = NULL;
    switch (opx) {
    case 10:	/* RPMTS_OP_DIGEST */
	sw = &dig->dops;
	break;
    case 11:	/* RPMTS_OP_SIGNATURE */
	sw = &dig->sops;
	break;
    }
    return sw;
}

pgpVSFlags pgpGetVSFlags(pgpDig dig)
{
    pgpVSFlags vsflags = 0;
    if (dig != NULL)
	vsflags = dig->vsflags;
    return vsflags;
}

pgpVSFlags pgpSetVSFlags(pgpDig dig, pgpVSFlags vsflags)
{
    pgpVSFlags ovsflags = 0;
    if (dig != NULL) {
	ovsflags = dig->vsflags;
	dig->vsflags = vsflags;
    }
    return ovsflags;
}

int pgpSetFindPubkey(pgpDig dig,
		int (*findPubkey) (void *ts, /*@null@*/ void *dig), void * _ts)
{
    if (dig) {
/*@-assignexpose@*/
	dig->findPubkey = findPubkey;
/*@=assignexpose@*/
/*@-dependenttrans@*/
	dig->_ts = _ts;
/*@=dependenttrans@*/
    }
    return 0;
}

int pgpFindPubkey(pgpDig dig)
{
    int rc = 1;	/* XXX RPMRC_NOTFOUND */
    if (dig && dig->findPubkey && dig->_ts)
	rc = (*dig->findPubkey) (dig->_ts, dig);
    return rc;
}

static int pgpGrabPkts(const byte * pkts, unsigned int pktlen,
		/*@out@*/ byte *** pppkts, /*@out@*/ int * pnpkts)
	/*@modifies *pppkts, *pnpkts @*/
{
    pgpPkt pp = alloca(sizeof(*pp));
    const byte *p;
    unsigned int pleft;
    int len;
    int npkts = 0;
    byte ** ppkts;

    for (p = pkts, pleft = pktlen; p < (pkts + pktlen); p += len, pleft -= len) {
	if (pgpPktLen(p, pleft, pp) < 0)
	    return -1;
	len = pp->pktlen;
	npkts++;
    }
    if (npkts <= 0)
	return -2;

    ppkts = xcalloc(npkts, sizeof(*ppkts));

    npkts = 0;
    for (p = pkts, pleft = pktlen; p < (pkts + pktlen); p += len, pleft -= len) {
  
	if (pgpPktLen(p, pleft, pp) < 0)
	    return -1;
	len = pp->pktlen;
	ppkts[npkts++] = (byte *) p;
    }

    if (pppkts != NULL)
	*pppkts = ppkts;
   else
	ppkts = _free(ppkts);

    if (pnpkts != NULL)
	*pnpkts = npkts;

    return 0;
}

int pgpPrtPkts(const byte * pkts, unsigned int pktlen, pgpDig dig, int printing)
	/*@globals _dig, _digp, _print @*/
	/*@modifies _dig, _digp, *_digp, _print @*/
{
    pgpPkt pp = alloca(sizeof(*pp));
    unsigned int val = *pkts;
    unsigned int pleft;
    int len;
    byte ** ppkts = NULL;
    int npkts;
    int i;

    _print = printing;
    _dig = dig;
    if (dig != NULL && (val & 0x80)) {
	pgpTag tag = (val & 0x40) ? (val & 0x3f) : ((val >> 2) & 0xf);
	_digp = (tag == PGPTAG_SIGNATURE) ? &_dig->signature : &_dig->pubkey;
	_digp->tag = tag;
    } else
	_digp = NULL;

    if (pgpGrabPkts(pkts, pktlen, &ppkts, &npkts) || ppkts == NULL)
	return -1;

    if (ppkts != NULL)
    for (i = 0, pleft = pktlen; i < npkts; i++, pleft -= len) {
	len = pgpPktLen(ppkts[i], pleft, pp);
	len = pgpPrtPkt(ppkts[i], pp->pktlen);
    }

    if (dig != NULL) {
	dig->ppkts = _free(dig->ppkts);		/* XXX memory leak plugged. */
	dig->ppkts = ppkts;
	dig->npkts = npkts;
    } else
	ppkts = _free(ppkts);

    return 0;
}

pgpArmor pgpReadPkts(const char * fn, const byte ** pkt, size_t * pktlen)
{
    byte * b = NULL;
    ssize_t blen;
    const char * enc = NULL;
    const char * crcenc = NULL;
    byte * dec;
    byte * crcdec;
    size_t declen;
    size_t crclen;
    uint32_t crcpkt, crc;
    const char * armortype = NULL;
    char * t, * te;
    int pstate = 0;
    pgpArmor ec = PGPARMOR_ERR_NO_BEGIN_PGP;	/* XXX assume failure */
    int rc;

    rc = rpmioSlurp(fn, &b, &blen);
    if (rc || b == NULL || blen <= 0) {
	goto exit;
    }

    if (pgpIsPkt(b)) {
#ifdef NOTYET	/* XXX ASCII Pubkeys only, please. */
	ec = 0;	/* XXX fish out pkt type. */
#endif
	goto exit;
    }

#define	TOKEQ(_s, _tok)	(!strncmp((_s), (_tok), sizeof(_tok)-1))

    for (t = (char *)b; t && *t; t = te) {
	if ((te = strchr(t, '\n')) == NULL)
	    te = t + strlen(t);
	else
	    te++;

	switch (pstate) {
	case 0:
	    armortype = NULL;
	    if (!TOKEQ(t, "-----BEGIN PGP "))
		continue;
	    t += sizeof("-----BEGIN PGP ")-1;

	    rc = pgpValTok(pgpArmorTbl, t, te);
	    if (rc < 0) {
		ec = PGPARMOR_ERR_UNKNOWN_ARMOR_TYPE;
		goto exit;
	    }
	    if (rc != PGPARMOR_PUBKEY)	/* XXX ASCII Pubkeys only, please. */
		continue;
	    armortype = t;

	    t = strchr(t, '\n');
	    if (t == NULL)
		continue;
	    if (t[-1] == '\r')
		--t;
	    t -= (sizeof("-----")-1);
	    if (!TOKEQ(t, "-----"))
		continue;
	    *t = '\0';
	    pstate++;
	    /*@switchbreak@*/ break;
	case 1:
	    enc = NULL;
	    rc = pgpValTok(pgpArmorKeyTbl, t, te);
	    if (rc >= 0)
		continue;
	    if (!(*t == '\n' || *t == '\r')) {
		pstate = 0;
		continue;
	    }
	    enc = te;		/* Start of encoded packets */
	    pstate++;
	    /*@switchbreak@*/ break;
	case 2:
	    crcenc = NULL;
	    if (*t != '=')
		continue;
	    *t++ = '\0';	/* Terminate encoded packets */
	    crcenc = t;		/* Start of encoded crc */
	    pstate++;
	    /*@switchbreak@*/ break;
	case 3:
	    pstate = 0;
	    if (!TOKEQ(t, "-----END PGP ")) {
		ec = PGPARMOR_ERR_NO_END_PGP;
		goto exit;
	    }
	    *t = '\0';		/* Terminate encoded crc */
	    t += sizeof("-----END PGP ")-1;
	    if (t >= te) continue;

	    if (armortype == NULL) /* XXX can't happen */
		continue;
	    rc = strncmp(t, armortype, strlen(armortype));
	    if (rc)
		continue;

	    t += strlen(armortype);
	    if (t >= te) continue;

	    if (!TOKEQ(t, "-----")) {
		ec = PGPARMOR_ERR_NO_END_PGP;
		goto exit;
	    }
	    t += (sizeof("-----")-1);
	    if (t >= te) continue;
	    /* XXX permitting \r here is not RFC-2440 compliant <shrug> */
	    if (!(*t == '\n' || *t == '\r')) continue;

	    crcdec = NULL;
	    crclen = 0;
	    if (b64decode(crcenc, (void **)&crcdec, &crclen) != 0) {
		ec = PGPARMOR_ERR_CRC_DECODE;
		goto exit;
	    }
	    crcpkt = pgpGrab(crcdec, crclen);
	    crcdec = _free(crcdec);
	    dec = NULL;
	    declen = 0;
	    if (b64decode(enc, (void **)&dec, &declen) != 0) {
		ec = PGPARMOR_ERR_BODY_DECODE;
		goto exit;
	    }
	    crc = pgpCRC(dec, declen);
	    if (crcpkt != crc) {
		ec = PGPARMOR_ERR_CRC_CHECK;
		goto exit;
	    }
	    b = _free(b);
	    b = dec;
	    blen = declen;
	    ec = PGPARMOR_PUBKEY;	/* XXX ASCII Pubkeys only, please. */
	    goto exit;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	}
    }
    ec = PGPARMOR_NONE;

exit:
    if (ec > PGPARMOR_NONE && pkt)
	*pkt = b;
    else if (b != NULL)
	b = _free(b);
    if (pktlen)
	*pktlen = blen;
    return ec;
}

char * pgpArmorWrap(int atype, const unsigned char * s, size_t ns)
{
    const char * enc;
    char * t;
    size_t nt;
    char * val;
    int lc;

    nt = ((ns + 2) / 3) * 4;
    /*@-globs@*/
    /* Add additional bytes necessary for eol string(s). */
    if (b64encode_chars_per_line > 0 && b64encode_eolstr != NULL) {
	lc = (nt + b64encode_chars_per_line - 1) / b64encode_chars_per_line;
       if (((nt + b64encode_chars_per_line - 1) % b64encode_chars_per_line) != 0)
        ++lc;
	nt += lc * strlen(b64encode_eolstr);
    }
    /*@=globs@*/

    nt += 512;	/* XXX slop for armor and crc */

    val = t = xmalloc(nt + 1);
    *t = '\0';
    t = stpcpy(t, "-----BEGIN PGP ");
    t = stpcpy(t, pgpValStr(pgpArmorTbl, atype));
    /*@-globs@*/
    t = stpcpy( stpcpy(t, "-----\nVersion: RPM "), VERSION);
    /*@=globs@*/
    t = stpcpy(t, " (BeeCrypt)\n\n");

    if ((enc = b64encode(s, ns)) != NULL) {
	t = stpcpy(t, enc);
	enc = _free(enc);
	if ((enc = b64crc(s, ns)) != NULL) {
	    *t++ = '=';
	    t = stpcpy(t, enc);
	    enc = _free(enc);
	}
    }
	
    t = stpcpy(t, "-----END PGP ");
    t = stpcpy(t, pgpValStr(pgpArmorTbl, atype));
    t = stpcpy(t, "-----\n");

/*@-globstate@*/	/* XXX b64encode_eolstr needs annotation. */
    return val;
/*@=globstate@*/
}

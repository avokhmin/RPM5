/** \ingroup rpmpgp
 * \file rpmio/rpmssl.c
 */

#include "system.h"
#include <rpmio.h>
#include <rpmlog.h>

#define	_RPMPGP_INTERNAL
#if defined(WITH_SSL)
#define	_RPMSSL_INTERNAL
#include <rpmssl.h>
#else
#include <rpmpgp.h>		/* XXX DIGEXT_CTX */
#endif

#include "debug.h"

/*@access pgpDig @*/
/*@access pgpDigParams @*/

/*@-redecl@*/
/*@unchecked@*/
extern int _pgp_debug;

/*@unchecked@*/
extern int _pgp_print;
/*@=redecl@*/

#if defined(WITH_SSL)
/**
 * Convert hex to binary nibble.
 * @param c            hex character
 * @return             binary nibble
 */
static
unsigned char nibble(char c)
	/*@*/
{
    if (c >= '0' && c <= '9')
	return (unsigned char) (c - '0');
    if (c >= 'A' && c <= 'F')
	return (unsigned char)((int)(c - 'A') + 10);
    if (c >= 'a' && c <= 'f')
	return (unsigned char)((int)(c - 'a') + 10);
    return (unsigned char) '\0';
}
#endif	/* WITH_SSL */

static
int rpmsslSetRSA(/*@only@*/ DIGEST_CTX ctx, pgpDig dig, pgpDigParams sigp)
	/*@modifies ctx, dig @*/
{
#if defined(WITH_SSL)
    rpmssl ssl = dig->impl;
    unsigned int nbits = BN_num_bits(ssl->c);
    unsigned int nb = (nbits + 7) >> 3;
    const char * prefix;
    const char * hexstr;
    const char * s;
    uint8_t signhash16[2];
    char * tt;
    int xx;

    ssl->type = 0;
    /* XXX Values from PKCS#1 v2.1 (aka RFC-3447) */
    switch (sigp->hash_algo) {
    case PGPHASHALGO_MD5:
	prefix = "3020300c06082a864886f70d020505000410";
	ssl->type = NID_md5;
	break;
    case PGPHASHALGO_SHA1:
	prefix = "3021300906052b0e03021a05000414";
	ssl->type = NID_sha1;
	break;
    case PGPHASHALGO_RIPEMD160:
	prefix = "3021300906052b2403020105000414";
	ssl->type = NID_ripemd160;
	break;
    case PGPHASHALGO_MD2:
	prefix = "3020300c06082a864886f70d020205000410";
	ssl->type = NID_md2;
	break;
    case PGPHASHALGO_TIGER192:
	prefix = "3029300d06092b06010401da470c0205000418";
	break;
    case PGPHASHALGO_HAVAL_5_160:
	prefix = NULL;
	break;
    case PGPHASHALGO_SHA256:
	prefix = "3031300d060960864801650304020105000420";
	break;
    case PGPHASHALGO_SHA384:
	prefix = "3041300d060960864801650304020205000430";
	break;
    case PGPHASHALGO_SHA512:
	prefix = "3051300d060960864801650304020305000440";
	break;
    default:
	prefix = NULL;
	break;
    }
    if (prefix == NULL || ssl->type == 0)
	return 1;

    xx = rpmDigestFinal(ctx, (void **)&dig->md5, &dig->md5len, 1);
    hexstr = tt = xmalloc(2 * nb + 1);
    memset(tt, (int) 'f', (2 * nb));
    tt[0] = '0'; tt[1] = '0';
    tt[2] = '0'; tt[3] = '1';
    tt += (2 * nb) - strlen(prefix) - strlen(dig->md5) - 2;
    *tt++ = '0'; *tt++ = '0';
    tt = stpcpy(tt, prefix);
    tt = stpcpy(tt, dig->md5);

    /* Set RSA hash. */
/*@-moduncon -noeffectuncon @*/
    xx = BN_hex2bn(&ssl->rsahm, hexstr);
/*@=moduncon =noeffectuncon @*/

    hexstr = _free(hexstr);

    /* Compare leading 16 bits of digest for quick check. */
    s = dig->md5;
/*@-type@*/
    signhash16[0] = (uint8_t) (nibble(s[0]) << 4) | nibble(s[1]);
    signhash16[1] = (uint8_t) (nibble(s[2]) << 4) | nibble(s[3]);
/*@=type@*/
    return memcmp(signhash16, sigp->signhash16, sizeof(sigp->signhash16));
#else
    return 1;
#endif	/* WITH_SSL */
}

static
int rpmsslVerifyRSA(pgpDig dig)
	/*@*/
{
#if defined(WITH_SSL)
    rpmssl ssl = dig->impl;
    unsigned char * rsahm;
    unsigned char * dbuf;
    size_t nb, ll;
    int rc = 0;
    int xx;

    /* Verify RSA signature. */
/*@-moduncon@*/
    /* XXX This is _NOT_ the correct openssl function to use:
     *	rc = RSA_verify(type, m, m_len, sigbuf, siglen, ssl->rsa)
     *
     * Here's what needs doing (from OpenPGP reference sources in 1999):
     *	static u32_t checkrsa(BIGNUM * a, RSA * key, u8_t * hash, int hlen)
     *	{
     *	  u8_t dbuf[MAXSIGM];
     *	  int j, ll;
     *
     *	  j = BN_bn2bin(a, dbuf);
     *	  ll = BN_num_bytes(key->n);
     *	  while (j < ll)
     *	    memmove(&dbuf[1], dbuf, j++), dbuf[0] = 0;
     *	  j = RSA_public_decrypt(ll, dbuf, dbuf, key, RSA_PKCS1_PADDING);
     *	  RSA_free(key);
     *	  return (j != hlen || memcmp(dbuf, hash, j));
     *	}
     */

    nb = BN_num_bytes(ssl->rsahm);
    rsahm = xmalloc(nb);
    xx = BN_bn2bin(ssl->rsahm, rsahm);
    ll = BN_num_bytes(ssl->rsa->n);
    xx = ll;	/* WRONG WRONG WRONG */
    dbuf = xcalloc(1, ll);
    /* XXX FIXME: what parameter goes into dbuf? */
    while (xx < (int)ll)
	memmove(&dbuf[1], dbuf, xx++), dbuf[0] = 0;
    xx = RSA_public_decrypt(ll, dbuf, dbuf, ssl->rsa, RSA_PKCS1_PADDING);
/*@=moduncon@*/
    rc = (xx == (int)nb && (memcmp(rsahm, dbuf, nb) == 0));
    dbuf = _free(dbuf);
    rsahm = _free(rsahm);

    if (rc != 1) {
	rpmlog(RPMLOG_WARNING, "RSA verification using openssl is not yet implemented. rpmmsslVerifyRSA() will continue without verifying the RSA signature.\n");
	rc = 1;
    }

    return rc;
#else
    return 0;
#endif	/* WITH_SSL */
}

static
int rpmsslSetDSA(/*@only@*/ DIGEST_CTX ctx, pgpDig dig, pgpDigParams sigp)
	/*@modifies ctx, dig @*/
{
    int xx;

    /* Set DSA hash. */
    xx = rpmDigestFinal(ctx, (void **)&dig->sha1, &dig->sha1len, 0);

    /* Compare leading 16 bits of digest for quick check. */
    return memcmp(dig->sha1, sigp->signhash16, sizeof(sigp->signhash16));
}

static
int rpmsslVerifyDSA(pgpDig dig)
	/*@*/
{
#if defined(WITH_SSL)
    rpmssl ssl = dig->impl;
    int rc;

    /* Verify DSA signature. */
/*@-moduncon@*/
    rc = (DSA_do_verify(dig->sha1, dig->sha1len, ssl->dsasig, ssl->dsa) == 1);
/*@=moduncon@*/

    return rc;
#else
    return 0;
#endif	/* WITH_SSL */
}

static
int rpmsslMpiItem(const char * pre, pgpDig dig, int itemno,
		const uint8_t * p, /*@null@*/ const uint8_t * pend)
	/*@modifies dig @*/
{
#if defined(WITH_SSL)
    rpmssl ssl = dig->impl;
    unsigned int nb = ((pgpMpiBits(p) + 7) >> 3);
    int rc = 0;

/*@-moduncon@*/
    switch (itemno) {
    default:
assert(0);
	break;
    case 10:		/* RSA m**d */
	ssl->c = BN_bin2bn(p+2, nb, ssl->c);
	break;
    case 20:		/* DSA r */
	if (ssl->dsasig == NULL) ssl->dsasig = DSA_SIG_new();
	ssl->dsasig->r = BN_bin2bn(p+2, nb, ssl->dsasig->r);
	break;
    case 21:		/* DSA s */
	if (ssl->dsasig == NULL) ssl->dsasig = DSA_SIG_new();
	ssl->dsasig->s = BN_bin2bn(p+2, nb, ssl->dsasig->s);
	break;
    case 30:		/* RSA n */
	if (ssl->rsa == NULL) ssl->rsa = RSA_new();
	ssl->rsa->n = BN_bin2bn(p+2, nb, ssl->rsa->n);
	break;
    case 31:		/* RSA e */
	if (ssl->rsa == NULL) ssl->rsa = RSA_new();
	ssl->rsa->e = BN_bin2bn(p+2, nb, ssl->rsa->e);
	break;
    case 40:		/* DSA p */
	if (ssl->dsa == NULL) ssl->dsa = DSA_new();
	ssl->dsa->p = BN_bin2bn(p+2, nb, ssl->dsa->p);
	break;
    case 41:		/* DSA q */
	if (ssl->dsa == NULL) ssl->dsa = DSA_new();
	ssl->dsa->q = BN_bin2bn(p+2, nb, ssl->dsa->q);
	break;
    case 42:		/* DSA g */
	if (ssl->dsa == NULL) ssl->dsa = DSA_new();
	ssl->dsa->g = BN_bin2bn(p+2, nb, ssl->dsa->g);
	break;
    case 43:		/* DSA y */
	if (ssl->dsa == NULL) ssl->dsa = DSA_new();
	ssl->dsa->pub_key = BN_bin2bn(p+2, nb, ssl->dsa->pub_key);
	break;
    }
/*@=moduncon@*/
    return rc;
#else
    return 1;
#endif	/* WITH_SSL */
}

static
void rpmsslClean(void * impl)
	/*@modifies impl @*/
{
#if defined(WITH_SSL)
    rpmssl ssl = impl;
    if (ssl != NULL) {
	if (ssl->dsa) {
	    DSA_free(ssl->dsa);
	    ssl->dsa = NULL;
	}
	if (ssl->dsasig) {
	    DSA_SIG_free(ssl->dsasig);
	    ssl->dsasig = NULL;
	}
	if (ssl->rsa) {
	    RSA_free(ssl->rsa);
	    ssl->rsa = NULL;
	}
	if (ssl->c) {
	    BN_free(ssl->c);
	    ssl->c = NULL;
	}
    }
#endif	/* WITH_SSL */
}

static
void * rpmsslFree(/*@only@*/ void * impl)
	/*@modifies impl @*/
{
#if defined(WITH_SSL)
    rpmssl ssl = impl;
    if (ssl != NULL) {
	ssl = _free(ssl);
    }
#endif	/* WITH_SSL */
    return NULL;
}

static
void * rpmsslInit(void)
	/*@*/
{
#if defined(WITH_SSL)
    rpmssl ssl = xcalloc(1, sizeof(*ssl));
    ERR_load_crypto_strings();
    return (void *) ssl;
#else
    return NULL;
#endif	/* WITH_SSL */
}

struct pgpImplVecs_s rpmsslImplVecs = {
	rpmsslSetRSA, rpmsslVerifyRSA,
	rpmsslSetDSA, rpmsslVerifyDSA,
	rpmsslMpiItem, rpmsslClean,
	rpmsslFree, rpmsslInit
};

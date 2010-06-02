/* crypto/ecdsa/ecdsatest.c */
/*
 * Written by Nils Larsch for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 2000-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by 
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * The elliptic curve binary polynomial software is originally written by 
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems Laboratories.
 *
 */

#include "system.h"

#include <rpmio.h>
#include <rpmlog.h>

#define	_RPMPGP_INTERNAL
#include <poptIO.h>

#define	_RPMGC_INTERNAL
#include <rpmgc.h>

#ifdef	NOTNOW
#define	_RPMBC_INTERNAL
#include <rpmbc.h>
#define	_RPMNSS_INTERNAL
#include <rpmnss.h>
#include <pk11pub.h>

#define	_RPMSSL_INTERNAL
#include <rpmssl.h>
#include <openssl/opensslconf.h>	/* XXX OPENSSL_NO_ECDSA */
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#endif	/* NOTNOW */

#include "debug.h"

/*@access pgpDig @*/
/*@access pgpDigParams @*/

/*@-redecl@*/
/*@unchecked@*/
extern int _pgp_debug;

/*@unchecked@*/
extern int _pgp_print;
/*@=redecl@*/

/*==============================================================*/

#define FLAG_CRYPT (1 << 0)
#define FLAG_SIGN  (1 << 1)
#define FLAG_GRIP  (1 << 2)

typedef struct _KP_RSA_s {
    const char * n;
    const char * e;
    const char * d;
    const char * p;
    const char * q;
    const char * u;
} KP_RSA_t;
typedef struct _KP_DSA_s {
    const char * p;
    const char * q;
    const char * g;
    const char * y;
    const char * x;
} KP_DSA_t;
typedef struct _KP_ELG_s {
    const char * p;
    const char * g;
    const char * y;
    const char * x;
} KP_ELG_t;
typedef struct _KP_ECDSA_s {
    const char * curve;
    const char * p;
    const char * a;
    const char * b;
    const char * G;
    const char * n;
    const char * Q;
    const char * d;	/* FIXME: add to test vector. */

    const char * k;
    const char * r;
    const char * s;

} KP_ECDSA_t;
typedef union _KP_u {
    void * sentinel;
    KP_RSA_t RSA;
    KP_DSA_t DSA;
    KP_ELG_t ELG;
    KP_ECDSA_t ECDSA;
} KP_t;
typedef struct _AFKP_s {
    int algo;
    int flags;
    int nbits;
    int qbits;
    
    const char * msg;
    int dalgo;

    KP_t KP;
    const unsigned char grip[20];
} AFKP_t;

/*==============================================================*/
#if defined(_RPMGC_INTERNAL)

static const char * rpmgcSecSexpr(int algo, KP_t * kp)
{
    char * t = NULL;

    switch (algo) {
    default:
	break;
    case PGPPUBKEYALGO_RSA:
	t = rpmExpand(
		"(private-key\n",
		" (RSA\n",
		"  (n #", kp->RSA.n, "#)\n",
		"  (e #", kp->RSA.e, "#)\n",
		"  (d #", kp->RSA.d, "#)\n",
		"  (p #", kp->RSA.p, "#)\n",
		"  (q #", kp->RSA.q, "#)\n",
		"  (u #", kp->RSA.u, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_DSA:
	t = rpmExpand(
		"(private-key\n",
		" (DSA\n",
		"  (p #", kp->DSA.p, "#)\n",
		"  (q #", kp->DSA.q, "#)\n",
		"  (g #", kp->DSA.g, "#)\n",
		"  (y #", kp->DSA.y, "#)\n",
		"  (x #", kp->DSA.x, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_ELGAMAL:
	t = rpmExpand(
		"(private-key\n",
		" (ELG\n",
		"  (p #", kp->ELG.p, "#)\n",
		"  (g #", kp->ELG.g, "#)\n",
		"  (y #", kp->ELG.y, "#)\n",
		"  (x #", kp->ELG.x, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_ECDSA:
/* XXX FIXME: curve (same memory as sentinel in AFKP_t) is never NULL. */
	if (kp->ECDSA.d || kp->ECDSA.curve == NULL || *kp->ECDSA.curve == '\0')
	{
/* XXX FIXME */
	    t = rpmExpand(
		"(public-key\n",
		" (ECDSA\n",
		"  (p #", kp->ECDSA.p, "#)\n",
		"  (a #", kp->ECDSA.a, "#)\n",
		"  (b #", kp->ECDSA.b, "#)\n",
		"  (G #", kp->ECDSA.G, "#)\n",
		"  (n #", kp->ECDSA.n, "#)\n",
		"  (d #", kp->ECDSA.d, "#)))\n",
		"  (Q #", kp->ECDSA.Q, "#)))\n",
#ifdef	NOTYET
    const char * d;	/* FIXME: add to test vector for sec_key. */
#endif
		NULL);
	} else {
/* XXX FIXME */
	    t = rpmExpand(
		"(public-key\n",
		" (ECDSA\n",
		"  (curve ", kp->ECDSA.curve, ")\n",
#ifdef	NOTYET	/* XXX optional overrides? */
		"  (b #", kp->ECDSA.b, "#)\n",
		"  (G #", kp->ECDSA.G, "#)\n",
		"  (n #", kp->ECDSA.n, "#)\n",
#endif
		"  (Q #", kp->ECDSA.Q, "#)))\n",
		NULL);
	}
	break;
    }
    return t;
}

static const char * rpmgcPubSexpr(int algo, KP_t * kp)
{
    char * t = NULL;

    switch (algo) {
    default:
	break;
    case PGPPUBKEYALGO_RSA:
	t = rpmExpand(
		"(public-key\n",
		" (RSA\n",
		"  (n #", kp->RSA.n, "#)\n",
		"  (e #", kp->RSA.e, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_DSA:
	t = rpmExpand(
		"(public-key\n",
		" (DSA\n",
		"  (p #", kp->DSA.p, "#)\n",
		"  (q #", kp->DSA.q, "#)\n",
		"  (g #", kp->DSA.g, "#)\n",
		"  (y #", kp->DSA.y, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_ELGAMAL:
	t = rpmExpand(
		"(public-key\n",
		" (ELG\n",
		"  (p #", kp->ELG.p, "#)\n",
		"  (g #", kp->ELG.g, "#)\n",
		"  (y #", kp->ELG.y, "#)))\n",
		NULL);
	break;
    case PGPPUBKEYALGO_ECDSA:
/* XXX FIXME: curve (same memory as sentinel in AFKP_t) is never NULL. */
	if (kp->ECDSA.d || kp->ECDSA.curve == NULL || *kp->ECDSA.curve == '\0')
	{
	    t = rpmExpand(
		"(public-key\n",
		" (ECDSA\n",
		"  (p #", kp->ECDSA.p, "#)\n",
		"  (a #", kp->ECDSA.a, "#)\n",
		"  (b #", kp->ECDSA.b, "#)\n",
		"  (G #", kp->ECDSA.G, "#)\n",
		"  (n #", kp->ECDSA.n, "#)\n",
		"  (Q #", kp->ECDSA.Q, "#)))\n",
		NULL);
	} else {
	    t = rpmExpand(
		"(public-key\n",
		" (ECDSA\n",
		"  (curve ", kp->ECDSA.curve, ")\n",
#ifdef	NOTYET	/* XXX optional overrides? */
		"  (b #", kp->ECDSA.b, "#)\n",
		"  (G #", kp->ECDSA.G, "#)\n",
		"  (n #", kp->ECDSA.n, "#)\n",
#endif
		"  (Q #", kp->ECDSA.Q, "#)))\n",
		NULL);
	}
	break;
    }
    return t;
}
#endif	/* _RPMGC_INTERNAL */

/*==============================================================*/

typedef struct key_s {
/*@observer@*/
    const char * name;		/* key name */
    uint32_t value;
} KEY;

static int
keyCmp(const void * a, const void * b)
{
    return strcmp(((KEY *)a)->name, ((KEY *)b)->name);
}

static uint32_t
keyValue(KEY * keys, size_t nkeys, /*@null@*/ const char *name)
{
    uint32_t keyval = 0;

    if (name && *name) {
	KEY needle = { .name = name, .value = 0 };
	KEY *k = (KEY *)bsearch(&needle, keys, nkeys, sizeof(*keys), keyCmp);
	if (k)
	    keyval = k->value;
    }
    return keyval;
}

/*==============================================================*/

#if defined(_RPMNSS_INTERNAL)
extern SECStatus
EC_DecodeParams(const SECItem *encodedParams, ECParams **ecparams);

static KEY rpmnssOIDS[] = {
  { "c2onb191v4", SEC_OID_ANSIX962_EC_C2ONB191V4 },
  { "c2onb191v5", SEC_OID_ANSIX962_EC_C2ONB191V5 },
  { "c2onb239v4", SEC_OID_ANSIX962_EC_C2ONB239V4 },
  { "c2onb239v5", SEC_OID_ANSIX962_EC_C2ONB239V5 },
  { "c2pnb163v1", SEC_OID_ANSIX962_EC_C2PNB163V1 },
  { "c2pnb163v2", SEC_OID_ANSIX962_EC_C2PNB163V2 },
  { "c2pnb163v3", SEC_OID_ANSIX962_EC_C2PNB163V3 },
  { "c2pnb176v1", SEC_OID_ANSIX962_EC_C2PNB176V1 },
  { "c2pnb208w1", SEC_OID_ANSIX962_EC_C2PNB208W1 },
  { "c2pnb272w1", SEC_OID_ANSIX962_EC_C2PNB272W1 },
  { "c2pnb304w1", SEC_OID_ANSIX962_EC_C2PNB304W1 },
  { "c2pnb368w1", SEC_OID_ANSIX962_EC_C2PNB368W1 },
  { "c2tnb191v1", SEC_OID_ANSIX962_EC_C2TNB191V1 },
  { "c2tnb191v2", SEC_OID_ANSIX962_EC_C2TNB191V2 },
  { "c2tnb191v3", SEC_OID_ANSIX962_EC_C2TNB191V3 },
  { "c2tnb239v1", SEC_OID_ANSIX962_EC_C2TNB239V1 },
  { "c2tnb239v2", SEC_OID_ANSIX962_EC_C2TNB239V2 },
  { "c2tnb239v3", SEC_OID_ANSIX962_EC_C2TNB239V3 },
  { "c2tnb359v1", SEC_OID_ANSIX962_EC_C2TNB359V1 },
  { "c2tnb431r1", SEC_OID_ANSIX962_EC_C2TNB431R1 },
  { "nistb163", SEC_OID_SECG_EC_SECT163R2},
  { "nistb233", SEC_OID_SECG_EC_SECT233R1},
  { "nistb283", SEC_OID_SECG_EC_SECT283R1},
  { "nistb409", SEC_OID_SECG_EC_SECT409R1},
  { "nistb571", SEC_OID_SECG_EC_SECT571R1},
  { "nistk163", SEC_OID_SECG_EC_SECT163K1},
  { "nistk233", SEC_OID_SECG_EC_SECT233K1},
  { "nistk283", SEC_OID_SECG_EC_SECT283K1},
  { "nistk409", SEC_OID_SECG_EC_SECT409K1},
  { "nistk571", SEC_OID_SECG_EC_SECT571K1},
  { "nistp192", SEC_OID_SECG_EC_SECP192R1},
  { "nistp224", SEC_OID_SECG_EC_SECP224R1},
  { "nistp256", SEC_OID_SECG_EC_SECP256R1},
  { "nistp384", SEC_OID_SECG_EC_SECP384R1},
  { "nistp521", SEC_OID_SECG_EC_SECP521R1},
  { "prime192v1", SEC_OID_ANSIX962_EC_PRIME192V1 },
  { "prime192v2", SEC_OID_ANSIX962_EC_PRIME192V2 },
  { "prime192v3", SEC_OID_ANSIX962_EC_PRIME192V3 },
  { "prime239v1", SEC_OID_ANSIX962_EC_PRIME239V1 },
  { "prime239v2", SEC_OID_ANSIX962_EC_PRIME239V2 },
  { "prime239v3", SEC_OID_ANSIX962_EC_PRIME239V3 },
  { "secp112r1", SEC_OID_SECG_EC_SECP112R1},
  { "secp112r2", SEC_OID_SECG_EC_SECP112R2},
  { "secp128r1", SEC_OID_SECG_EC_SECP128R1},
  { "secp128r2", SEC_OID_SECG_EC_SECP128R2},
  { "secp160k1", SEC_OID_SECG_EC_SECP160K1},
  { "secp160r1", SEC_OID_SECG_EC_SECP160R1},
  { "secp160r2", SEC_OID_SECG_EC_SECP160R2},
  { "secp192k1", SEC_OID_SECG_EC_SECP192K1},
  { "secp192r1", SEC_OID_SECG_EC_SECP192R1},
  { "secp224k1", SEC_OID_SECG_EC_SECP224K1},
  { "secp224r1", SEC_OID_SECG_EC_SECP224R1},
  { "secp256k1", SEC_OID_SECG_EC_SECP256K1},
  { "secp256r1", SEC_OID_SECG_EC_SECP256R1},
  { "secp384r1", SEC_OID_SECG_EC_SECP384R1},
  { "secp521r1", SEC_OID_SECG_EC_SECP521R1},
  { "sect113r1", SEC_OID_SECG_EC_SECT113R1},
  { "sect113r2", SEC_OID_SECG_EC_SECT113R2},
  { "sect131r1", SEC_OID_SECG_EC_SECT131R1},
  { "sect131r2", SEC_OID_SECG_EC_SECT131R2},
  { "sect163k1", SEC_OID_SECG_EC_SECT163K1},
  { "sect163r1", SEC_OID_SECG_EC_SECT163R1},
  { "sect163r2", SEC_OID_SECG_EC_SECT163R2},
  { "sect193r1", SEC_OID_SECG_EC_SECT193R1},
  { "sect193r2", SEC_OID_SECG_EC_SECT193R2},
  { "sect233k1", SEC_OID_SECG_EC_SECT233K1},
  { "sect233r1", SEC_OID_SECG_EC_SECT233R1},
  { "sect239k1", SEC_OID_SECG_EC_SECT239K1},
  { "sect283k1", SEC_OID_SECG_EC_SECT283K1},
  { "sect283r1", SEC_OID_SECG_EC_SECT283R1},
  { "sect409k1", SEC_OID_SECG_EC_SECT409K1},
  { "sect409r1", SEC_OID_SECG_EC_SECT409R1},
  { "sect571k1", SEC_OID_SECG_EC_SECT571K1},
  { "sect571r1", SEC_OID_SECG_EC_SECT571R1},
};
static size_t nrpmnssOIDS = sizeof(rpmnssOIDS) / sizeof(rpmnssOIDS[0]);

static void rpmnssDumpSECITEM(const char * msg, SECItem * p)
{
    fprintf(stderr, "%s: %p", msg, p);
    if (p)
	fprintf(stderr, " type %d %p[%u]\n\t%s",
		p->type, p->data, (unsigned)p->len, pgpHexStr(p->data, p->len));
    fprintf(stderr, "\n");
}

static void rpmnssDumpPUBKEY(const char * msg, SECKEYPublicKey * p)
{
    fprintf(stderr, "%s: %p", msg, p);
    if (p)
	fprintf(stderr, " type %d slot %p id %p",
		p->keyType, p->pkcs11Slot, (void *)p->pkcs11ID);
    fprintf(stderr, "\n");
}

static void rpmnssDumpPRVKEY(const char * msg, SECKEYPrivateKey * p)
{
    fprintf(stderr, "%3s: %p", msg, p);
    if (p)
	fprintf(stderr, " type %d slot %p id %p",
		p->keyType, p->pkcs11Slot, (void *)p->pkcs11ID);
    fprintf(stderr, "\n");
}

static uint32_t curve2oid(const char * name)
{
    uint32_t oid = keyValue(rpmnssOIDS, nrpmnssOIDS, name);
    if (oid == 0)
	oid = SEC_OID_UNKNOWN;

if (_pgp_debug)
fprintf(stderr, "<-- %s(%s) oid %u\n", __FUNCTION__, name, oid);

    return oid;
}

static void rpmnssLoadParams(pgpDig dig, const char * name)
{
    rpmnss nss = dig->impl;
    SECOidTag curveOidTag = curve2oid(name);
    SECOidData * oidData = SECOID_FindOIDByTag(curveOidTag);
    
    if (curveOidTag == SEC_OID_UNKNOWN || oidData == NULL) {
	nss->sigalg = curveOidTag;
	goto exit;
    }
    nss->sigalg = curveOidTag;

    nss->ecparams = SECITEM_AllocItem(NULL, NULL, (2 + oidData->oid.len));
    nss->ecparams->data[0] = SEC_ASN1_OBJECT_ID;
    nss->ecparams->data[1] = oidData->oid.len;
    memcpy(nss->ecparams->data + 2, oidData->oid.data, oidData->oid.len);

exit:
if (_pgp_debug)
fprintf(stderr, "<-- %s(%p,%s) oid %u params %p\n", __FUNCTION__, dig, name, nss->sigalg, nss->ecparams);
    return;
}

static
int rpmnssSetECDSA(/*@only@*/ DIGEST_CTX ctx, /*@unused@*/pgpDig dig, pgpDigParams sigp)
	/*@*/
{
    int rc = 1;		/* assume failure */;
    rpmnss nss = dig->impl;
    int xx;

assert(sigp->hash_algo == rpmDigestAlgo(ctx));

nss->digest = _free(nss->digest);
nss->digestlen = 0;
    xx = rpmDigestFinal(ctx, (void **)&nss->digest, &nss->digestlen, 0);

#ifdef	DYING
    nss->sigalg = SEC_OID_UNKNOWN;
    switch (sigp->hash_algo) {
    case PGPHASHALGO_SHA1:
	nss->sigalg = SEC_OID_ANSIX962_ECDSA_SHA1_SIGNATURE;
	break;
    case PGPHASHALGO_SHA224:
	nss->sigalg = SEC_OID_ANSIX962_ECDSA_SHA224_SIGNATURE;
	break;
    case PGPHASHALGO_SHA256:
	nss->sigalg = SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE;
	break;
    case PGPHASHALGO_SHA384:
	nss->sigalg = SEC_OID_ANSIX962_ECDSA_SHA384_SIGNATURE;
	break;
    case PGPHASHALGO_SHA512:
	nss->sigalg = SEC_OID_ANSIX962_ECDSA_SHA512_SIGNATURE;
	break;
    default:
	break;
    }
#endif
    if (nss->sigalg == SEC_OID_UNKNOWN)
	goto exit;
assert(nss->sigalg != 0);

#ifdef	NOTYET
    /* Compare leading 16 bits of digest for quick check. */
    rc = memcmp(nss->digest, sigp->signhash16, sizeof(sigp->signhash16));
#else
    rc = 0;
#endif

exit:
if (_pgp_debug)
fprintf(stderr, "<-- %s(%p,%p) rc %d\n", __FUNCTION__, dig, sigp, rc);
    return rc;
}

static
int rpmnssVerifyECDSA(/*@unused@*/pgpDig dig)
	/*@*/
{
    rpmnss nss = dig->impl;
    int rc = 0;		/* assume failure */;

    nss->item.type = siBuffer;
    nss->item.data = nss->digest;
    nss->item.len = (unsigned) nss->digestlen;

    if (VFY_VerifyDigest(&nss->item, nss->ecdsa, nss->ecdsasig, nss->sigalg, NULL) == SECSuccess)
	rc = 1;

if (_pgp_debug || rc == 0) {
rpmnssDumpSECITEM("  digest", &nss->item);
rpmnssDumpPUBKEY( "     pub", nss->ecdsa);
rpmnssDumpSECITEM("     sig", nss->ecdsasig);
fprintf(stderr, "<-- %s(%p) rc %d\n", __FUNCTION__, dig, rc);
}

    return rc;
}

static
int rpmnssSignECDSA(/*@unused@*/pgpDig dig)
	/*@*/
{
    rpmnss nss = dig->impl;
    int rc = 0;		/* assume failure. */

    nss->item.type = siBuffer;
    nss->item.data = nss->digest;
    nss->item.len = (unsigned) nss->digestlen;

if (nss->ecdsasig != NULL) {
    SECITEM_ZfreeItem(nss->ecdsasig, PR_TRUE);
    nss->ecdsasig = NULL;
}

nss->ecdsasig = SECITEM_AllocItem(NULL, NULL, 0);
nss->ecdsasig->type = siBuffer;

    if (SGN_Digest(nss->ecpriv, nss->sigalg, nss->ecdsasig, &nss->item) == SECSuccess)
	rc = 1;

if (_pgp_debug || rc == 0) {
rpmnssDumpSECITEM("  digest", &nss->item);
rpmnssDumpPRVKEY( "    priv", nss->ecpriv);
rpmnssDumpSECITEM("     sig", nss->ecdsasig);
fprintf(stderr, "<-- %s(%p) rc %d\n", __FUNCTION__, dig, rc);
}

    return rc;
}

static
int rpmnssGenerateECDSA(/*@unused@*/pgpDig dig)
	/*@*/
{
    rpmnss nss = dig->impl;
    int rc = 0;		/* assume failure. */

if (nss->ecpriv != NULL) {
    SECKEY_DestroyPrivateKey(nss->ecpriv);
    nss->ecpriv = NULL;
}
if (nss->ecdsa != NULL) {
    SECKEY_DestroyPublicKey(nss->ecdsa);
    nss->ecdsa = NULL;
}

#ifndef	DYING
    {	CK_MECHANISM_TYPE _type = CKM_EC_KEY_PAIR_GEN;
	PK11SlotInfo * _slot = PK11_GetBestSlot(_type, NULL);

	if (_slot) {
	    nss->ecpriv = PK11_GenerateKeyPair(_slot, _type, nss->ecparams,
			&nss->ecdsa, PR_FALSE, PR_FALSE, NULL);
	    PK11_FreeSlot(_slot);
	}
    }
#else
    nss->ecpriv = SECKEY_CreateECPrivateKey(nss->ecparams, &nss->ecdsa, NULL);
#endif

    rc = (nss->ecpriv && nss->ecdsa);

if (_pgp_debug || rc == 0) {
rpmnssDumpPRVKEY("    priv", nss->ecpriv);
rpmnssDumpPUBKEY("     pub", nss->ecdsa);
fprintf(stderr, "<-- %s(%p) rc %d\n", __FUNCTION__, dig, rc);
}

    return rc;
}
#endif	/* _RPMNSS_INTERNAL */

/*==============================================================*/

#if defined(_RPMSSL_INTERNAL)
static KEY rpmsslNIDS[] = {
  { "c2pnb163v1", 684 },	/* X9.62 curve over a 163 bit binary field */
  { "c2pnb163v2", 685 },	/* X9.62 curve over a 163 bit binary field */
  { "c2pnb163v3", 686 },	/* X9.62 curve over a 163 bit binary field */
  { "c2pnb176v1", 687 },	/* X9.62 curve over a 176 bit binary field */
  { "c2pnb208w1", 693 },	/* X9.62 curve over a 208 bit binary field */
  { "c2pnb272w1", 699 },	/* X9.62 curve over a 272 bit binary field */
  { "c2pnb304w1", 700 },	/* X9.62 curve over a 304 bit binary field */
  { "c2pnb368w1", 702 },	/* X9.62 curve over a 368 bit binary field */
  { "c2tnb191v1", NID_X9_62_c2tnb191v1 },	/* X9.62 curve over a 191 bit binary field */
  { "c2tnb191v2", 689 },	/* X9.62 curve over a 191 bit binary field */
  { "c2tnb191v3", 690 },	/* X9.62 curve over a 191 bit binary field */
  { "c2tnb239v1", NID_X9_62_c2tnb239v1 },	/* X9.62 curve over a 239 bit binary field */
  { "c2tnb239v2", 695 },	/* X9.62 curve over a 239 bit binary field */
  { "c2tnb239v3", 696 },	/* X9.62 curve over a 239 bit binary field */
  { "c2tnb359v1", 701 },	/* X9.62 curve over a 359 bit binary field */
  { "c2tnb431r1", 703 },	/* X9.62 curve over a 431 bit binary field */

  { "nistp192", 711 },			/* XXX */
  { "nistp224", NID_secp224r1 },	/* XXX NIST/SECG */
  { "nistp256", NID_X9_62_prime256v1 },	/* XXX */
  { "nistp384", NID_secp384r1 },	/* XXX NIST/SECG */
  { "nistp521", NID_secp521r1 },	/* XXX NIST/SECG */

  { "prime192v1", NID_X9_62_prime192v1 }, /* NIST/X9.62/SECG curve over a 192 bit prime field */
  { "prime192v2", 410 },	/* X9.62 curve over a 192 bit prime field */
  { "prime192v3", 411 },	/* X9.62 curve over a 192 bit prime field */
  { "prime239v1", NID_X9_62_prime239v1 },	/* X9.62 curve over a 239 bit prime field */
  { "prime239v2", 413 },	/* X9.62 curve over a 239 bit prime field */
  { "prime239v3", 414 },	/* X9.62 curve over a 239 bit prime field */
  { "prime256v1", NID_X9_62_prime256v1 },	/* X9.62/SECG curve over a 256 bit prime field */
  { "secp112r1", 704 },	/* SECG/WTLS curve over a 112 bit prime field */
  { "secp112r2", 705 },	/* SECG curve over a 112 bit prime field */
  { "secp128r1", 706 },	/* SECG curve over a 128 bit prime field */
  { "secp128r2", 707 },	/* SECG curve over a 128 bit prime field */
  { "secp160k1", 708 },	/* SECG curve over a 160 bit prime field */
  { "secp160r1", 709 },	/* SECG curve over a 160 bit prime field */
  { "secp160r2", 710 },	/* SECG/WTLS curve over a 160 bit prime field */
  { "secp192k1", 711 },	/* SECG curve over a 192 bit prime field */
  { "secp224k1", 712 },	/* SECG curve over a 224 bit prime field */
  { "secp224r1", NID_secp224r1 },	/* NIST/SECG curve over a 224 bit prime field */
  { "secp256k1", 714 },	/* SECG curve over a 256 bit prime field */
  { "secp384r1", NID_secp384r1 },	/* NIST/SECG curve over a 384 bit prime field */
  { "secp521r1", NID_secp521r1 },	/* NIST/SECG curve over a 521 bit prime field */
  { "sect113r1", 717 },	/* SECG curve over a 113 bit binary field */
  { "sect113r2", 718 },	/* SECG curve over a 113 bit binary field */
  { "sect131r1", 719 },	/* SECG/WTLS curve over a 131 bit binary field */
  { "sect131r2", 720 },	/* SECG curve over a 131 bit binary field */
  { "sect163k1", 721 },	/* NIST/SECG/WTLS curve over a 163 bit binary field */
  { "sect163r1", 722 },	/* SECG curve over a 163 bit binary field */
  { "sect163r2", 723 },	/* NIST/SECG curve over a 163 bit binary field */
  { "sect193r1", 724 },	/* SECG curve over a 193 bit binary field */
  { "sect193r2", 725 },	/* SECG curve over a 193 bit binary field */
  { "sect233k1", 726 },	/* NIST/SECG/WTLS curve over a 233 bit binary field */
  { "sect233r1", 727 },	/* NIST/SECG/WTLS curve over a 233 bit binary field */
  { "sect239k1", 728 },	/* SECG curve over a 239 bit binary field */
  { "sect283k1", 729 },	/* NIST/SECG curve over a 283 bit binary field */
  { "sect283r1", 730 },	/* NIST/SECG curve over a 283 bit binary field */
  { "sect409k1", 731 },	/* NIST/SECG curve over a 409 bit binary field */
  { "sect409r1", 732 },	/* NIST/SECG curve over a 409 bit binary field */
  { "sect571k1", 733 },	/* NIST/SECG curve over a 571 bit binary field */
  { "sect571r1", 734 },	/* NIST/SECG curve over a 571 bit binary field */
  { "wap-wsg-idm-ecid-wtls10", 743 },	/* NIST/SECG/WTLS curve over a 233 bit binary field */
  { "wap-wsg-idm-ecid-wtls11", 744 },	/* NIST/SECG/WTLS curve over a 233 bit binary field */
  { "wap-wsg-idm-ecid-wtls12", 745 },	/* WTLS curvs over a 224 bit prime field */
  { "wap-wsg-idm-ecid-wtls1", 735 },	/* WTLS curve over a 113 bit binary field */
  { "wap-wsg-idm-ecid-wtls3", 736 },	/* NIST/SECG/WTLS curve over a 163 bit binary field */
  { "wap-wsg-idm-ecid-wtls4", 737 },	/* SECG curve over a 113 bit binary field */
  { "wap-wsg-idm-ecid-wtls5", 738 },	/* X9.62 curve over a 163 bit binary field */
  { "wap-wsg-idm-ecid-wtls6", 739 },	/* SECG/WTLS curve over a 112 bit prime field */
  { "wap-wsg-idm-ecid-wtls7", 740 },	/* SECG/WTLS curve over a 160 bit prime field */
  { "wap-wsg-idm-ecid-wtls8", 741 },	/* WTLS curve over a 112 bit prime field */
  { "wap-wsg-idm-ecid-wtls9", 742 },	/* WTLS curve over a 160 bit prime field */
};
static size_t nrpmsslNIDS = sizeof(rpmsslNIDS) / sizeof(rpmsslNIDS[0]);

static int curve2nid(const char * name)
{
    int nid = keyValue(rpmsslNIDS, nrpmsslNIDS, name);
fprintf(stderr, "<-- %s(%s) nid %u\n", __FUNCTION__, name, nid);
    return nid;
}

static int _rpmssl_spew;

static int rpmsslLoadBN(BIGNUM ** bnp, const char * bnstr, int spew)
{
    int rc;

    *bnp = NULL;
    if (bnstr[0] == '0' && bnstr[1] == 'x')
	rc = BN_hex2bn(bnp, bnstr + 2);
    else
	rc = BN_dec2bn(bnp, bnstr);

    if (rc && (spew || _rpmssl_spew)) {
	char * t;
	rpmlog(RPMLOG_DEBUG, "\t%p: %s\n", *bnp, t=BN_bn2dec(*bnp));
	OPENSSL_free(t);
	rpmlog(RPMLOG_DEBUG, "\t%p: 0x%s\n", *bnp, t=BN_bn2hex(*bnp));
	OPENSSL_free(t);
    }

    return rc;
}
#endif	/* _RPMSSL_INTERNAL */

/*==============================================================*/

static const char ** _numbers;
static int _ix = 0;
static int _numbers_max = 2;

#if defined(_RPMSSL_INTERNAL)
static const RAND_METHOD *old_rand;

static int fbytes(unsigned char *buf, int num)
{
    BIGNUM *tmp = NULL;
    int ret = 0;	/* assume failure */

    if (!(_numbers && _ix < _numbers_max && _numbers[_ix]))
	return 0;

    tmp = NULL;
    if (!rpmsslLoadBN(&tmp, _numbers[_ix], _rpmssl_spew))
	goto exit;

    _ix++;
    ret = BN_bn2bin(tmp, buf);
    if (ret == 0 || ret != num)
	ret = 0;
    else
	ret = 1;

exit:
    if (tmp)
	BN_free(tmp);
    return ret;
}
#endif	/* _RPMSSL_INTERNAL */

static int change_rand(void)
{
    int rc = 1;		/* assume success */
#if defined(_RPMSSL_INTERNAL)
    static RAND_METHOD fake_rand;

    /* save old rand method */
    if ((old_rand = RAND_get_rand_method()) == NULL) {
	rc = 0;
	goto exit;
    }

    fake_rand.seed = old_rand->seed;
    fake_rand.cleanup = old_rand->cleanup;
    fake_rand.add = old_rand->add;
    fake_rand.status = old_rand->status;
    /* use own random function */
    fake_rand.bytes = fbytes;
    fake_rand.pseudorand = old_rand->bytes;

    rc = (RAND_set_rand_method(&fake_rand) ? 1 : 0);
exit:
#endif	/* _RPMSSL_INTERNAL */
    return rc;
}

static int restore_rand(void)
{
    int rc = 1;		/* assume success */
#if defined(_RPMSSL_INTERNAL)
    rc = (RAND_set_rand_method(old_rand) ? 1 : 0);
#endif	/* _RPMSSL_INTERNAL */
    return rc;
}

/*==============================================================*/

static AFKP_t ECDSAvecs[] = {
 { /* ----- X9.66-1998 J.3.1 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "abc",
  .nbits = 192,
  .dalgo = PGPHASHALGO_SHA1,
  .KP = {
  .ECDSA = {
.curve = "prime192v1",
   .d =	"0x1A8D598FC15BF0FD89030B5CB1111AEB92AE8BAF5EA475FB",
   .k =	"0xFA6DE29746BBEB7F8BB1E761F85F7DFB2983169D82FA2F4E",
   .r =	"0x885052380FF147B734C330C43D39B2C4A89F29B0F749FEAD",
   .s =	"0xE9ECC78106DEF82BF1070CF1D4D804C3CB390046951DF686"
   }	/* .ECDSA */
  }	/* .KP */
 },
 {
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "abc",
  .nbits = 239,
  .dalgo = PGPHASHALGO_SHA1,
  .KP = {
  .ECDSA = {
.curve = "prime239v1",
   .d =	"0x7EF7C6FABEFFFDEA864206E80B0B08A9331ED93E698561B64CA0F7777F3D",
   .k =	"0x656C7196BF87DCC5D1F1020906DF2782360D36B2DE7A17ECE37D503784AF",
   .r =	"0x2CB7F36803EBB9C427C58D8265F11FC5084747133078FC279DE874FBECB0",
   .s =	"0x2EEAE988104E9C2234A3C2BEB1F53BFA5DC11FF36A875D1E3CCB1F7E45CF"
   }	/* .ECDSA */
  }	/* .KP */
 },
 {
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "abc",
  .nbits = 191,
  .dalgo = PGPHASHALGO_SHA1,
  .KP = {
  .ECDSA = {
.curve = "c2tnb191v1",
   .d =	"0x340562E1DDA332F9D2AEC168249B5696EE39D0ED4D03760F",
   .k =	"0x3EEACE72B4919D991738D521879F787CB590AFF8189D2B69",
   .r =	"0x038E5A11FB55E4C65471DCD4998452B1E02D8AF7099BB930",
   .s =	"0x0C9A08C34468C244B4E5D6B21B3C68362807416020328B6E"
   }	/* .ECDSA */
  }	/* .KP */
 },
 {
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "abc",
  .nbits = 239,
  .dalgo = PGPHASHALGO_SHA1,
  .KP = {
  .ECDSA = {
.curve = "c2tnb239v1",
   .d =	"0x151A30A6D843DB3B25063C5108255CC4448EC0F4D426D4EC884502229C96",
   .k =	"0x18D114BDF47E2913463E50375DC92784A14934A124F83D28CAF97C5D8AAB",
   .r =	"0x03210D71EF6C10157C0D1053DFF93E8B085F1E9BC22401F7A24798A63C00",
   .s =	"0x1C8C4343A8ECBF7C4D4E48F7D76D5658BC027C77086EC8B10097DEB307D6"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-192 FIPS 186-3 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "Example of ECDSA with P-192",
  .nbits = 192,
  .dalgo = PGPHASHALGO_SHA1,
  .KP = {
  .ECDSA = {
.curve = "nistp192",
   .p =	"0xfffffffffffffffffffffffffffffffeffffffffffffffff",
   .a =	"0xfffffffffffffffffffffffffffffffefffffffffffffffc",
   .b =	"0x64210519e59c80e70fa7e9ab72243049feb8deecc146b9b1",
   .G = "0x04"
	"188da80eb03090f67cbf20eb43a18800f4ff0afd82ff1012"
	"07192b95ffc8da78631011ed6b24cdd573f977a11e794811",
   .n =	"0xffffffffffffffffffffffff99def836146bc9b1b4d22831",
/* XXX checkme! */
   .Q = "0x04"
	"BBA0C04725F939ABE6A0D1E9CEB82F336B3A04205C6E5094"
	"FE6D6A3F6F72616D7488113AF4ABA44581ADEE297B984A08",

   .d =	"0x7891686032fd8057f636b44b1f47cce564d2509923a7465b",
   .k =	"0xd06cb0a0ef2f708b0744f08aa06b6deedea9c0f80a69d847",

   .r =	"0xf0ecba72b88cde399cc5a18e2a8b7da54d81d04fb9802821",
   .s =	"0x1e6d3d4ae2b1fab2bd2040f5dabf00f854fa140b6d21e8ed"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-224 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "Example of ECDSA with P-224",
  .nbits = 224,
  .dalgo = PGPHASHALGO_SHA224,
  .KP = {
  .ECDSA = {
.curve = "nistp224",
   .p =	"0xffffffffffffffffffffffffffffffff000000000000000000000001",
   .a =	"0xfffffffffffffffffffffffffffffffefffffffffffffffffffffffe",
   .b =	"0xb4050a850c04b3abf54132565044b0b7d7bfd8ba270b39432355ffb4",
   .G = "0x04"
	"b70e0cbd6bb4bf7f321390b94a03c1d356c21122343280d6115c1d21"
	"bd376388b5f723fb4c22dfe6cd4375a05a07476444d5819985007e34",
   .n =	"0xffffffffffffffffffffffffffff16a2e0b8f03e13dd29455c5c2a3d" ,
   .Q = "0x04"
	"e84fb0b8e7000cb657d7973cf6b42ed78b301674276df744af130b3e"
	"4376675c6fc5612c21a0ff2d2a89d2987df7a2bc52183b5982298555",

   .d =	"0x3f0c488e987c80be0fee521f8d90be6034ec69ae11ca72aa777481e8",
   .k =	"0xa548803b79df17c40cde3ff0e36d025143bcbba146ec32908eb84937",

   .r =	"0xc3a3f5b82712532004c6f6d1db672f55d931c3409ea1216d0be77380",
   .s =	"0xc5aa1eae6095dea34c9bd84da3852cca41a8bd9d5548f36dabdf6617"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-256 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "Example of ECDSA with P-256",
  .nbits = 256,
  .dalgo = PGPHASHALGO_SHA256,
  .KP = {
  .ECDSA = {
.curve = "nistp256",
   .p =	"0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
   .a =	"0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc",
   .b =	"0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b",
   .G = "0x04"
	"6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
	"4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
   .n =	"0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
   .Q = "0x04"
	"b7e08afdfe94bad3f1dc8c734798ba1c62b3a0ad1e9ea2a38201cd0889bc7a19"
	"3603f747959dbf7a4bb226e41928729063adc7ae43529e61b563bbc606cc5e09",

   .d =	"0xc477f9f65c22cce20657faa5b2d1d8122336f851a508a1ed04e479c34985bf96",
   .k =	"0x7a1a7e52797fc8caaa435d2a4dace39158504bf204fbe19f14dbb427faee50ae",

   .r =	"0x2b42f576d07f4165ff65d1f3b1500f81e44c316f1f0b3ef57325b69aca46104f",
   .s =	"0xdc42c2122d6392cd3e3a993a89502a8198c1886fe69d262c4b329bdb6b63faf1"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-384 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "Example of ECDSA with P-384",
  .nbits = 384,
  .dalgo = PGPHASHALGO_SHA384,
  .KP = {
  .ECDSA = {
.curve = "nistp384",
   .p =	"0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
	"ffffffff0000000000000000ffffffff",
   .a =	"0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
	"ffffffff0000000000000000fffffffc",
   .b =	"0xb3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875a"
	"c656398d8a2ed19d2a85c8edd3ec2aef",
   .G = "0x04"
	"aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a38"
	"5502f25dbf55296c3a545e3872760ab7"
	"3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c0"
	"0a60b1ce1d7e819d7a431d7c90ea0e5f",
   .n =	"0xffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf"
	"581a0db248b0a77aecec196accc52973",
   .Q =	"0x04"
	"3bf701bc9e9d36b4d5f1455343f09126f2564390f2b487365071243c61e6471f"
	"b9d2ab74657b82f9086489d9ef0f5cb5"
	"d1a358eafbf952e68d533855ccbdaa6ff75b137a5101443199325583552a6295"
	"ffe5382d00cfcda30344a9b5b68db855",

   .d =	"0xf92c02ed629e4b48c0584b1c6ce3a3e3b4faae4afc6acb0455e73dfc392e6a0a"
	"e393a8565e6b9714d1224b57d83f8a08",
   .k =	"0x2e44ef1f8c0bea8394e3dda81ec6a7842a459b534701749e2ed95f054f013768"
	"0878e0749fc43f85edcae06cc2f43fef",

   .r =	"0x30ea514fc0d38d8208756f068113c7cada9f66a3b40ea3b313d040d9b57dd41a"
	"332795d02cc7d507fcef9faf01a27088",
   .s =	"0xcc808e504be414f46c9027bcbf78adf067a43922d6fcaa66c4476875fbb7b94e"
	"fd1f7d5dbe620bfb821c46d549683ad8"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-521 */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "Example of ECDSA with P-521",
  .nbits = 521,
  .dalgo = PGPHASHALGO_SHA512,
  .KP = {
  .ECDSA = {
.curve = "nistp521",
   .p =	"0x01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
	"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
   .a =	"0x01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
	"fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc",
   .b =	"0x051953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef10"
	"9e156193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b503f00",
   .G = "0x04"
	"c6858e06b70404e9cd9e3ecb662395b4429c648139053fb521f828af606b4d3d"
	"baa14b5e77efe75928fe1dc127a2ffa8de3348b3c1856a429bf97e7e31c2e5bd66"
	"11839296a789a3bc0045c8a5fb42c7d1bd998f54449579b446817afbd17273e6"
	"62c97ee72995ef42640c550b9013fad0761353c7086a272c24088be94769fd16650",
   .n =	"0x1fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
	"ffa51868783bf2f966b7fcc0148f709a5d03bb5c9b8899c47aebb6fb71e91386409",
   .Q =	"0x04"
	"0098e91eef9a68452822309c52fab453f5f117c1da8ed796b255e9ab8f6410cc"
	"a16e59df403a6bdc6ca467a37056b1e54b3005d8ac030decfeb68df18b171885d5c4"
	"0164350c321aecfc1cca1ba4364c9b15656150b4b78d6a48d7d28e7f31985ef1"
	"7be8554376b72900712c4b83ad668327231526e313f5f092999a4632fd50d946bc2e",

   .d =	"0x0100085f47b8e1b8b11b7eb33028c0b2888e304bfc98501955b45bba1478dc18"
	"4eeedf09b86a5f7c21994406072787205e69a63709fe35aa93ba333514b24f961722",
   .k =	"0xc91e2349ef6ca22d2de39dd51819b6aad922d3aecdeab452ba172f7d63e370ce"
	"cd70575f597c09a174ba76bed05a48e562be0625336d16b8703147a6a231d6bf",

   .r =	"0x0140c8edca57108ce3f7e7a240ddd3ad74d81e2de62451fc1d558fdc79269ada"
	"cd1c2526eeeef32f8c0432a9d56e2b4a8a732891c37c9b96641a9254ccfe5dc3e2ba",
   .s =	"0x00d72f15229d0096376da6651d9985bfd7c07f8d49583b545db3eab20e0a2c1e"
	"8615bd9e298455bdeb6b61378e77af1c54eee2ce37b2c61f5c9a8232951cb988b5b1"

   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-256 NSA Suite B */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "This is only a test message. It is 48 bytes long",
  .nbits = 256,
  .dalgo = PGPHASHALGO_SHA256,
  .KP = {
  .ECDSA = {
.curve = "nistp256",
   .p =	"0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
   .a =	"0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc",
   .b =	"0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b",
   .G = "0x04"
	"6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
	"4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
   .n =	"0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
   .Q = "0x04"
	"b7e08afdfe94bad3f1dc8c734798ba1c62b3a0ad1e9ea2a38201cd0889bc7a19"
	"3603f747959dbf7a4bb226e41928729063adc7ae43529e61b563bbc606cc5e09",

   .d =	"0x70a12c2db16845ed56ff68cfc21a472b3f04d7d6851bf6349f2d7d5b3452b38a",
   .k =	"0x580ec00d856434334cef3f71ecaed4965b12ae37fa47055b1965c7b134ee45d0",

   .r =	"0x7214bc9647160bbd39ff2f80533f5dc6ddd70ddf86bb815661e805d5d4e6f27c",
   .s =	"0x7d1ff961980f961bdaa3233b6209f4013317d3e3f9e1493592dbeaa1af2bc367"
   }	/* .ECDSA */
  }	/* .KP */
 },
 { /* --- P-384 NSA Suite B */
  .algo = PGPPUBKEYALGO_ECDSA,
  .flags = FLAG_SIGN | FLAG_GRIP,
  .msg = "This is only a test message. It is 48 bytes long",
  .nbits = 384,
  .dalgo = PGPHASHALGO_SHA384,
  .KP = {
  .ECDSA = {
.curve = "nistp384",
   .p =	"0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
	"ffffffff0000000000000000ffffffff",
   .a =	"0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
	"ffffffff0000000000000000fffffffc",
   .b =	"0xb3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875a"
	"c656398d8a2ed19d2a85c8edd3ec2aef",
   .G = "0x04"
	"aa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a38"
	"5502f25dbf55296c3a545e3872760ab7"
	"3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c0"
	"0a60b1ce1d7e819d7a431d7c90ea0e5f",
   .n =	"0xffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf"
	"581a0db248b0a77aecec196accc52973",
   .Q =	"0x04"
	"3bf701bc9e9d36b4d5f1455343f09126f2564390f2b487365071243c61e6471f"
	"b9d2ab74657b82f9086489d9ef0f5cb5"
	"d1a358eafbf952e68d533855ccbdaa6ff75b137a5101443199325583552a6295"
	"ffe5382d00cfcda30344a9b5b68db855",

   .d =	"0xc838b85253ef8dc7394fa5808a5183981c7deef5a69ba8f4f2117ffea39cfcd9"
	"0e95f6cbc854abacab701d50c1f3cf24",
   .k =	"0xdc6b44036989a196e39d1cdac000812f4bdd8b2db41bb33af51372585ebd1db6"
	"3f0ce8275aa1fd45e2d2a735f8749359",

   .r =	"0xa0c27ec893092dea1e1bd2ccfed3cf945c8134ed0c9f81311a0f4a05942db8db"
	"ed8dd59f267471d5462aa14fe72de856",
   .s =	"0x20ab3f45b74f10b6e11f96a2c8eb694d206b9dda86d3c7e331c26b22c987b753"
	"7726577667adadf168ebbe803794a402"
   }	/* .ECDSA */
  }	/* .KP */
 },
 {
  .algo = 0,
  .flags = 0,
  .msg = NULL,
  .nbits = 0,
  .dalgo = 0,
  .KP = {
  .ECDSA = {
.curve = NULL,
   .d =	NULL,
   .k =	NULL,
   .r =	NULL,
   .s =	NULL
   }	/* .ECDSA */
  }	/* .KP */
 }
};

static int pgpDigTestOne(pgpDig dig,
		const char * msg, const char *r_in, const char *s_in)
{
    pgpDigParams sigp = pgpGetSignature(dig);
    DIGEST_CTX ctx;
    int rc;
int bingo = 0;

    /* create the key */
    rc = pgpImplGenerate(dig);
    if (!rc)
	goto exit;
bingo++;

#ifdef	REFERENCE
{   rpmgc gc = dig->impl;
    unsigned char sgrip[20];
    (void) gcry_pk_get_keygrip (gc->sec_key, sgrip);
    fprintf(stderr, "\t%s\n", pgpHexStr(sgrip, sizeof(sgrip)));
}
#endif

    /* generate the hash */
    ctx = rpmDigestInit(sigp->hash_algo, RPMDIGEST_NONE);
    rc = rpmDigestUpdate(ctx, msg, strlen(msg));

    /* create the signature */
    rc = pgpImplSetECDSA(rpmDigestDup(ctx), dig, sigp);
    rc = pgpImplSign(dig);
    if (!rc)
	goto exit;
bingo++;

#if defined(_RPMSSL_INTERNAL)
    /* check the {r,s} parameters */
    if (pgpImplVecs == &rpmsslImplVecs) {
	rpmssl ssl = dig->impl;
	if (!rpmsslLoadBN(&ssl->r, r_in, _rpmssl_spew)
	 || BN_cmp(ssl->ecdsasig->r, ssl->r))
	    goto exit;
	if (!rpmsslLoadBN(&ssl->s, s_in, _rpmssl_spew)
	 || BN_cmp(ssl->ecdsasig->s, ssl->s))
	    goto exit;
    }
#endif	/* _RPMSSL_INTERNAL */
bingo++;

    /* verify the signature */
    rc = pgpImplSetECDSA(ctx, dig, sigp);
    ctx = NULL;

    rc = pgpImplVerify(dig);
    if (rc != 1)
	goto exit;
bingo++;

exit:

    return bingo;		/* XXX 1 on success */
}

static int pgpDigTests(pgpDig dig)
{
    pgpDigParams pubp = pgpGetPubkey(dig);
    pgpDigParams sigp = pgpGetSignature(dig);
    AFKP_t * v;
    int ret = -1;	/* assume failure */
static const char dots[] = "..........";

    rpmlog(RPMLOG_NOTICE, "========== X9.62/NIST/NSA ECDSA vectors:\n");

    /* set own rand method */
    if (!change_rand())
	goto exit;

    for (v = ECDSAvecs; v->KP.ECDSA.r != NULL; v++) {
	int rc;

	pgpDigClean(dig);
pubp->pubkey_algo = PGPPUBKEYALGO_ECDSA;

#if defined(_RPMBC_INTERNAL)
if (pgpImplVecs == &rpmbcImplVecs) {
}
#endif
#if defined(_RPMGC_INTERNAL)
if (pgpImplVecs == &rpmgcImplVecs) {
    rpmgc gc = dig->impl;
#ifdef	REFERENCE
    char * t;
    t = rpmgcSecSexpr(pubp->pubkey_algo, &v->KP);
    gc->err = gcry_sexp_sscan (&gc->sec_key, NULL, t, strlen(t));
    t = _free(t);
    t = rpmgcPubSexpr(pubp->pubkey_algo, &v->KP);
    gc->err = gcry_sexp_sscan (&gc->pub_key, NULL, t, strlen(t));
    t = _free(t);
#endif
    gc->nbits = v->nbits;
}
#endif
#if defined(_RPMNSS_INTERNAL)
if (pgpImplVecs == &rpmnssImplVecs) {
    rpmnssLoadParams(dig, v->KP.ECDSA.curve);
}
#endif
#if defined(_RPMSSL_INTERNAL)
if (pgpImplVecs == &rpmsslImplVecs) {
    rpmssl ssl = dig->impl;
    ssl->nid = curve2nid(v->KP.ECDSA.curve);
}
#endif

sigp->hash_algo = v->dalgo;
_ix = 0;
_numbers = &v->KP.ECDSA.d;
_numbers_max = 2;

	rpmlog(RPMLOG_INFO, "%s:\t", v->KP.ECDSA.curve);

	rc = pgpDigTestOne(dig, v->msg, v->KP.ECDSA.r, v->KP.ECDSA.s);

	rpmlog(RPMLOG_INFO, "%s %s\n", &dots[strlen(dots)-rc],
			(rc >= 4 ? " ok" : " failed"));
	if (rc < 4)
	    ret = 0;

	pgpImplClean(dig->impl);	/* XXX needed for memleaks */

    }
    if (ret == -1)
	ret = 1;

exit:
    if (!restore_rand())
	ret = -1;

    return ret;
}

#if defined(_RPMSSL_INTERNAL)
static int test_builtin(pgpDig dig)
{
    pgpDigParams sigp = pgpGetSignature(dig);
rpmssl ssl = dig->impl;
    size_t n = 0;
    unsigned char * digest = NULL;
    size_t digestlen = 0;
    unsigned char * digest_bad = NULL;
    size_t digestlen_bad = 0;
    unsigned char *ecdsasig = NULL;
    unsigned int ecsdasiglen;
    int ret = -1;	/* assume failure */
static const char dots[] = "...............";
int bingo = 0;
int xx;

pgpDigClean(dig);
sigp->hash_algo = PGPHASHALGO_SHA1;

    /* create a "good" and a "bad" digest. */
    {	const char * msg;
	DIGEST_CTX ctx;
	int xx;

	msg = "bad";
	ctx = rpmDigestInit(sigp->hash_algo, RPMDIGEST_NONE);
	xx = rpmDigestUpdate(ctx, msg, strlen(msg));
	xx = rpmDigestFinal(ctx, &digest_bad, &digestlen_bad, 0);
	ctx = NULL;

	msg = "good";
	ctx = rpmDigestInit(sigp->hash_algo, RPMDIGEST_NONE);
	xx = rpmDigestUpdate(ctx, msg, strlen(msg));
	xx = rpmDigestFinal(ctx, &digest, &digestlen, 0);
	ctx = NULL;
    }

    /* create and verify a ecdsa signature with every availble curve */
    rpmlog(RPMLOG_NOTICE, "========== sign/verify on internal ECDSA curves:\n");

    /* get a list of all internal curves */
    ssl->ncurves = EC_get_builtin_curves(NULL, 0);

    ssl->curves = OPENSSL_malloc(ssl->ncurves * sizeof(EC_builtin_curve));
assert(ssl->curves);

    xx = EC_get_builtin_curves(ssl->curves, ssl->ncurves);
assert(xx);

    /* now create and verify a signature for every curve */
    for (n = 0; n < ssl->ncurves; n++) {
	unsigned char offset;
	unsigned char dirt;

	ssl->nid = ssl->curves[n].nid;
	if (ssl->nid == NID_ipsec3 || ssl->nid == NID_ipsec4)
	    continue;

	/* create new ecdsa key (for EC_KEY_set_group) */
	ssl->ecdsakey = EC_KEY_new();
assert(ssl->ecdsakey);
	ssl->group = EC_GROUP_new_by_curve_name(ssl->nid);
assert(ssl->group);
	xx = EC_KEY_set_group(ssl->ecdsakey, ssl->group);
assert(xx);
	EC_GROUP_free(ssl->group);
	ssl->group = NULL;

	rpmlog(RPMLOG_INFO, "%s:\t", OBJ_nid2sn(ssl->nid));
bingo = 0;

	/* create key */
	if (!EC_KEY_generate_key(ssl->ecdsakey))
	    goto bottom;

	/* create second key */
	ssl->ecdsakey_bad = EC_KEY_new();
assert(ssl->ecdsakey_bad);
	ssl->group = EC_GROUP_new_by_curve_name(ssl->nid);
assert(ssl->group);
	xx = EC_KEY_set_group(ssl->ecdsakey_bad, ssl->group);
assert(xx);
	EC_GROUP_free(ssl->group);
	ssl->group = NULL;

	if (!EC_KEY_generate_key(ssl->ecdsakey_bad))
	    goto bottom;
bingo++;

	/* check key */
	if (!EC_KEY_check_key(ssl->ecdsakey))
	    goto bottom;
bingo++;

	/* create signature */
	ecsdasiglen = ECDSA_size(ssl->ecdsakey);
	ecdsasig = OPENSSL_malloc(ecsdasiglen);
assert(ecdsasig);
	if (!ECDSA_sign(0, digest, digestlen, ecdsasig, &ecsdasiglen, ssl->ecdsakey))
	    goto bottom;
bingo++;

	/* verify signature */
	if (ECDSA_verify(0, digest, digestlen, ecdsasig, ecsdasiglen, ssl->ecdsakey) != 1)
	    goto bottom;
bingo++;

	/* verify signature with the wrong key */
	if (ECDSA_verify(0, digest, digestlen, ecdsasig, ecsdasiglen,
			 ssl->ecdsakey_bad) == 1)
	    goto bottom;
bingo++;

	/* wrong digest */
	if (ECDSA_verify(0, digest_bad, sizeof(digest_bad), ecdsasig, ecsdasiglen,
			 ssl->ecdsakey) == 1)
	    goto bottom;
bingo++;

	/* modify a single byte of the signature */
	offset = ecdsasig[10] % ecsdasiglen;
	dirt = ecdsasig[11];
	ecdsasig[offset] ^= dirt ? dirt : 1;
	if (ECDSA_verify(0, digest, digestlen, ecdsasig, ecsdasiglen, ssl->ecdsakey) == 1)
	    goto bottom;
bingo++;

bottom:
	rpmlog(RPMLOG_INFO, "%s %s\n", &dots[strlen(dots) - bingo],
			(bingo >= 7 ? " ok" : " failed"));
	if (bingo < 7)
	    ret = 0;

	/* cleanup */
	if (ecdsasig)
	    OPENSSL_free(ecdsasig);
	ecdsasig = NULL;
	pgpImplClean(ssl);
    }
    if (ret == -1)
	ret = 1;

    if (ecdsasig)
	OPENSSL_free(ecdsasig);
    ecdsasig = NULL;

    if (ssl->curves)
            OPENSSL_free(ssl->curves);
    ssl->curves = NULL;
    ssl->ncurves = 0;

    pgpImplClean(ssl);

digest_bad = _free(digest_bad);
digestlen_bad = 0;
digest = _free(digest);
digestlen = 0;

    return ret;
}
#endif	/* _RPMSSL_INTERNAL */

/*==============================================================*/

static struct poptOption optionsTable[] = {
 { NULL, '\0', POPT_ARG_INCLUDE_TABLE, rpmioAllPoptTable, 0,
	N_("Common options:"), NULL },

  POPT_AUTOHELP
  POPT_TABLEEND
};

#if defined(_RPMBC_INTERNAL)
static pgpDig _rpmbcFini(pgpDig dig)
{
rpmbc bc = (dig ? dig->impl : NULL);
    if (bc) {
    }
dig = pgpDigFree(dig);
    return NULL;
}

static pgpDig _rpmbcInit(void)
{
pgpDig dig;
rpmbc bc;
    pgpImplVecs = &rpmbcImplVecs;

dig = pgpDigNew(0);
bc = dig->impl;

    return dig;
}
#endif	/* _RPMBC_INTERNAL */

#if defined(_RPMGC_INTERNAL)
static pgpDig _rpmgcFini(pgpDig dig)
{
rpmgc gc = (dig ? dig->impl : NULL);
    if (gc) {
    }
dig = pgpDigFree(dig);
    return NULL;
}

static pgpDig _rpmgcInit(void)
{
pgpDig dig;
rpmgc gc;

    pgpImplVecs = &rpmgcImplVecs;

dig = pgpDigNew(0);
gc = dig->impl;

gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
gcry_control (GCRYCTL_ENABLE_QUICK_RANDOM, 0);

    return dig;
}
#endif	/* _RPMGC_INTERNAL */

#if defined(_RPMNSS_INTERNAL)
static pgpDig _rpmnssFini(pgpDig dig)
{
rpmnss nss = (dig ? dig->impl : NULL);
    if (nss) {
    }
dig = pgpDigFree(dig);
    return NULL;
}

static pgpDig _rpmnssInit(void)
{
pgpDig dig;
rpmnss nss;

rpmnssImplVecs._pgpSetECDSA = rpmnssSetECDSA;
rpmnssImplVecs._pgpVerifyECDSA = rpmnssVerifyECDSA;
rpmnssImplVecs._pgpSignECDSA = rpmnssSignECDSA;
rpmnssImplVecs._pgpGenerateECDSA = rpmnssGenerateECDSA;

    pgpImplVecs = &rpmnssImplVecs;

dig = pgpDigNew(0);
nss = dig->impl;

    return dig;
}
#endif	/* _RPMNSS_INTERNAL */

#if defined(_RPMSSL_INTERNAL)
static pgpDig _rpmsslFini(pgpDig dig)
{
rpmssl ssl = (dig ? dig->impl : NULL);
    if (ssl) {
	CRYPTO_cleanup_all_ex_data();
	ERR_remove_thread_state(NULL);
	ERR_free_strings();

	if (ssl->out) {
	    CRYPTO_mem_leaks(ssl->out);
	    BIO_free(ssl->out);
	}
	ssl->out = NULL;
    }
dig = pgpDigFree(dig);
    return NULL;
}

static pgpDig _rpmsslInit(void)
{
pgpDig dig;
rpmssl ssl;
    pgpImplVecs = &rpmsslImplVecs;

    /* enable memory leak checking unless explicitly disabled */
    if (!((getenv("OPENSSL_DEBUG_MEMORY") != NULL) &&
	  (0 == strcmp(getenv("OPENSSL_DEBUG_MEMORY"), "off")))) {
	    CRYPTO_malloc_debug_init();
	CRYPTO_set_mem_debug_options(V_CRYPTO_MDEBUG_ALL);
    } else {
	/* OPENSSL_DEBUG_MEMORY=off */
	CRYPTO_set_mem_debug_functions(0, 0, 0, 0, 0);
    }
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

    /* initialize the prng */
    {   static const char rnd_seed[] =
	      "string to make the random number generator think it has entropy";
	RAND_seed(rnd_seed, sizeof(rnd_seed));
    }

dig = pgpDigNew(0);
ssl = dig->impl;
ssl->out = BIO_new_fp(stdout, BIO_NOCLOSE);

    return dig;
}
#endif	/* _RPMSSL_INTERNAL */

int main(int argc, char *argv[])
{
    poptContext con = rpmioInit(argc, argv, optionsTable);
pgpDig dig;
    int ec = 1;	/* assume failure */
int rc;

#if defined(_RPMSSL_INTERNAL) && !defined(OPENSSL_NO_ECDSA)
dig = _rpmsslInit();
    if (pgpDigTests(dig) <= 0)
	goto exit;
    if (test_builtin(dig) <= 0)
	goto exit;
dig = _rpmsslFini(dig);
#endif

#if defined(_RPMGC_INTERNAL)
dig = _rpmgcInit();
    rc = 1;	/* assume success */
    if (pgpDigTests(dig) <= 0)
	rc = 0;
dig = _rpmgcFini(dig);
    if (rc <= 0)
	goto exit;
#endif	/* _RPMGC_INTERNAL */

#if defined(_RPMNSS_INTERNAL)
dig = _rpmnssInit();
    rc = 1;	/* assume success */
    if (pgpDigTests(dig) <= 0)
	rc = 0;
dig = _rpmnssFini(dig);
    if (rc <= 0)
	goto exit;
#endif	/* _RPMNSS_INTERNAL */

#if defined(_RPMBC_INTERNAL)
dig = _rpmbcInit();
dig = _rpmbcFini(dig);
#endif	/* _RPMBC_INTERNAL */

    ec = 0;

exit:
    rpmlog(RPMLOG_INFO, "ECDSA tests %s\n", (ec ? "failed" : "passed"));

    con = rpmioFini(con);

    return ec;
}

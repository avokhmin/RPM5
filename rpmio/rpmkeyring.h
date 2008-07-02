#ifndef _RPMKEYRING_H
#define _RPMKEYRING_H

/** \ingroup rpmkeyring
 * \file rpmio/rpmkeyring.h
 */

/** \ingroup rpmkeyring
 */
typedef struct rpmPubkey_s * rpmPubkey;

/** \ingroup rpmkeyring
 */
typedef struct rpmKeyring_s * rpmKeyring;

/** \ingroup rpmkeyring
 * Create a new, empty keyring
 * @return	new keyring handle
 */
rpmKeyring rpmKeyringNew(void);

/** \ingroup rpmkeyring
 * Free keyring and the keys within it
 * @return	NULL always
 */
rpmKeyring rpmKeyringFree(rpmKeyring keyring);

/** \ingroup rpmkeyring
 * Add a public key to keyring.
 * @param keyring	keyring handle
 * @param key		pubkey handle
 * @return		0 on success, -1 on error, 1 if key already present
 */
int rpmKeyringAddKey(rpmKeyring keyring, rpmPubkey key);

/** \ingroup rpmkeyring
 * Perform keyring lookup for a key matching a signature
 * @param keyring	keyring handle
 * @param sig		OpenPGP packet container of signature
 * @return		RPMRC_OK if found, RPMRC_NOKEY otherwise
 */
rpmRC rpmKeyringLookup(rpmKeyring keyring, pgpDig sig);

/** \ingroup rpmkeyring
 * Create a new rpmPubkey from OpenPGP packet
 * @param pkt		OpenPGP packet data
 * @param pktlen	Data length
 * @return		new pubkey handle
 */
rpmPubkey rpmPubkeyNew(const uint8_t *pkt, size_t pktlen);

/** \ingroup rpmkeyring
 * Create a new rpmPubkey from ASCII-armored pubkey file
 * @param filename	Path to pubkey file
 * @return		new pubkey handle
 */
rpmPubkey rpmPubkeyRead(const char *filename);

/** \ingroup rpmkeyring
 * Free a pubkey.
 * @param key		Pubkey to free
 * @return		NULL always
 */
rpmPubkey rpmPubkeyFree(rpmPubkey key);

#endif /* _RPMKEYDB_H */

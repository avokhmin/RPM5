#ifndef H_DBINDEX
#define H_DBINDEX

/** \file lib/dbindex.h
 * Access RPM indices using Berkeley db[123] interface.
 */

typedef void DBI_t;
typedef enum { DBI_BTREE, DBI_HASH, DBI_RECNO, DBI_QUEUE, DBI_UNKNOWN } DBI_TYPE;

typedef /*@abstract@*/ struct _dbiIndexRecord * dbiIndexRecord;
typedef /*@abstract@*/ struct _dbiIndex * dbiIndex;

/* this will break if sizeof(int) != 4 */
/**
 * A single item from an index database (i.e. the "data returned").
 * Note: In rpm-3.0.4 and earlier, this structure was passed by value,
 * and was identical to the "data saved" structure below.
 */
struct _dbiIndexRecord {
    unsigned int recOffset;		/*!< byte offset of header in db */
    unsigned int fileNumber;		/*!< file array index */
    int fpNum;				/*!< finger print index */
    int dbNum;				/*!< database index */
};

/**
 * A single item in an index database (i.e. the "data saved").
 */
struct _dbiIR {
    unsigned int recOffset;		/*!< byte offset of header in db */
    unsigned int fileNumber;		/*!< file array index */
};
typedef	struct _dbiIR * DBIR_t;

/**
 * Items retrieved from the index database.
 */
struct _dbiIndexSet {
/*@owned@*/ struct _dbiIndexRecord * recs; /*!< array of records */
    int count;				/*!< number of records */
};

/**
 * Private methods for accessing an index database.
 */
struct _dbiVec {
    int dbv_major;			/*<! Berkeley db version major */
    int dbv_minor;			/*<! Berkeley db version minor */
    int dbv_patch;			/*<! Berkeley db version patch */

/**
 * Return handle for an index database.
 * @param dbi	index database handle
 * @return	0 success 1 fail
 */
    int (*open) (dbiIndex dbi);

/**
 * Close index database.
 * @param dbi	index database handle
 * @param flags
 */
    int (*close) (dbiIndex dbi, unsigned int flags);

/**
 * Flush pending operations to disk.
 * @param dbi	index database handle
 * @param flags
 */
    int (*sync) (dbiIndex dbi, unsigned int flags);

/**
 * Return items that match criteria.
 * @param dbi	index database handle
 * @param str	search key
 * @param set	items retrieved from index database
 * @return	-1 error, 0 success, 1 not found
 */
    int (*SearchIndex) (dbiIndex dbi, const void * str, size_t len, dbiIndexSet * set);

/**
 * Change/delete items that match criteria.
 * @param dbi	index database handle
 * @param str	update key
 * @param set	items to update in index database
 * @return	0 success, 1 not found
 */
    int (*UpdateIndex) (dbiIndex dbi, const char * str, dbiIndexSet set);

/**
 * Delete item using db->del.
 * @param dbi	index database handle
 * @param keyp	key data
 * @param keylen key data length
 */
    int (*del) (dbiIndex dbi, void * keyp, size_t keylen);

/**
 * Retrieve item using db->get.
 * @param dbi	index database handle
 * @param keyp	key data
 * @param keylen key data length
 * @param datap	address of data pointer
 * @param datalen address of data length
 */
    int (*get) (dbiIndex dbi, void * keyp, size_t keylen,
			void ** datap, size_t * datalen);

/**
 * Save item using db->put.
 * @param dbi	index database handle
 * @param keyp	key data
 * @param keylen key data length
 * @param datap	data pointer
 * @param datalen data length
 */
    int (*put) (dbiIndex dbi, void * keyp, size_t keylen,
			void * datap, size_t datalen);

/**
 */
    int (*copen) (dbiIndex dbi);

/**
 */
    int (*cclose) (dbiIndex dbi);

/**
 */
    int (*join) (dbiIndex dbi);

/**
 * Retrieve item using dbcursor->c_get.
 * @param dbi	index database handle
 * @param keyp	address of key data
 * @param keylen address of key data length
 * @param datap	address of data pointer
 * @param datalen address of data length
 */
    int (*cget) (dbiIndex dbi, void ** keyp, size_t * keylen,
			void ** datap, size_t * datalen);

};

/**
 * Describes an index database (implemented on Berkeley db[123] API).
 */
struct _dbiIndex {
    const char * dbi_basename;		/*<! last component of name */
    int dbi_rpmtag;			/*<! rpm tag used for index */

    DBI_TYPE dbi_type;			/*<! type of access */
    int dbi_flags;			/*<! flags to use on open */
    int dbi_perms;			/*<! file permission to use on open */
    int dbi_major;			/*<! Berkeley db version major */
    unsigned int dbi_lastoffset;	/*<! db0 with falloc.c needs this */

    const char * dbi_file;		/*<! name of index database */
    void * dbi_db;			/*<! Berkeley db[123] handle */
    void * dbi_dbenv;
    void * dbi_dbinfo;
    void * dbi_dbjoin;
    void * dbi_dbcursor;
    void * dbi_pkgs;
/*@observer@*/ const struct _dbiVec * dbi_vec;	/*<! private methods */
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return handle for an index database.
 * @param filename	file name of database
 * @param flags		type of open
 * @param dbiTemplate	template to initialize new dbiIndex
 * @return		index database handle
 */
/*@only@*/ dbiIndex dbiOpenIndex(const char * filename, int flags,
		const dbiIndex dbiTemplate);

/**
 * Close index database.
 * @param dbi	index database handle
 */
int dbiCloseIndex( /*@only@*/ dbiIndex dbi);

#ifdef	DYING
/**
 * Flush pending operations to disk.
 * @param dbi	index database handle
 */
int dbiSyncIndex(dbiIndex dbi);

/**
 * Return items that match criteria.
 * @param dbi	index database handle
 * @param str	search key
 * @param set	items retrieved from index database
 * @return	-1 error, 0 success, 1 not found
 */
int dbiSearchIndex(dbiIndex dbi, const char * str, size_t len, /*@out@*/ dbiIndexSet * set);

/**
 * Change/delete items that match criteria.
 * @param dbi	index database handle
 * @param str	update key
 * @param set	items to update in index database
 * @return	0 success, 1 not found
 */
int dbiUpdateIndex(dbiIndex dbi, const char * str, dbiIndexSet set);

/**
 * Append element to set of index database items.
 * @param set	set of index database items
 * @param rec	item to append to set
 * @return	0 success (always)
 */
int dbiAppendIndexRecord( /*@out@*/ dbiIndexSet set, dbiIndexRecord rec);
#endif

/**
 * Create empty set of index database items.
 * @return	empty set of index database items
 */
/*@only@*/ dbiIndexSet dbiCreateIndexSet(void);

#ifdef	DYING
/**
 * Remove element from set of index database items.
 * @param set	set of index database items
 * @param rec	item to remove from set
 * @return	0 success, 1 failure
 */
int dbiRemoveIndexRecord(dbiIndexSet set, dbiIndexRecord rec);

/**
 * Return next index database key.
 * @param dbi	index database handle
 * @param keyp	address of first key
 * @param keylenp address of first key size
 * @return	0 success - fails if rec is not found
 */
int dbiGetNextKey(dbiIndex dbi, /*@out@*/ void ** keyp, size_t * keylenp);

/**
 * Close cursor (if any);
 * @param dbi	index database handle
 * @return	0 success
 */
int dbiFreeCursor(dbiIndex dbi);

/**
 * Create and initialize element of index database set.
 * @param recOffset	byte offset of header in db
 * @param fileNumber	file array index
 * @return	new element
 */
/*@only@*/ dbiIndexRecord dbiReturnIndexRecordInstance(unsigned int recOffset,
		unsigned int fileNumber);
/**
 * Destroy element of index database set.
 * @param rec	element of index database set.
 */
void dbiFreeIndexRecordInstance( /*@only@*/ dbiIndexRecord rec);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* H_DBINDEX */

#ifndef H_DBINDEX
#define H_DBINDEX

/** \file lib/dbindex.h
 * Access RPM indices using Berkeley db[123] interface.
 */

typedef void DBI_t;
typedef enum { DBI_BTREE, DBI_HASH, DBI_RECNO } DBI_TYPE;

typedef /*@abstract@*/ struct _dbiIndexRecord * dbiIndexRecord;
typedef /*@abstract@*/ struct _dbiIndex * dbiIndex;

/* this will break if sizeof(int) != 4 */
/**
 * A single item in an index database.
 * Note: In rpm-3.0.4 and earlier, this structure was passed by value.
 */
struct _dbiIndexRecord {
    unsigned int recOffset;		/*!< byte offset of header in db */
    unsigned int fileNumber;		/*!< file array index */
};

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
 * Return first index database key.
 * @param dbi	index database handle
 * @param key	address of first key
 * @return	0 success - fails if rec is not found
 */
    int (*GetFirstKey) (dbiIndex dbi, const char ** keyp);

/**
 * Return items that match criteria.
 * @param dbi	index database handle
 * @param str	search key
 * @param set	items retrieved from index database
 * @return	-1 error, 0 success, 1 not found
 */
    int (*SearchIndex) (dbiIndex dbi, const char * str, dbiIndexSet * set);

/**
 * Change/delete items that match criteria.
 * @param dbi	index database handle
 * @param str	update key
 * @param set	items to update in index database
 * @return	0 success, 1 not found
 */
    int (*UpdateIndex) (dbiIndex dbi, const char * str, dbiIndexSet set);
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

    const char * dbi_file;		/*<! name of index database */
    void * dbi_db;			/*<! Berkeley db[123] handle */
    void * dbi_dbenv;
    void * dbi_dbinfo;
    void * dbi_dbcursor;
    const void * dbi_openinfo;		/*<! private data passed on open */
    FD_t dbi_fd;			/*<! private data for fadio access */
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
int dbiSearchIndex(dbiIndex dbi, const char * str, /*@out@*/ dbiIndexSet * set);

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

/**
 * Create empty set of index database items.
 * @return	empty set of index database items
 */
/*@only@*/ dbiIndexSet dbiCreateIndexSet(void);

/**
 * Remove element from set of index database items.
 * @param set	set of index database items
 * @param rec	item to remove from set
 * @return	0 success, 1 failure
 */
int dbiRemoveIndexRecord(dbiIndexSet set, dbiIndexRecord rec);

/**
 * Return first index database key.
 * @param dbi	index database handle
 * @param key	address of first key
 * @return	0 success - fails if rec is not found
 */
int dbiGetFirstKey(dbiIndex dbi, /*@out@*/ const char ** key);

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

#ifdef __cplusplus
}
#endif

#endif	/* H_DBINDEX */

#ifndef H_RPMHASH
#define H_RPMHASH

/**
 * \file rpmio/rpmhash.h
 * Hash table implemenation.
 */

/**
 */
typedef /*@abstract@*/ struct hashTable_s * hashTable;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compare two hash table entries for equality.
 * @param key1          entry 1
 * @param key2          entry 2
 * @return		0 if entries are equal
 */
typedef int (*hashEqualityType) (const void * key1, const void * key2)
	/*@*/;

/**
 * Compare two hash table entries for equality.
 * @param key1          entry 1
 * @param key2          entry 2
 * @return		0 if entries are equal
 */
int hashEqualityString(const void * key1, const void * key2)
	/*@*/;

/**
 * Return hash value.
 * @param h		hash initial value
 * @param data		data on which to calculate hash value
 * @param size		size of data in bytes
 * @return		hash value
 */
typedef uint32_t (*hashFunctionType) (uint32_t h, const void * data, size_t size)
	/*@*/;

/**
 * Return hash value of a string.
 * @param h		hash initial value
 * @param data		data on which to calculate hash value
 * @param size		size of data in bytes (0 will use strlen(data))
 * @return		hash value
 */
uint32_t hashFunctionString(uint32_t h, const void * data, size_t size)
	/*@*/;

/**
 * Create hash table.
 * If keySize > 0, the key is duplicated within the table (which costs
 * memory, but may be useful anyway.
 * @param numBuckets    number of hash buckets
 * @param keySize       size of key (0 if unknown)
 * @param freeData      Should data be freed when table is destroyed?
 * @param fn            function to generate hash key (NULL for default)
 * @param eq            function to compare keys for equality (NULL for default)
 * @return		pointer to initialized hash table
 */
hashTable htCreate(int numBuckets, size_t keySize, int freeData,
		/*@null@*/ hashFunctionType fn, /*@null@*/ hashEqualityType eq)
	/*@*/; 

/**
 * Destroy hash table.
 * @param ht            pointer to hash table
 * @return		NULL always
 */
/*@null@*/
hashTable htFree( /*@only@*/ hashTable ht)
	/*@modifies ht @*/;

/**
 * Add item to hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key
 * @param data          pointer to data value
 */
void htAddEntry(hashTable ht, /*@owned@*/ const void * key,
		/*@owned@*/ const void * data)
	/*@modifies ht */;

/**
 * Retrieve item from hash table.
 * @param ht		pointer to hash table
 * @param key		pointer to key value
 * @retval *data	data value from bucket
 * @retval *dataCount	data value size from bucket
 * @retval *tableKey	key value from bucket (may be NULL)
 * @return		0 on success, 1 if the item is not found.
 */
int htGetEntry(hashTable ht, const void * key,
		/*@null@*/ /*@out@*/ const void * data,
		/*@null@*/ /*@out@*/ int * dataCount,
		/*@null@*/ /*@out@*/ const void * tableKey)
	/*@modifies *data, *dataCount, *tableKey @*/;

/**
 * Check for key in hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key value
 * @return		1 if the key is present, 0 otherwise
 */
/*@unused@*/
int htHasEntry(hashTable ht, const void * key)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif
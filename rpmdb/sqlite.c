/*
 * sqlite.c
 * sqlite interface for rpmdb
 *
 * Author: Mark Hatle <mhatle@mvista.com> or <fray@kernel.crashing.org>
 * Copyright (c) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * or GNU Library General Public License, at your option,
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and GNU Library Public License along with this program;
 * if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include "system.h"

#include <endian.h>

#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>     /* XXX urlPath proto */

#include <rpmdb.h>

#include "sqlite3.h"

#include "debug.h"

#define	USE_SQLITE3
#define	SQLITE			sqlite3

#define	SQLITE_BUSY_HANDLER	sqlite3_busy_handler
#define	SQLITE_CLOSE		sqlite3_close
#define	SQLITE_EXEC		sqlite3_exec
#define	SQLITE_FREE		sqlite3_free
#define	SQLITE_FREE_TABLE	sqlite3_free_table
#define	SQLITE_GET_TABLE	sqlite3_get_table

#if 0
  /* Turn off some of the COMMIT transactions */
  #define SQL_FAST_DB
#endif

#if 0
  /* define this to trace the functions that are called in here... */
  #define SQL_TRACE_FUNCTIONS
  #define SQL_TRACE_ENCODINGS
  #define SQL_TRACE_TRANSACTIONS
  #define SQL_TRACE_CURSOR
#endif


/* Define the things normally in a header... */
struct __sql_mem;	typedef struct __sql_mem      SQL_MEM;

struct __sql_db;	typedef struct __sql_db	      SQL_DB;
struct __sql_dbcursor;	typedef struct __sql_dbcursor SQL_CURSOR; 

struct __sql_db {
  SQLITE * db;     /* Database pointer */

  int transaction; /* Do we have a transaction open? */

  SQL_CURSOR * head_cursor; /* List of open cursors */

  int count;
};

struct __sql_dbcursor {
  DBC * name;  /* Which DBC am I emulating? */

  /* Table -- result of query */
  char **resultp;
  int nrow;
  int ncolumn;

  int row_iterator;  /* Which row are we on? 1, 2, 3 ... */

  int all; /* Cursor is for all items, not a specific key */

  SQL_MEM * memory;

  int count;

  struct __sql_dbcursor * next_cursor;
};

struct __sql_mem {
  void * mem_ptr;
  SQL_MEM * next;
};

/* local prototypes */
int sql_initDB(dbiIndex dbi);

int sql_startTransaction(dbiIndex dbi);
int sql_endTransaction(dbiIndex dbi);
int sql_commitTransaction(dbiIndex dbi, int flag);

void * allocTempBuffer(DBC * dbcursor, size_t len);

/*===================================================================*/
/*
** How This Encoder Works
**
** The output is allowed to contain any character except 0x27 (') and
** 0x00.  This is accomplished by using an escape character to encode
** 0x27 and 0x00 as a two-byte sequence.  The escape character is always
** 0x01.  An 0x00 is encoded as the two byte sequence 0x01 0x01.  The
** 0x27 character is encoded as the two byte sequence 0x01 0x03.  Finally,
** the escape character itself is encoded as the two-character sequence
** 0x01 0x02.
**
** To summarize, the encoder works by using an escape sequences as follows:
**
**       0x00  ->  0x01 0x01
**       0x01  ->  0x01 0x02
**       0x27  ->  0x01 0x03
**
** If that were all the encoder did, it would work, but in certain cases
** it could double the size of the encoded string.  For example, to
** encode a string of 100 0x27 characters would require 100 instances of
** the 0x01 0x03 escape sequence resulting in a 200-character output.
** We would prefer to keep the size of the encoded string smaller than
** this.
**
** To minimize the encoding size, we first add a fixed offset value to each 
** byte in the sequence.  The addition is modulo 256.  (That is to say, if
** the sum of the original character value and the offset exceeds 256, then
** the higher order bits are truncated.)  The offset is chosen to minimize
** the number of characters in the string that need to be escaped.  For
** example, in the case above where the string was composed of 100 0x27
** characters, the offset might be 0x01.  Each of the 0x27 characters would
** then be converted into an 0x28 character which would not need to be
** escaped at all and so the 100 character input string would be converted
** into just 100 characters of output.  Actually 101 characters of output - 
** we have to record the offset used as the first byte in the sequence so
** that the string can be decoded.  Since the offset value is stored as
** part of the output string and the output string is not allowed to contain
** characters 0x00 or 0x27, the offset cannot be 0x00 or 0x27.
**
** Here, then, are the encoding steps:
**
**     (1)   Choose an offset value and make it the first character of
**           output.
**
**     (2)   Copy each input character into the output buffer, one by
**           one, adding the offset value as you copy.
**
**     (3)   If the value of an input character plus offset is 0x00, replace
**           that one character by the two-character sequence 0x01 0x01.
**           If the sum is 0x01, replace it with 0x01 0x02.  If the sum
**           is 0x27, replace it with 0x01 0x03.
**
**     (4)   Put a 0x00 terminator at the end of the output.
**
** Decoding is obvious:
**
**     (5)   Copy encoded characters except the first into the decode 
**           buffer.  Set the first encoded character aside for use as
**           the offset in step 7 below.
**
**     (6)   Convert each 0x01 0x01 sequence into a single character 0x00.
**           Convert 0x01 0x02 into 0x01.  Convert 0x01 0x03 into 0x27.
**
**     (7)   Subtract the offset value that was the first character of
**           the encoded buffer from all characters in the output buffer.
**
** The only tricky part is step (1) - how to compute an offset value to
** minimize the size of the output buffer.  This is accomplished by testing
** all offset values and picking the one that results in the fewest number
** of escapes.  To do that, we first scan the entire input and count the
** number of occurances of each character value in the input.  Suppose
** the number of 0x00 characters is N(0), the number of occurances of 0x01
** is N(1), and so forth up to the number of occurances of 0xff is N(255).
** An offset of 0 is not allowed so we don't have to test it.  The number
** of escapes required for an offset of 1 is N(1)+N(2)+N(40).  The number
** of escapes required for an offset of 2 is N(2)+N(3)+N(41).  And so forth.
** In this way we find the offset that gives the minimum number of escapes,
** and thus minimizes the length of the output string.
*/

/*
** Encode a binary buffer "in" of size n bytes so that it contains
** no instances of characters '\'' or '\000'.  The output is 
** null-terminated and can be used as a string value in an INSERT
** or UPDATE statement.  Use sqlite_decode_binary() to convert the
** string back into its original binary.
**
** The result is written into a preallocated output buffer "out".
** "out" must be able to hold at least 2 +(257*n)/254 bytes.
** In other words, the output will be expanded by as much as 3
** bytes for every 254 bytes of input plus 2 bytes of fixed overhead.
** (This is approximately 2 + 1.0118*n or about a 1.2% size increase.)
**
** The return value is the number of characters in the encoded
** string, excluding the "\000" terminator.
*/
static size_t sqlite_encode_binary(const unsigned char *in, size_t n,
		unsigned char *out)
{
  long i, j, e, m;
  int cnt[256];
  if( n<=0 ){
    out[0] = 'x';
    out[1] = 0;
    return 1;
  }
  memset(cnt, 0, sizeof(cnt));
  for(i=n-1; i>=0; i--){ cnt[in[i]]++; }
  m = n;
  for(i=1; i<256; i++){
    int sum;
    if( i=='\'' ) continue;
    sum = cnt[i] + cnt[(i+1)&0xff] + cnt[(i+'\'')&0xff];
    if( sum<m ){
      m = sum;
      e = i;
      if( m==0 ) break;
    }
  }
  out[0] = e;
  j = 1;
  for(i=0; i<n; i++){
    int c = (in[i] - e)&0xff;
    if( c==0 ){
      out[j++] = 1;
      out[j++] = 1;
    }else if( c==1 ){
      out[j++] = 1;
      out[j++] = 2;
    }else if( c=='\'' ){
      out[j++] = 1;
      out[j++] = 3;
    }else{
      out[j++] = c;
    }
  }
  out[j] = 0;
  return j;
}

/*
** Decode the string "in" into binary data and write it into "out".
** This routine reverses the encoding created by sqlite_encode_binary().
** The output will always be a few bytes less than the input.  The number
** of bytes of output is returned.  If the input is not a well-formed
** encoding, -1 is returned.
**
** The "in" and "out" parameters may point to the same buffer in order
** to decode a string in place.
*/
static size_t sqlite_decode_binary(const unsigned char *in, unsigned char *out)
{
  long i;
  int c, e;
  e = *(in++);
  i = 0;
  while( (c = *(in++))!=0 ){
    if( c==1 ){
      c = *(in++);
      if( c==1 ){
        c = 0;
      }else if( c==2 ){
        c = 1;
      }else if( c==3 ){
        c = '\'';
      }else{
        return -1;
      }
    }
    out[i++] = (c + e)&0xff;
  }
  return i;
}
/*===================================================================*/

/* sqlite prototypes */
int sql_open(rpmdb rpmdb, rpmTag rpmtag, /*@out@*/ dbiIndex * dbip);

int sql_close(/*@only@*/ dbiIndex dbi, unsigned int flags);

int sql_sync (dbiIndex dbi, unsigned int flags);

int sql_associate (dbiIndex dbi, dbiIndex dbisecondary,
		int (*callback) (DB *, const DBT *, const DBT *, DBT *),
		unsigned int flags);
     
int sql_join (dbiIndex dbi, DBC ** curslist, /*@out@*/ DBC ** dbcp,
		unsigned int flags);
   
int sql_copen (dbiIndex dbi, /*@null@*/ DB_TXN * txnid,
		/*@out@*/ DBC ** dbcp, unsigned int dbiflags);   

int sql_cclose (dbiIndex dbi, /*@only@*/ DBC * dbcursor, unsigned int flags);

int sql_cdup (dbiIndex dbi, DBC * dbcursor, /*@out@*/ DBC ** dbcp,
		unsigned int flags);

int sql_cdel (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
		unsigned int flags);

int sql_cget (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
		unsigned int flags);
     
int sql_cpget (dbiIndex dbi, /*@null@*/ DBC * dbcursor,
		DBT * key, DBT * pkey, DBT * data, unsigned int flags);
   
int sql_cput (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
		unsigned int flags);

int sql_ccount (dbiIndex dbi, DBC * dbcursor,
		/*@out@*/ unsigned int * countp,
		unsigned int flags);

int sql_byteswapped (dbiIndex dbi);

int sql_stat (dbiIndex dbi, unsigned int flags);


/*
 * Transaction support
 */

int sql_startTransaction(dbiIndex dbi)
{
    DB * db = dbi->dbi_db;
    assert(db != NULL);

    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;

    /* XXX:  Transaction Support */
    if (!sqldb->transaction) {
      char * pzErrmsg;
      rc = SQLITE_EXEC(sqldb->db, "BEGIN TRANSACTION;", NULL, NULL, &pzErrmsg);

#ifdef SQL_TRACE_TRANSACTIONS
      rpmMessage(RPMMESS_DEBUG, "Begin %s SQL transaction %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);
#endif

      if ( rc == 0 )
	sqldb->transaction=1;
    }

    return rc;
}

int sql_endTransaction(dbiIndex dbi)
{
    DB * db = dbi->dbi_db;
    assert(db != NULL);

    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;

    /* XXX:  Transaction Support */
    if (sqldb->transaction) {
      char * pzErrmsg;
      rc = SQLITE_EXEC(sqldb->db, "END TRANSACTION;", NULL, NULL, &pzErrmsg);

#ifdef SQL_TRACE_TRANSACTIONS
      rpmMessage(RPMMESS_DEBUG, "End %s SQL transaction %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);
#endif

      if ( rc == 0 )
	sqldb->transaction=0;
    }

    return rc;
}

int sql_commitTransaction(dbiIndex dbi, int flag)
{
    DB * db = dbi->dbi_db;
    assert(db != NULL);

    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;

    /* XXX:  Transactions */
    if ( sqldb->transaction ) {
      char * pzErrmsg;
      rc = SQLITE_EXEC(sqldb->db, "COMMIT;", NULL, NULL, &pzErrmsg);

#ifdef SQL_TRACE_TRANSACTIONS
      rpmMessage(RPMMESS_DEBUG, "Commit %s SQL transaction(s) %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);
#endif

      sqldb->transaction=0;

      /* Start a new transaction if we were in the middle of one */
      if ( flag == 0 )
	rc = sql_startTransaction(dbi);
    }

    return rc;
}

/*
 * Allocate a temporary buffer
 *
 * Life span of memory in db3 is apparently a db_env,
 * this works for db3 because items are mmaped from
 * the database.. however we can't do that...
 *
 * Life span has been changed to the life of a cursor.
 *
 * Minor changes were required to RPM for this to work
 * valgrind was used to verify...
 *
 */
void * allocTempBuffer(DBC * dbcursor, size_t len)
{
    DB * db = dbcursor->dbp;
    assert(db != NULL);

    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL);

    SQL_CURSOR * sqlcursor = sqldb->head_cursor;

    /* Find our version of the db3 cursor */
    while ( sqlcursor != NULL && sqlcursor->name != dbcursor ) {
      sqlcursor = sqlcursor->next_cursor;
    }

    assert(sqlcursor != NULL);

    SQL_MEM * item = xmalloc(sizeof(SQL_MEM));
    item->mem_ptr = xmalloc(len);

#if 0
    /* Only keep two pointers per cursor */
    if ( sqlcursor->memory ) {
      if ( sqlcursor->memory->next ) {
	sqlcursor->memory->next->mem_ptr = _free(sqlcursor->memory->next->mem_ptr);

	sqlcursor->memory->next = _free(sqlcursor->memory->next);

	sqlcursor->count--;
      }
    }
#endif

    item->next = sqlcursor->memory;

    sqlcursor->memory = item;
    sqlcursor->count++;

    return item->mem_ptr;
}

static int sql_busy_handler(void * dbi_void, int time)
{
  dbiIndex dbi = (dbiIndex) dbi_void;

  rpmMessage(RPMMESS_WARNING, _("Unable to get lock on db %s, retrying... (%d)\n"),
		dbi->dbi_file, time);

  sleep(1);

  return 1;
}

/**
 * Verify the DB is setup.. if not initialize it
 *
 * Create the table.. create the db_info
 */
int sql_initDB(dbiIndex dbi)
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_initDB()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL);

    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;

    char ** resultp;
    int nrow;
    int ncolumn;
    char * pzErrmsg;
    char cmd[BUFSIZ];

    /* Check if the table exists... */
    sprintf(cmd,
	"SELECT name FROM 'sqlite_master' WHERE type='table' and name='%s';",
		dbi->dbi_subfile);
    rc = SQLITE_GET_TABLE(sqldb->db, cmd,
	&resultp, &nrow, &ncolumn, &pzErrmsg);

    (void) SQLITE_FREE_TABLE(resultp);

    if ( rc == 0 && nrow < 1 ) {
      sprintf(cmd, "CREATE TABLE '%s' (key blob UNIQUE, value blob);",
		dbi->dbi_subfile);
      rc = SQLITE_EXEC(sqldb->db, cmd, NULL, NULL, &pzErrmsg);

      if ( rc == 0 ) {
        sprintf(cmd, "CREATE TABLE 'db_info' (endian TEXT);");
	rc = SQLITE_EXEC(sqldb->db, cmd, NULL, NULL, &pzErrmsg);
      }

      if ( rc == 0 ) {
	sprintf(cmd, "INSERT INTO 'db_info' values('%d');", __BYTE_ORDER);
	rc = SQLITE_EXEC(sqldb->db, cmd, NULL, NULL, &pzErrmsg);
      }
    }

    if ( rc )
      rpmMessage(RPMMESS_WARNING, "Unable to initDB %s (%d)\n",
		pzErrmsg, rc);

    return rc;
}

/**
 * Return handle for an index database.
 * @param rpmdb         rpm database
 * @param rpmtag        rpm tag
 * @return              0 on success
 */
int sql_open(rpmdb rpmdb, rpmTag rpmtag, /*@out@*/ dbiIndex * dbip)
	/*@globals fileSystem @*/
	/*@modifies *dbip, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_open()\n");
#endif

    extern struct _dbiVec sqlitevec;
   
    const char * urlfn = NULL;
    const char * root;
    const char * home;
    const char * dbhome;
    const char * dbfile;  
    const char * dbfname;
    const char * sql_errcode;
    int rc = 0;
    int xx;

    size_t len;
    
    dbiIndex dbi = NULL;
    DB * db = NULL;

    SQL_DB * sqldb;

    dbi = xcalloc(1, sizeof(*dbi));
    
    dbi->dbi_rpmdb = rpmdb;
    dbi->dbi_rpmtag = rpmtag;
    dbi->dbi_api = 4; /* I have assigned 4 to sqlite */

    /* 
     * Inverted lists have join length of 2, primary data has join length of 1.
     * MGH: No idea what this is for.. but w/o it things don't work...
     */
    /*@-sizeoftype@*/
    switch (dbi->dbi_rpmtag) {
    case RPMDBI_PACKAGES:
    case RPMDBI_DEPENDS:   
	dbi->dbi_jlen = 1 * sizeof(int_32);
	break;
    default:
	dbi->dbi_jlen = 2 * sizeof(int_32);
	break;
    }  
    /*@=sizeoftype@*/

    dbi->dbi_byteswapped = -1;
  
    if (dbip)
	*dbip = NULL;
   /*
     * Get the prefix/root component and directory path
     */
    root = rpmdb->db_root;
    if ((root[0] == '/' && root[1] == '\0') || rpmdb->db_chrootDone)
	root = NULL;
    home = rpmdb->db_home;
    
    dbi->dbi_root=root;
    dbi->dbi_home=home;
      
    dbfile = tagName(dbi->dbi_rpmtag);

    /*
     * Make a copy of the tagName result..
     * use this for the filename and table name
     */
    {
      len=strlen(dbfile);
      char * t = xcalloc(len + 1, sizeof(char));
      (void) stpcpy( t, dbfile );
      dbi->dbi_file = t;
      dbi->dbi_subfile= t;
    }

    dbi->dbi_mode=O_RDWR;
       
    /*
     * Either the root or directory components may be a URL. Concatenate,
     * convert the URL to a path, and add the name of the file.
     */
    /*@-mods@*/
    urlfn = rpmGenPath(root, home, NULL);
    /*@=mods@*/
    (void) urlPath(urlfn, &dbhome);

    /* 
     * Create the /var/lib/rpm directory if it doesn't exist (root only).
     */
    (void) rpmioMkpath(dbhome, 0755, getuid(), getgid());
       
    dbfname = rpmGenPath(dbhome, dbi->dbi_file, NULL);

    rpmMessage(RPMMESS_DEBUG, _("opening  sql db         %s (%s) mode=0x%x\n"),
		dbfname, dbi->dbi_subfile, dbi->dbi_mode);

    /* Open the Database */
    db = xcalloc(1, sizeof(*db));
    sqldb = xcalloc(1,sizeof(*sqldb));
       
#if defined(USE_SQLITE3)
    sql_errcode = NULL;
    xx = sqlite3_open(dbfname, &sqldb->db);
    if (xx != SQLITE_OK)
	sql_errcode = sqlite3_errmsg(sqldb->db);
#else
    sqldb->db = sqlite_open(dbfname, dbi->dbi_mode, &sql_errcode);
#endif

    if (sqldb->db)
      SQLITE_BUSY_HANDLER(sqldb->db, &sql_busy_handler, dbi);

    sqldb->transaction = 0;	/* Initialize no current transactions */
    sqldb->head_cursor = NULL; 	/* no current cursors */

    db->app_private = sqldb;
    dbi->dbi_db = db;

    if (sql_errcode != NULL)
    {
      rpmMessage(RPMMESS_DEBUG, "Unable to open database: %s\n", sql_errcode);
      rc = EINVAL;
    }

#if 0
    /* This setup colides with db3 if we're using it... */
    /* Setup my db_env */
    if ( rpmdb->db_dbenv == NULL ) {
      dbenv = xcalloc(1, sizeof(DB_ENV));

      rpmdb->db_dbenv = dbenv;
      rpmdb->db_opens = 1;
    } else {
      dbenv = rpmdb->db_dbenv;

      rpmdb->db_opens++;
    }
#endif

    /* initialize table */
    if ( rc == 0 )
      rc = sql_initDB(dbi);

    if (rc == 0 && dbi->dbi_db != NULL && dbip != NULL) {
	dbi->dbi_vec = &sqlitevec;
	*dbip = dbi;
    }
    else {
	sql_close(dbi, 0);
    }
 
    urlfn = _free(urlfn);
    dbfname = _free(dbfname);
   
    return rc;
}

/**
 * Close index database, and destroy database handle.
 * @param dbi           index database handle
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_close(/*@only@*/ dbiIndex dbi, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_close()\n");
#endif

    DB * db = dbi->dbi_db;
    SQL_DB * sqldb;

    int rc=0;

    if (db && db->app_private && ((SQL_DB *)db->app_private)->db)
    {
      sqldb = (SQL_DB *)db->app_private;
      assert(sqldb != NULL && sqldb->db != NULL);

      /* Commit, don't open a new one */
      rc = sql_commitTransaction(dbi, 1);

      /* close all cursors */
      while ( sqldb->head_cursor ) {
	sql_cclose(dbi, sqldb->head_cursor->name, 0);
      }

      if (sqldb->count)
	rpmMessage(RPMMESS_DEBUG, "cursors %ld\n", sqldb->count);

      SQLITE_CLOSE(sqldb->db);

      rpmMessage(RPMMESS_DEBUG, _("closed   sql db         %s\n"),
		dbi->dbi_subfile);

#if 0
      /* Since we didn't setup the memory, don't clear it! */
      /* Free up memory */
      if (rpmdb->db_dbenv != NULL) {
	dbenv = rpmdb->db_dbenv;
	if (rpmdb->db_opens == 1) {
	  rpmdb->db_dbenv = _free(rpmdb->db_dbenv);
	}
	rpmdb->db_opens--;
      }
#endif

      if (dbi->dbi_stats) {
	dbi->dbi_stats = _free(dbi->dbi_stats);
      }

      dbi->dbi_file = _free(dbi->dbi_file);
#if 0
      /* They're the same so only free one! */
      dbi->dbi_subfile = _free(dbi->dbi_subfile);
#endif
      dbi->dbi_db->app_private = _free(dbi->dbi_db->app_private);  /* sqldb */
      dbi->dbi_db = _free(dbi->dbi_db);
    }
    dbi = _free(dbi);

    return 0;
}

/**
 * Flush pending operations to disk.
 * @param dbi           index database handle
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_sync (dbiIndex dbi, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_sync()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
 
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc = 0;

#ifndef SQL_FAST_DB
    rc = sql_commitTransaction(dbi, 0);
#endif

    return rc;
}

/**
 * Open database cursor.
 * @param dbi           index database handle
 * @param txnid         database transaction handle
 * @retval dbcp         address of new database cursor
 * @param dbiflags      DB_WRITECURSOR or 0
 * @return              0 on success
 */
int sql_copen (dbiIndex dbi, /*@null@*/ DB_TXN * txnid,
		/*@out@*/ DBC ** dbcp, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *txnid, *dbcp, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_copen()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
  
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    DBC * dbcursor;
    SQL_CURSOR * sqlcursor;

    int rc=0;

    dbcursor = xcalloc(1, sizeof(*dbcursor));
    dbcursor->dbp=db;

    sqlcursor = xcalloc(1, sizeof(*sqlcursor));
    sqlcursor->name=dbcursor;
    sqlcursor->resultp=NULL;
    sqlcursor->nrow=0;
    sqlcursor->ncolumn=0;
    sqlcursor->row_iterator=0;
    sqlcursor->all=0;
    sqlcursor->memory=NULL;

    sqlcursor->next_cursor = sqldb->head_cursor;
    sqldb->head_cursor = sqlcursor;

    sqldb->count++;

    /* If we're going to write, start a transaction (lock the DB) */
    if ( flags == DB_WRITECURSOR ) {
      rc = sql_startTransaction(dbi);
    }

    if (dbcp)
	/*@-onlytrans@*/ *dbcp = dbcursor; /*@=onlytrans@*/
    else
	(void) sql_cclose(dbi, dbcursor, 0);
     
    return rc;
}

/**   
 * Close database cursor.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param flags         (unused)
 * @return              0 on success
 */   
int sql_cclose (dbiIndex dbi, /*@only@*/ DBC * dbcursor, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *dbcursor, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_cclose()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
    
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    SQL_CURSOR * sqlcursor, *prev;

    int rc=0;

    prev=NULL;
    sqlcursor = sqldb->head_cursor;

    /* Find our version of the db3 cursor */
    while ( sqlcursor != NULL && sqlcursor->name != dbcursor ) {
      prev = sqlcursor;
      sqlcursor = sqlcursor->next_cursor;
    }

    assert(sqlcursor != NULL);

    /* Free memory */
    if ( sqlcursor->resultp ) {
      (void) SQLITE_FREE_TABLE( sqlcursor->resultp );
    }

    if ( sqlcursor->memory ) {
      SQL_MEM * curr_mem = sqlcursor->memory;
      SQL_MEM * next_mem;
      int loc_count=0;

      while ( curr_mem ) {
	next_mem = curr_mem->next;
	free ( curr_mem->mem_ptr );
	free ( curr_mem );
	curr_mem = next_mem;
	loc_count++;
      }

      if ( sqlcursor->count != loc_count)
	rpmMessage(RPMMESS_DEBUG, "Alloced %ld -- free %ld\n", 
		sqlcursor->count, loc_count);
    }

    /* Remove from the list */
    if (prev == NULL) {
      sqldb->head_cursor = sqlcursor->next_cursor;
    } else {
      prev->next_cursor = sqlcursor->next_cursor;
    }

    sqldb->count--;

    sqlcursor = _free(sqlcursor);
    dbcursor = _free(dbcursor);

#ifndef SQL_FAST_DB
    if ( flags == DB_WRITECURSOR ) {
       rc = sql_commitTransaction(dbi, 1);
    } else {
       rc = sql_endTransaction(dbi);
    }
#else
       rc = sql_endTransaction(dbi);
#endif

    return rc;
}

/**
 * Delete (key,data) pair(s) using db->del or dbcursor->c_del.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->del)   
 * @param key           delete key value/length/flags
 * @param data          delete data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_cdel (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
		unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_cdel()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;
    unsigned char * key_enc_string, * data_enc_string;
    int key_len, data_len;
    char * pzErrmsg;
    char * cmd;

#ifdef SQL_TRACE_CURSOR
    rpmMessage(RPMMESS_DEBUG, "  cdel on %s  key 0x%x (%d), data 0x%x (%d), flags %d\n",
		dbi->dbi_subfile,
		*(long *)key->data, key->size,
		*(long *)data->data, data->size,
		flags);   
#endif

    key_enc_string = alloca (2 + ((257 * key->size)/254) + 1);
    key_len=sqlite_encode_binary((char *)key->data, key->size, key_enc_string);
    key_enc_string[key_len]='\0';

    data_enc_string = alloca (2 + ((257 * data->size)/254) + 1);
    data_len=sqlite_encode_binary((char *)data->data, data->size, data_enc_string);
    data_enc_string[data_len]='\0';

#ifdef SQL_TRACE_ENCODINGS
    rpmMessage(RPMMESS_DEBUG, " encoded key  0x%x (%d)\n", *(long *)key_enc_string, key_len);
    rpmMessage(RPMMESS_DEBUG, " encoded data 0x%x (%d)\n", *(long *)data_enc_string, data_len);
#endif
    
    cmd = sqlite3_mprintf("DELETE FROM '%q' WHERE key='%q' AND value='%q';",
	dbi->dbi_subfile, key_enc_string, data_enc_string);
    rc = SQLITE_EXEC(sqldb->db, cmd, NULL, NULL, &pzErrmsg);
    SQLITE_FREE(cmd);

    if ( rc )
      rpmMessage(RPMMESS_DEBUG, "cdel %s (%d)\n",
		pzErrmsg, rc);

    return rc;
}

/**
 * Retrieve (key,data) pair using db->get or dbcursor->c_get.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->get)
 * @param key           retrieve key value/length/flags
 * @param data          retrieve data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_cget (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
		unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, *key, *data, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_cget()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    SQL_CURSOR * sqlcursor;

    int rc=0;
    int cleanup=0;   
    
    char * pzErrmsg;
    char * cmd;

    if ( dbcursor == NULL ) {
      rc = sql_copen ( dbi, NULL, &dbcursor, 0 );
      cleanup = 1;
    }

    /* Find our version of the db3 cursor */
    sqlcursor = sqldb->head_cursor;
    while ( sqlcursor != NULL && sqlcursor->name != dbcursor ) {
      sqlcursor = sqlcursor->next_cursor;
    }

    assert(sqlcursor != NULL);

    /*
     * First determine if we have a key, or if we're going to
     * scan the whole DB
     */

    if ( rc == 0 && key->size == 0 ) {
      sqlcursor->all++;

      /*
       * Scan the whole db..
       * We are not guarenteed a return order..
       * .. so get the 0x0 key first ..
       * (rpm expects the 0x0 key first)
       */
      if ( sqlcursor->all == 1 && dbi->dbi_rpmtag == RPMDBI_PACKAGES ) {
static int mykeydata;
	flags=DB_SET;
	key->size=4;
if (key->data == NULL) key->data = &mykeydata;
	memset(key->data, 0, 4);
      }
    }

    /* New retrieval */
    if ( rc == 0 &&  ( ( flags == DB_SET ) || ( sqlcursor->resultp == NULL )) ) {
#ifdef SQL_TRACE_CURSOR
      rpmMessage(RPMMESS_DEBUG, " cget  rc %d, flags %d, sqlcursor->result 0%x\n",
		rc, flags, sqlcursor->resultp);
#endif

      if ( sqlcursor->resultp ) {
	(void) SQLITE_FREE_TABLE( sqlcursor->resultp );
	sqlcursor->resultp = NULL;
	sqlcursor->nrow=0;
	sqlcursor->ncolumn=0;
	sqlcursor->row_iterator=0;
      }

      switch(key->size) {
	case 0:
	  cmd = sqlite3_mprintf("SELECT key,value FROM '%q';",
			dbi->dbi_subfile);
	  rc = SQLITE_GET_TABLE(sqldb->db, cmd,
			&sqlcursor->resultp, &sqlcursor->nrow, &sqlcursor->ncolumn,
			&pzErrmsg
		);
	  SQLITE_FREE(cmd);
	  break;
	default:
	{
	  unsigned char * key_enc_string;
	  int key_len;

#ifdef SQL_TRACE_CURSOR
	  rpmMessage(RPMMESS_DEBUG, "  cget on %s  find   key 0x%x (%d), flags %d\n",
		dbi->dbi_subfile,
		key->data == NULL ? 0 : *(long *)key->data, key->size,
		flags);
#endif

	  key_enc_string = alloca (2 + ((257 * key->size)/254) + 1);
	  key_len=sqlite_encode_binary((char *)key->data, key->size, key_enc_string);
	  key_enc_string[key_len]='\0';

#ifdef SQL_TRACE_ENCODINGS
	  rpmMessage(RPMMESS_DEBUG, " encoded key  0x%x (%d)\n",
		 *(long *)key_enc_string, key_len);
#endif

	  cmd = sqlite3_mprintf("SELECT key,value FROM '%q' WHERE key='%q';",
			dbi->dbi_subfile, key_enc_string);
	  rc = SQLITE_GET_TABLE(sqldb->db, cmd,
			&sqlcursor->resultp, &sqlcursor->nrow, &sqlcursor->ncolumn,
			&pzErrmsg);
	  SQLITE_FREE(cmd);

	  break;
	}
      }
#ifdef SQL_TRACE_CURSOR
      rpmMessage(RPMMESS_DEBUG, "  cget got %d rows, %d columns\n",
	sqlcursor->nrow, sqlcursor->ncolumn);
#endif
    }

    if ( rc == 0 && ! sqlcursor->resultp )
      rc = DB_NOTFOUND;

repeat:
    if ( rc == 0 ) {
      sqlcursor->row_iterator++;
      if ( sqlcursor->row_iterator > sqlcursor->nrow )
	rc = DB_NOTFOUND; /* At the end of the list */
      else {
#ifdef SQL_TRACE_ENCODINGS
	rpmMessage(RPMMESS_DEBUG, " encoded key  0x%x\n",
		 *(long *)sqlcursor->resultp[((sqlcursor->row_iterator*2)+0)]);
	rpmMessage(RPMMESS_DEBUG, " encoded data 0x%x\n",
		 *(long *)sqlcursor->resultp[((sqlcursor->row_iterator*2)+1)]);
#endif
	
	/* If we're looking at the whole db, return the key */
	if ( sqlcursor->all ) {
	  unsigned char * key_dec_string;
	  size_t key_len=strlen(sqlcursor->resultp[((sqlcursor->row_iterator*2)+0)]
			);

	  key_dec_string=alloca (2 + ((257 * key_len)/254));

	  key->size=sqlite_decode_binary(
		sqlcursor->resultp[((sqlcursor->row_iterator*2)+0)],
		key_dec_string
		);

	  if (key->flags & DB_DBT_MALLOC)
	    key->data=xmalloc(key->size);
	  else
	    key->data=allocTempBuffer(dbcursor, key->size);

	  (void) memcpy( key->data, key_dec_string, key->size );
	}

	/* Decode the data */
	{
	  unsigned char * data_dec_string;
	  size_t data_len=strlen(sqlcursor->resultp[((sqlcursor->row_iterator*2)+1)]);

	  data_dec_string=alloca (2 + ((257 * data_len)/254));

	  data->size=sqlite_decode_binary(
		sqlcursor->resultp[((sqlcursor->row_iterator*2)+1)],
		data_dec_string
		);

	  if (data->flags & DB_DBT_MALLOC)
	    data->data=xmalloc(data->size);
	  else
	    data->data=allocTempBuffer(dbcursor, data->size);

	  (void) memcpy( data->data, data_dec_string, data->size );
	}

	/* We need to skip this entry... (we've already returned it) */
	if ( dbi->dbi_rpmtag == RPMDBI_PACKAGES &&
		sqlcursor->all > 1 &&
		key->size ==4 && *(long *)key->data == 0 
	   ) {
#ifdef SQL_TRACE_CURSOR
	  rpmMessage(RPMMESS_DEBUG, "  cget on %s  skipping 0x0 record\n",
		dbi->dbi_subfile);
#endif
	  goto repeat;
	}

#ifdef SQL_TRACE_CURSOR
	rpmMessage(RPMMESS_DEBUG, "  cget on %s  found  key 0x%x (%d)\n",
		dbi->dbi_subfile,
		key->data == NULL ? 0 : *(long *)key->data, key->size
		);
	rpmMessage(RPMMESS_DEBUG, "  cget on %s  found data 0x%x (%d)\n",
		dbi->dbi_subfile,
		key->data == NULL ? 0 : *(long *)data->data, data->size
		);
#endif
      }  
    }

#ifdef SQL_TRACE_CURSOR
    if ( rc == DB_NOTFOUND )
	rpmMessage(RPMMESS_DEBUG, "  cget on %s  not found\n",
		dbi->dbi_subfile);
#endif

    /* If we retrieved the 0x0 record.. clear so next pass we'll get them all.. */
    if ( sqlcursor->all == 1 && dbi->dbi_rpmtag == RPMDBI_PACKAGES ) {
      if ( sqlcursor->resultp ) {
	(void) SQLITE_FREE_TABLE( sqlcursor->resultp );
	sqlcursor->resultp = NULL;
	sqlcursor->nrow=0;
	sqlcursor->ncolumn=0;
	sqlcursor->row_iterator=0;
      }
    }

    if ( cleanup ) {
      (void) sql_cclose(dbi, dbcursor, 0);
    }

    return rc;
}

/**
 * Store (key,data) pair using db->put or dbcursor->c_put.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->put)
 * @param key           store key value/length/flags
 * @param data          store data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_cput (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key, DBT * data,
			unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_cput()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;
    unsigned char * key_enc_string, * data_enc_string;
    int key_len, data_len;
    char * pzErrmsg;
    char * cmd;

#ifdef SQL_TRACE_CURSOR
    rpmMessage(RPMMESS_DEBUG, "  cput on %s  key 0x%x (%d), data 0x%x (%d), flags %d\n",
		dbi->dbi_subfile,
		*(long *)key->data, key->size,
		*(long *)data->data, data->size,
		flags);   
#endif

    key_enc_string = alloca (2 + ((257 * key->size)/254) + 1);
    key_len=sqlite_encode_binary((char *)key->data, key->size, key_enc_string);
    key_enc_string[key_len]='\0';

    data_enc_string = alloca (2 + ((257 * data->size)/254) + 1);
    data_len=sqlite_encode_binary((char *)data->data, data->size, data_enc_string);
    data_enc_string[data_len]='\0';

#ifdef SQL_TRACE_ENCODINGS
    rpmMessage(RPMMESS_DEBUG, " encoded key 0x%x (%d)\n", *(long *)key_enc_string, key_len);
    rpmMessage(RPMMESS_DEBUG, " encoded data 0x%x (%d)\n", *(long *)data_enc_string, data_len);
#endif
    
    cmd = sqlite3_mprintf("INSERT OR REPLACE INTO '%q' VALUES('%q', '%q');",
	dbi->dbi_subfile, key_enc_string, data_enc_string);
    rc = SQLITE_EXEC(sqldb->db, cmd, NULL, NULL, &pzErrmsg);
    SQLITE_FREE(cmd);

    if ( rc )
      rpmMessage(RPMMESS_WARNING, "cput %s (%d)\n",
		pzErrmsg, rc);

    return rc;
}

/**
 * Is database byte swapped?
 * @param dbi           index database handle   
 * @return              0 no
 */
int sql_byteswapped (dbiIndex dbi)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_byteswapped()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
       
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int sql_rc, rc=0;

    char ** resultp;
    int nrow;
    int ncolumn;
    char * pzErrmsg;

    long db_endian;

    sql_rc = SQLITE_GET_TABLE(sqldb->db, "SELECT endian FROM 'db_info';",
	&resultp, &nrow, &ncolumn, &pzErrmsg);

    if (sql_rc == 0 && nrow > 0) {
      db_endian=strtol(resultp[1], NULL, 10);

      if ( db_endian == __BYTE_ORDER )
	rc = 0; /* Native endian */
      else
	rc = 1; /* swapped */

#if 0
      rpmMessage(RPMMESS_DEBUG, "DB Endian %ld ?= %ld = %d\n",
		db_endian, __BYTE_ORDER, rc);
#endif
    } else {
      if ( sql_rc ) {
	rpmMessage(RPMMESS_DEBUG, "db_info failed %s (%d)\n",
		pzErrmsg, sql_rc);
      }
      rpmMessage(RPMMESS_WARNING, "Unable to determine DB endian.\n");
    }

    (void) SQLITE_FREE_TABLE(resultp);
   
    return rc;
}

/** \ingroup dbi
 * Save statistics in database handle.
 * @param dbi           index database handle
 * @param flags         retrieve statistics that don't require traversal?
 * @return              0 on success
 */
int sql_stat (dbiIndex dbi, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, fileSystem @*/
{
#ifdef SQL_TRACE_FUNCTIONS
    rpmMessage(RPMMESS_DEBUG, "sql_stat()\n");
#endif

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    int rc=0;

    char ** resultp;
    int nrow;
    int ncolumn;
    char * pzErrmsg;
    char * cmd;

    long nkeys=-1;

    if ( dbi->dbi_stats ) {
	dbi->dbi_stats = _free(dbi->dbi_stats);
    }

    dbi->dbi_stats = xcalloc(1, sizeof(DB_HASH_STAT));

    cmd = sqlite3_mprintf("SELECT COUNT('key') FROM '%q';", dbi->dbi_subfile);
    rc = SQLITE_GET_TABLE(sqldb->db, cmd,
	&resultp, &nrow, &ncolumn, &pzErrmsg);
    SQLITE_FREE(cmd);

    if ( rc == 0 && nrow > 0) {
      nkeys=strtol(resultp[1], NULL, 10);

      rpmMessage(RPMMESS_DEBUG, "  stat on %s nkeys=%ld\n",
		dbi->dbi_subfile, nkeys);
    } else {
      if ( rc ) {
	rpmMessage(RPMMESS_DEBUG, "stat failed %s (%d)\n",
		pzErrmsg, rc);
      }
    }

    (void) SQLITE_FREE_TABLE(resultp);

    if (nkeys < 0)
      nkeys = 4096;  /* Good high value */

    ((DB_HASH_STAT *)(dbi->dbi_stats))->hash_nkeys=nkeys;

    return rc;
}

/**************************************************
 *
 *  All of the following are not implemented!
 *  they are not used by the rest of the system
 *
 **************************************************/

/**
 * Associate secondary database with primary.
 * @param dbi           index database handle
 * @param dbisecondary  secondary index database handle
 * @param callback      create secondary key from primary (NULL if DB_RDONLY)
 * @param flags         DB_CREATE or 0
 * @return              0 on success
 */
int sql_associate (dbiIndex dbi, dbiIndex dbisecondary,
		int (*callback) (DB *, const DBT *, const DBT *, DBT *),
		unsigned int flags)
{
    /* unused */
    rpmMessage(RPMMESS_ERROR, "sql_associate() not implemented\n");

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);
 
    return EINVAL;
}

/**
 * Return join cursor for list of cursors.
 * @param dbi           index database handle
 * @param curslist      NULL terminated list of database cursors
 * @retval dbcp         address of join database cursor
 * @param flags         DB_JOIN_NOSORT or 0
 * @return              0 on success
 */
int sql_join (dbiIndex dbi, DBC ** curslist, /*@out@*/ DBC ** dbcp,
		unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *dbcp, fileSystem @*/
{
    /* unused */
    rpmMessage(RPMMESS_ERROR, "sql_join() not implemented\n");
    
    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);
 
    return EINVAL;
}

/**
 * Duplicate a database cursor.   
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @retval dbcp         address of new database cursor
 * @param flags         DB_POSITION for same position, 0 for uninitialized
 * @return              0 on success
 */
int sql_cdup (dbiIndex dbi, DBC * dbcursor, /*@out@*/ DBC ** dbcp,
		unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *dbcp, fileSystem @*/
{
    /* unused */
    rpmMessage(RPMMESS_ERROR, "sql_cdup() not implemented\n");

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);
 
    return EINVAL;
}

/**
 * Retrieve (key,data) pair using dbcursor->c_pget.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param key           secondary retrieve key value/length/flags
 * @param pkey          primary retrieve key value/length/flags
 * @param data          primary retrieve data value/length/flags 
 * @param flags         DB_NEXT, DB_SET, or 0
 * @return              0 on success
 */
int sql_cpget (dbiIndex dbi, /*@null@*/ DBC * dbcursor,
		DBT * key, DBT * pkey, DBT * data, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, *key, *pkey, *data, fileSystem @*/
{
    /* unused */
    rpmMessage(RPMMESS_ERROR, "sql_cpget() not implemented\n");

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);
 
    return EINVAL;
}

/**
 * Retrieve count of (possible) duplicate items using dbcursor->c_count.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param countp        address of count
 * @param flags         (unused)
 * @return              0 on success
 */
int sql_ccount (dbiIndex dbi, DBC * dbcursor,   
		/*@out@*/ unsigned int * countp,
		unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, fileSystem @*/
{
    /* unused */
    rpmMessage(RPMMESS_ERROR, "sql_cpget() not implemented\n");

    DB * db = dbi->dbi_db;
    assert(db != NULL); 
      
    SQL_DB * sqldb = (SQL_DB *)db->app_private;
    assert(sqldb != NULL && sqldb->db != NULL);

    return EINVAL;
}




/* Major, minor, patch version of DB.. we're not using db.. so set to 0 */
/* open, close, sync, associate, join */
/* cursor_open, cursor_close, cursor_dup, cursor_delete, cursor_get, */
/* cursor_pget?, cursor_put, cursor_count */
/* db_bytewapped, stat */
struct _dbiVec sqlitevec = {
    0, 0, 0, 
    sql_open, 
    sql_close,
    sql_sync,  
    sql_associate,  
    sql_join,
    sql_copen,
    sql_cclose,
    sql_cdup, 
    sql_cdel,
    sql_cget,
    sql_cpget,
    sql_cput,
    sql_ccount,
    sql_byteswapped,
    sql_stat
};


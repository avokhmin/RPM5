#include "system.h"

#include "rpmlib.h"

dbiIndex * dbiOpenIndex(char * filename, int flags, int perms) {
    dbiIndex * db;
        
    db = malloc(sizeof(*db));
    db->indexname = strdup(filename);
    db->db = dbopen(filename, flags, perms, DB_HASH, NULL);
    if (!db->db) {
	free(db->indexname);
	free(db);
	rpmError(RPMERR_DBOPEN, _("cannot open file %s: "), filename, 
			      strerror(errno));
	return NULL;
    }

    return db;
}

void dbiCloseIndex(dbiIndex * dbi) {
    dbi->db->close(dbi->db);
    free(dbi->indexname);
    free(dbi);
}

void dbiSyncIndex(dbiIndex * dbi) {
    dbi->db->sync(dbi->db, 0);
}

int dbiSearchIndex(dbiIndex * dbi, char * str, dbiIndexSet * set) {
    DBT key, data;
    int rc;

    key.data = str;
    key.size = strlen(str);

    rc = dbi->db->get(dbi->db, &key, &data, 0);
    if (rc == -1) {
	rpmError(RPMERR_DBGETINDEX, _("error getting record %s from %s"),
		str, dbi->indexname);
	return -1;
    } else if (rc == 1) {
	return 1;
    } 

    set->recs = data.data;
    set->recs = malloc(data.size);
    memcpy(set->recs, data.data, data.size);
    set->count = data.size / sizeof(dbiIndexRecord);
    return 0;
}

int dbiUpdateIndex(dbiIndex * dbi, char * str, dbiIndexSet * set) {
   /* 0 on success */
    DBT key, data;
    int rc;

    key.data = str;
    key.size = strlen(str);

    if (set->count) {
	data.data = set->recs;
	data.size = set->count * sizeof(dbiIndexRecord);

	rc = dbi->db->put(dbi->db, &key, &data, 0);
	if (rc) {
	    rpmError(RPMERR_DBPUTINDEX, _("error storing record %s into %s"),
		    str, dbi->indexname);
	    return 1;
	}
    } else {
	rc = dbi->db->del(dbi->db, &key, 0);
	if (rc) {
	    rpmError(RPMERR_DBPUTINDEX, _("error removing record %s into %s"),
		    str, dbi->indexname);
	    return 1;
	}
    }

    return 0;
}

int dbiAppendIndexRecord(dbiIndexSet * set, dbiIndexRecord rec) {
    set->count++;

    if (set->count == 1) {
	set->recs = malloc(set->count * sizeof(dbiIndexRecord));
    } else {
	set->recs = realloc(set->recs, set->count * sizeof(dbiIndexRecord));
    }
    set->recs[set->count - 1] = rec;

    return 0;
}

/* structure return */
dbiIndexSet dbiCreateIndexRecord(void) {
    dbiIndexSet set;

    set.recs = NULL;
    set.count = 0;
    return set;
}
    
void dbiFreeIndexRecord(dbiIndexSet set) {
    free(set.recs);
}

/* returns 1 on failure */
int dbiRemoveIndexRecord(dbiIndexSet * set, dbiIndexRecord rec) {
    int from;
    int to = 0;
    int num = set->count;
    int numCopied = 0;
  
    for (from = 0; from < num; from++) {
	if (rec.recOffset != set->recs[from].recOffset ||
	    rec.fileNumber != set->recs[from].fileNumber) {
	    if (from != to) set->recs[to] = set->recs[from];
	    to++;
	    numCopied++;
	} else {
	    set->count--;
	}
    }

    return (numCopied == num);
}

#ifndef H_MISC
#define H_MISC

#include <unistd.h>
#include <sys/types.h>

char ** splitString(char * str, int length, char sep);
void freeSplitString(char ** list);
void stripTrailingSlashes(char * str);

int rpmfileexists(char * filespec);

int rpmvercmp(char * one, char * two);

/* these are like the normal functions, but they malloc() the space which
   is needed */
int dosetenv(const char *name, const char *value, int overwrite);
int doputenv(const char * str);

/* These may be called w/ a NULL argument to flush the cache -- they return
   -1 if the user can't be found */
int unameToUid(char * thisUname, uid_t * uid);
int gnameToGid(char * thisGname, gid_t * gid);

/* Call w/ -1 to flush the cache, returns NULL if the user can't be found */
char * uidToUname(uid_t uid);
char * gidToGname(gid_t gid);

int makeTempFile(char * prefix, /*@out@*/char ** fnptr, /*@out@*/FD_t * fdptr);

#endif	/* H_MISC */

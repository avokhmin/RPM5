#ifndef H_LOOKUP
#define H_LOOKUP

#include <rpmlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int findMatches(rpmdb db, const char * name, const char * version,
	const char * release, dbiIndexSet * matches);

#ifdef __cplusplus
}
#endif

#endif

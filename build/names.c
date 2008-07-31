/*@-mods@*/
/** \ingroup rpmbuild
 * \file build/names.c
 * Simple user/group name/id cache (plus hostname and buildtime)
 */


#include "system.h"

#include <rpmio.h>
#include <rpmiotypes.h>
#include <rpmlog.h>
#include "rpmbuild.h"
#include "debug.h"

typedef /*@owned@*/ /*@null@*/ const char * ugstr_t;

/*@unchecked@*/
static uid_t uids[1024];
/*@unchecked@*/
static ugstr_t unames[1024];
/*@unchecked@*/
static int uid_used = 0;

/*@unchecked@*/
static gid_t gids[1024];
/*@unchecked@*/
static ugstr_t gnames[1024];
/*@unchecked@*/
static int gid_used = 0;
    
void freeNames(void)
{
    int x;
    for (x = 0; x < uid_used; x++)
	unames[x] = _free(unames[x]);
    for (x = 0; x < gid_used; x++)
	gnames[x] = _free(gnames[x]);
}

const char *getUname(uid_t uid)
	/*@globals uid_used, uids, unames @*/
	/*@modifies uid_used, uids, unames @*/
{
    struct passwd *pw;
    int x;

    for (x = 0; x < uid_used; x++) {
	if (unames[x] == NULL) continue;
	if (uids[x] == uid)
	    return unames[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getUname: too many uid's\n"));
    
    if ((pw = getpwuid(uid)) == NULL)
	return NULL;
    uids[uid_used] = uid;
    unames[uid_used] = xstrdup(pw->pw_name);
    return unames[uid_used++];
}

const char *getUnameS(const char *uname)
	/*@globals uid_used, uids, unames @*/
	/*@modifies uid_used, uids, unames @*/
{
    struct passwd *pw;
    int x;

    for (x = 0; x < uid_used; x++) {
	if (unames[x] == NULL) continue;
	if (!strcmp(unames[x],uname))
	    return unames[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getUnameS: too many uid's\n"));
    
    if ((pw = getpwnam(uname)) == NULL) {
	uids[uid_used] = -1;
	unames[uid_used] = xstrdup(uname);
    } else {
	uids[uid_used] = pw->pw_uid;
	unames[uid_used] = xstrdup(pw->pw_name);
    }
    return unames[uid_used++];
}

uid_t getUidS(const char *uname)
	/*@globals uid_used, uids, unames @*/
	/*@modifies uid_used, uids, unames @*/
{
    struct passwd *pw;
    int x;

    for (x = 0; x < uid_used; x++) {
	if (unames[x] == NULL) continue;
	if (!strcmp(unames[x],uname))
	    return uids[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getUidS: too many uid's\n"));
    
    if ((pw = getpwnam(uname)) == NULL) {
	uids[uid_used] = -1;
	unames[uid_used] = xstrdup(uname);
    } else {
	uids[uid_used] = pw->pw_uid;
	unames[uid_used] = xstrdup(pw->pw_name);
    }
    return uids[uid_used++];
}

const char *getGname(gid_t gid)
	/*@globals gid_used, gids, gnames @*/
	/*@modifies gid_used, gids, gnames @*/
{
    struct group *gr;
    int x;

    for (x = 0; x < gid_used; x++) {
	if (gnames[x] == NULL) continue;
	if (gids[x] == gid)
	    return gnames[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getGname: too many gid's\n"));
    
    if ((gr = getgrgid(gid)) == NULL)
	return NULL;
    gids[gid_used] = gid;
    gnames[gid_used] = xstrdup(gr->gr_name);
    return gnames[gid_used++];
}

const char *getGnameS(const char *gname)
	/*@globals gid_used, gids, gnames @*/
	/*@modifies gid_used, gids, gnames @*/
{
    struct group *gr;
    int x;

    for (x = 0; x < gid_used; x++) {
	if (gnames[x] == NULL) continue;
	if (!strcmp(gnames[x], gname))
	    return gnames[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getGnameS: too many gid's\n"));
    
    if ((gr = getgrnam(gname)) == NULL) {
	gids[gid_used] = -1;
	gnames[gid_used] = xstrdup(gname);
    } else {
	gids[gid_used] = gr->gr_gid;
	gnames[gid_used] = xstrdup(gr->gr_name);
    }
    return gnames[gid_used++];
}

gid_t getGidS(const char *gname)
	/*@globals gid_used, gids, gnames @*/
	/*@modifies gid_used, gids, gnames @*/
{
    struct group *gr;
    int x;

    for (x = 0; x < gid_used; x++) {
	if (gnames[x] == NULL) continue;
	if (!strcmp(gnames[x], gname))
	    return gids[x];
    }

    /* XXX - This is the other hard coded limit */
    if (x == 1024)
	rpmlog(RPMLOG_CRIT, _("getGidS: too many gid's\n"));
    
    if ((gr = getgrnam(gname)) == NULL) {
	gids[gid_used] = -1;
	gnames[gid_used] = xstrdup(gname);
    } else {
	gids[gid_used] = gr->gr_gid;
	gnames[gid_used] = xstrdup(gr->gr_name);
    }
    return gids[gid_used++];
}

rpmuint32_t * getBuildTime(void)
{
    static rpmuint32_t buildTime[1];

    if (buildTime[0] == 0)
	buildTime[0] = (rpmuint32_t) time(NULL);
    return buildTime;
}

const char * buildHost(void)
{
    static char hostname[1024];
    static int oneshot = 0;
    struct hostent *hbn;

    if (! oneshot) {
        (void) gethostname(hostname, sizeof(hostname));
	/*@-unrecog -multithreaded @*/
	/*@-globs@*/	/* FIX: h_errno access */
	hbn = gethostbyname(hostname);
	/*@=globs@*/
	/*@=unrecog =multithreaded @*/
	if (hbn)
	    strcpy(hostname, hbn->h_name);
	else
	    rpmlog(RPMLOG_WARNING,
			_("Could not canonicalize hostname: %s\n"), hostname);
	oneshot = 1;
    }
    return(hostname);
}
/*@=mods@*/

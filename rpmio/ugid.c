/** \ingroup rpmio
 * \file rpmio/ugid.c
 */

#include "system.h"
#include "ugid.h"
#include "debug.h"

/* unameToUid(), uidTouname() and the group variants are really poorly
   implemented. They really ought to use hash tables. I just made the
   guess that most files would be owned by root or the same person/group
   who owned the last file. Those two values are cached, everything else
   is looked up via getpw() and getgr() functions.  If this performs
   too poorly I'll have to implement it properly :-( */

int unameToUid(const char * thisUname, uid_t * uid)
{
/*@only@*/ static char * lastUname = NULL;
    static size_t lastUnameLen = 0;
    static size_t lastUnameAlloced;
    static uid_t lastUid;
    struct passwd * pwent;
    size_t thisUnameLen;

#ifdef	SUSE_REFERENCE
news
uucp
man
nobody
wwwrun
mail
lp
#endif
    if (!thisUname) {
	lastUnameLen = 0;
	return -1;
#if !defined(RPM_VENDOR_OPENPKG) /* no-hard-coded-ugid */
    } else if (strcmp(thisUname, "root") == 0) {
	*uid = 0;
	return 0;
#endif
    }

    thisUnameLen = strlen(thisUname);
    if (lastUname == NULL || thisUnameLen != lastUnameLen ||
	strcmp(thisUname, lastUname) != 0)
    {
	if (lastUnameAlloced < thisUnameLen + 1) {
	    lastUnameAlloced = thisUnameLen + 10;
	    lastUname = (char *) xrealloc(lastUname, lastUnameAlloced);
	}
	strcpy(lastUname, thisUname);

	pwent = getpwnam(thisUname);
	if (pwent == NULL) {
	    /*@-internalglobs@*/ /* FIX: shrug */
	    endpwent();
	    /*@=internalglobs@*/
	    pwent = getpwnam(thisUname);
	    if (pwent == NULL) return -1;
	}

	lastUid = pwent->pw_uid;
    }

    *uid = lastUid;

    return 0;
}

int gnameToGid(const char * thisGname, gid_t * gid)
{
/*@only@*/ static char * lastGname = NULL;
    static size_t lastGnameLen = 0;
    static size_t lastGnameAlloced;
    static gid_t lastGid;
    size_t thisGnameLen;
    struct group * grent;

#ifdef	SUSE_REFERENCE
news
dialout
uucp
lp
#endif
    if (thisGname == NULL) {
	lastGnameLen = 0;
	return -1;
#if !defined(RPM_VENDOR_OPENPKG) /* no-hard-coded-ugid */
    } else if (strcmp(thisGname, "root") == 0) {
	*gid = 0;
	return 0;
#endif
    }

    thisGnameLen = strlen(thisGname);
    if (lastGname == NULL || thisGnameLen != lastGnameLen ||
	strcmp(thisGname, lastGname) != 0)
    {
	if (lastGnameAlloced < thisGnameLen + 1) {
	    lastGnameAlloced = thisGnameLen + 10;
	    lastGname = (char *) xrealloc(lastGname, lastGnameAlloced);
	}
	strcpy(lastGname, thisGname);

	grent = getgrnam(thisGname);
	if (grent == NULL) {
	    /*@-internalglobs@*/ /* FIX: shrug */
	    endgrent();
	    /*@=internalglobs@*/
	    grent = getgrnam(thisGname);
	    if (grent == NULL) {
#if !defined(RPM_VENDOR_OPENPKG) /* no-hard-coded-ugid */
		/* XXX The filesystem package needs group/lock w/o getgrnam. */
		if (strcmp(thisGname, "lock") == 0) {
		    *gid = lastGid = 54;
		    return 0;
		} else
		if (strcmp(thisGname, "mail") == 0) {
		    *gid = lastGid = 12;
		    return 0;
		} else
#endif
		return -1;
	    }
	}
	lastGid = grent->gr_gid;
    }

    *gid = lastGid;

    return 0;
}

char * uidToUname(uid_t uid)
{
    static uid_t lastUid = (uid_t) -1;
/*@only@*/ static char * lastUname = NULL;
    static size_t lastUnameLen = 0;

    if (uid == (uid_t) -1) {
	lastUid = (uid_t) -1;
	return NULL;
#if !defined(RPM_VENDOR_OPENPKG) /* no-hard-coded-ugid */
    } else if (uid == (uid_t) 0) {
	return (char *) "root";
#endif
    } else if (uid == lastUid) {
	return lastUname;
    } else {
	struct passwd * pwent = getpwuid(uid);
	size_t len;

	if (pwent == NULL) return NULL;

	lastUid = uid;
	len = strlen(pwent->pw_name);
	if (lastUnameLen < len + 1) {
	    lastUnameLen = len + 20;
	    lastUname = (char *) xrealloc(lastUname, lastUnameLen);
	}
	strcpy(lastUname, pwent->pw_name);

	return lastUname;
    }
}

char * gidToGname(gid_t gid)
{
    static gid_t lastGid = (gid_t) -1;
/*@only@*/ static char * lastGname = NULL;
    static size_t lastGnameLen = 0;

    if (gid == (gid_t) -1) {
	lastGid = (gid_t) -1;
	return NULL;
#if !defined(RPM_VENDOR_OPENPKG) /* no-hard-coded-ugid */
    } else if (gid == (gid_t) 0) {
	return (char *) "root";
#endif
    } else if (gid == lastGid) {
	return lastGname;
    } else {
	struct group * grent = getgrgid(gid);
	size_t len;

	if (grent == NULL) return NULL;

	lastGid = gid;
	len = strlen(grent->gr_name);
	if (lastGnameLen < len + 1) {
	    lastGnameLen = len + 20;
	    lastGname = (char *) xrealloc(lastGname, lastGnameLen);
	}
	strcpy(lastGname, grent->gr_name);

	return lastGname;
    }
}

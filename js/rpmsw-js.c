/** \ingroup js_c
 * \file js/rpmsw-js.c
 */

#include "system.h"

#include "rpmsw-js.h"
#include "rpmjs-debug.h"

#include <rpmsw.h>

#include "debug.h"

/*@unchecked@*/
static int _debug = 0;

/* Required JSClass vectors */
#define	rpmsw_addprop		JS_PropertyStub
#define	rpmsw_delprop		JS_PropertyStub
#define	rpmsw_convert		JS_ConvertStub

/* Optional JSClass vectors */
#define	rpmsw_getobjectops	NULL
#define	rpmsw_checkaccess	NULL
#define	rpmsw_call		NULL
#define	rpmsw_construct		rpmsw_ctor
#define	rpmsw_xdrobject		NULL
#define	rpmsw_hasinstance	NULL
#define	rpmsw_mark		NULL
#define	rpmsw_reserveslots	NULL

/* Extended JSClass vectors */
#define rpmsw_equality		NULL
#define rpmsw_outerobject	NULL
#define rpmsw_innerobject	NULL
#define rpmsw_iteratorobject	NULL
#define rpmsw_wrappedobject	NULL

#define	rpmswNew()	xcalloc(1, sizeof(struct rpmop_s))
#define	rpmswFree(_sw)	_free(_sw)

/* --- helpers */

/* --- Object methods */

static JSFunctionSpec rpmsw_funcs[] = {
    JS_FS_END
};

/* --- Object properties */
enum rpmsw_tinyid {
    _DEBUG	= -2,
};

static JSPropertySpec rpmsw_props[] = {
    {"debug",	_DEBUG,		JSPROP_ENUMERATE,	NULL,	NULL},
    {NULL, 0, 0, NULL, NULL}
};

static JSBool
rpmsw_getprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmswClass, NULL);
    jsint tiny = JSVAL_TO_INT(id);

    /* XXX the class has ptr == NULL, instances have ptr != NULL. */
    if (ptr == NULL)
	return JS_TRUE;

    switch (tiny) {
    case _DEBUG:
	*vp = INT_TO_JSVAL(_debug);
	break;
    default:
	break;
    }

    return JS_TRUE;
}

static JSBool
rpmsw_setprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmswClass, NULL);
    jsint tiny = JSVAL_TO_INT(id);

    /* XXX the class has ptr == NULL, instances have ptr != NULL. */
    if (ptr == NULL)
	return JS_TRUE;

    switch (tiny) {
    case _DEBUG:
	if (!JS_ValueToInt32(cx, *vp, &_debug))
	    break;
	break;
    default:
	break;
    }

    return JS_TRUE;
}

static JSBool
rpmsw_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
	JSObject **objp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmswClass, NULL);

_RESOLVE_DEBUG_ENTRY(_debug < 0);

    if ((flags & JSRESOLVE_ASSIGNING)
     || (ptr == NULL)) { /* don't resolve to parent prototypes objects. */
	*objp = NULL;
	goto exit;
    }

    *objp = obj;	/* XXX always resolve in this object. */

exit:
    return JS_TRUE;
}

static JSBool
rpmsw_enumerate(JSContext *cx, JSObject *obj, JSIterateOp op,
		  jsval *statep, jsid *idp)
{

_ENUMERATE_DEBUG_ENTRY(_debug < 0);

    switch (op) {
    case JSENUMERATE_INIT:
	*statep = JSVAL_VOID;
        if (idp)
            *idp = JSVAL_ZERO;
        break;
    case JSENUMERATE_NEXT:
	*statep = JSVAL_VOID;
        if (*idp != JSVAL_VOID)
            break;
        /*@fallthrough@*/
    case JSENUMERATE_DESTROY:
	*statep = JSVAL_NULL;
        break;
    }
    return JS_TRUE;
}

/* --- Object ctors/dtors */
static rpmsw
rpmsw_init(JSContext *cx, JSObject *obj)
{
    rpmsw sw = rpmswNew();

if (_debug)
fprintf(stderr, "==> %s(%p,%p) sw %p\n", __FUNCTION__, cx, obj, sw);

    if (!JS_SetPrivate(cx, obj, (void *)sw)) {
	/* XXX error msg */
	return NULL;
    }
    return sw;
}

static void
rpmsw_dtor(JSContext *cx, JSObject *obj)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmswClass, NULL);
    rpmsw sw = ptr;

_DTOR_DEBUG_ENTRY(_debug);

    (void) rpmswFree(sw);
}

static JSBool
rpmsw_ctor(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSBool ok = JS_FALSE;

_CTOR_DEBUG_ENTRY(_debug);

    if (JS_IsConstructing(cx)) {
	(void) rpmsw_init(cx, obj);
    } else {
	if ((obj = JS_NewObject(cx, &rpmswClass, NULL, NULL)) == NULL)
	    goto exit;
	*rval = OBJECT_TO_JSVAL(obj);
    }
    ok = JS_TRUE;

exit:
    return ok;
}

/* --- Class initialization */
JSClass rpmswClass = {
    "Sw", JSCLASS_NEW_RESOLVE | JSCLASS_NEW_ENUMERATE | JSCLASS_HAS_PRIVATE,
    rpmsw_addprop,   rpmsw_delprop, rpmsw_getprop, rpmsw_setprop,
    (JSEnumerateOp)rpmsw_enumerate, (JSResolveOp)rpmsw_resolve,
    rpmsw_convert,	rpmsw_dtor,

    rpmsw_getobjectops,	rpmsw_checkaccess,
    rpmsw_call,		rpmsw_construct,
    rpmsw_xdrobject,	rpmsw_hasinstance,
    rpmsw_mark,		rpmsw_reserveslots,
};

JSObject *
rpmjs_InitSwClass(JSContext *cx, JSObject* obj)
{
    JSObject *proto;

if (_debug)
fprintf(stderr, "==> %s(%p,%p)\n", __FUNCTION__, cx, obj);

    proto = JS_InitClass(cx, obj, NULL, &rpmswClass, rpmsw_ctor, 1,
		rpmsw_props, rpmsw_funcs, NULL, NULL);
assert(proto != NULL);
    return proto;
}

JSObject *
rpmjs_NewSwObject(JSContext *cx)
{
    JSObject *obj;
    rpmsw sw;

    if ((obj = JS_NewObject(cx, &rpmswClass, NULL, NULL)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    if ((sw = rpmsw_init(cx, obj)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    return obj;
}

GPSEE_MODULE_WRAP(rpmsw, Sw, JS_TRUE)

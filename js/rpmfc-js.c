/** \ingroup js_c
 * \file js/rpmfc-js.c
 */

#include "system.h"

#include "rpmfc-js.h"
#include "rpmjs-debug.h"

#include <argv.h>
#define	_RPMFC_INTERNAL
#include <rpmfc.h>

#include "debug.h"

/*@unchecked@*/
static int _debug = 0;

/* Required JSClass vectors */
#define	rpmfc_addprop		JS_PropertyStub
#define	rpmfc_delprop		JS_PropertyStub
#define	rpmfc_convert		JS_ConvertStub

/* Optional JSClass vectors */
#define	rpmfc_getobjectops	NULL
#define	rpmfc_checkaccess	NULL
#define	rpmfc_call		NULL
#define	rpmfc_construct		rpmfc_ctor
#define	rpmfc_xdrobject	NULL
#define	rpmfc_hasinstance	NULL
#define	rpmfc_mark		NULL
#define	rpmfc_reserveslots	NULL

/* Extended JSClass vectors */
#define rpmfc_equality		NULL
#define rpmfc_outerobject	NULL
#define rpmfc_innerobject	NULL
#define rpmfc_iteratorobject	NULL
#define rpmfc_wrappedobject	NULL

/* --- helpers */

/* --- Object methods */

static JSFunctionSpec rpmfc_funcs[] = {
    JS_FS_END
};

/* --- Object properties */
enum rpmfc_tinyid {
    _DEBUG	= -2,
};

static JSPropertySpec rpmfc_props[] = {
    {"debug",	_DEBUG,		JSPROP_ENUMERATE,	NULL,	NULL},
    {NULL, 0, 0, NULL, NULL}
};

static JSBool
rpmfc_getprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmfcClass, NULL);
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
rpmfc_setprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmfcClass, NULL);
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
rpmfc_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
	JSObject **objp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmfcClass, NULL);

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
rpmfc_enumerate(JSContext *cx, JSObject *obj, JSIterateOp op,
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
static rpmfc
rpmfc_init(JSContext *cx, JSObject *obj)
{
    rpmfc fc = rpmfcNew();

if (_debug)
fprintf(stderr, "==> %s(%p,%p) fc %p\n", __FUNCTION__, cx, obj, fc);

    if (!JS_SetPrivate(cx, obj, (void *)fc)) {
	/* XXX error msg */
	return NULL;
    }
    return fc;
}

static void
rpmfc_dtor(JSContext *cx, JSObject *obj)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmfcClass, NULL);
    rpmfc fc = ptr;

_DTOR_DEBUG_ENTRY(_debug);

    if (fc)
	(void) rpmfcFree(fc);
}

static JSBool
rpmfc_ctor(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSBool ok = JS_FALSE;

_CTOR_DEBUG_ENTRY(_debug);

    if (JS_IsConstructing(cx)) {
	(void) rpmfc_init(cx, obj);
    } else {
	if ((obj = JS_NewObject(cx, &rpmfcClass, NULL, NULL)) == NULL)
	    goto exit;
	*rval = OBJECT_TO_JSVAL(obj);
    }
    ok = JS_TRUE;

exit:
    return ok;
}

/* --- Class initialization */
JSClass rpmfcClass = {
    "Fc", JSCLASS_NEW_RESOLVE | JSCLASS_NEW_ENUMERATE | JSCLASS_HAS_PRIVATE,
    rpmfc_addprop,   rpmfc_delprop, rpmfc_getprop, rpmfc_setprop,
    (JSEnumerateOp)rpmfc_enumerate, (JSResolveOp)rpmfc_resolve,
    rpmfc_convert,	rpmfc_dtor,

    rpmfc_getobjectops,	rpmfc_checkaccess,
    rpmfc_call,		rpmfc_construct,
    rpmfc_xdrobject,	rpmfc_hasinstance,
    rpmfc_mark,		rpmfc_reserveslots,
};

JSObject *
rpmjs_InitFcClass(JSContext *cx, JSObject* obj)
{
    JSObject *proto;

if (_debug)
fprintf(stderr, "==> %s(%p,%p)\n", __FUNCTION__, cx, obj);

    proto = JS_InitClass(cx, obj, NULL, &rpmfcClass, rpmfc_ctor, 1,
		rpmfc_props, rpmfc_funcs, NULL, NULL);
assert(proto != NULL);
    return proto;
}

JSObject *
rpmjs_NewFcObject(JSContext *cx)
{
    JSObject *obj;
    rpmfc fc;

    if ((obj = JS_NewObject(cx, &rpmfcClass, NULL, NULL)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    if ((fc = rpmfc_init(cx, obj)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    return obj;
}

GPSEE_MODULE_WRAP(rpmfc, Fc, JS_TRUE)

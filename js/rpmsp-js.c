/** \ingroup js_c
 * \file js/rpmsp-js.c
 */

#include "system.h"

#include "rpmsp-js.h"
#include "rpmjs-debug.h"

#if defined(WITH_SEPOL)
#include <sepol/sepol.h>
#endif
#define	_RPMSP_INTERNAL
#include <rpmsp.h>

#include "debug.h"

/*@unchecked@*/
static int _debug = 0;

/* Required JSClass vectors */
#define	rpmsp_addprop		JS_PropertyStub
#define	rpmsp_delprop		JS_PropertyStub
#define	rpmsp_convert		JS_ConvertStub

/* Optional JSClass vectors */
#define	rpmsp_getobjectops	NULL
#define	rpmsp_checkaccess	NULL
#define	rpmsp_call		rpmsp_call
#define	rpmsp_construct		rpmsp_ctor
#define	rpmsp_xdrobject		NULL
#define	rpmsp_hasinstance	NULL
#define	rpmsp_mark		NULL
#define	rpmsp_reserveslots	NULL

/* Extended JSClass vectors */
#define rpmsp_equality		NULL
#define rpmsp_outerobject	NULL
#define rpmsp_innerobject	NULL
#define rpmsp_iteratorobject	NULL
#define rpmsp_wrappedobject	NULL

/* --- helpers */

/* --- Object methods */

static JSFunctionSpec rpmsp_funcs[] = {
    JS_FS_END
};

/* --- Object properties */
enum rpmsp_tinyid {
    _DEBUG	= -2,
};

static JSPropertySpec rpmsp_props[] = {
    {"debug",	_DEBUG,		JSPROP_ENUMERATE,	NULL,	NULL},
    {NULL, 0, 0, NULL, NULL}
};

#define	_GET_STR(_str)	\
    ((_str) ? STRING_TO_JSVAL(JS_NewStringCopyZ(cx, (_str))) : JSVAL_VOID)
#define	_GET_CON(_test)	((_test) ? _GET_STR(con) : JSVAL_VOID)

static JSBool
rpmsp_getprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmspClass, NULL);
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

#define	_PUT_CON(_put)	(JSVAL_IS_STRING(*vp) && !(_put) \
	? JSVAL_TRUE : JSVAL_FALSE)
#define	_PUT_INT(_put)	(JSVAL_IS_INT(*vp) && !(_put) \
	? JSVAL_TRUE : JSVAL_FALSE)

static JSBool
rpmsp_setprop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmspClass, NULL);
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
rpmsp_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
	JSObject **objp)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmspClass, NULL);

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
rpmsp_enumerate(JSContext *cx, JSObject *obj, JSIterateOp op,
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
static rpmsp
rpmsp_init(JSContext *cx, JSObject *obj)
{
    const char * _fn = NULL;
    int _flags = 0;
    rpmsp sp = rpmspNew(_fn, _flags);

if (_debug)
fprintf(stderr, "==> %s(%p,%p) sp %p\n", __FUNCTION__, cx, obj, sp);

    if (!JS_SetPrivate(cx, obj, (void *)sp)) {
	/* XXX error msg */
	return NULL;
    }
    return sp;
}

static void
rpmsp_dtor(JSContext *cx, JSObject *obj)
{
    void * ptr = JS_GetInstancePrivate(cx, obj, &rpmspClass, NULL);
    rpmsp sp = ptr;

_DTOR_DEBUG_ENTRY(_debug);

    (void) rpmspFree(sp);
}

static JSBool
rpmsp_ctor(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSBool ok = JS_FALSE;

_CTOR_DEBUG_ENTRY(_debug);

    if (JS_IsConstructing(cx)) {
	(void) rpmsp_init(cx, obj);
    } else {
	if ((obj = JS_NewObject(cx, &rpmspClass, NULL, NULL)) == NULL)
	    goto exit;
	*rval = OBJECT_TO_JSVAL(obj);
    }
    ok = JS_TRUE;

exit:
    return ok;
}

static JSBool
rpmsp_call(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    /* XXX obj is the global object so lookup "this" object. */
    JSObject * o = JSVAL_TO_OBJECT(argv[-2]);
    void * ptr = JS_GetInstancePrivate(cx, o, &rpmspClass, NULL);
    JSBool ok = JS_FALSE;
#ifdef	FIXME
    rpmsp sp = ptr;
    const char *_fn = NULL;
    const char * _con = NULL;

    if (!(ok = JS_ConvertArguments(cx, argc, argv, "s", &_fn)))
        goto exit;

    *rval = (sp && _fn && (_con = rpmspLgetfilecon(sp, _fn)) != NULL)
	? STRING_TO_JSVAL(JS_NewStringCopyZ(cx, _con)) : JSVAL_VOID;
    _con = _free(_con);

    ok = JS_TRUE;

exit:
#endif

if (_debug)
fprintf(stderr, "<== %s(%p,%p,%p[%u],%p) o %p ptr %p\n", __FUNCTION__, cx, obj, argv, (unsigned)argc, rval, o, ptr);

    return ok;
}

/* --- Class initialization */
JSClass rpmspClass = {
    "Sp", JSCLASS_NEW_RESOLVE | JSCLASS_NEW_ENUMERATE | JSCLASS_HAS_PRIVATE,
    rpmsp_addprop,   rpmsp_delprop, rpmsp_getprop, rpmsp_setprop,
    (JSEnumerateOp)rpmsp_enumerate, (JSResolveOp)rpmsp_resolve,
    rpmsp_convert,	rpmsp_dtor,

    rpmsp_getobjectops,	rpmsp_checkaccess,
    rpmsp_call,		rpmsp_construct,
    rpmsp_xdrobject,	rpmsp_hasinstance,
    rpmsp_mark,		rpmsp_reserveslots,
};

JSObject *
rpmjs_InitSpClass(JSContext *cx, JSObject* obj)
{
    JSObject *proto;

if (_debug)
fprintf(stderr, "==> %s(%p,%p)\n", __FUNCTION__, cx, obj);

    proto = JS_InitClass(cx, obj, NULL, &rpmspClass, rpmsp_ctor, 1,
		rpmsp_props, rpmsp_funcs, NULL, NULL);
assert(proto != NULL);
    return proto;
}

JSObject *
rpmjs_NewSpObject(JSContext *cx)
{
    JSObject *obj;
    rpmsp sp;

    if ((obj = JS_NewObject(cx, &rpmspClass, NULL, NULL)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    if ((sp = rpmsp_init(cx, obj)) == NULL) {
	/* XXX error msg */
	return NULL;
    }
    return obj;
}

GPSEE_MODULE_WRAP(rpmsp, Sp, JS_TRUE)

/*
 * Portions adapted from CPython's Modules/errnomodule.c at v3.14.0.
 * Copyright (c) Python Software Foundation.
 * The derived portions are licensed under the PSF License Agreement.
 */

#include <clovervm/native_module.h>

#include <errno.h>

static clover_status add_errno(clover_context *ctx,
                               clover_native_module_builder *builder,
                               clover_handle errorcode, const char *name,
                               int value)
{
    clover_handle code = clover_int_from_int64(ctx, value);
    clover_handle name_value = clover_string_from_utf8(ctx, name);
    if(clover_module_add_value(builder, name, code) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_set_item(ctx, errorcode, code, name_value);
}

#define ADD_ERRNO(name)                                                        \
    do                                                                         \
    {                                                                          \
        if(add_errno(ctx, builder, errorcode, #name, name) !=                  \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__errno(
    clover_context *ctx, clover_native_module_builder *builder)
{
    clover_handle errorcode = clover_dict_new(ctx);

#ifdef ENODEV
    ADD_ERRNO(ENODEV);
#endif
#ifdef ENOCSI
    ADD_ERRNO(ENOCSI);
#endif
#ifdef EHOSTUNREACH
    ADD_ERRNO(EHOSTUNREACH);
#endif
#ifdef ENOMSG
    ADD_ERRNO(ENOMSG);
#endif
#ifdef EUCLEAN
    ADD_ERRNO(EUCLEAN);
#endif
#ifdef EL2NSYNC
    ADD_ERRNO(EL2NSYNC);
#endif
#ifdef EL2HLT
    ADD_ERRNO(EL2HLT);
#endif
#ifdef ENODATA
    ADD_ERRNO(ENODATA);
#endif
#ifdef ENOTBLK
    ADD_ERRNO(ENOTBLK);
#endif
#ifdef ENOSYS
    ADD_ERRNO(ENOSYS);
#endif
#ifdef EPIPE
    ADD_ERRNO(EPIPE);
#endif
#ifdef EINVAL
    ADD_ERRNO(EINVAL);
#endif
#ifdef EOVERFLOW
    ADD_ERRNO(EOVERFLOW);
#endif
#ifdef EADV
    ADD_ERRNO(EADV);
#endif
#ifdef EINTR
    ADD_ERRNO(EINTR);
#endif
#ifdef EUSERS
    ADD_ERRNO(EUSERS);
#endif
#ifdef ENOTEMPTY
    ADD_ERRNO(ENOTEMPTY);
#endif
#ifdef ENOBUFS
    ADD_ERRNO(ENOBUFS);
#endif
#ifdef EPROTO
    ADD_ERRNO(EPROTO);
#endif
#ifdef EREMOTE
    ADD_ERRNO(EREMOTE);
#endif
#ifdef ENAVAIL
    ADD_ERRNO(ENAVAIL);
#endif
#ifdef ECHILD
    ADD_ERRNO(ECHILD);
#endif
#ifdef ELOOP
    ADD_ERRNO(ELOOP);
#endif
#ifdef EXDEV
    ADD_ERRNO(EXDEV);
#endif
#ifdef E2BIG
    ADD_ERRNO(E2BIG);
#endif
#ifdef ESRCH
    ADD_ERRNO(ESRCH);
#endif
#ifdef EMSGSIZE
    ADD_ERRNO(EMSGSIZE);
#endif
#ifdef EAFNOSUPPORT
    ADD_ERRNO(EAFNOSUPPORT);
#endif
#ifdef EBADR
    ADD_ERRNO(EBADR);
#endif
#ifdef EHOSTDOWN
    ADD_ERRNO(EHOSTDOWN);
#endif
#ifdef EPFNOSUPPORT
    ADD_ERRNO(EPFNOSUPPORT);
#endif
#ifdef ENOPROTOOPT
    ADD_ERRNO(ENOPROTOOPT);
#endif
#ifdef EBUSY
    ADD_ERRNO(EBUSY);
#endif
#ifdef EWOULDBLOCK
    ADD_ERRNO(EWOULDBLOCK);
#endif
#ifdef EBADFD
    ADD_ERRNO(EBADFD);
#endif
#ifdef EDOTDOT
    ADD_ERRNO(EDOTDOT);
#endif
#ifdef EISCONN
    ADD_ERRNO(EISCONN);
#endif
#ifdef ENOANO
    ADD_ERRNO(ENOANO);
#endif
#ifdef ESHUTDOWN
    ADD_ERRNO(ESHUTDOWN);
#endif
#ifdef ECHRNG
    ADD_ERRNO(ECHRNG);
#endif
#ifdef ELIBBAD
    ADD_ERRNO(ELIBBAD);
#endif
#ifdef ENONET
    ADD_ERRNO(ENONET);
#endif
#ifdef EBADE
    ADD_ERRNO(EBADE);
#endif
#ifdef EBADF
    ADD_ERRNO(EBADF);
#endif
#ifdef EMULTIHOP
    ADD_ERRNO(EMULTIHOP);
#endif
#ifdef EIO
    ADD_ERRNO(EIO);
#endif
#ifdef EUNATCH
    ADD_ERRNO(EUNATCH);
#endif
#ifdef EPROTOTYPE
    ADD_ERRNO(EPROTOTYPE);
#endif
#ifdef ENOSPC
    ADD_ERRNO(ENOSPC);
#endif
#ifdef ENOEXEC
    ADD_ERRNO(ENOEXEC);
#endif
#ifdef EALREADY
    ADD_ERRNO(EALREADY);
#endif
#ifdef ENETDOWN
    ADD_ERRNO(ENETDOWN);
#endif
#ifdef ENOTNAM
    ADD_ERRNO(ENOTNAM);
#endif
#ifdef EACCES
    ADD_ERRNO(EACCES);
#endif
#ifdef ELNRNG
    ADD_ERRNO(ELNRNG);
#endif
#ifdef EILSEQ
    ADD_ERRNO(EILSEQ);
#endif
#ifdef ENOTDIR
    ADD_ERRNO(ENOTDIR);
#endif
#ifdef ENOTUNIQ
    ADD_ERRNO(ENOTUNIQ);
#endif
#ifdef EPERM
    ADD_ERRNO(EPERM);
#endif
#ifdef EDOM
    ADD_ERRNO(EDOM);
#endif
#ifdef EXFULL
    ADD_ERRNO(EXFULL);
#endif
#ifdef ECONNREFUSED
    ADD_ERRNO(ECONNREFUSED);
#endif
#ifdef EISDIR
    ADD_ERRNO(EISDIR);
#endif
#ifdef EPROTONOSUPPORT
    ADD_ERRNO(EPROTONOSUPPORT);
#endif
#ifdef EROFS
    ADD_ERRNO(EROFS);
#endif
#ifdef EADDRNOTAVAIL
    ADD_ERRNO(EADDRNOTAVAIL);
#endif
#ifdef EIDRM
    ADD_ERRNO(EIDRM);
#endif
#ifdef ECOMM
    ADD_ERRNO(ECOMM);
#endif
#ifdef ESRMNT
    ADD_ERRNO(ESRMNT);
#endif
#ifdef EREMOTEIO
    ADD_ERRNO(EREMOTEIO);
#endif
#ifdef EL3RST
    ADD_ERRNO(EL3RST);
#endif
#ifdef EBADMSG
    ADD_ERRNO(EBADMSG);
#endif
#ifdef ENFILE
    ADD_ERRNO(ENFILE);
#endif
#ifdef ELIBMAX
    ADD_ERRNO(ELIBMAX);
#endif
#ifdef ESPIPE
    ADD_ERRNO(ESPIPE);
#endif
#ifdef ENOLINK
    ADD_ERRNO(ENOLINK);
#endif
#ifdef ENETRESET
    ADD_ERRNO(ENETRESET);
#endif
#ifdef ETIMEDOUT
    ADD_ERRNO(ETIMEDOUT);
#endif
#ifdef ENOENT
    ADD_ERRNO(ENOENT);
#endif
#ifdef EEXIST
    ADD_ERRNO(EEXIST);
#endif
#ifdef EDQUOT
    ADD_ERRNO(EDQUOT);
#endif
#ifdef ENOSTR
    ADD_ERRNO(ENOSTR);
#endif
#ifdef EBADSLT
    ADD_ERRNO(EBADSLT);
#endif
#ifdef EBADRQC
    ADD_ERRNO(EBADRQC);
#endif
#ifdef ELIBACC
    ADD_ERRNO(ELIBACC);
#endif
#ifdef EFAULT
    ADD_ERRNO(EFAULT);
#endif
#ifdef EFBIG
    ADD_ERRNO(EFBIG);
#endif
#ifdef EDEADLK
    ADD_ERRNO(EDEADLK);
#endif
#ifdef ENOTCONN
    ADD_ERRNO(ENOTCONN);
#endif
#ifdef EDESTADDRREQ
    ADD_ERRNO(EDESTADDRREQ);
#endif
#ifdef ELIBSCN
    ADD_ERRNO(ELIBSCN);
#endif
#ifdef ENOLCK
    ADD_ERRNO(ENOLCK);
#endif
#ifdef EISNAM
    ADD_ERRNO(EISNAM);
#endif
#ifdef ECONNABORTED
    ADD_ERRNO(ECONNABORTED);
#endif
#ifdef ENETUNREACH
    ADD_ERRNO(ENETUNREACH);
#endif
#ifdef ESTALE
    ADD_ERRNO(ESTALE);
#endif
#ifdef ENOSR
    ADD_ERRNO(ENOSR);
#endif
#ifdef ENOMEM
    ADD_ERRNO(ENOMEM);
#endif
#ifdef ENOTSOCK
    ADD_ERRNO(ENOTSOCK);
#endif
#ifdef ESTRPIPE
    ADD_ERRNO(ESTRPIPE);
#endif
#ifdef EMLINK
    ADD_ERRNO(EMLINK);
#endif
#ifdef ERANGE
    ADD_ERRNO(ERANGE);
#endif
#ifdef ELIBEXEC
    ADD_ERRNO(ELIBEXEC);
#endif
#ifdef EL3HLT
    ADD_ERRNO(EL3HLT);
#endif
#ifdef ECONNRESET
    ADD_ERRNO(ECONNRESET);
#endif
#ifdef EADDRINUSE
    ADD_ERRNO(EADDRINUSE);
#endif
#ifdef EOPNOTSUPP
    ADD_ERRNO(EOPNOTSUPP);
#endif
#ifdef EREMCHG
    ADD_ERRNO(EREMCHG);
#endif
#ifdef EAGAIN
    ADD_ERRNO(EAGAIN);
#endif
#ifdef ENAMETOOLONG
    ADD_ERRNO(ENAMETOOLONG);
#endif
#ifdef ENOTTY
    ADD_ERRNO(ENOTTY);
#endif
#ifdef ERESTART
    ADD_ERRNO(ERESTART);
#endif
#ifdef ESOCKTNOSUPPORT
    ADD_ERRNO(ESOCKTNOSUPPORT);
#endif
#ifdef ETIME
    ADD_ERRNO(ETIME);
#endif
#ifdef EBFONT
    ADD_ERRNO(EBFONT);
#endif
#ifdef EDEADLOCK
    ADD_ERRNO(EDEADLOCK);
#endif
#ifdef ETOOMANYREFS
    ADD_ERRNO(ETOOMANYREFS);
#endif
#ifdef EMFILE
    ADD_ERRNO(EMFILE);
#endif
#ifdef ETXTBSY
    ADD_ERRNO(ETXTBSY);
#endif
#ifdef EINPROGRESS
    ADD_ERRNO(EINPROGRESS);
#endif
#ifdef ENXIO
    ADD_ERRNO(ENXIO);
#endif
#ifdef ENOPKG
    ADD_ERRNO(ENOPKG);
#endif
#ifdef ENOMEDIUM
    ADD_ERRNO(ENOMEDIUM);
#endif
#ifdef EMEDIUMTYPE
    ADD_ERRNO(EMEDIUMTYPE);
#endif
#ifdef ECANCELED
    ADD_ERRNO(ECANCELED);
#endif
#ifdef ENOKEY
    ADD_ERRNO(ENOKEY);
#endif
#ifdef EHWPOISON
    ADD_ERRNO(EHWPOISON);
#endif
#ifdef EKEYEXPIRED
    ADD_ERRNO(EKEYEXPIRED);
#endif
#ifdef EKEYREVOKED
    ADD_ERRNO(EKEYREVOKED);
#endif
#ifdef EKEYREJECTED
    ADD_ERRNO(EKEYREJECTED);
#endif
#ifdef EOWNERDEAD
    ADD_ERRNO(EOWNERDEAD);
#endif
#ifdef ENOTRECOVERABLE
    ADD_ERRNO(ENOTRECOVERABLE);
#endif
#ifdef ERFKILL
    ADD_ERRNO(ERFKILL);
#endif
#ifdef ENOTSUP
    ADD_ERRNO(ENOTSUP);
#endif
#ifdef ELOCKUNMAPPED
    ADD_ERRNO(ELOCKUNMAPPED);
#endif
#ifdef ENOTACTIVE
    ADD_ERRNO(ENOTACTIVE);
#endif
#ifdef EAUTH
    ADD_ERRNO(EAUTH);
#endif
#ifdef EBADARCH
    ADD_ERRNO(EBADARCH);
#endif
#ifdef EBADEXEC
    ADD_ERRNO(EBADEXEC);
#endif
#ifdef EBADMACHO
    ADD_ERRNO(EBADMACHO);
#endif
#ifdef EBADRPC
    ADD_ERRNO(EBADRPC);
#endif
#ifdef EDEVERR
    ADD_ERRNO(EDEVERR);
#endif
#ifdef EFTYPE
    ADD_ERRNO(EFTYPE);
#endif
#ifdef ENEEDAUTH
    ADD_ERRNO(ENEEDAUTH);
#endif
#ifdef ENOATTR
    ADD_ERRNO(ENOATTR);
#endif
#ifdef ENOPOLICY
    ADD_ERRNO(ENOPOLICY);
#endif
#ifdef EPROCLIM
    ADD_ERRNO(EPROCLIM);
#endif
#ifdef EPROCUNAVAIL
    ADD_ERRNO(EPROCUNAVAIL);
#endif
#ifdef EPROGMISMATCH
    ADD_ERRNO(EPROGMISMATCH);
#endif
#ifdef EPROGUNAVAIL
    ADD_ERRNO(EPROGUNAVAIL);
#endif
#ifdef EPWROFF
    ADD_ERRNO(EPWROFF);
#endif
#ifdef ERPCMISMATCH
    ADD_ERRNO(ERPCMISMATCH);
#endif
#ifdef ESHLIBVERS
    ADD_ERRNO(ESHLIBVERS);
#endif
#ifdef EQFULL
    ADD_ERRNO(EQFULL);
#endif
#ifdef ENOTCAPABLE
    ADD_ERRNO(ENOTCAPABLE);
#endif

    return clover_module_add_value(builder, "errorcode", errorcode);
}

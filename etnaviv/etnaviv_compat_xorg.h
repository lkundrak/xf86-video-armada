/*
 * Vivante GPU Acceleration Xorg driver
 *
 * Written by Russell King, 2012, derived in part from the
 * Intel xorg X server driver.
 */
#ifndef ETNAVIV_COMPAT_XORG_H
#define ETNAVIV_COMPAT_XORG_H

#if HAS_DEVPRIVATEKEYREC
#define etnaviv_CreateKey(key, type) dixRegisterPrivateKey(key, type, 0)
#define etnaviv_GetKeyPriv(dp, key)  dixGetPrivate(dp, key)
#define etnaviv_Key                  DevPrivateKeyRec
#else
#define etnaviv_CreateKey(key, type) dixRequestPrivate(key, 0)
#define etnaviv_GetKeyPriv(dp, key)  dixLookupPrivate(dp, key)
#define etnaviv_Key                  int
#endif

#endif

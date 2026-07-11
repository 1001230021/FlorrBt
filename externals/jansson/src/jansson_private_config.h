/*
 * Static Visual Studio private configuration for vendored Jansson.
 */
#ifndef JANSSON_PRIVATE_CONFIG_H
#define JANSSON_PRIVATE_CONFIG_H

#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SETLOCALE 1

#define HAVE_INT32_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT8_T 1

#define HAVE_SSIZE_T 1
#define ssize_t intptr_t

#define USE_WINDOWS_CRYPTOAPI 1
#define INITIAL_HASHTABLE_ORDER 3

#endif

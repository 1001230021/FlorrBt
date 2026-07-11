/*
 * Static Visual Studio configuration for vendored Jansson.
 */
#ifndef JANSSON_CONFIG_H
#define JANSSON_CONFIG_H

#ifndef JANSSON_USING_CMAKE
#define JANSSON_USING_CMAKE
#endif

#define HAVE_STDINT_H 1
#include <stdint.h>

#ifdef __cplusplus
#define JSON_INLINE inline
#else
#define JSON_INLINE inline
#endif

#define json_int_t long long
#define json_strtoint _strtoi64
#define JSON_INTEGER_FORMAT "I64d"

#define JSON_HAVE_LOCALECONV 1
#define JSON_HAVE_ATOMIC_BUILTINS 0
#define JSON_HAVE_SYNC_BUILTINS 0
#define JSON_PARSER_MAX_DEPTH 2048

#endif

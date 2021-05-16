/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ATTR_NO_WARN_UNUSED __attribute__((unused))
#define ATTR_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define ATTR_FORMAT_PRINTF(str, first_arg) __attribute__((format(printf, str, first_arg)))

ATTR_NO_WARN_UNUSED static void *StealPointerImpl(void **ptr) {
  void *temp = *ptr;
  *ptr = NULL;
  return temp;
}

#define STEAL_POINTER(ptr) ((__typeof__(*(ptr)))StealPointerImpl((void **)(ptr)))

ATTR_NO_WARN_UNUSED static void FreePImpl(void *ptr) {
  free(STEAL_POINTER((void **)ptr));
}

#define CLEANUP(func) __attribute__((__cleanup__(func)))
#define CLEANUP_AUTOFREE CLEANUP(FreePImpl)
#define CLEANUP_AUTOPTR(type) CLEANUP(CleanupAutoImpl_##type) struct type *

#define CLEANUP_AUTOPTR_DEFINE(type, deleter)                                   \
  ATTR_NO_WARN_UNUSED static void CleanupAutoImpl_##type(struct type **value) { \
    if (*value != NULL) {                                                       \
      deleter(STEAL_POINTER(value));                                            \
    }                                                                           \
  }

#define CLEANUP_AUTOPTR_ALIAS(type, alias)                                      \
  ATTR_NO_WARN_UNUSED static void CleanupAutoImpl_##type(struct type **value) { \
    alias(value);                                                               \
  }

ATTR_NO_WARN_UNUSED static void *Alloc(size_t bytes) {
  void *p = calloc(1, bytes);
  if (p == NULL) {
    abort();
  }

  return p;
}

ATTR_NO_WARN_UNUSED static char *StrDup(const char *str) {
  size_t len = strlen(str);
  char *buffer = Alloc(len + 1);
  memcpy(buffer, str, len);
  return buffer;
}

void SetupLogLevels();

ATTR_FORMAT_PRINTF(1, 2) void LogDebug(const char *fmt, ...);
ATTR_FORMAT_PRINTF(1, 2) void LogInfo(const char *fmt, ...);
ATTR_FORMAT_PRINTF(1, 2) void LogError(const char *fmt, ...);
ATTR_FORMAT_PRINTF(2, 3) void LogErrno(int errno_, const char *fmt, ...);

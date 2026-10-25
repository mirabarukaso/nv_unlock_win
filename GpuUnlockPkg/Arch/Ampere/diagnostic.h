/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPU_UNLOCK_DIAGNOSTIC_H
#define GPU_UNLOCK_DIAGNOSTIC_H

#include "TuCompat.h"

static inline int
gu_diagnostic_result (
  int          error,
  const CHAR8  *file,
  const CHAR8  *function,
  unsigned int line
  )
{
  if (error) {
    GU_ERROR ("failure %a:%u %a error=%d\n", file, line, function, error);
  }
  return error;
}

#if defined (_MSC_VER)
#define GU_FUNC  __FUNCTION__
#else
#define GU_FUNC  __func__
#endif
#define GU_DIAG(result) gu_diagnostic_result ((result), __FILE__, GU_FUNC, __LINE__)

#endif

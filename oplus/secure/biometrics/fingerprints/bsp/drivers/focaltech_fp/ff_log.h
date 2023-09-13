// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FF_LOGGING_H__
#define __FF_LOGGING_H__

/*
 * The default LOG_TAG is 'focaltech', using these two lines to define newtag.
 * # undef LOG_TAG
 * #define LOG_TAG "newtag"
 */
#ifndef LOG_TAG
#define LOG_TAG "focaltech"
#endif

/*
 * Log level can be used in 'logcat <tag>[:priority]', and also be
 * used in output control while '__FF_EARLY_LOG_LEVEL' is defined.
 */
typedef enum {
    FF_LOG_LEVEL_ALL = 0,
    FF_LOG_LEVEL_VBS = 1, /* Verbose */
    FF_LOG_LEVEL_DBG = 2, /* Debug   */
    FF_LOG_LEVEL_INF = 3, /* Info    */
    FF_LOG_LEVEL_WRN = 4, /* Warning */
    FF_LOG_LEVEL_ERR = 5, /* Error   */
    FF_LOG_LEVEL_DIS = 6, /* Disable */
} ff_log_level_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Logging API. Do NOT use it directly but FF_LOG* instead.
 *
 * @params
 *  level: Logging level for logcat.
 *  tag  : Logging tag for logcat.
 *  fmt  : See POSIX printf(..).
 *  ...  : See POSIX printf(..).
 *
 * @return
 *  The number of characters printed, or a negative value if there
 *  was an output error.
 */
int ff_log_printf(ff_log_level_t level, const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __FF_LOGGING_H__ */

/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __FP_HEALTH_H
#define __FP_HEALTH_H

typedef enum fp_scene_id {
    FP_SCENE_HAL_MAX       = 100,
    FP_SCENE_DRV_INIT      = 101,
    FP_SCENE_DRV_OPEN_FAIL = 102,
} fp_scene_id_t;

typedef enum fp_log_type {
    FP_LOG_APP,
    FP_LOG_KERNEL,
    FP_LOG_TA,
    FP_LOG_ALL,
    FP_LOG_UNKNOWN
} fp_log_type_t;

typedef struct fp_exception_info {
    uint64_t time;
    uint32_t id;
    uint32_t pid;
    uint32_t exceptionType;
    uint32_t faultLevel;
    uint64_t logOption;
    char module[128];
    char logPath[256];
    char summary[256];
} fp_exception_info_t;

int fp_olc_raise_exception(fp_exception_info_t *exp_info);
int fp_exception_report_drv(fp_scene_id_t scene_id);

#endif /*__FP_HEALTH_H*/

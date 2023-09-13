#ifndef FINGERPRINT_LOG_H
#define FINGERPRINT_LOG_H

#define FP_DRIVER_TAG "[fp_driver] "
typedef enum {
    FP_ERR,
    FP_INFO,
    FP_DEBUG,
    FP_ALL,
} fp_log_level;

static int log_level = FP_ALL;

#define FP_LOGE(fmt, args...) do { \
    if (FP_ERR <= log_level) {\
        pr_err(FP_DRIVER_TAG fmt, ##args); \
    } \
} while (0)

#define FP_LOGD(fmt, args...) do { \
    if (FP_DEBUG <= log_level) {\
        pr_debug(FP_DRIVER_TAG fmt, ##args); \
    } \
} while (0)

#define FP_LOGI(fmt, args...) do { \
    if (FP_INFO <= log_level) {\
        pr_info(FP_DRIVER_TAG fmt, ##args); \
    } \
} while (0)

#define func_enter() FP_LOGI("%s, enter\n", __func__)
#define func_exit()  FP_LOGI("%s, exit\n", __func__)

#endif //FINGERPRINT_LOG_H

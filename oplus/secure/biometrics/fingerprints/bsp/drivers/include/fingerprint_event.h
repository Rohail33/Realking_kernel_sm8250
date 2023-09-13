#ifndef FINGERPRINT_EVENT_H
#define FINGERPRINT_EVENT_H

#define SEND_FINGERPRINT_EVENT_ENABLE (1)
#define SEND_FINGERPRINT_EVENT_DISABLE (0)
#define MAX_MESSAGE_SIZE (128)

#define FP_DIRVER_NETLINK (1)
#define FP_DRIVER_INTERRUPT (2)

enum fingerprint_event {
    E_FP_EVENT_TEST = 0,
    E_FP_EVENT_IRQ = 1,
    E_FP_EVENT_SCR_OFF = 2,
    E_FP_EVENT_SCR_ON = 3,
    E_FP_EVENT_TP_TOUCHDOWN = 4,
    E_FP_EVENT_TP_TOUCHUP = 5,
    E_FP_EVENT_UI_READY = 6,
    E_FP_EVENT_UI_DISAPPEAR = 7,
    E_FP_EVENT_STOP_INTERRUPT= 8,
    E_FP_EVENT_MAX,
};

enum fingerprint_event_module {E_FP_TP = 0, E_FP_LCD = 1, E_FP_HAL = 2, E_FP_SENSOR = 3};

struct fingerprint_message_t {
    int module;
    int event;
    int in_size;
    char in_buf[MAX_MESSAGE_SIZE];
    int out_size;
    char out_buf[MAX_MESSAGE_SIZE];
};

int fp_event_register_proc_fs(void);
void set_fp_driver_event_type(int type);
int get_fp_driver_event_type(void);

int send_fingerprint_message(int module, int event, void *data,
                             unsigned int size);
int wait_fingerprint_event(void *data, unsigned int size,
                           struct fingerprint_message_t **msg);
#endif
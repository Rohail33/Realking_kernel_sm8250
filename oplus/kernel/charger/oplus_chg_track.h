#ifndef __OPLUS_CHG_TRACK_H__
#define __OPLUS_CHG_TRACK_H__

#include <linux/version.h>
#include <linux/of.h>
#include <linux/debugfs.h>

#define OPLUS_CHG_TRACK_CURX_INFO_LEN		(1024 + 512 + 256)
#define ADSP_TRACK_CURX_INFO_LEN		500
#define ADSP_TRACK_PROPERTY_DATA_SIZE_MAX	512
#define ADSP_TRACK_FIFO_NUMS			6
#define OPLUS_CHG_TRACK_POWER_INFO_LEN		256

#define OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN	32
#define OPLUS_CHG_TRACK_SOFT_ERR_NAME_LEN	32
#define OPLUS_CHG_TRACK_SCENE_GPIO_LEVEL_ERR	"gpio_level_err"
enum oplus_chg_track_gpio_device_error {
	TRACK_GPIO_ERR_DEFAULT,
	TRACK_GPIO_ERR_CHARGER_ID,
	TRACK_GPIO_ERR_VOOC_SWITCH,
};

#define OPLUS_CHG_TRACK_SCENE_PMIC_ICL_ERR	"icl_err"
#define OPLUS_CHG_TRACK_ICL_MONITOR_THD_MA	1200
enum oplus_chg_track_pmic_device_error {
	TRACK_PMIC_ERR_DEFAULT,
	TRACK_PMIC_ERR_ICL_VBUS_COLLAPSE,
	TRACK_PMIC_ERR_ICL_VBUS_LOW_POINT,
};

#define OPLUS_CHG_TRACK_SCENE_I2C_ERR		"i2c_err"

#define OPLUS_CHG_TRACK_SCENE_WLS_RX_ERR	"wls_rx_err"
#define OPLUS_CHG_TRACK_SCENE_WLS_TX_ERR	"wls_tx_err"
#define OPLUS_CHG_TRACK_SCENE_WLS_UPDATE_ERR	"wls_update_err"
enum oplus_chg_track_wls_device_error {
	TRACK_WLS_TRX_ERR_DEFAULT,
	TRACK_WLS_TRX_ERR_RXAC,
	TRACK_WLS_TRX_ERR_OCP,
	TRACK_WLS_TRX_ERR_OVP,
	TRACK_WLS_TRX_ERR_LVP,
	TRACK_WLS_TRX_ERR_FOD,
	TRACK_WLS_TRX_ERR_CEPTIMEOUT,
	TRACK_WLS_TRX_ERR_RXEPT,
	TRACK_WLS_TRX_ERR_OTP,
	TRACK_WLS_TRX_ERR_VOUT,
	TRACK_WLS_UPDATE_ERR_I2C,
	TRACK_WLS_UPDATE_ERR_CRC,
	TRACK_WLS_UPDATE_ERR_OTHER,
	TRACK_WLS_TRX_VOUT_ABNORMAL,
};

#define OPLUS_CHG_TRACK_SCENE_CP_ERR "cp_work_err"
enum oplus_chg_track_cp_device_error {
	TRACK_CP_ERR_DEFAULT,
	TRACK_CP_ERR_NO_WORK,
	TRACK_CP_ERR_CFLY_CDRV_FAULT,
	TRACK_CP_ERR_VBAT_OVP,
	TRACK_CP_ERR_IBAT_OCP,
	TRACK_CP_ERR_VBUS_OVP,
	TRACK_CP_ERR_IBUS_OCP,
};

#define OPLUS_CHG_TRACK_SCENE_BIDIRECT_CP_ERR "bidirect_cp_work_err"
enum oplus_chg_track_bidirect_cp_device_error {
	TRACK_BIDIRECT_CP_ERR_DEFAULT,
	TRACK_BIDIRECT_CP_ERR_SC_EN_STAT,
	TRACK_BIDIRECT_CP_ERR_V2X_OVP,
	TRACK_BIDIRECT_CP_ERR_V1X_OVP,
	TRACK_BIDIRECT_CP_ERR_VAC_OVP,
	TRACK_BIDIRECT_CP_ERR_FWD_OCP,
	TRACK_BIDIRECT_CP_ERR_RVS_OCP,
	TRACK_BIDIRECT_CP_ERR_TSHUT,
	TRACK_BIDIRECT_CP_ERR_VAC2V2X_OVP,
	TRACK_BIDIRECT_CP_ERR_VAC2V2X_UVP,
	TRACK_BIDIRECT_CP_ERR_V1X_ISS_OPP,
	TRACK_BIDIRECT_CP_ERR_WD_TIMEOUT,
	TRACK_BIDIRECT_CP_ERR_LNC_SS_TIMEOUT,
};

#define OPLUS_CHG_TRACK_SCENE_MOS_ERR "parallel_mos_err"
enum oplus_chg_track_mos_device_error {
	TRACK_MOS_ERR_DEFAULT,
	TRACK_MOS_I2C_ERROR,
	TRACK_MOS_OPEN_ERROR,
	TRACK_MOS_SUB_BATT_FULL,
	TRACK_MOS_VBAT_GAP_BIG,
	TRACK_MOS_SOC_NOT_FULL,
	TRACK_MOS_CURRENT_UNBALANCE,
	TRACK_MOS_SOC_GAP_TOO_BIG,
	TRACK_MOS_RECORD_SOC,
};

#define OPLUS_CHG_TRACK_SCENE_GAGUE_SEAL_ERR	"seal_err"
#define OPLUS_CHG_TRACK_SCENE_GAGUE_UNSEAL_ERR	"unseal_err"
enum oplus_chg_track_gague_device_error {
	TRACK_GAGUE_ERR_DEFAULT,
	TRACK_GAGUE_ERR_SEAL,
	TRACK_GAGUE_ERR_UNSEAL,
};

#define OPLUS_CHG_TRACK_SCENE_UFCS_ERR "ufcs_err"
enum oplus_chg_track_ufcs_device_error {
	TRACK_UFCS_ERR_DEFAULT,
	TRACK_UFCS_ERR_IBUS_LIMIT,
	TRACK_UFCS_ERR_CP_ENABLE,
	TRACK_UFCS_ERR_R_COOLDOWN,
	TRACK_UFCS_ERR_BTB_OVER,
	TRACK_UFCS_ERR_USBTEMP_OVER,
	TRACK_UFCS_ERR_TFG_OVER,
	TRACK_UFCS_ERR_VBAT_DIFF,
};

#define OPLUS_CHG_TRACK_SCENE_COOLDOWN_ERR	"cooldown_err"
enum oplus_chg_track_cooldown_error {
	TRACK_COOLDOWN_ERR_DEFAULT,
	TRACK_COOLDOWN_NOT_MATCH,
	TRACK_COOLDOWN_INVALID,
};

enum oplus_chg_track_cmd_error {
	TRACK_CMD_ACK_OK,
	TRACK_CMD_ERROR_CHIP_NULL = 1,
	TRACK_CMD_ERROR_DATA_NULL,
	TRACK_CMD_ERROR_DATA_INVALID,
	TRACK_CMD_ERROR_TIME_OUT,
};

#define OPLUS_CHG_TRACK_PEN_MATCH_ERR "pen_match_err"
enum oplus_chg_track_pen_match_error {
	TRACK_PEN_MATCH_ERR_DEFAULT,
	TRACK_PEN_MATCH_TIMEOUT,
	TRACK_PEN_MATCH_VERIFY_FAILED,
	TRACK_PEN_MATCH_TIMEOUT_AND_VERIFY_FAILED,
};

enum oplus_chg_track_info_type {
	TRACK_NOTIFY_TYPE_DEFAULT,
	TRACK_NOTIFY_TYPE_SOC_JUMP,
	TRACK_NOTIFY_TYPE_GENERAL_RECORD,
	TRACK_NOTIFY_TYPE_NO_CHARGING,
	TRACK_NOTIFY_TYPE_CHARGING_SLOW,
	TRACK_NOTIFY_TYPE_CHARGING_BREAK,
	TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL,
	TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL,
	TRACK_NOTIFY_TYPE_MAX,
};

enum oplus_chg_track_info_flag {
	TRACK_NOTIFY_FLAG_DEFAULT,
	TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP,
	TRACK_NOTIFY_FLAG_SOC_JUMP,
	TRACK_NOTIFY_FLAG_UI_SOC_JUMP,
	TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP,

	TRACK_NOTIFY_FLAG_CHARGER_INFO,
	TRACK_NOTIFY_FLAG_UISOC_KEEP_1_T_INFO,
	TRACK_NOTIFY_FLAG_VBATT_TOO_LOW_INFO,
	TRACK_NOTIFY_FLAG_USBTEMP_INFO,
	TRACK_NOTIFY_FLAG_VBATT_DIFF_OVER_INFO,
	TRACK_NOTIFY_FLAG_SERVICE_UPDATE_WLS_THIRD_INFO,
	TRACK_NOTIFY_FLAG_WLS_TRX_INFO,

	TRACK_NOTIFY_FLAG_NO_CHARGING,

	TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM,
	TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_COLD,
	TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA,
	TRACK_NOTIFY_FLAG_CHG_SLOW_BATT_CAP_HIGH,
	TRACK_NOTIFY_FLAG_CHG_SLOW_COOLDOWN,
	TRACK_NOTIFY_FLAG_CHG_SLOW_WLS_SKEW,
	TRACK_NOTIFY_FLAG_CHG_SLOW_VERITY_FAIL,
	TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER,

	TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK,
	TRACK_NOTIFY_FLAG_GENERAL_CHARGING_BREAK,
	TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK,

	TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL,
	TRACK_NOTIFY_FLAG_GPIO_ABNORMAL,
	TRACK_NOTIFY_FLAG_CP_ABNORMAL,
	TRACK_NOTIFY_FLAG_PLAT_PMIC_ABNORMAL,
	TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL,
	TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL,
	TRACK_NOTIFY_FLAG_DCHG_ABNORMAL,
	TRACK_NOTIFY_FLAG_PARALLEL_UNBALANCE_ABNORMAL,
	TRACK_NOTIFY_FLAG_MOS_ERROR_ABNORMAL,

	TRACK_NOTIFY_FLAG_UFCS_ABNORMAL,
	TRACK_NOTIFY_FLAG_COOLDOWN_ABNORMAL,
	TRACK_NOTIFY_FLAG_SMART_CHG_ABNORMAL,
	TRACK_NOTIFY_FLAG_WLS_THIRD_ENCRY_ABNORMAL,
	TRACK_NOTIFY_FLAG_PEN_MATCH_STATE_ABNORMAL,
	TRACK_NOTIFY_FLAG_MAX_CNT,
};

enum oplus_chg_track_mcu_voocphy_break_code {
	TRACK_VOOCPHY_BREAK_DEFAULT = 0,
	TRACK_MCU_VOOCPHY_FAST_ABSENT,
	TRACK_MCU_VOOCPHY_BAD_CONNECTED,
	TRACK_MCU_VOOCPHY_BTB_TEMP_OVER,
	TRACK_MCU_VOOCPHY_TEMP_OVER,
	TRACK_MCU_VOOCPHY_NORMAL_TEMP_FULL,
	TRACK_MCU_VOOCPHY_LOW_TEMP_FULL,
	TRACK_MCU_VOOCPHY_BAT_TEMP_EXIT,
	TRACK_MCU_VOOCPHY_DATA_ERROR,
	TRACK_MCU_VOOCPHY_HEAD_ERROR,
	TRACK_MCU_VOOCPHY_OTHER,
	TRACK_MCU_VOOCPHY_ADAPTER_FW_UPDATE,
};

enum oplus_chg_track_adsp_voocphy_break_code {
	TRACK_ADSP_VOOCPHY_BREAK_DEFAULT = 0,
	TRACK_ADSP_VOOCPHY_BAD_CONNECTED,
	TRACK_ADSP_VOOCPHY_FRAME_H_ERR,
	TRACK_ADSP_VOOCPHY_CLK_ERR,
	TRACK_ADSP_VOOCPHY_HW_VBATT_HIGH,
	TRACK_ADSP_VOOCPHY_HW_TBATT_HIGH,
	TRACK_ADSP_VOOCPHY_COMMU_TIME_OUT,
	TRACK_ADSP_VOOCPHY_ADAPTER_COPYCAT,
	TRACK_ADSP_VOOCPHY_BTB_TEMP_OVER,
	TRACK_ADSP_VOOCPHY_FULL,
	TRACK_ADSP_VOOCPHY_BATT_TEMP_OVER,
	TRACK_ADSP_VOOCPHY_SWITCH_TEMP_RANGE,
	TRACK_ADSP_VOOCPHY_OTHER,
};

enum oplus_chg_track_chg_status {
	TRACK_CHG_DEFAULT,
	TRACK_WIRED_FASTCHG_FULL,
	TRACK_WIRED_REPORT_FULL,
	TRACK_WIRED_CHG_DONE,
	TRACK_WLS_FASTCHG_FULL,
	TRACK_WLS_REPORT_FULL,
	TRACK_WLS_CHG_DONE,
};

enum oplus_chg_track_cp_voocphy_break_code {
	TRACK_CP_VOOCPHY_BREAK_DEFAULT = 0,
	TRACK_CP_VOOCPHY_FAST_ABSENT,
	TRACK_CP_VOOCPHY_BAD_CONNECTED,
	TRACK_CP_VOOCPHY_FRAME_H_ERR,
	TRACK_CP_VOOCPHY_BTB_TEMP_OVER,
	TRACK_CP_VOOCPHY_COMMU_TIME_OUT,
	TRACK_CP_VOOCPHY_ADAPTER_COPYCAT,
	TRACK_CP_VOOCPHY_FULL,
	TRACK_CP_VOOCPHY_BATT_TEMP_OVER,
	TRACK_CP_VOOCPHY_USER_EXIT_FASTCHG,
	TRACK_CP_VOOCPHY_SWITCH_TEMP_RANGE,
	TRACK_CP_VOOCPHY_OTHER,
};

typedef struct {
	unsigned int type_reason;
	unsigned int flag_reason;
	unsigned char crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
} __attribute__((packed)) oplus_chg_track_trigger;

typedef struct {
	u32 adsp_type_reason;
	u32 adsp_flag_reason;
	u8 adsp_crux_info[ADSP_TRACK_CURX_INFO_LEN];
} __attribute__((packed)) adsp_track_trigger;

int oplus_chg_track_handle_adsp_info(u8 *crux_info, int len);
int oplus_chg_track_upload_trigger_data(oplus_chg_track_trigger data);
int oplus_chg_track_comm_monitor(void);
int oplus_chg_track_check_wired_charging_break(int vbus_rising);
int oplus_chg_track_parallel_mos_error(int reason);
int oplus_chg_track_set_fastchg_break_code(int fastchg_break_code);
int oplus_chg_track_check_wls_charging_break(int wls_connect);
struct dentry *oplus_chg_track_get_debugfs_root(void);
int oplus_chg_track_obtain_power_info(char *power_info, int len);
int oplus_chg_track_get_i2c_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_wls_trx_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_gpio_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_pmic_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_gague_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_obtain_wls_general_crux_info(char *crux_info, int len);
int oplus_chg_track_get_cp_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_bidirect_cp_err_reason(int err_type, char * err_reason, int len);
int oplus_chg_track_get_mos_err_reason(int err_type, char *err_reason, int len);
void oplus_chg_track_record_chg_type_info(void);
void oplus_chg_track_record_ffc_start_info(void);
void oplus_chg_track_record_ffc_end_info(void);
void oplus_chg_track_aging_ffc_trigger(bool ffc1_stage);
int oplus_chg_track_obtain_general_info(u8 *curx, int index, int len);
int oplus_chg_track_get_ufcs_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_get_cooldown_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_get_track_pen_match_err_reason(int err_type, char *err_reason, int len);
int oplus_chg_track_set_hidl_info(const char *buf, size_t count);
#endif

#ifndef __AW87XXX_MONITOR_H__
#define __AW87XXX_MONITOR_H__

/**********************************************************
 * aw87xxx monitor
***********************************************************/
static const char aw87xxx_vmax_cfg_name[] = { "aw87xxx_vmax_" };

/**********************************************************
 * aw87xxx dsp
***********************************************************/
//#define AWINIC_ADSP_ENABLE
/*#define AWINIC_DSP_MSG*/
/*#define AWINIC_DSP_HMUTE*/
#define AWINIC_DSP_MSG_HDR_VER (1)

#define INLINE_PARAM_ID_ENABLE_CALI			(0x00000001)
#define INLINE_PARAM_ID_ENABLE_HMUTE			(0x00000002)
#define INLINE_PARAM_ID_F0_Q				(0x00000003)
#define INLINE_PARAM_ID_ACTIVE_FLAG			(0x00000004)

#define AFE_PARAM_ID_AWDSP_RX_SET_ENABLE		(0x10013D11)
#define AFE_PARAM_ID_AWDSP_RX_PARAMS			(0x10013D12)
#define AFE_PARAM_ID_AWDSP_TX_SET_ENABLE		(0x10013D13)
#define AFE_PARAM_ID_AWDSP_RX_VMAX_L			(0X10013D17)
#define AFE_PARAM_ID_AWDSP_RX_VMAX_R			(0X10013D18)
#define AFE_PARAM_ID_AWDSP_RX_CALI_CFG_L		(0X10013D19)
#define AFE_PARAM_ID_AWDSP_RX_CALI_CFG_R		(0x10013d1A)
#define AFE_PARAM_ID_AWDSP_RX_RE_L			(0x10013d1B)
#define AFE_PARAM_ID_AWDSP_RX_RE_R			(0X10013D1C)
#define AFE_PARAM_ID_AWDSP_RX_NOISE_L			(0X10013D1D)
#define AFE_PARAM_ID_AWDSP_RX_NOISE_R			(0X10013D1E)
#define AFE_PARAM_ID_AWDSP_RX_F0_L			(0X10013D1F)
#define AFE_PARAM_ID_AWDSP_RX_F0_R			(0X10013D20)
#define AFE_PARAM_ID_AWDSP_RX_REAL_DATA_L		(0X10013D21)
#define AFE_PARAM_ID_AWDSP_RX_REAL_DATA_R		(0X10013D22)
#define AFE_PARAM_ID_AWDSP_RX_MSG			(0X10013D2A)

#define AW87XXX_MONITOR_DEFAULT_FLAG			0
#define AW87XXX_MONITOR_DEFAULT_TIMER_VAL		30000
#define AW87XXX_MONITOR_DEFAULT_TIMER_COUNT		2

#define AW87XXX_VBAT_CAPACITY_MIN			0
#define AW87XXX_VBAT_CAPACITY_MAX			100
#define AW_VMAX_INIT_VAL				(0xFFFFFFFF)

enum aw87519_dsp_msg_type {
	DSP_MSG_TYPE_DATA = 0,
	DSP_MSG_TYPE_CMD = 1,
};

enum aef_module_type {
	AW_RX_MODULE = 0,
	AW_TX_MODULE = 1,
};

enum aw_monitor_first_enter {
	AW_FIRST_ENTRY = 0,
	AW_NOT_FIRST_ENTRY = 1,
};

struct vmax_single_config {
	uint32_t min_thr;
	uint32_t vmax;
};

struct vmax_config {
	int vmax_cfg_num;
	struct vmax_single_config vmax_cfg_total[];
};

struct aw87xxx_monitor {
	uint8_t first_entry;
	uint8_t timer_cnt;
	uint8_t cfg_update_flag;
	uint8_t update_num;
	uint32_t monitor_flag;
	uint32_t timer_cnt_max;
	uint32_t timer_val;
	uint32_t vbat_sum;
	uint32_t custom_capacity;
	uint32_t pre_vmax;

	struct delayed_work work;
	struct vmax_config *vmax_cfg;
};

enum afe_param_id_awdsp {
	INDEX_PARAMS_ID_RX_PARAMS = 0,
	INDEX_PARAMS_ID_RX_ENBALE,
	INDEX_PARAMS_ID_TX_ENABLE,
	INDEX_PARAMS_ID_RX_VMAX,
	INDEX_PARAMS_ID_RX_CALI_CFG,
	INDEX_PARAMS_ID_RX_RE,
	INDEX_PARAMS_ID_RX_NOISE,
	INDEX_PARAMS_ID_RX_F0,
	INDEX_PARAMS_ID_RX_REAL_DATA,
	INDEX_PARAMS_ID_AWDSP_RX_MSG,
	INDEX_PARAMS_ID_MAX
};

static const uint32_t PARAM_ID_INDEX_TABLE[][INDEX_PARAMS_ID_MAX] = {
	{
	 AFE_PARAM_ID_AWDSP_RX_PARAMS,
	 AFE_PARAM_ID_AWDSP_RX_SET_ENABLE,
	 AFE_PARAM_ID_AWDSP_TX_SET_ENABLE,
	 AFE_PARAM_ID_AWDSP_RX_VMAX_L,
	 AFE_PARAM_ID_AWDSP_RX_CALI_CFG_L,
	 AFE_PARAM_ID_AWDSP_RX_RE_L,
	 AFE_PARAM_ID_AWDSP_RX_NOISE_L,
	 AFE_PARAM_ID_AWDSP_RX_F0_L,
	 AFE_PARAM_ID_AWDSP_RX_REAL_DATA_L,
	 AFE_PARAM_ID_AWDSP_RX_MSG,
	 },
	{
	 AFE_PARAM_ID_AWDSP_RX_PARAMS,
	 AFE_PARAM_ID_AWDSP_RX_SET_ENABLE,
	 AFE_PARAM_ID_AWDSP_TX_SET_ENABLE,
	 AFE_PARAM_ID_AWDSP_RX_VMAX_R,
	 AFE_PARAM_ID_AWDSP_RX_CALI_CFG_R,
	 AFE_PARAM_ID_AWDSP_RX_RE_R,
	 AFE_PARAM_ID_AWDSP_RX_NOISE_R,
	 AFE_PARAM_ID_AWDSP_RX_F0_R,
	 AFE_PARAM_ID_AWDSP_RX_REAL_DATA_R,
	 AFE_PARAM_ID_AWDSP_RX_MSG,
	 },
};

/**********************************************************
 * aw87xxx monitor function
***********************************************************/
void aw87xxx_monitor_stop(struct aw87xxx_monitor *monitor);
void aw87xxx_monitor_init(struct aw87xxx_monitor *monitor);
void aw87xxx_parse_monitor_dt(struct aw87xxx_monitor *monitor);
void aw87xxx_monitor_work_func(struct work_struct *work);

/********************************************
 * dsp function
 *******************************************/
int aw_get_dsp_msg_data(char *data_ptr, int data_size, int inline_id,
			int channel);

#endif

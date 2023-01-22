/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_PREVENTION_H_
#define _TOUCHPANEL_PREVENTION_H_

#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#define TOUCH_MAX_NUM               (10)
#define GRIP_TAG_SIZE               (64)
#define MAX_STRING_CNT              (15)
#define MAX_AREA_PARAMETER          (10)
#define UP2CANCEL_PRESSURE_VALUE    (0xFF)

typedef enum edge_grip_side {
	TYPE_UNKNOW,
	TYPE_LONG_SIDE,
	TYPE_SHORT_SIDE,
	TYPE_LONG_CORNER_SIDE,
	TYPE_SHORT_CORNER_SIDE,
} grip_side;

/*shows a rectangle
* x start at @start_x and end at @start_x+@x_width
* y start at @start_y and end at @start_y+@y_width
* @area_list: list used for connected with other area
* @start_x: original x coordinate
* @start_y: original y coordinate
* @x_width: witdh in x axis
* @y_width: witdh in y axis
*/
struct grip_zone_area {
	struct list_head area_list;
	/*must be the first member to easy get the pointer of grip_zone_area*/
	uint16_t                start_x;
	uint16_t                start_y;
	uint16_t                x_width;
	uint16_t                y_width;
	uint16_t                exit_thd;
	uint16_t                exit_tx_er;
	uint16_t                exit_rx_er;
	uint16_t                support_dir;
	uint16_t                grip_side;
	char                    name[GRIP_TAG_SIZE];
};

struct coord_buffer {
	uint16_t            x;
	uint16_t            y;
	uint16_t            weight;
};

enum large_judge_status {
	JUDGE_LARGE_CONTINUE,
	JUDGE_LARGE_TIMEOUT,
	JUDGE_LARGE_OK,
};

enum large_reject_type {
	TYPE_REJECT_NONE,
	TYPE_REJECT_HOLD,
	TYPE_REJECT_DONE,
};

enum large_finger_status {
	TYPE_NORMAL_FINGER,
	TYPE_HOLD_FINGER,
	TYPE_EDGE_FINGER,
	TYPE_PALM_SHORT_SIZE,
	TYPE_PALM_LONG_SIZE,
	TYPE_SMALL_PALM_CORNER,
	TYPE_PALM_CORNER,
	TYPE_LARGE_PALM_CORNER,

	TYPE_LONG_AROUND_TOUCH,
	TYPE_LONG_FINGER_HOLD,
	TYPE_LONG_EDGE_FINGER,
	TYPE_LONG_CENTER_DOWN,
	TYPE_SHORT_AROUND_TOUCH,
	TYPE_SHORT_FINGER_HOLD,
	TYPE_SHORT_EDGE_FINGER,
	TYPE_SHORT_CENTER_DOWN,
	TYPE_CORNER_SHAPE_SIZE,
	TYPE_CORNER_LARGE_SIZE,
	TYPE_CORNER_EDGE_FINGER,
	TYPE_CORNER_CENTER_DOWN,
	TYPE_CORNER_SHORT_MOVE,
	TYPE_CORNER_MISTOUCH_AGAIN,
	TYPE_TOP_LONG_PRESS,
	TYPE_ALL_SHORT_CLICK,
	TYPE_LONG_EDGE_TOUCH,
	TYPE_SHORT_EDGE_TOUCH,
	TYPE_TOP_SHORT_MOVE,
};

enum large_point_status {
	UP_POINT,
	DOWN_POINT,
	DOWN_POINT_NEED_MAKEUP,
};

enum grip_diable_level {
	GRIP_DISABLE_LARGE,
	GRIP_DISABLE_ELI,
	GRIP_DISABLE_UP2CANCEL,
};

typedef enum grip_operate_cmd {
	OPERATE_UNKNOW,
	OPERATE_ADD,
	OPERATE_DELTE,
	OPERATE_MODIFY,
} operate_cmd;

typedef enum grip_operate_object {
	OBJECT_UNKNOW,
	OBJECT_PARAMETER,
	OBJECT_PARAMETER_V2,
	OBJECT_LONG_CURVED_PARAMETER,    /* for modify curved srceen judge para for long side */
	OBJECT_SHORT_CURVED_PARAMETER,   /* for modify curved srceen judge para for short side */
	OBJECT_CONDITION_AREA,      /* for modify condition area */
	OBJECT_LARGE_AREA,          /* for modify large judge area */
	OBJECT_ELI_AREA,            /* for modify elimination area */
	OBJECT_DEAD_AREA,           /* for modify dead area */
	OBJECT_SKIP_HANDLE,         /* for modify no handle setting */
	OBJECT_EDGE_LIMIT,
} operate_oject;

enum grip_position {
	POS_UNDEFINE = 0,
	POS_CENTER_INNER = 1,
	POS_LONG_LEFT,
	POS_LONG_RIGHT,
	POS_SHORT_LEFT,
	POS_SHORT_RIGHT,
	POS_VERTICAL_LEFT_CORNER,
	POS_VERTICAL_RIGHT_CORNER,
	POS_VERTICAL_LEFT_TOP,
	POS_VERTICAL_RIGHT_TOP,
	POS_HORIZON_B_LEFT_CORNER,
	POS_HORIZON_B_RIGHT_CORNER,
	POS_HORIZON_T_LEFT_CORNER,
	POS_HORIZON_T_RIGHT_CORNER,
	POS_HORIZON_B_LEFT_TOP,
	POS_HORIZON_B_RIGHT_TOP,
	POS_HORIZON_T_LEFT_TOP,
	POS_HORIZON_T_RIGHT_TOP,
};

struct grip_point_info {
	uint16_t x;
	uint16_t y;
	uint8_t  tx_press;
	uint8_t  rx_press;
	uint8_t  tx_er;
	uint8_t  rx_er;
	s64 time_ms;  /* record the point first down time */
};

struct curved_judge_para {
	uint16_t edge_finger_thd;
	uint16_t hold_finger_thd;
	uint16_t normal_finger_thd_1;
	uint16_t normal_finger_thd_2;
	uint16_t normal_finger_thd_3;
	uint16_t large_palm_thd_1;
	uint16_t large_palm_thd_2;
	uint16_t palm_thd_1;
	uint16_t palm_thd_2;
	uint16_t small_palm_thd_1;
	uint16_t small_palm_thd_2;
};

typedef enum point_info_type {
	TYPE_START_POINT = 1,               /* for record first frame point */
	TYPE_SECOND_POINT,                  /* for record second frame point */
	TYPE_LAST_POINT,                    /* for record last frame points */
	TYPE_LATEST_POINT,                  /* for record latest points */
	TYPE_MAX_TX_POINT,                  /* for record max tx frame points */
	TYPE_MAX_RX_POINT,                  /* for record max rx frame points */
	TYPE_INIT_TX_POINT,                 /* for record init rx points */
	TYPE_INIT_RX_POINT,                 /* for record init rx points */
	TYPE_RX_CHANGED_POINT,              /*  for record rx changed frame points */
	TYPE_TX_CHANGED_POINT,              /*  for record tx changed frame points */
} point_info_type;

enum center_down_status {
	STATUS_CENTER_UNKNOW = 0,
	STATUS_CENTER_UP,
	STATUS_CENTER_DOWN,
};

enum corner_judge_shape {
	CORNER_SHAPE_NONE = 0,
	CORNER_SHAPE_LARGE,
	CORNER_SHAPE_RATIO,
};

struct key_addr {
	char name[64];
	uint16_t *addr;
};

#define MAKEUP_REAL_POINT (0xFF)
#define POINT_DIFF_CNT  10
struct kernel_grip_info {
	int
	touch_dir;                              /*shows touchpanel direction*/
	uint32_t
	max_x;                                  /*touchpanel width*/
	uint32_t
	max_y;                                  /*touchpanel height*/
	int
	tx_num;                                 /*touchpanel tx num*/
	int
	rx_num;                                 /*touchpanel rx num*/
	struct mutex
		grip_mutex;                         /*using for protect grip parameter working*/
	uint32_t
	no_handle_y1;                           /*min y of no grip handle*/
	uint32_t
	no_handle_y2;                           /*max y of no grip handle*/
	uint8_t
	no_handle_dir;                          /*show which side no grip handle*/
	uint8_t
	grip_disable_level;                     /*show whether if do grip handle*/
	uint8_t
	record_total_cnt;                       /*remember total count*/

	struct grip_point_info
		first_point[TOUCH_MAX_NUM];         /*store the fist frame point of each ID*/
	struct grip_point_info
		second_point[TOUCH_MAX_NUM];        /*store the second frame point of each ID*/
	struct grip_point_info
		latest_points[TOUCH_MAX_NUM][POINT_DIFF_CNT];             /*store the latest different 5 points of each ID*/
	bool
	sync_up_makeup[TOUCH_MAX_NUM];          /*shows this point need make up while report up*/
	bool
	dead_out_status[TOUCH_MAX_NUM];         /*show if exit the dead grip*/
	struct list_head
		dead_zone_list;                     /*list all area using dead grip strategy*/

	uint16_t frame_cnt[TOUCH_MAX_NUM];      /*show down frame of each id*/
	int obj_prev_bit;                       /*show last frame obj attention*/
	int obj_bit_rcd;                        /*show current frame obj attention for record*/
	int obj_prced_bit_rcd;                  /*show current frame processed obj attention for record*/
	uint16_t coord_filter_cnt;              /*cnt of make up points*/
	struct coord_buffer
		*coord_buf;                         /*store point and its weight to do filter*/
	bool
	large_out_status[TOUCH_MAX_NUM];        /*show if exit the large area grip*/
	uint8_t
	large_reject[TOUCH_MAX_NUM];            /*show if rejected state*/
	uint8_t
	large_finger_status[TOUCH_MAX_NUM];     /*show large area finger status for grip strategy*/
	uint8_t
	large_point_status[TOUCH_MAX_NUM];      /*parameter for judge if ponits in large area need make up or not*/
	uint16_t
	large_detect_time_ms;                   /*large area detection time limit in ms*/
	struct list_head
		large_zone_list;                    /*list all area using large area grip strategy*/
	struct list_head
		condition_zone_list;                /*list all area using conditional grip strategy*/
	bool
	condition_out_status[TOUCH_MAX_NUM];    /*show if exit the conditional rejection*/
	uint8_t
	makeup_cnt[TOUCH_MAX_NUM];              /*show makeup count of each id*/
	uint16_t
	large_frame_limit;                      /*max time to judge big area*/
	uint16_t
	large_ver_thd;                          /*threshold to determine large area size in vetical*/
	uint16_t
	large_hor_thd;                          /*threshold to determine large area size in horizon*/
	uint16_t
	large_corner_frame_limit;               /*max time to judge big area in corner area*/
	uint16_t
	large_ver_corner_thd;                   /*threshold to determine large area size in vetical corner*/
	uint16_t
	large_hor_corner_thd;                   /*threshold to determine large area size in horizon corner*/
	uint16_t
	large_ver_corner_width;                 /*threshold to determine should be judged in vertical corner way*/
	uint16_t
	large_hor_corner_width;                 /*threshold to determine should be judged in horizon corner way*/
	uint16_t
	large_corner_distance;                  /*threshold to judge move from edge*/

	struct curved_judge_para
		curved_long_side_para;              /*parameter for curved touchscreen long side judge*/
	struct curved_judge_para
		curved_short_side_para;             /*parameter for curved touchscreen short side judge*/
	bool
	is_curved_screen;                       /*curved screen judge flag*/
	s64
	lastest_down_time_ms;                   /*record the lastest down time out of large judged*/
	s64
	down_delta_time_ms;                     /*threshold to judge whether need to make up point*/
	bool point_unmoved[TOUCH_MAX_NUM];      /*show point have already moved*/
	uint8_t condition_frame_limit;          /*keep rejeected while beyond this time*/
	unsigned long condition_updelay_ms;     /*time after to report touch up*/
	struct kfifo up_fifo;                   /*store up touch id according  to the sequence*/
	struct hrtimer grip_up_timer[TOUCH_MAX_NUM];     /*using for report touch up event*/
	bool grip_hold_status[TOUCH_MAX_NUM];            /*show if this id is in hold status*/
	struct work_struct grip_up_work[TOUCH_MAX_NUM];  /*using for report touch up*/
	struct workqueue_struct *grip_up_handle_wq;      /*just for handle report up event*/

	bool eli_out_status[TOUCH_MAX_NUM];     /*store cross range status of each id*/
	bool eli_reject_status[TOUCH_MAX_NUM];  /*show reject status if each id*/
	struct list_head elimination_zone_list;
	/*list all area using elimination strategy*/

	bool grip_handle_in_fw;  /*show whether we should handle prevention in fw*/
	bool dir_change_set_grip;
	struct fw_grip_operations *fw_ops;      /*fw grip setting func call back*/
	struct touchpanel_data *p_ts;           /*record the ts address*/
	int work_id;
	bool
	is_curved_screen_V2;                    /*curved screen judge flag using new grip function*/
	uint8_t
	exit_match_times[TOUCH_MAX_NUM];        /*record actual match the exit condition times*/
	struct grip_point_info
		txMax_frame_point[TOUCH_MAX_NUM];       /*record the max tx frame point info*/
	struct grip_point_info
		rxMax_frame_point[TOUCH_MAX_NUM];       /*record the max rx frame point info*/
	struct grip_point_info
		last_frame_point[TOUCH_MAX_NUM];        /*record the last frame point info*/
	struct grip_point_info
		rxChanged_frame_point[TOUCH_MAX_NUM];   /*record the rx changed frame point info*/
	struct grip_point_info
		txChanged_frame_point[TOUCH_MAX_NUM];   /*record the tx changed frame point info*/
	uint32_t
	fsr_stable_time[TOUCH_MAX_NUM];         /*record fsr stable time under detect period*/
	uint8_t
	points_pos[TOUCH_MAX_NUM];              /*record point area distribution*/
	uint8_t
	points_center_down[TOUCH_MAX_NUM];      /*record whether points down after center point down*/
	uint8_t
	last_large_reject[TOUCH_MAX_NUM];       /*show last touch rejected state*/
	uint8_t
	last_points_pos[TOUCH_MAX_NUM];         /*show last touch point position*/
	uint16_t
	large_corner_exit_distance;             /*large corner distance exit condition*/
	uint16_t
	large_corner_detect_time_ms;            /*large corner judge detect time*/
	uint16_t
	large_corner_debounce_ms;               /*large corner judge center down debounce time interval*/
	uint16_t
	large_corner_width;                     /*large corner width*/
	uint16_t
	large_corner_height;                    /*large corner height*/
	uint16_t
	xfsr_corner_exit_thd;                   /*corner exit fsr threshold of x direction*/
	uint16_t
	yfsr_corner_exit_thd;                   /*corner exit fsr threshold of y direction*/
	uint16_t
	exit_match_thd;                         /*exit match times threshold*/
	uint16_t
	rx_reject_thd;                          /*rx reject threshold*/
	uint16_t
	tx_reject_thd;                          /*tx reject threshold*/
	uint16_t
	trx_reject_thd;                         /*tx and rx reject threshold*/
	uint16_t
	fsr_stable_time_thd;                    /*fsr stable time threshold*/
	uint16_t
	single_channel_x_len;                   /*single channel x coordinate*/
	uint16_t
	single_channel_y_len;                   /*single channel y coordinate*/
	uint16_t
	normal_tap_min_time_ms;                 /*normal touch min time interval*/
	uint16_t
	normal_tap_max_time_ms;                 /*normal touch max time interval*/
	uint16_t
	long_start_coupling_thd;                /*long start threshold of rx_er*rx_press*/
	uint16_t
	long_stable_coupling_thd;               /*long stable threshold of rx_er*rx_press*/
	uint16_t
	long_detect_time_ms;                    /*long detect time threshold*/
	uint16_t
	long_hold_changed_thd;                  /*long stable rx_er changed threshold*/
	uint16_t
	long_hold_maxfsr_gap;                   /*long hold fsr gap max setting*/
	uint16_t
	long_hold_divided_factor;               /*long hold neighbouring range setting, default 6*/
	uint16_t
	long_hold_debounce_time_ms;             /*long hold debounce time of judge*/
	uint16_t
	xfsr_normal_exit_thd;                   /*normal exit fsr of x direction*/
	uint16_t
	yfsr_normal_exit_thd;                   /*normal exit fsr of y direction*/
	uint16_t
	xfsr_hold_exit_thd;                     /*finger hold exit fsr of x direction*/
	uint16_t
	yfsr_hold_exit_thd;                     /*finger hold exit fsr of y direction*/
	uint16_t
	large_reject_debounce_time_ms;          /*landed in large reject down or up debounce time will be rejected also*/
	uint16_t
	report_updelay_ms;                      /*time after to report touch up*/
	uint16_t
	short_start_coupling_thd;               /*short start threshold of tx_er*tx_press*/
	uint16_t
	short_stable_coupling_thd;              /*short stable threshold of tx_er*tx_press*/
	uint16_t
	short_hold_changed_thd;                 /*short stable tx_er changed threshold*/
	uint16_t
	short_hold_maxfsr_gap;                  /*short hold fsr gap max setting*/
	uint16_t
	large_top_width;                        /*large top corner width*/
	uint16_t
	large_top_height;                       /*large top corner height*/
	uint16_t
	large_top_exit_distance;                /*large top corner exit distance*/
	uint16_t
	edge_swipe_narrow_witdh;                /*normal edge swipe width limit*/
	uint16_t
	edge_swipe_exit_distance;               /*normal edge swipe distance threshold*/
	uint16_t
	long_strict_start_coupling_thd;         /*long side strict start coupling threshold*/
	uint16_t
	long_strict_stable_coupling_thd;        /*long side strict stable coupling threshold*/
	uint16_t
	rx_strict_reject_thd;                   /*long side strict rx reject threshold*/
	uint16_t
	tx_strict_reject_thd;                   /*short side strict tx reject threshold*/
	uint16_t
	trx_strict_reject_thd;                  /*tx and rx add strict reject threshold**/
	uint16_t
	short_strict_start_coupling_thd;        /*short side strict start coupling threshold*/
	uint16_t
	short_strict_stable_coupling_thd;       /*short side strict stable coupling threshold*/
	uint16_t
	xfsr_strict_exit_thd;                   /*strict exit fsr of x direction*/
	uint16_t
	yfsr_strict_exit_thd;                   /*strict exit fsr of y direction*/
	uint16_t
	corner_move_rejected;                   /*flag to show whether we need to do corner move reject*/
};

struct fw_grip_operations {
	int (*set_fw_grip_area)(struct grip_zone_area *grip_zone, bool enable);     /*set the fw grip area*/
	void (*set_touch_direction)(uint8_t dir);                                   /*set touch direction in fw*/
	int (*set_no_handle_area)(struct kernel_grip_info *grip_info);              /*set no handle area in fw*/
	int (*set_condition_frame_limit)(int frame);                                /*set condition frame limit in fw*/
	int (*set_large_frame_limit)(int frame);                                    /*set large frame limit in fw*/
	int (*set_large_corner_frame_limit)(int frame);                             /*set large condition frame limit in fw*/
	int (*set_large_ver_thd)(int thd);                                          /*set large ver thd in fw*/
};

struct kernel_grip_info *kernel_grip_init(struct device *dev);
void init_kernel_grip_proc(struct proc_dir_entry *prEntry_tp, struct kernel_grip_info *grip_info);
void grip_status_reset(struct kernel_grip_info *grip_info, uint8_t index);
void kernel_grip_reset(struct kernel_grip_info *grip_info);
int kernel_grip_print_func(struct seq_file *s, struct kernel_grip_info *grip_info);

#endif /*_TOUCHPANEL_PREVENTION_H_*/

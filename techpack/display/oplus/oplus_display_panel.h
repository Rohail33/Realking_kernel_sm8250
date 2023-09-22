/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_display_panel.h
** Description : oplus display panel char dev  /dev/oplus_panel
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_DISPLAY_PANEL_H_
#define _OPLUS_DISPLAY_PANEL_H_

#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <asm/ioctl.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
/*#include <oplus_display_dc.h>
 *#include <oplus_display_common.h>
 *#include <oplus_display_alwaysondisplay.h>
 #include <oplus_display_onscreenfingerprint.h>*/

#define OPLUS_PANEL_NAME "oplus_display"
#define OPLUS_PANEL_CLASS_NAME "oplus_display_class"

#define OPLUS_PANEL_IOCTL_BASE			'o'

#define PANEL_IO(nr)			_IO(OPLUS_PANEL_IOCTL_BASE, nr)
#define PANEL_IOR(nr, type)		_IOR(OPLUS_PANEL_IOCTL_BASE, nr, type)
#define PANEL_IOW(nr, type)		_IOW(OPLUS_PANEL_IOCTL_BASE, nr, type)
#define PANEL_IOWR(nr, type)		_IOWR(OPLUS_PANEL_IOCTL_BASE, nr, type)

#define PANEL_IOCTL_NR(n)       _IOC_NR(n)
#define PANEL_IOCTL_SIZE(n)		_IOC_SIZE(n)

typedef int oplus_panel_feature(void *data);

static dev_t dev_num = 0;
static struct class *panel_class;
static struct device *panel_dev;
static int panel_ref = 0;
static struct cdev panel_cdev;

#define APOLLO_BACKLIGHT_LENS 4096*19

enum APOLLO_BL_ID : int {
	APOLLO_BL_4096 = 4096,
	APOLLO_BL_8192 = 8192,
	APOLLO_BL_14336 = 14336,
	APOLLO_BL_18432 = 18432,
};

struct oplus_apollo_backlight_list {
	bool bl_fix; //for 4096/8192 fix
	int bl_id_lens;    //1 for 4096, 2 for 8192;
	int bl_level_last;
	int bl_index_last;
	int buf_size;
	void *vaddr; //dmabuf virtual address
	unsigned short *apollo_bl_list;
	unsigned short *panel_bl_list;
	struct dma_buf *dmabuf;
};

struct apollo_backlight_map_value
{
	int index; //backlight index
	int bl_level; //the value of the index
	int apollo_bl_level;
};
struct panel_ioctl_desc {
	unsigned int cmd;
	oplus_panel_feature *func;
	const char *name;
};

struct softiris_color
{
	uint32_t color_vivid_status;
	uint32_t color_srgb_status;
	uint32_t color_softiris_status;
};

/*oplus ioctl case start, please keep pace with DisplayPanelInterface.cpp*/
#define PANEL_COMMOND_BASE 0x00
#define PANEL_COMMOND_MAX  0xBC

#define PANEL_IOCTL_SET_POWER				  PANEL_IOW(0x01, struct panel_vol_set)
#define PANEL_IOCTL_GET_POWER				  PANEL_IOWR(0x02, struct panel_vol_get)
#define PANEL_IOCTL_SET_SEED				  PANEL_IOW(0x03, unsigned int)
#define PANEL_IOCTL_GET_SEED				  PANEL_IOWR(0x04, unsigned int)
#define PANEL_IOCTL_GET_PANELID			      PANEL_IOWR(0x05, struct panel_id)
#define PANEL_IOCTL_SET_FFL				      PANEL_IOW(0x06, unsigned int)
#define PANEL_IOCTL_GET_FFL				      PANEL_IOWR(0x07, unsigned int)
#define PANEL_IOCTL_SET_AOD				      PANEL_IOW(0x08, unsigned int)
#define PANEL_IOCTL_GET_AOD				      PANEL_IOWR(0x09, unsigned int)
#define PANEL_IOCTL_SET_MAX_BRIGHTNESS		  PANEL_IOW(0x0A, unsigned int)
#define PANEL_IOCTL_GET_MAX_BRIGHTNESS		  PANEL_IOWR(0x0B, unsigned int)
#define PANEL_IOCTL_GET_PANELINFO			  PANEL_IOWR(0x0C, struct panel_info)
#define PANEL_IOCTL_GET_CCD				      PANEL_IOWR(0x0D, unsigned int)
#define PANEL_IOCTL_GET_SERIAL_NUMBER		  PANEL_IOWR(0x0E, struct panel_serial_number)
#define PANEL_IOCTL_SET_HBM				      PANEL_IOW(0x0F, unsigned int)
#define PANEL_IOCTL_GET_HBM				      PANEL_IOWR(0x10, unsigned int)
#define PANEL_IOCTL_SET_DIM_ALPHA			  PANEL_IOW(0x11, unsigned int)
#define PANEL_IOCTL_GET_DIM_ALPHA			  PANEL_IOWR(0x12, unsigned int)
#define PANEL_IOCTL_SET_DIM_DC_ALPHA		  PANEL_IOW(0x13, unsigned int)
#define PANEL_IOCTL_GET_DIM_DC_ALPHA		  PANEL_IOWR(0x14, unsigned int)
#define PANEL_IOCTL_SET_AUDIO_READY  		  PANEL_IOW(0x15, unsigned int)
#define PANEL_IOCTL_GET_DISPLAY_TIMING_INFO   PANEL_IOWR(0x16, struct display_timing_info)
#define PANEL_IOCTL_GET_PANEL_DSC 			  PANEL_IOWR(0x17, unsigned int)
#define PANEL_IOCTL_SET_POWER_STATUS		  PANEL_IOW(0x18, unsigned int)
#define PANEL_IOCTL_GET_POWER_STATUS		  PANEL_IOWR(0x19, unsigned int)
#define PANEL_IOCTL_SET_REGULATOR_CONTROL     PANEL_IOW(0x1A, unsigned int)
#define PANEL_IOCTL_SET_CLOSEBL_FLAG		  PANEL_IOW(0x1B, unsigned int)
#define PANEL_IOCTL_GET_CLOSEBL_FLAG		  PANEL_IOWR(0x1C, unsigned int)
#define PANEL_IOCTL_SET_PANEL_REG   		  PANEL_IOW(0x1D, struct panel_reg_rw)
#define PANEL_IOCTL_GET_PANEL_REG   		  PANEL_IOWR(0x1E, struct panel_reg_get)
#define PANEL_IOCTL_SET_DIMLAYER_HBM		  PANEL_IOW(0x1F, unsigned int)
#define PANEL_IOCTL_GET_DIMLAYER_HBM		  PANEL_IOWR(0x20, unsigned int)
#define PANEL_IOCTL_SET_DIMLAYER_BL_EN		  PANEL_IOW(0x21, unsigned int)
#define PANEL_IOCTL_GET_DIMLAYER_BL_EN		  PANEL_IOWR(0x22, unsigned int)
#define PANEL_IOCTL_SET_PANEL_BLANK           PANEL_IOW(0x23, unsigned int)
#define PANEL_IOCTL_SET_SPR		              PANEL_IOW(0x24, unsigned int)
#define PANEL_IOCTL_GET_SPR		              PANEL_IOWR(0x25, unsigned int)
#define PANEL_IOCTL_GET_ROUNDCORNER	          PANEL_IOWR(0x26, unsigned int)
#define PANEL_IOCTL_SET_DYNAMIC_OSC_CLOCK	  PANEL_IOW(0x27, unsigned int)
#define PANEL_IOCTL_GET_DYNAMIC_OSC_CLOCK	  PANEL_IOWR(0x28, unsigned int)
#define PANEL_IOCTL_SET_FP_PRESS              PANEL_IOW(0x29, unsigned int)
#define PANEL_IOCTL_SET_OPLUS_BRIGHTNESS      PANEL_IOW(0x2A, unsigned int)
#define PANEL_IOCTL_GET_OPLUS_BRIGHTNESS      PANEL_IOWR(0x2B, unsigned int)
#define PANEL_IOCTL_SET_LCM_CABC              PANEL_IOW(0x2C, unsigned int)
#define PANEL_IOCTL_GET_LCM_CABC              PANEL_IOWR(0x2D, unsigned int)
#define PANEL_IOCTL_SET_AOD_AREA              PANEL_IOW(0x2E, struct panel_aod_area_para)
/* Apollo DC backlight */
#define PANEL_IOCTL_SET_DC_REAL_BACKLIGHT     PANEL_IOWR(0x37, unsigned int)
#define PANEL_IOCTL_SET_APOLLO_BACKLIGHT      PANEL_IOW(0x51, struct apollo_backlight_map_value)
#define PANEL_IOCTL_GET_SOFTIRIS_COLOR        PANEL_IOWR(0x53, struct softiris_color)
#define PANEL_IOCTL_SET_DITHER_STATUS        PANEL_IOWR(0x54, unsigned int)
#define PANEL_IOCTL_GET_DITHER_STATUS        PANEL_IOWR(0x55, unsigned int)
#ifdef OPLUS_FEATURE_ADFR
#define PANEL_IOCTL_SET_TE_REFCOUNT_ENABLE	  PANEL_IOW(0x56, unsigned int)
#define PANEL_IOCTL_GET_TE_REFCOUNT_ENABLE	  PANEL_IOWR(0x57, unsigned int)
#endif
#define PANEL_IOCTL_SET_CABC_STATUS              PANEL_IOW(0x59, unsigned int)
#define PANEL_IOCTL_GET_CABC_STATUS              PANEL_IOWR(0x5A, unsigned int)
#define PANEL_IOCTL_SET_FP_TYPE               PANEL_IOW(0x64, unsigned int)
#define PANEL_IOCTL_GET_FP_TYPE               PANEL_IOWR(0x65, unsigned int)
/*oplus ioctl case end*/

#endif /*_OPLUS_DISPLAY_PANEL_H_*/

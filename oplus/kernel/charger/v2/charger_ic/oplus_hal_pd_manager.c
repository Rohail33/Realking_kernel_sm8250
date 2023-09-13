// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[PD_MANAGER]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/extcon-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb/typec.h>
#include <linux/version.h>

#include <tcpci.h>
#include <tcpm.h>
#include <tcpci_typec.h>

#include <oplus_chg_module.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>

#define PROBE_CNT_MAX			100
/* 10ms * 100 = 1000ms = 1s */
#define USB_TYPE_POLLING_INTERVAL	10
#define USB_TYPE_POLLING_CNT_MAX	100
#define BC12_TIMEOUT_MS			1500
#define MICRO_5V 			5000

enum dr {
	DR_IDLE,
	DR_DEVICE,
	DR_HOST,
	DR_DEVICE_TO_HOST,
	DR_HOST_TO_DEVICE,
	DR_MAX,
};

static char *dr_names[DR_MAX] = {
	"Idle", "Device", "Host", "Device to Host", "Host to Device",
};

struct pd_manager_chip {
	struct device *dev;
	struct extcon_dev *extcon;
	struct oplus_chg_ic_dev *ic_dev;
	struct delayed_work usb_dwork;
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	enum dr usb_dr;
	int usb_type_polling_cnt;
	int sink_mv_new;
	int sink_ma_new;
	int sink_mv_old;
	int sink_ma_old;

	struct typec_capability typec_caps;
	struct typec_port *typec_port;
	struct typec_partner *partner;
	struct typec_partner_desc partner_desc;
	struct usb_pd_identity partner_identity;

	struct delayed_work bc12_wait_work;
	struct delayed_work vconn_wait_work;

	struct oplus_mms *wired_topic;
	struct mms_subscribe *wired_subs;
	int chg_type;
	int pd_type;
	int voltage_max_mv;
	int voltage_min_mv;
	int current_max_ma;
	bool otg_enable;
	enum typec_data_role data_role;

	bool bc12_completed;
	bool bc12_ready;
	bool start_peripheral;
	bool first_check;
	bool pd_svooc;
	bool svid_completed;
};

static const unsigned int rpm_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static bool oplus_pd_without_usb(struct pd_manager_chip *chip)
{
	if (!tcpm_inquire_pd_connected(chip->tcpc))
		return true;
	return (tcpm_inquire_dpm_flags(chip->tcpc) &
		DPM_FLAGS_PARTNER_USB_COMM) ?
		       false : true;
}

static void tcpc_set_pd_type(struct pd_manager_chip *chip, int type)
{
	if (type != OPLUS_CHG_USB_TYPE_UNKNOWN && !oplus_pd_without_usb(chip))
		type = OPLUS_CHG_USB_TYPE_PD_SDP;

	if (chip->pd_type == type)
		return;
	chg_info("pd_type = %d\n", type);
	chip->pd_type = type;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
}

static void tcpc_set_otg_enbale(struct pd_manager_chip *chip, bool en)
{
	if (chip->otg_enable == en)
		return;
	chg_info("otg_enable = %d\n", en);
	chip->otg_enable = en;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_OTG_ENABLE);
}

static void tcpc_set_data_role(struct pd_manager_chip *chip,
			       enum typec_data_role role)
{
	typec_set_data_role(chip->typec_port, role);
	if (chip->data_role == role)
		return;
	chg_info("data_role = %d\n", role);
	chip->data_role = role;
	oplus_chg_ic_virq_trigger(chip->ic_dev,
				  OPLUS_IC_VIRQ_DATA_ROLE_CHANGED);
}

static void tcpc_set_voltage_max_and_min(struct pd_manager_chip *chip, int max, int min)
{
	if (chip->voltage_max_mv == max && chip->voltage_min_mv == min)
		return;
	chg_info("voltage_max_mv = %d, voltage_min_mv = %d\n", max, min);
	chip->voltage_max_mv = max;
	chip->voltage_min_mv = min;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_VOLTAGE_CHANGED);
}

static void tcpc_set_current_max(struct pd_manager_chip *chip, int max)
{
	if (chip->current_max_ma == max)
		return;
	chg_info("current_max_ma = %d\n", max);
	chip->current_max_ma = max;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_CURRENT_CHANGED);
}

#define OPLUS_SVID 0x22d9
#define VCONN_TIMEOUT_MS 1000
static void tcpc_get_adapter_svid(struct pd_manager_chip *chip)
{
	int i = 0;
	uint32_t vdos[VDO_MAX_NR] = {0};
	struct tcpc_device *tcpc_dev = chip->tcpc;
	struct tcpm_svid_list svid_list = { 0, { 0 } };

	if (tcpc_dev == NULL) {
		chg_err("tcpc_dev is null return\n");
		return;
	}

	tcpm_inquire_pd_partner_svids(tcpc_dev, &svid_list);
	for (i = 0; i < svid_list.cnt; i++) {
		chg_info("svid[%d] = 0x%04x\n", i, svid_list.svids[i]);
		if (svid_list.svids[i] == OPLUS_SVID) {
			chip->pd_svooc = true;
			chg_info("match svid and this is oplus adapter\n");
			break;
		}
	}

	tcpm_inquire_pd_partner_inform(tcpc_dev, vdos);
	if ((vdos[0] & 0xFFFF) == OPLUS_SVID) {
		chip->pd_svooc = true;
		chg_info("match svid and this is oplus adapter 11\n");
	}

	if (chip->pd_svooc)
		schedule_delayed_work(&chip->vconn_wait_work,
				      msecs_to_jiffies(VCONN_TIMEOUT_MS));
	else
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_SVID);
}

static int extcon_init(struct pd_manager_chip *chip)
{
	int ret = 0;

	/*
	 * associate extcon with the dev as it could have a DT
	 * node which will be useful for extcon_get_edev_by_phandle()
	 */
	chip->extcon = devm_extcon_dev_allocate(chip->dev, rpm_extcon_cable);
	if (IS_ERR(chip->extcon)) {
		ret = PTR_ERR(chip->extcon);
		chg_err("extcon dev alloc fail(%d)\n", ret);
		goto out;
	}

	ret = devm_extcon_dev_register(chip->dev, chip->extcon);
	if (ret) {
		chg_err("extcon dev reg fail(%d)\n", ret);
		goto out;
	}

	/* Support reporting polarity and speed via properties */
	extcon_set_property_capability(chip->extcon, EXTCON_USB,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(chip->extcon, EXTCON_USB,
				       EXTCON_PROP_USB_SS);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	extcon_set_property_capability(chip->extcon, EXTCON_USB,
				       EXTCON_PROP_USB_TYPEC_MED_HIGH_CURRENT);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) */
	extcon_set_property_capability(chip->extcon, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(chip->extcon, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_SS);
out:
	return ret;
}

static inline void stop_usb_host(struct pd_manager_chip *chip)
{
	extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, false);
}

static inline void start_usb_host(struct pd_manager_chip *chip)
{
	union extcon_property_value val = {.intval = 0};

	val.intval = tcpm_inquire_cc_polarity(chip->tcpc);
	extcon_set_property(chip->extcon, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = 1;
	extcon_set_property(chip->extcon, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_SS, val);

	extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, true);
}

static inline void stop_usb_peripheral(struct pd_manager_chip *chip)
{
	chip->start_peripheral = false;
	extcon_set_state_sync(chip->extcon, EXTCON_USB, false);
}

static inline void start_usb_peripheral(struct pd_manager_chip *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	int rp = 0;
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) */
	union extcon_property_value val = {.intval = 0};

	if (!chip->bc12_completed) {
		chg_info("wait bc1.2 completed\n");
		chip->start_peripheral = true;
		schedule_delayed_work(&chip->bc12_wait_work,
				      msecs_to_jiffies(BC12_TIMEOUT_MS));
		return;
	}
	chg_info("start usb peripheral\n");
	chip->start_peripheral = false;

	val.intval = tcpm_inquire_cc_polarity(chip->tcpc);
	extcon_set_property(chip->extcon, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = 1;
	extcon_set_property(chip->extcon, EXTCON_USB, EXTCON_PROP_USB_SS, val);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	rp = tcpm_inquire_typec_remote_rp_curr(chip->tcpc);
	val.intval = rp > 500 ? 1 : 0;
	extcon_set_property(chip->extcon, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_MED_HIGH_CURRENT, val);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) */
	extcon_set_state_sync(chip->extcon, EXTCON_USB, true);
}

static void tcpc_bc12_wait_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct pd_manager_chip *chip =
		container_of(dwork, struct pd_manager_chip, bc12_wait_work);

	chip->bc12_completed = true;

	if (chip->start_peripheral)
		start_usb_peripheral(chip);
}

static void tcpc_vconn_wait_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct pd_manager_chip *chip =
		container_of(dwork, struct pd_manager_chip, vconn_wait_work);

	if (chip->pd_svooc)
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_SVID);
}

static void usb_dwork_handler(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct pd_manager_chip *chip =
		container_of(dwork, struct pd_manager_chip, usb_dwork);
	enum dr usb_dr = chip->usb_dr;
	union mms_msg_data data = { 0 };

	if (usb_dr < DR_IDLE || usb_dr >= DR_MAX) {
		chg_err("invalid usb_dr = %d\n", usb_dr);
		return;
	}

	chg_debug("dr_names: %s\n", dr_names[usb_dr]);

	switch (usb_dr) {
	case DR_IDLE:
	case DR_MAX:
		stop_usb_peripheral(chip);
		stop_usb_host(chip);
		break;
	case DR_DEVICE:
		oplus_mms_get_item_data(chip->wired_topic,
					WIRED_ITEM_CHG_TYPE, &data,
					true);
		chip->chg_type = data.intval;
		chg_info("polling_cnt=%d, ret=%d type=%d, bc12_ready=%d\n",
			 ++chip->usb_type_polling_cnt, ret, chip->chg_type,
			 chip->bc12_ready);
		if (!chip->bc12_ready || chip->chg_type == OPLUS_CHG_USB_TYPE_UNKNOWN) {
			if (chip->usb_type_polling_cnt <
			    USB_TYPE_POLLING_CNT_MAX)
				schedule_delayed_work(
					&chip->usb_dwork,
					msecs_to_jiffies(
						USB_TYPE_POLLING_INTERVAL));
			break;
		} else if (chip->chg_type == OPLUS_CHG_USB_TYPE_SDP ||
			   chip->chg_type == OPLUS_CHG_USB_TYPE_CDP ||
			   chip->chg_type == OPLUS_CHG_USB_TYPE_PD_SDP) {
			chg_info("start usb peripheral\n");
			stop_usb_host(chip);
			start_usb_peripheral(chip);
		}
		break;
	case DR_HOST_TO_DEVICE:
		stop_usb_host(chip);
		start_usb_peripheral(chip);
		break;
	case DR_HOST:
	case DR_DEVICE_TO_HOST:
		stop_usb_peripheral(chip);
		start_usb_host(chip);
		break;
	}
}

static void pd_sink_set_vol_and_cur(struct pd_manager_chip *chip,
				    int mv, int ma, uint8_t type)
{
	unsigned long sel = 0;
	int max, min;

	/* Charger plug-in first time */
	if (chip->pd_type == OPLUS_CHG_USB_TYPE_UNKNOWN)
		tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_PD);

	if (mv < MICRO_5V)
		mv = MICRO_5V;

	switch (type) {
	case TCP_VBUS_CTRL_PD_HRESET:
	case TCP_VBUS_CTRL_PD_PR_SWAP:
	case TCP_VBUS_CTRL_PD_REQUEST:
		set_bit(0, &sel);
		set_bit(1, &sel);
		max = min = mv;
		break;
	case TCP_VBUS_CTRL_PD_STANDBY_UP:
		set_bit(1, &sel);
		max = mv;
		min = chip->voltage_min_mv;
		break;
	case TCP_VBUS_CTRL_PD_STANDBY_DOWN:
		set_bit(0, &sel);
		max = chip->voltage_max_mv;
		min = mv;
		break;
	default:
		max = chip->voltage_max_mv;
		min = chip->voltage_min_mv;
		break;
	}

	tcpc_set_voltage_max_and_min(chip, max, min);
	tcpc_set_current_max(chip, ma);
}

static int tcpc_pd_state_change(struct pd_manager_chip *chip, struct tcp_notify *noti)
{
	uint32_t partner_vdos[VDO_MAX_NR];
	int pd_type;
	int ret = 0;

	switch (noti->pd_state.connected) {
	case PD_CONNECT_NONE:
		tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_UNKNOWN);
		break;
	case PD_CONNECT_HARD_RESET:
		tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_UNKNOWN);
		break;
	case PD_CONNECT_PE_READY_SNK:
	case PD_CONNECT_PE_READY_SNK_PD30:
		tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_PD);
		tcpc_get_adapter_svid(chip);
		typec_set_pwr_opmode(chip->typec_port, TYPEC_PWR_MODE_PD);
		if (!chip->partner)
			break;
		ret = tcpm_inquire_pd_partner_inform(chip->tcpc, partner_vdos);
		if (ret != TCPM_SUCCESS)
			break;
		chip->partner_identity.id_header = partner_vdos[0];
		chip->partner_identity.cert_stat = partner_vdos[1];
		chip->partner_identity.product = partner_vdos[2];
		typec_partner_set_identity(chip->partner);
		break;
	case PD_CONNECT_PE_READY_SNK_APDO:
		ret = tcpm_inquire_dpm_flags(chip->tcpc);
		tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_PD_PPS);
		tcpc_get_adapter_svid(chip);

		typec_set_pwr_opmode(chip->typec_port, TYPEC_PWR_MODE_PD);
		if (!chip->partner)
			break;
		ret = tcpm_inquire_pd_partner_inform(chip->tcpc, partner_vdos);
		if (ret != TCPM_SUCCESS)
			break;
		chip->partner_identity.id_header = partner_vdos[0];
		chip->partner_identity.cert_stat = partner_vdos[1];
		chip->partner_identity.product = partner_vdos[2];
		typec_partner_set_identity(chip->partner);
		break;
	case PD_CONNECT_PE_READY_SRC:
	case PD_CONNECT_PE_READY_SRC_PD30:
		/* update chip->pd_active */
		pd_type = noti->pd_state.connected ==
					  PD_CONNECT_PE_READY_SNK_APDO ?
				  OPLUS_CHG_USB_TYPE_PD_PPS :
					OPLUS_CHG_USB_TYPE_PD;
		tcpc_set_pd_type(chip, pd_type);
		pd_sink_set_vol_and_cur(chip, chip->sink_mv_old,
					chip->sink_ma_old,
					TCP_VBUS_CTRL_PD_STANDBY);

		typec_set_pwr_opmode(chip->typec_port, TYPEC_PWR_MODE_PD);
		if (!chip->partner)
			break;
		ret = tcpm_inquire_pd_partner_inform(chip->tcpc, partner_vdos);
		if (ret != TCPM_SUCCESS)
			break;
		chip->partner_identity.id_header = partner_vdos[0];
		chip->partner_identity.cert_stat = partner_vdos[1];
		chip->partner_identity.product = partner_vdos[2];
		typec_partner_set_identity(chip->partner);
		break;
	case PD_CONNECT_TYPEC_ONLY_SNK:
		/* not support svid */
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_SVID);
		break;
	}

	return ret;
}

static int pd_tcp_notifier_call(struct notifier_block *nb, unsigned long event,
				void *data)
{
	int ret = 0;
	struct tcp_notify *noti = data;
	struct pd_manager_chip *chip =
		container_of(nb, struct pd_manager_chip, pd_nb);
	uint8_t old_state = TYPEC_UNATTACHED, new_state = TYPEC_UNATTACHED;
	enum typec_pwr_opmode opmode = TYPEC_PWR_MODE_USB;
	bool hard_reset;
	bool first_boot = false;

	switch (event) {
	case TCP_NOTIFY_SINK_VBUS:
		chip->sink_mv_new = noti->vbus_state.mv;
		chip->sink_ma_new = noti->vbus_state.ma;
		chg_info("sink vbus %dmV %dmA type(0x%02X)\n",
			 chip->sink_mv_new, chip->sink_ma_new,
			 noti->vbus_state.type);

		if ((chip->sink_mv_new != chip->sink_mv_old) ||
		    (chip->sink_ma_new != chip->sink_ma_old)) {
			chip->sink_mv_old = chip->sink_mv_new;
			chip->sink_ma_old = chip->sink_ma_new;
			if (chip->sink_mv_new && chip->sink_ma_new) {
				/* enable VBUS power path */
			} else {
				/* disable VBUS power path */
			}
		}

		if (noti->vbus_state.type & TCP_VBUS_CTRL_PD_DETECT)
			pd_sink_set_vol_and_cur(chip, chip->sink_mv_new,
						chip->sink_ma_new,
						noti->vbus_state.type);
		break;
	case TCP_NOTIFY_SOURCE_VBUS:
		chg_info("source vbus %dmV\n", noti->vbus_state.mv);
		/* enable/disable OTG power output */
		if (noti->vbus_state.mv)
			tcpc_set_otg_enbale(chip, true);
		else
			tcpc_set_otg_enbale(chip, false);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		if (chip->first_check) {
			old_state = TYPEC_UNATTACHED;
			chip->first_check = false;
			first_boot = true;
		} else {
			old_state = noti->typec_state.old_state;
		}
		new_state = noti->typec_state.new_state;
		chg_err("old_state=%d, new_state=%d\n", old_state, new_state);
		if (old_state == TYPEC_UNATTACHED &&
		    (new_state == TYPEC_ATTACHED_SNK ||
		     new_state == TYPEC_ATTACHED_NORP_SRC ||
		     new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		     new_state == TYPEC_ATTACHED_DBGACC_SNK)) {
			chg_info("Charger plug in, polarity = %d\n",
				 noti->typec_state.polarity);
			/*
			 * start charger type detection,
			 * and enable device connection
			 */
			if (first_boot)
				chip->bc12_completed = true;
			else
				chip->bc12_completed = false;
			chip->bc12_ready = false;
			chip->start_peripheral = false;
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_DEVICE;
			chip->usb_type_polling_cnt = 0;
			schedule_delayed_work(
				&chip->usb_dwork,
				msecs_to_jiffies(USB_TYPE_POLLING_INTERVAL));
			tcpc_set_data_role(chip, TYPEC_DEVICE);
			typec_set_pwr_role(chip->typec_port, TYPEC_SINK);
			opmode = noti->typec_state.rp_level -
				 TYPEC_CC_VOLT_SNK_DFT;
			if (opmode > TYPEC_PWR_MODE_PD) {
				chg_err("Unknown typec power opmode, opmode=%d\n",
					opmode);
				opmode = TYPEC_PWR_MODE_3_0A;
			}
			typec_set_pwr_opmode(chip->typec_port, opmode);
			typec_set_vconn_role(chip->typec_port, TYPEC_SINK);
		} else if ((old_state == TYPEC_ATTACHED_SNK ||
			    old_state == TYPEC_ATTACHED_NORP_SRC ||
			    old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			    old_state == TYPEC_ATTACHED_DBGACC_SNK) &&
			   new_state == TYPEC_UNATTACHED) {
			chg_info("Charger plug out\n");
			/*
			 * report charger plug-out,
			 * and disable device connection
			 */
			chip->pd_svooc = false;
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_IDLE;
			schedule_delayed_work(&chip->usb_dwork, 0);
		} else if (old_state == TYPEC_UNATTACHED &&
			   (new_state == TYPEC_ATTACHED_SRC ||
			    new_state == TYPEC_ATTACHED_DEBUG)) {
			chg_info("OTG plug in, polarity = %d\n",
				 noti->typec_state.polarity);
			/* enable host connection */
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_HOST;
			schedule_delayed_work(&chip->usb_dwork, 0);
			tcpc_set_data_role(chip, TYPEC_HOST);
			typec_set_pwr_role(chip->typec_port, TYPEC_SOURCE);
			switch (noti->typec_state.local_rp_level) {
			case TYPEC_CC_RP_3_0:
				opmode = TYPEC_PWR_MODE_3_0A;
				break;
			case TYPEC_CC_RP_1_5:
				opmode = TYPEC_PWR_MODE_1_5A;
				break;
			case TYPEC_CC_RP_DFT:
			default:
				opmode = TYPEC_PWR_MODE_USB;
				break;
			}
			typec_set_pwr_opmode(chip->typec_port, opmode);
			typec_set_vconn_role(chip->typec_port, TYPEC_SOURCE);
		} else if ((old_state == TYPEC_ATTACHED_SRC ||
			    old_state == TYPEC_ATTACHED_DEBUG) &&
			   new_state == TYPEC_UNATTACHED) {
			chg_info("OTG plug out\n");
			/* disable host connection */
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_IDLE;
			schedule_delayed_work(&chip->usb_dwork, 0);
		} else if (old_state == TYPEC_UNATTACHED &&
			   new_state == TYPEC_ATTACHED_AUDIO) {
			chg_info("Audio plug in\n");
			/* enable AudioAccessory connection */
		} else if (old_state == TYPEC_ATTACHED_AUDIO &&
			   new_state == TYPEC_UNATTACHED) {
			chg_info("Audio plug out\n");
			/* disable AudioAccessory connection */
		}

		if (new_state == TYPEC_UNATTACHED) {
			tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_UNKNOWN);

			typec_unregister_partner(chip->partner);
			chip->partner = NULL;
			if (chip->typec_caps.prefer_role == TYPEC_SOURCE) {
				tcpc_set_data_role(chip, TYPEC_HOST);
				typec_set_pwr_role(chip->typec_port,
						   TYPEC_SOURCE);
				typec_set_pwr_opmode(chip->typec_port,
						     TYPEC_PWR_MODE_USB);
				typec_set_vconn_role(chip->typec_port,
						     TYPEC_SOURCE);
			} else {
				tcpc_set_data_role(chip, TYPEC_DEVICE);
				typec_set_pwr_role(chip->typec_port,
						   TYPEC_SINK);
				typec_set_pwr_opmode(chip->typec_port,
						     TYPEC_PWR_MODE_USB);
				typec_set_vconn_role(chip->typec_port,
						     TYPEC_SINK);
			}
		} else if (!chip->partner) {
			memset(&chip->partner_identity, 0,
			       sizeof(chip->partner_identity));
			chip->partner_desc.usb_pd = false;
			switch (new_state) {
			case TYPEC_ATTACHED_AUDIO:
				chip->partner_desc.accessory =
					TYPEC_ACCESSORY_AUDIO;
				break;
			case TYPEC_ATTACHED_DEBUG:
			case TYPEC_ATTACHED_DBGACC_SNK:
			case TYPEC_ATTACHED_CUSTOM_SRC:
				chip->partner_desc.accessory =
					TYPEC_ACCESSORY_DEBUG;
				break;
			default:
				chip->partner_desc.accessory =
					TYPEC_ACCESSORY_NONE;
				break;
			}
			chip->partner = typec_register_partner(
				chip->typec_port, &chip->partner_desc);
			if (IS_ERR(chip->partner)) {
				ret = PTR_ERR(chip->partner);
				chg_debug("typec register partner fail(%d)\n",
					  ret);
			}
		}

		if (new_state == TYPEC_ATTACHED_SNK) {
			switch (noti->typec_state.rp_level) {
			/* SNK_RP_3P0 */
			case TYPEC_CC_VOLT_SNK_3_0:
				break;
			/* SNK_RP_1P5 */
			case TYPEC_CC_VOLT_SNK_1_5:
				break;
			/* SNK_RP_STD */
			case TYPEC_CC_VOLT_SNK_DFT:
			default:
				break;
			}
		} else if (new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			   new_state == TYPEC_ATTACHED_DBGACC_SNK) {
			switch (noti->typec_state.rp_level) {
			/* DAM_3000 */
			case TYPEC_CC_VOLT_SNK_3_0:
				break;
			/* DAM_1500 */
			case TYPEC_CC_VOLT_SNK_1_5:
				break;
			/* DAM_500 */
			case TYPEC_CC_VOLT_SNK_DFT:
			default:
				break;
			}
		} else if (new_state == TYPEC_ATTACHED_NORP_SRC) {
			/* Both CCs are open */
		}
		break;
	case TCP_NOTIFY_PR_SWAP:
		chg_info("power role swap, new role = %d\n",
			 noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_SINK) {
			chg_info("swap power role to sink\n");
			/*
			 * report charger plug-in without charger type detection
			 * to not interfering with USB2.0 communication
			 */

			/* toggle chip->pd_active to clean up the effect of
			 * smblib_uusb_removal() */
			tcpc_set_pd_type(chip, OPLUS_CHG_USB_TYPE_UNKNOWN);

			typec_set_pwr_role(chip->typec_port, TYPEC_SINK);
		} else if (noti->swap_state.new_role == PD_ROLE_SOURCE) {
			chg_info("swap power role to source\n");
			/* report charger plug-out */

			typec_set_pwr_role(chip->typec_port, TYPEC_SOURCE);
		}
		break;
	case TCP_NOTIFY_DR_SWAP:
		chg_info("data role swap, new role = %d\n",
			 noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP) {
			chg_info("swap data role to device\n");
			/*
			 * disable host connection,
			 * and enable device connection
			 */
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_HOST_TO_DEVICE;
			schedule_delayed_work(&chip->usb_dwork, 0);
			tcpc_set_data_role(chip, TYPEC_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP) {
			chg_info("swap data role to host\n");
			/*
			 * disable device connection,
			 * and enable host connection
			 */
			cancel_delayed_work_sync(&chip->usb_dwork);
			chip->usb_dr = DR_DEVICE_TO_HOST;
			schedule_delayed_work(&chip->usb_dwork, 0);
			tcpc_set_data_role(chip, TYPEC_HOST);
		}
		break;
	case TCP_NOTIFY_VCONN_SWAP:
		chg_info("vconn role swap, new role = %d\n",
			 noti->swap_state.new_role);
		if (noti->swap_state.new_role) {
			chg_info("swap vconn role to on\n");
			typec_set_vconn_role(chip->typec_port, TYPEC_SOURCE);
		} else {
			chg_info("swap vconn role to off\n");
			typec_set_vconn_role(chip->typec_port, TYPEC_SINK);
			cancel_delayed_work_sync(&chip->vconn_wait_work);
			schedule_delayed_work(&chip->vconn_wait_work, 0);
		}
		break;
	case TCP_NOTIFY_EXT_DISCHARGE:
		chg_info("ext discharge = %d\n", noti->en_state.en);
		/* enable/disable VBUS discharge */
		break;
	case TCP_NOTIFY_PD_STATE:
		chg_info("pd state = %d\n", noti->pd_state.connected);
		ret = tcpc_pd_state_change(chip, noti);
		break;
	case TCP_NOTIFY_HARD_RESET_STATE:
		switch (noti->hreset_state.state) {
		case TCP_HRESET_SIGNAL_SEND:
		case TCP_HRESET_SIGNAL_RECV:
			hard_reset = true;
			break;
		default:
			hard_reset = false;
			break;
		}
		/* smblib_set_prop(chip, POWER_SUPPLY_PROP_PD_IN_HARD_RESET, &val); */
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static int tcpc_typec_try_role(struct typec_port *port, int role)
{
	struct pd_manager_chip *chip = typec_get_drvdata(port);
#else
static int tcpc_typec_try_role(const struct typec_capability *cap, int role)
{
	struct pd_manager_chip *chip =
		container_of(cap, struct pd_manager_chip, typec_caps);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */
	uint8_t typec_role = TYPEC_ROLE_UNKNOWN;

	chg_info("role = %d\n", role);

	switch (role) {
	case TYPEC_NO_PREFERRED_ROLE:
		typec_role = TYPEC_ROLE_DRP;
		break;
	case TYPEC_SINK:
		typec_role = TYPEC_ROLE_TRY_SNK;
		break;
	case TYPEC_SOURCE:
		typec_role = TYPEC_ROLE_TRY_SRC;
		break;
	default:
		return 0;
	}

	return tcpm_typec_change_role_postpone(chip->tcpc, typec_role, true);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static int tcpc_typec_dr_set(struct typec_port *port, enum typec_data_role role)
{
	struct pd_manager_chip *chip = typec_get_drvdata(port);
#else
static int tcpc_typec_dr_set(const struct typec_capability *cap,
			     enum typec_data_role role)
{
	struct pd_manager_chip *chip =
		container_of(cap, struct pd_manager_chip, typec_caps);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */
	int ret = 0;
	uint8_t data_role = tcpm_inquire_pd_data_role(chip->tcpc);
	bool do_swap = false;

	chg_info("role = %d\n", role);

	if (role == TYPEC_HOST) {
		if (data_role == PD_ROLE_UFP) {
			do_swap = true;
			data_role = PD_ROLE_DFP;
		}
	} else if (role == TYPEC_DEVICE) {
		if (data_role == PD_ROLE_DFP) {
			do_swap = true;
			data_role = PD_ROLE_UFP;
		}
	} else {
		chg_err("invalid role\n");
		return -EINVAL;
	}

	if (do_swap) {
		ret = tcpm_dpm_pd_data_swap(chip->tcpc, data_role, NULL);
		if (ret != TCPM_SUCCESS) {
			chg_err("data role swap fail(%d)\n", ret);
			return -EPERM;
		}
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static int tcpc_typec_pr_set(struct typec_port *port, enum typec_role role)
{
	struct pd_manager_chip *chip = typec_get_drvdata(port);
#else
static int tcpc_typec_pr_set(const struct typec_capability *cap,
			     enum typec_role role)
{
	struct pd_manager_chip *chip =
		container_of(cap, struct pd_manager_chip, typec_caps);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */
	int ret = 0;
	uint8_t power_role = tcpm_inquire_pd_power_role(chip->tcpc);
	bool do_swap = false;

	chg_info("role = %d\n", role);

	if (role == TYPEC_SOURCE) {
		if (power_role == PD_ROLE_SINK) {
			do_swap = true;
			power_role = PD_ROLE_SOURCE;
		}
	} else if (role == TYPEC_SINK) {
		if (power_role == PD_ROLE_SOURCE) {
			do_swap = true;
			power_role = PD_ROLE_SINK;
		}
	} else {
		chg_err("invalid role\n");
		return -EINVAL;
	}

	if (do_swap) {
		ret = tcpm_dpm_pd_power_swap(chip->tcpc, power_role, NULL);
		if (ret == TCPM_ERROR_NO_PD_CONNECTED)
			ret = tcpm_typec_role_swap(chip->tcpc);
		if (ret != TCPM_SUCCESS) {
			chg_err("power role swap fail(%d)\n", ret);
			return -EPERM;
		}
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static int tcpc_typec_vconn_set(struct typec_port *port, enum typec_role role)
{
	struct pd_manager_chip *chip = typec_get_drvdata(port);
#else
static int tcpc_typec_vconn_set(const struct typec_capability *cap,
				enum typec_role role)
{
	struct pd_manager_chip *chip =
		container_of(cap, struct pd_manager_chip, typec_caps);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */
	int ret = 0;
	uint8_t vconn_role = tcpm_inquire_pd_vconn_role(chip->tcpc);
	bool do_swap = false;

	chg_info("role = %d\n", role);

	if (role == TYPEC_SOURCE) {
		if (vconn_role == PD_ROLE_VCONN_OFF) {
			do_swap = true;
			vconn_role = PD_ROLE_VCONN_ON;
		}
	} else if (role == TYPEC_SINK) {
		if (vconn_role == PD_ROLE_VCONN_ON) {
			do_swap = true;
			vconn_role = PD_ROLE_VCONN_OFF;
		}
	} else {
		chg_err("invalid role\n");
		return -EINVAL;
	}

	if (do_swap) {
		ret = tcpm_dpm_pd_vconn_swap(chip->tcpc, vconn_role, NULL);
		if (ret != TCPM_SUCCESS) {
			chg_err("vconn role swap fail(%d)\n", ret);
			return -EPERM;
		}
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static int tcpc_typec_port_type_set(struct typec_port *port,
				    enum typec_port_type type)
{
	struct pd_manager_chip *chip = typec_get_drvdata(port);
	const struct typec_capability *cap = &chip->typec_caps;
#else
static int tcpc_typec_port_type_set(const struct typec_capability *cap,
				    enum typec_port_type type)
{
	struct pd_manager_chip *chip =
		container_of(cap, struct pd_manager_chip, typec_caps);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */
	bool as_sink = tcpc_typec_is_act_as_sink_role(chip->tcpc);
	uint8_t typec_role = TYPEC_ROLE_UNKNOWN;

	chg_info("type = %d, as_sink = %d\n", type, as_sink);

	switch (type) {
	case TYPEC_PORT_SNK:
		if (as_sink)
			return 0;
		break;
	case TYPEC_PORT_SRC:
		if (!as_sink)
			return 0;
		break;
	case TYPEC_PORT_DRP:
		if (cap->prefer_role == TYPEC_SOURCE)
			typec_role = TYPEC_ROLE_TRY_SRC;
		else if (cap->prefer_role == TYPEC_SINK)
			typec_role = TYPEC_ROLE_TRY_SNK;
		else
			typec_role = TYPEC_ROLE_DRP;
		return tcpm_typec_change_role(chip->tcpc, typec_role);
	default:
		return 0;
	}

	return tcpm_typec_role_swap(chip->tcpc);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
const struct typec_operations tcpc_typec_ops = {
	.try_role = tcpc_typec_try_role,
	.dr_set = tcpc_typec_dr_set,
	.pr_set = tcpc_typec_pr_set,
	.vconn_set = tcpc_typec_vconn_set,
	.port_type_set = tcpc_typec_port_type_set,
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */

static int typec_init(struct pd_manager_chip *chip)
{
	int ret = 0;

	chip->typec_caps.type = TYPEC_PORT_DRP;
	chip->typec_caps.data = TYPEC_PORT_DRD;
	chip->typec_caps.revision = 0x0120;
	chip->typec_caps.pd_revision = 0x0300;
	switch (chip->tcpc->desc.role_def) {
	case TYPEC_ROLE_SRC:
	case TYPEC_ROLE_TRY_SRC:
		chip->typec_caps.prefer_role = TYPEC_SOURCE;
		break;
	case TYPEC_ROLE_SNK:
	case TYPEC_ROLE_TRY_SNK:
		chip->typec_caps.prefer_role = TYPEC_SINK;
		break;
	default:
		chip->typec_caps.prefer_role = TYPEC_NO_PREFERRED_ROLE;
		break;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	chip->typec_caps.driver_data = chip;
	chip->typec_caps.ops = &tcpc_typec_ops;
#else
	chip->typec_caps.try_role = tcpc_typec_try_role;
	chip->typec_caps.dr_set = tcpc_typec_dr_set;
	chip->typec_caps.pr_set = tcpc_typec_pr_set;
	chip->typec_caps.vconn_set = tcpc_typec_vconn_set;
	chip->typec_caps.port_type_set = tcpc_typec_port_type_set;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */

	chip->typec_port = typec_register_port(chip->dev, &chip->typec_caps);
	if (IS_ERR(chip->typec_port)) {
		ret = PTR_ERR(chip->typec_port);
		chg_err("typec register port fail(%d)\n", ret);
		goto out;
	}

	chip->partner_desc.identity = &chip->partner_identity;
out:
	return ret;
}

static int oplus_pdc_setup(struct pd_manager_chip *chip, int *vbus_mv, int *ibus_ma)
{
	int ret = 0;
	int vbus_mv_t = 0;
	int ibus_ma_t = 0;
	struct tcpc_device *tcpc = chip->tcpc;

	ret = tcpm_dpm_pd_request(tcpc, *vbus_mv, *ibus_ma, NULL);
	if (ret != TCPM_SUCCESS) {
		chg_err("tcpm_dpm_pd_request fail, rc=%d\n", ret);
		return -EINVAL;
	}

	ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
	if (ret != TCPM_SUCCESS) {
		chg_err("inquire current vbus_mv and ibus_ma fail\n");
		return -EINVAL;
	}

	chg_info("request vbus_mv[%d], ibus_ma[%d]\n", vbus_mv_t, ibus_ma_t);

	return 0;
}

static int oplus_pdo_select(struct pd_manager_chip *chip, int vbus_mv, int ibus_ma)
{
	int vbus = 5000, ibus = 2000;
	struct tcpm_power_cap_val apdo_cap;
	struct tcpm_remote_power_cap pd_cap;

	uint8_t cap_i = 0;
	int ret;
	int i;

	if (chip->pd_type == OPLUS_CHG_USB_TYPE_PD_PPS) {
		while (1) {
			ret = tcpm_inquire_pd_source_apdo(chip->tcpc,
					TCPM_POWER_CAP_APDO_TYPE_PPS,
					&cap_i, &apdo_cap);
			if (ret == TCPM_ERROR_NOT_FOUND) {
				break;
			} else if (ret != TCPM_SUCCESS) {
				chg_err("tcpm_inquire_pd_source_apdo failed, rc=%d\n", ret);
				break;
			}

			chg_info(
				"pps_boundary[%d], %d mv ~ %d mv, %d ma pl:%d\n",
				cap_i, apdo_cap.min_mv, apdo_cap.max_mv,
				apdo_cap.ma, apdo_cap.pwr_limit);

			if (apdo_cap.min_mv <= vbus_mv && vbus_mv <= apdo_cap.max_mv) {
				vbus = vbus_mv;
				ibus = apdo_cap.ma;
				if (ibus > ibus_ma)
					ibus = ibus_ma;
				break;
			}
		}
		if (cap_i == 0)
			chg_err("no APDO for pps\n");
	} else if (chip->pd_type == OPLUS_CHG_USB_TYPE_PD) {
		pd_cap.nr = 0;
		pd_cap.selected_cap_idx = 0;
		tcpm_get_remote_power_cap(chip->tcpc, &pd_cap);

		if (pd_cap.nr != 0) {
			for (i = 0; i < pd_cap.nr; i++) {
				if (vbus_mv <= pd_cap.max_mv[i]) {
					vbus = pd_cap.max_mv[i];
					ibus = pd_cap.ma[i];
					if (ibus > ibus_ma)
						ibus = ibus_ma;
					break;
				}
				chg_info("%d mv:[%d,%d] type:%d %d\n", i,
					 pd_cap.min_mv[i], pd_cap.max_mv[i],
					 pd_cap.ma[i], pd_cap.type[i]);
			}
		}
	} else {
		vbus = 5000;
		ibus = 2000;
	}

	return oplus_pdc_setup(chip, &vbus, &ibus);
}

static int pd_manager_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int pd_manager_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static int pd_manager_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int pd_manager_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[],
			       int len)
{
	return 0;
}

static int pd_manager_get_charger_type(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*type = chip->pd_type;

	return 0;
}

static int pd_manager_get_cc_orientation(struct oplus_chg_ic_dev *ic_dev,
					 int *orientation)
{
	struct pd_manager_chip *chip;
	int val;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (tcpm_inquire_typec_attach_state(chip->tcpc) != TYPEC_UNATTACHED)
		val = (int)tcpm_inquire_cc_polarity(chip->tcpc) + 1;
	else
		val = 0;
	*orientation = val;
	chg_info("cc_orientation = %d\n", val);

	return 0;
}

static int pd_manager_suspend_charger(bool suspend)
{
	struct votable *suspend_votable;
	int rc;

	suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	if (!suspend_votable) {
		chg_err("WIRED_CHARGE_SUSPEND votable not found\n");
		return -EINVAL;
	}

	rc = vote(suspend_votable, PDQC_VOTER, suspend, 0, true);
	if (rc < 0)
		chg_err("%s charger error, rc=%d\n",
			suspend ? "suspend" : "unsuspend", rc);
	else
		chg_info("%s charger\n", suspend ? "suspend" : "unsuspend");

	return rc;
}

#define PD_BOOST_DELAY_MS	300
static int pd_manager_set_pd_config(struct oplus_chg_ic_dev *ic_dev, u32 pdo)
{
	struct pd_manager_chip *chip;
	int vol_mv, curr_ma;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	pd_manager_suspend_charger(true);

	switch (PD_SRC_PDO_TYPE(pdo)) {
	case PD_SRC_PDO_TYPE_FIXED:
		vol_mv = PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50;
		curr_ma = PD_SRC_PDO_FIXED_MAX_CURR(pdo) * 10;
		rc = oplus_pdo_select(chip, vol_mv, curr_ma);
		if (rc < 0)
			chg_err("set PD to %d mV fail, rc=%d\n", vol_mv, rc);
		else
			chg_err("set PD to %d mV, rc=%d\n", vol_mv, rc);
		break;
	case PD_SRC_PDO_TYPE_BATTERY:
	case PD_SRC_PDO_TYPE_VARIABLE:
	case PD_SRC_PDO_TYPE_AUGMENTED:
	default:
		chg_err("Unsupported pdo type(=%d)\n", PD_SRC_PDO_TYPE(pdo));
		rc = -EINVAL;
		goto out;
	}

	msleep(PD_BOOST_DELAY_MS);
out:
	pd_manager_suspend_charger(false);
	return rc;
}

static int pd_manager_get_typec_mode(struct oplus_chg_ic_dev *ic_dev,
				     enum oplus_chg_typec_port_role_type *mode)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch(chip->tcpc->typec_role) {
	case TYPEC_ROLE_SNK:
		*mode = TYPEC_PORT_ROLE_SNK;
		break;
	case TYPEC_ROLE_SRC:
		*mode = TYPEC_PORT_ROLE_SRC;
		break;
	case TYPEC_ROLE_DRP:
		*mode = TYPEC_PORT_ROLE_DRP;
		break;
	case TYPEC_ROLE_TRY_SRC:
		*mode = TYPEC_PORT_ROLE_TRY_SRC;
		break;
	case TYPEC_ROLE_TRY_SNK:
		*mode = TYPEC_PORT_ROLE_TRY_SNK;
		break;
	case TYPEC_ROLE_UNKNOWN:
	case TYPEC_ROLE_NR:
	default:
		*mode = TYPEC_PORT_ROLE_INVALID;
		break;
	}

	return 0;
}

static int pd_manager_set_typec_mode(struct oplus_chg_ic_dev *ic_dev,
				     enum oplus_chg_typec_port_role_type mode)
{
	struct pd_manager_chip *chip;
	uint8_t typec_role;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch(mode) {
	case TYPEC_PORT_ROLE_DRP:
		typec_role = TYPEC_ROLE_DRP;
		break;
	case TYPEC_PORT_ROLE_SNK:
		typec_role = TYPEC_ROLE_SNK;
		break;
	case TYPEC_PORT_ROLE_SRC:
		typec_role = TYPEC_ROLE_SRC;
		break;
	case TYPEC_PORT_ROLE_TRY_SNK:
		typec_role = TYPEC_ROLE_TRY_SNK;
		break;
	case TYPEC_PORT_ROLE_TRY_SRC:
		typec_role = TYPEC_ROLE_TRY_SRC;
		break;
	case TYPEC_PORT_ROLE_DISABLE:
		rc = tcpm_typec_disable_function(chip->tcpc, true);
		return rc;
	case TYPEC_PORT_ROLE_ENABLE:
		rc = tcpm_typec_disable_function(chip->tcpc, false);
		return rc;
	case TYPEC_PORT_ROLE_INVALID:
		typec_role = TYPEC_ROLE_UNKNOWN;
		break;
	}

	rc = tcpm_typec_change_role_postpone(chip->tcpc, typec_role, true);
	if (rc < 0)
		chg_err("can't set typec_role = %d\n", mode);

	return rc;
}

static int pd_manager_get_charger_vol_max(struct oplus_chg_ic_dev *ic_dev,
					  int *vol)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol = chip->voltage_max_mv;
	return 0;
}

static int pd_manager_get_charger_vol_min(struct oplus_chg_ic_dev *ic_dev,
					  int *vol)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol = chip->voltage_min_mv;
	return 0;
}

static int pd_manager_get_charger_curr_max(struct oplus_chg_ic_dev *ic_dev,
					   int *curr)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*curr = chip->current_max_ma;
	return 0;
}

static int pd_manager_bc12_completed(struct oplus_chg_ic_dev *ic_dev)
{
	struct pd_manager_chip *chip;
	union mms_msg_data data = { 0 };
	static bool first_boot = true;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (first_boot) {
		first_boot = false;
		oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE,
					&data, true);
		chip->chg_type = data.intval;
		chg_info("chg_type=%d\n", chip->chg_type);
		if (chip->chg_type == OPLUS_CHG_USB_TYPE_SDP ||
		    chip->chg_type == OPLUS_CHG_USB_TYPE_CDP ||
		    chip->chg_type == OPLUS_CHG_USB_TYPE_PD_SDP)
			chip->start_peripheral = true;
	}

	chip->bc12_completed = true;
	chip->bc12_ready = true;
	chg_info("bc1.2 completed, start_peripheral=%d\n",
		 chip->start_peripheral);
	if (chip->start_peripheral)
		start_usb_peripheral(chip);

	return 0;
}

static int pd_manager_get_otg_enable(struct oplus_chg_ic_dev *ic_dev, bool *en)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*en = chip->otg_enable;

	return 0;
}

static int pd_manager_is_oplus_svid(struct oplus_chg_ic_dev *ic_dev, bool *oplus_svid)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*oplus_svid = chip->pd_svooc;

	return 0;
}

static int pd_manager_get_data_role(struct oplus_chg_ic_dev *ic_dev, int *role)
{
	struct pd_manager_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*role = chip->data_role;

	return 0;
}

static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       pd_manager_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       pd_manager_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       pd_manager_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       pd_manager_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE,
			pd_manager_get_charger_type);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION,
			pd_manager_get_cc_orientation);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG,
					       pd_manager_set_pd_config);
		break;
	case OPLUS_IC_FUNC_GET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_TYPEC_MODE,
					       pd_manager_get_typec_mode);
		break;
	case OPLUS_IC_FUNC_SET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_TYPEC_MODE,
					       pd_manager_set_typec_mode);
		break;
	case OPLUS_IC_FUNC_GET_CHARGER_VOL_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GET_CHARGER_VOL_MAX,
			pd_manager_get_charger_vol_max);
		break;
	case OPLUS_IC_FUNC_GET_CHARGER_VOL_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GET_CHARGER_VOL_MIN,
			pd_manager_get_charger_vol_min);
		break;
	case OPLUS_IC_FUNC_GET_CHARGER_CURR_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GET_CHARGER_CURR_MAX,
			pd_manager_get_charger_curr_max);
		break;
	case OPLUS_IC_FUNC_BUCK_BC12_COMPLETED:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_BC12_COMPLETED,
			pd_manager_bc12_completed);
		break;
	case OPLUS_IC_FUNC_GET_OTG_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GET_OTG_ENABLE,
			pd_manager_get_otg_enable);
		break;
	case OPLUS_IC_FUNC_IS_OPLUS_SVID:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_IS_OPLUS_SVID,
			pd_manager_is_oplus_svid);
		break;
	case OPLUS_IC_FUNC_GET_DATA_ROLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GET_DATA_ROLE,
			pd_manager_get_data_role);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq pd_manager_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_CHG_TYPE_CHANGE },
	{ .virq_id = OPLUS_IC_VIRQ_SVID },
	{ .virq_id = OPLUS_IC_VIRQ_VOLTAGE_CHANGED },
	{ .virq_id = OPLUS_IC_VIRQ_CURRENT_CHANGED },
	{ .virq_id = OPLUS_IC_VIRQ_OTG_ENABLE },
	{ .virq_id = OPLUS_IC_VIRQ_DATA_ROLE_CHANGED },
};

static void tcpc_wired_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct pd_manager_chip *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_CHG_TYPE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data,
						false);
			chip->chg_type = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void tcpc_subscribe_wired_topic(struct oplus_mms *topic, void *prv_data)
{
	struct pd_manager_chip *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    tcpc_wired_subs_callback, "tcpc");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE, &data,
				true);
	chip->chg_type = data.intval;

	/* Re-notify the typec status after the driver is loaded */
	chip->first_check = true;
	tcpci_notify_typec_state(chip->tcpc);
}

static void tcpc_variable_init(struct pd_manager_chip *chip)
{
	chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	chip->voltage_max_mv = MICRO_5V;
	chip->voltage_min_mv = MICRO_5V;
	chip->current_max_ma = 0;
}

static int oplus_pd_manager_probe(struct platform_device *pdev)
{
	int ret = 0;
	static int probe_cnt = 0;
	struct pd_manager_chip *chip = NULL;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	struct device_node *node = pdev->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index;

	chg_info("probe_cnt = %d\n", ++probe_cnt);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	INIT_DELAYED_WORK(&chip->bc12_wait_work, tcpc_bc12_wait_work);
	INIT_DELAYED_WORK(&chip->vconn_wait_work, tcpc_vconn_wait_work);

	ret = extcon_init(chip);
	if (ret) {
		chg_err("init extcon fail(%d)\n", ret);
		ret = -EPROBE_DEFER;
		if (probe_cnt >= PROBE_CNT_MAX)
			goto out;
		else
			goto err_init_extcon;
	}

	chip->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!chip->tcpc) {
		chg_err("get tcpc dev fail\n");
		ret = -EPROBE_DEFER;
		if (probe_cnt >= PROBE_CNT_MAX)
			goto out;
		else
			goto err_get_tcpc_dev;
	}

	INIT_DELAYED_WORK(&chip->usb_dwork, usb_dwork_handler);
	chip->usb_dr = DR_IDLE;
	chip->usb_type_polling_cnt = 0;
	chip->sink_mv_old = -1;
	chip->sink_ma_old = -1;

	ret = typec_init(chip);
	if (ret < 0) {
		chg_err("init typec fail(%d)\n", ret);
		ret = -EPROBE_DEFER;
		if (probe_cnt >= PROBE_CNT_MAX)
			goto out;
		else
			goto err_init_typec;
	}

	chip->pd_nb.notifier_call = pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(chip->tcpc, &chip->pd_nb,
					TCP_NOTIFY_TYPE_ALL);
	if (ret < 0) {
		chg_err("register tcpc notifier fail(%d)\n", ret);
		ret = -EPROBE_DEFER;
		if (probe_cnt >= PROBE_CNT_MAX)
			goto out;
		else
			goto err_reg_tcpc_notifier;
	}

	ret = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (ret < 0) {
		chg_err("can't get ic type, rc=%d\n", ret);
		goto reg_ic_err;
	}
	ret = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (ret < 0) {
		chg_err("can't get ic index, rc=%d\n", ret);
		goto reg_ic_err;
	}

	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	sprintf(ic_cfg.manu_name, "PD_MANAGER");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = pd_manager_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(pd_manager_virq_table);
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		ret = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

out:
	platform_set_drvdata(pdev, chip);
	tcpc_variable_init(chip);
	oplus_mms_wait_topic("wired", tcpc_subscribe_wired_topic, chip);
	chg_info("%s!!\n", ret == -EPROBE_DEFER ? "Over probe cnt max" : "OK");
	return 0;

reg_ic_err:
	unregister_tcp_dev_notifier(chip->tcpc, &chip->pd_nb,
				    TCP_NOTIFY_TYPE_ALL);
err_reg_tcpc_notifier:
	typec_unregister_port(chip->typec_port);
err_init_typec:
err_get_tcpc_dev:
err_init_extcon:
	return ret;
}

static int oplus_pd_manager_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct pd_manager_chip *chip = platform_get_drvdata(pdev);

	if (!chip)
		return -EINVAL;

	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);

	ret = unregister_tcp_dev_notifier(chip->tcpc, &chip->pd_nb,
					  TCP_NOTIFY_TYPE_ALL);
	if (ret < 0)
		chg_err("unregister tcpc notifier fail(%d)\n", ret);
	typec_unregister_port(chip->typec_port);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	if (chip->usb_psy)
		power_supply_put(chip->usb_psy);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) */

	return ret;
}

static const struct of_device_id oplus_pd_manager_of_match[] = {
	{ .compatible = "oplus,hal-pd-manager" },
	{ }
};
MODULE_DEVICE_TABLE(of, oplus_pd_manager_of_match);

static struct platform_driver oplus_pd_manager_driver = {
	.driver = {
		.name = "oplus-pd_manager",
		.of_match_table = of_match_ptr(oplus_pd_manager_of_match),
	},
	.probe = oplus_pd_manager_probe,
	.remove = oplus_pd_manager_remove,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init oplus_pd_manager_init(void)
{
	return platform_driver_register(&oplus_pd_manager_driver);
}

static void __exit oplus_pd_manager_exit(void)
{
	platform_driver_unregister(&oplus_pd_manager_driver);
}

late_initcall(oplus_pd_manager_init);
module_exit(oplus_pd_manager_exit);
#else
int oplus_pd_manager_init(void)
{
	return platform_driver_register(&oplus_pd_manager_driver);
}

void oplus_pd_manager_exit(void)
{
	platform_driver_unregister(&oplus_pd_manager_driver);
}
oplus_chg_module_register(oplus_pd_manager);
#endif

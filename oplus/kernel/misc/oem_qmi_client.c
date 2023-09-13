// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/qrtr.h>
#include <linux/net.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <net/sock.h>
#include <linux/soc/qcom/qmi.h>

#define OEM_QMI "oem_qmi"


#define log_fmt(fmt) "[line:%d][module:%s][%s] " fmt

#define OEM_QMI_ERR(a, arg...) \
do { \
	printk(KERN_NOTICE log_fmt(a), __LINE__, OEM_QMI, __func__, ##arg); \
} while (0)

#define OEM_QMI_MSG(a, arg...) \
do { \
	printk(KERN_INFO log_fmt(a), __LINE__, OEM_QMI, __func__, ##arg); \
} while (0)

#define OEM_QMI_SERVICE_ID 0xE4
#define OEM_QMI_SERVICE_VERSION 1
#define OEM_COMMON_REQ_MAX_LEN_V01 1024
#define QMI_OEM_COMMON_REQ_V01 0x000B
#define QMI_OEM_COMMON_REQ_RESP_V01 0x000B
#define QMI_OEM_TLV_TYPE1 0x01
#define QMI_OEM_TLV_TYPE2 0x02
#define QMI_OEM_OPT_TLV_TYPE1 0x10
#define QMI_OEM_OPT_TLV_TYPE2 0x11

#define UIM_QMI_SERVICE_ID 0x0B
#define UIM_QMI_SERVICE_VERSION 1
#define QMI_UIM_POWER_DOWN_REQ_V01 0x0030
#define QMI_UIM_POWER_DOWN_RESP_V01 0x0030
#define QMI_UIM_POWER_UP_REQ_V01 0x0031
#define QMI_UIM_POWER_UP_RESP_V01 0x0031


typedef struct {
	u32 cmd_type;

	u32 data_len;  /**< Must be set to # of elements in data */
	u8 data[OEM_COMMON_REQ_MAX_LEN_V01];
} qmi_oem_common_msg_type_v01; /* Type */


typedef struct {
	/* Mandatory */
	qmi_oem_common_msg_type_v01 oem_common_req;
} qmi_oem_update_common_req_msg_v01; /* Message */


typedef struct {
	/* Mandatory */
	struct qmi_response_type_v01 resp;

	/* Optional */
	u8 oem_common_req_resp_valid;  /**< Must be set to true if oem_common_req_resp is being passed */
	qmi_oem_common_msg_type_v01 oem_common_req_resp;
} qmi_oem_update_common_resp_msg_v01; /* Message */


typedef enum {
	UIM_SLOT_1_V01 = 0x01, /**<  Slot 1 \n  */
	UIM_SLOT_2_V01 = 0x02, /**<  Slot 2 \n  */
	UIM_SLOT_3_V01 = 0x03, /**<  Slot 3 \n  */
	UIM_SLOT_4_V01 = 0x04, /**<  Slot 4 \n  */
	UIM_SLOT_5_V01 = 0x05, /**<  Slot 5  */
} uim_slot_enum_v01;


typedef enum {
	UIM_CARD_MODE_TELECOM_CARD_V01 = 0x00, /**<  Telecom card (default) \n  */
	UIM_CARD_MODE_NON_TELECOM_CARD_V01 = 0x01, /**<  Non-telecom card  */
} uim_card_mode_enum_v01;


typedef struct {
	/* Mandatory */
	uim_slot_enum_v01 slot;

	/* Optional */
	uint8_t ignore_hotswap_switch_valid;  /**< Must be set to true if ignore_hotswap_switch is being passed */
	uint8_t ignore_hotswap_switch;

	/* Optional */
	uint8_t card_mode_valid;  /**< Must be set to true if card_mode is being passed */
	uim_card_mode_enum_v01 card_mode;
} uim_power_up_req_msg_v01; /* Message */


typedef struct {
	/* Mandatory */
	struct qmi_response_type_v01 resp;
} uim_power_up_resp_msg_v01; /* Message */


typedef struct {
	/* Mandatory */
	uim_slot_enum_v01 slot;
} uim_power_down_req_msg_v01; /* Message */

typedef struct {
	/* Mandatory */
	struct qmi_response_type_v01 resp;
} uim_power_down_resp_msg_v01; /* Message */


#define OEM_REQ_MAX_MSG_LEN_V01 (sizeof(qmi_oem_update_common_req_msg_v01))
#define OEM_RESP_MAX_MSG_LEN_V01 (sizeof(qmi_oem_update_common_resp_msg_v01))
#define OEM_MAX_MSG_LEN_V01 max_t(u32, OEM_REQ_MAX_MSG_LEN_V01, OEM_RESP_MAX_MSG_LEN_V01)


#define UIM_UP_REQ_MAX_MSG_LEN_V01 (sizeof(uim_power_up_req_msg_v01))
#define UIM_UP_RESP_MAX_MSG_LEN_V01 (sizeof(uim_power_up_resp_msg_v01))
#define UIM_DOWN_REQ_MAX_MSG_LEN_V01 (sizeof(uim_power_down_req_msg_v01))
#define UIM_DOWN_RESP_MAX_MSG_LEN_V01 (sizeof(uim_power_down_resp_msg_v01))
#define UIM_UP_MAX_MSG_LEN_V01 max_t(u32, UIM_UP_REQ_MAX_MSG_LEN_V01, UIM_UP_RESP_MAX_MSG_LEN_V01)
#define UIM_DOWN_MAX_MSG_LEN_V01 max_t(u32, UIM_DOWN_REQ_MAX_MSG_LEN_V01, UIM_DOWN_RESP_MAX_MSG_LEN_V01)
#define UIM_MAX_MSG_LEN_V01 max_t(u32, UIM_UP_MAX_MSG_LEN_V01, UIM_DOWN_MAX_MSG_LEN_V01)

static struct qmi_elem_info qmi_oem_common_msg_type_v01_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size  = sizeof(u32),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
		.offset   = offsetof(qmi_oem_common_msg_type_v01,
			cmd_type),
	},
	{
		.data_type  = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size  = sizeof(u16),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
		.offset   = offsetof(qmi_oem_common_msg_type_v01,
			data_len),
	},
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len   = OEM_COMMON_REQ_MAX_LEN_V01,
		.elem_size  = sizeof(u8),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type   = QMI_COMMON_TLV_TYPE,
		.offset     = offsetof(qmi_oem_common_msg_type_v01,
			data),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_oem_update_common_req_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len = 1,
		.elem_size  = sizeof(qmi_oem_common_msg_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE1,
		.offset   = offsetof(qmi_oem_update_common_req_msg_v01,
			oem_common_req),
		.ei_array = qmi_oem_common_msg_type_v01_ei,
	},
	{}
};

static struct qmi_elem_info qmi_oem_update_common_resp_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len = 1,
		.elem_size  = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE2,
		.offset   = offsetof(qmi_oem_update_common_resp_msg_v01,
			resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type  = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_OPT_TLV_TYPE1,
		.offset   = offsetof(qmi_oem_update_common_resp_msg_v01,
			oem_common_req_resp_valid),
	},
	{
		.data_type  = QMI_STRUCT,
		.elem_len   = 1,
		.elem_size  = sizeof(qmi_oem_common_msg_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type   = QMI_OEM_OPT_TLV_TYPE1,
		.offset     = offsetof(qmi_oem_update_common_resp_msg_v01,
			oem_common_req_resp),
		.ei_array   = qmi_oem_common_msg_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};


static struct qmi_elem_info uim_power_up_req_msg_v01_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE1,
		.offset   = offsetof(uim_power_up_req_msg_v01,
			slot),
	},
	{
		.data_type  = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_OPT_TLV_TYPE1,
		.offset   = offsetof(uim_power_up_req_msg_v01,
			ignore_hotswap_switch_valid),
	},
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type   = QMI_OEM_OPT_TLV_TYPE1,
		.offset     = offsetof(uim_power_up_req_msg_v01,
			ignore_hotswap_switch),
	},
	{
		.data_type  = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_OPT_TLV_TYPE2,
		.offset   = offsetof(uim_power_up_req_msg_v01,
			card_mode_valid),
	},
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type   = QMI_OEM_OPT_TLV_TYPE2,
		.offset     = offsetof(uim_power_up_req_msg_v01,
			card_mode),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info uim_power_up_resp_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len = 1,
		.elem_size  = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE2,
		.offset   = offsetof(uim_power_up_resp_msg_v01,
			resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};


static struct qmi_elem_info uim_power_down_req_msg_v01_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size  = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE1,
		.offset   = offsetof(uim_power_down_req_msg_v01,
			slot),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info uim_power_down_resp_msg_v01_ei[] = {
	{
		.data_type  = QMI_STRUCT,
		.elem_len = 1,
		.elem_size  = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_OEM_TLV_TYPE2,
		.offset   = offsetof(uim_power_down_resp_msg_v01,
			resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};


typedef struct  {
	struct platform_device *pdev;
	struct qmi_handle oem_qmi;
	struct qmi_handle uim_qmi;

	struct dentry *de_oem_dir;
	struct dentry *de_common;

	struct dentry *de_uim_dir;
	struct dentry *de_power;

	struct mutex lock;

} oem_qmi_controller;


static oem_qmi_controller *g_ctrl_ptr = NULL;
static struct dentry *qmi_debug_dir = NULL;

int uim_qmi_power_up_req(u8 slot_id)
{
	uim_power_up_resp_msg_v01 *resp;
	uim_power_up_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret = 0;

	if (!g_ctrl_ptr || (slot_id != UIM_SLOT_1_V01 && slot_id != UIM_SLOT_2_V01)) {
		return -EINVAL;
	}

	req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req) {
		return -ENOMEM;
	}

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);

	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	mutex_lock(&g_ctrl_ptr->lock);

	req->slot = slot_id;

	OEM_QMI_MSG("get slot: %d\n", req->slot);

	ret = qmi_txn_init(&g_ctrl_ptr->uim_qmi, &txn,
			uim_power_up_resp_msg_v01_ei, resp);

	if (ret < 0) {
		goto out;
	}

	ret = qmi_send_request(&g_ctrl_ptr->uim_qmi, NULL, &txn,
			QMI_UIM_POWER_UP_REQ_V01,
			UIM_UP_REQ_MAX_MSG_LEN_V01,
			uim_power_up_req_msg_v01_ei, req);

	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);

	if (ret < 0) {
		goto out;
	}

	OEM_QMI_MSG("resp result: %d, error: %d\n", resp->resp.result,
		resp->resp.error);

out:
	mutex_unlock(&g_ctrl_ptr->lock);
	kfree(resp);
	kfree(req);

	return ret;
}

int uim_qmi_power_down_req(u8 slot_id)
{
	uim_power_down_resp_msg_v01 *resp;
	uim_power_down_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret = 0;

	if (!g_ctrl_ptr || (slot_id != UIM_SLOT_1_V01 && slot_id != UIM_SLOT_2_V01)) {
		return -EINVAL;
	}

	req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req) {
		return -ENOMEM;
	}

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);

	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	mutex_lock(&g_ctrl_ptr->lock);

	req->slot = slot_id;

	OEM_QMI_MSG("get slot: %d\n", req->slot);

	ret = qmi_txn_init(&g_ctrl_ptr->uim_qmi, &txn,
			uim_power_down_resp_msg_v01_ei, resp);

	if (ret < 0) {
		goto out;
	}

	ret = qmi_send_request(&g_ctrl_ptr->uim_qmi, NULL, &txn,
			QMI_UIM_POWER_DOWN_REQ_V01,
			UIM_DOWN_REQ_MAX_MSG_LEN_V01,
			uim_power_down_req_msg_v01_ei, req);

	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);

	if (ret < 0) {
		goto out;
	}

	OEM_QMI_MSG("resp result: %d, error: %d\n", resp->resp.result,
		resp->resp.error);

out:
	mutex_unlock(&g_ctrl_ptr->lock);
	kfree(resp);
	kfree(req);

	return ret;
}


EXPORT_SYMBOL(uim_qmi_power_up_req);
EXPORT_SYMBOL(uim_qmi_power_down_req);


int oem_qmi_common_req(u32 cmd_type, const char *req_data, u32 req_len,
	char *resp_data, u32 resp_len)
{
	qmi_oem_update_common_resp_msg_v01 *resp;
	qmi_oem_update_common_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret = -1;

	if (!g_ctrl_ptr || !resp_data || !resp_len) {
		return -EINVAL;
	}

	req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req) {
		return -ENOMEM;
	}

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);

	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	mutex_lock(&g_ctrl_ptr->lock);

	req->oem_common_req.cmd_type = cmd_type;

	if (req_len && req_data) {
		req->oem_common_req.data_len = req_len;
		memcpy(req->oem_common_req.data, req_data, min_t(u32, req_len,
				OEM_COMMON_REQ_MAX_LEN_V01));
	}

	ret = qmi_txn_init(&g_ctrl_ptr->oem_qmi, &txn,
			qmi_oem_update_common_resp_msg_v01_ei, resp);

	if (ret < 0) {
		goto out;
	}

	ret = qmi_send_request(&g_ctrl_ptr->oem_qmi, NULL, &txn,
			QMI_OEM_COMMON_REQ_V01,
			OEM_REQ_MAX_MSG_LEN_V01,
			qmi_oem_update_common_req_msg_v01_ei, req);

	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);

	if (ret < 0) {
		goto out;
	}

	OEM_QMI_MSG("resp result: %d, error: %d\n", resp->resp.result,
		resp->resp.error);

	if (resp->oem_common_req_resp_valid) {
		memcpy(resp_data, resp->oem_common_req_resp.data, min_t(u32, resp_len,
				resp->oem_common_req_resp.data_len));
		OEM_QMI_ERR("resp data : %s\n", resp->oem_common_req_resp.data);
		ret = 0;
	}

out:
	mutex_unlock(&g_ctrl_ptr->lock);
	kfree(resp);
	kfree(req);

	return ret;
}


EXPORT_SYMBOL(oem_qmi_common_req);


static ssize_t uim_qmi_power_up_debug(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)

{
	char buf[8] = {0};
	int slot = -1;
	int power_up = -1;
	int ret;

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf), count))) {
		ret = -EFAULT;
		return ret;
	}
	buf[7] = '\0';
	slot = simple_strtol(buf, NULL, 0) / 10;
	power_up = simple_strtol(buf, NULL, 0) % 10;

	OEM_QMI_MSG("slot: %d, power_up: %d\n", slot, power_up);

	if (power_up == 0) {
		uim_qmi_power_down_req(slot);

	} else if (power_up == 1) {
		uim_qmi_power_up_req(slot);
	}

	ret = count;

	return ret;
}


static ssize_t oem_qmi_common_debug(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos)

{
	oem_qmi_controller *ctrl_ptr = file->private_data;
	qmi_oem_update_common_resp_msg_v01 *resp;
	qmi_oem_update_common_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req) {
		return -ENOMEM;
	}

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);

	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	mutex_lock(&ctrl_ptr->lock);
	req->oem_common_req.data_len = min_t(size_t, sizeof(req->oem_common_req.data),
			count);

	if (copy_from_user(req->oem_common_req.data, user_buf,
			req->oem_common_req.data_len)) {
		ret = -EFAULT;
		goto out;
	}

	req->oem_common_req.cmd_type = simple_strtol(req->oem_common_req.data, NULL, 0);

	ret = qmi_txn_init(&ctrl_ptr->oem_qmi, &txn,
			qmi_oem_update_common_resp_msg_v01_ei, resp);

	if (ret < 0) {
		goto out;
	}

	ret = qmi_send_request(&ctrl_ptr->oem_qmi, NULL, &txn,
			QMI_OEM_COMMON_REQ_V01,
			OEM_REQ_MAX_MSG_LEN_V01,
			qmi_oem_update_common_req_msg_v01_ei, req);

	if (ret < 0) {
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);

	if (ret < 0) {
		goto out;
	}

	OEM_QMI_MSG("resp result: %d, error: %d\n", resp->resp.result,
		resp->resp.error);

	if (resp->oem_common_req_resp_valid) {
		OEM_QMI_ERR("resp data : %s\n", resp->oem_common_req_resp.data);
	}

	ret = count;

out:
	mutex_unlock(&ctrl_ptr->lock);
	kfree(resp);
	kfree(req);

	return ret;
}

static const struct file_operations uim_power_fops = {
	.open = simple_open,
	.write = uim_qmi_power_up_debug,
};


static const struct file_operations oem_common_fops = {
	.open = simple_open,
	.write = oem_qmi_common_debug,
};


static int uim_qmi_new_server(struct qmi_handle *qmi,
	struct qmi_service *service)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, service->node, service->port };
	oem_qmi_controller *ctrl_ptr = container_of(qmi, oem_qmi_controller, uim_qmi);
	char path[20];
	int ret;

	OEM_QMI_MSG("enter\n");

	ret = kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);

	if (ret < 0) {
		OEM_QMI_ERR("failed to connect to remote service port\n");
		return ret;
	}

	snprintf(path, sizeof(path), "uim:%d:%d", sq.sq_node, sq.sq_port);

	if(qmi_debug_dir) {
		ctrl_ptr->de_uim_dir = debugfs_create_dir(path, qmi_debug_dir);

		if (IS_ERR(ctrl_ptr->de_uim_dir)) {
			ret = PTR_ERR(ctrl_ptr->de_uim_dir);
			return ret;
		}

		ctrl_ptr->de_power = debugfs_create_file("power", 0600, ctrl_ptr->de_uim_dir,
				ctrl_ptr, &uim_power_fops);

		if (IS_ERR(ctrl_ptr->de_power)) {
			ret = PTR_ERR(ctrl_ptr->de_power);
			debugfs_remove(ctrl_ptr->de_uim_dir);
			return ret;
		}
	}
	service->priv = ctrl_ptr;

	OEM_QMI_MSG("leave\n");

	return 0;
}

static void uim_qmi_del_server(struct qmi_handle *qmi,
	struct qmi_service *service)
{
	oem_qmi_controller *ctrl_ptr = service->priv;
	if(ctrl_ptr->de_power)
		debugfs_remove(ctrl_ptr->de_power);
	if(ctrl_ptr->de_uim_dir)
		debugfs_remove(ctrl_ptr->de_uim_dir);
}


static int oem_qmi_new_server(struct qmi_handle *qmi,
	struct qmi_service *service)
{
	struct sockaddr_qrtr sq = { AF_QIPCRTR, service->node, service->port };
	oem_qmi_controller *ctrl_ptr = container_of(qmi, oem_qmi_controller, oem_qmi);
	char path[20];
	int ret;

	OEM_QMI_MSG("enter\n");

	ret = kernel_connect(qmi->sock, (struct sockaddr *)&sq, sizeof(sq), 0);

	if (ret < 0) {
		OEM_QMI_ERR("failed to connect to remote service port\n");
		return ret;
	}

	snprintf(path, sizeof(path), "oem:%d:%d", sq.sq_node, sq.sq_port);

	if(qmi_debug_dir) {
		ctrl_ptr->de_oem_dir = debugfs_create_dir(path, qmi_debug_dir);

		if (IS_ERR(ctrl_ptr->de_oem_dir)) {
			ret = PTR_ERR(ctrl_ptr->de_oem_dir);
			return ret;
		}

		ctrl_ptr->de_common = debugfs_create_file("common", 0600, ctrl_ptr->de_oem_dir,
				ctrl_ptr, &oem_common_fops);

		if (IS_ERR(ctrl_ptr->de_common)) {
			ret = PTR_ERR(ctrl_ptr->de_common);
			debugfs_remove(ctrl_ptr->de_oem_dir);
			return ret;
		}
	}
	service->priv = ctrl_ptr;

	OEM_QMI_MSG("leave\n");

	return 0;
}

static void oem_qmi_del_server(struct qmi_handle *qmi,
	struct qmi_service *service)
{
	oem_qmi_controller *ctrl_ptr = service->priv;
	if(ctrl_ptr->de_common)
		debugfs_remove(ctrl_ptr->de_common);
	if(ctrl_ptr->de_oem_dir)
		debugfs_remove(ctrl_ptr->de_oem_dir);
}


static struct qmi_ops uim_lookup_ops = {
	.new_server = uim_qmi_new_server,
	.del_server = uim_qmi_del_server,
};


static struct qmi_ops oem_lookup_ops = {
	.new_server = oem_qmi_new_server,
	.del_server = oem_qmi_del_server,
};


static int oem_register_uim_service(oem_qmi_controller *ctrl_ptr)
{
	int ret;

	if (!ctrl_ptr) {
		return -1;
	}

	ret = qmi_handle_init(&ctrl_ptr->uim_qmi, UIM_MAX_MSG_LEN_V01, &uim_lookup_ops,
			NULL);

	if (ret < 0) {
		return ret;
	}

	ret = qmi_add_lookup(&ctrl_ptr->uim_qmi, UIM_QMI_SERVICE_ID,
			UIM_QMI_SERVICE_VERSION, 0);

	return ret;
}


static int oem_register_oem_service(oem_qmi_controller *ctrl_ptr)
{
	int ret;

	if (!ctrl_ptr) {
		return -1;
	}

	ret = qmi_handle_init(&ctrl_ptr->oem_qmi, OEM_MAX_MSG_LEN_V01, &oem_lookup_ops,
			NULL);

	if (ret < 0) {
		return ret;
	}

	ret = qmi_add_lookup(&ctrl_ptr->oem_qmi, OEM_QMI_SERVICE_ID,
			OEM_QMI_SERVICE_VERSION, 0);

	return ret;
}


static int oem_qmi_probe(struct platform_device *pdev)
{
	oem_qmi_controller *ctrl_ptr = platform_get_drvdata(pdev);

	OEM_QMI_MSG("enter\n");

	if (!ctrl_ptr) {
		return -1;
	}

	oem_register_uim_service(ctrl_ptr);
	oem_register_oem_service(ctrl_ptr);

	OEM_QMI_MSG("leave\n");

	return 0;
}


static int oem_qmi_remove(struct platform_device *pdev)
{
	oem_qmi_controller *ctrl_ptr = platform_get_drvdata(pdev);

	if (ctrl_ptr) {
		qmi_handle_release(&ctrl_ptr->oem_qmi);
		qmi_handle_release(&ctrl_ptr->uim_qmi);
	}

	return 0;
}

static struct platform_driver oem_qmi_driver = {
	.probe = oem_qmi_probe,
	.remove = oem_qmi_remove,
	.driver = {
		.name = "oem_qmi_client",
	},
};

static int oem_qmi_init(void)
{
	int ret;
	struct platform_device *pdev;
	oem_qmi_controller *ctrl_ptr = NULL;

	OEM_QMI_MSG("enter\n");

	#ifdef CONFIG_DEBUG_FS
	qmi_debug_dir = debugfs_create_dir("oem_qmi", NULL);

	if (IS_ERR(qmi_debug_dir)) {
		OEM_QMI_ERR("failed to create oem_qmi dir\n");
		return PTR_ERR(qmi_debug_dir);
	}
	#endif /* CONFIG_DEBUG_FS */

	ret = platform_driver_register(&oem_qmi_driver);

	if (ret) {
		goto err_remove_debug_dir;
	}

	ctrl_ptr = kzalloc(sizeof(oem_qmi_controller), GFP_KERNEL);

	if (!ctrl_ptr) {
		ret = -ENOMEM;
		goto err_unregister_driver;
	}

	pdev = platform_device_alloc("oem_qmi_client", PLATFORM_DEVID_AUTO);

	if (!pdev) {
		ret = -ENOMEM;
		goto err_release_mem;
	}

	ctrl_ptr->pdev = pdev;

	mutex_init(&ctrl_ptr->lock);

	g_ctrl_ptr = ctrl_ptr;

	platform_set_drvdata(pdev, ctrl_ptr);

	ret = platform_device_add(pdev);

	if (ret) {
		goto err_put_device;
	}

	OEM_QMI_MSG("leave\n");

	return 0;


err_put_device:
	platform_device_put(pdev);

err_release_mem:
	kfree(ctrl_ptr);

err_unregister_driver:
	platform_driver_unregister(&oem_qmi_driver);

err_remove_debug_dir:
	if(qmi_debug_dir)
		debugfs_remove(qmi_debug_dir);

	return ret;
}

static void oem_qmi_exit(void)
{
	OEM_QMI_MSG("enter\n");

	if (g_ctrl_ptr) {
		platform_device_put(g_ctrl_ptr->pdev);
		kfree(g_ctrl_ptr);
		g_ctrl_ptr = NULL;
	}

	platform_driver_unregister(&oem_qmi_driver);
	if(qmi_debug_dir)
		debugfs_remove(qmi_debug_dir);
}

module_init(oem_qmi_init);
module_exit(oem_qmi_exit);

MODULE_DESCRIPTION("OEM QMI client driver");
MODULE_LICENSE("GPL v2");

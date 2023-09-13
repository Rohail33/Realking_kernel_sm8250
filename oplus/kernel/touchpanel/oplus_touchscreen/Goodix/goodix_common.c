// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "goodix_common.h"

#define TPD_DEVICE "goodix_common"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (tp_debug)\
        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

/**
 * search_for_item_offset - get each item offset form test limit fw
 * @fw: pointer to fw
 * @item_cnt: max item number
 * @item_index: item index
 * we can using this function to get item offset form index item
 * Returning parameter number(success) or negative errno(failed)
 */
uint32_t search_for_item_offset(const struct firmware *fw, int item_cnt, uint8_t item_index)
{
    int i = 0;
    uint32_t item_offset = 0;
    struct auto_test_item_header *item_header = NULL;
    uint32_t *p_item_offset = (uint32_t *)(fw->data + 16);

    /*check the matched item offset*/
    for (i = 0; i < item_cnt; i++) {
        item_header = (struct auto_test_item_header *)(fw->data + p_item_offset[i]);
        if (item_header->item_bit == item_index) {
            item_offset = p_item_offset[i];
        }
    }
    return item_offset;
}

/**
 * getpara_for_item - get parameter from item
 * @fw: pointer to fw
 * @item_index: item index
 * @para_num: parameter number
 * we can using this function to get parameter form index item
 * Returning pointer to parameter buffer
 */
int32_t *getpara_for_item(const struct firmware *fw, uint8_t item_index, uint32_t *para_num)
{
    uint32_t item_offset = 0;
    int i = 0;
    uint32_t item_cnt = 0;
    struct auto_test_item_header *item_header = NULL;
    struct auto_test_header *test_header = NULL;
    int32_t *p_buffer = NULL;
    test_header = (struct auto_test_header *)fw->data;

    /*step1: check item index is support or not*/
    if (!(test_header->test_item & (1 << item_index))) {
        TPD_INFO("item_index:%d is not support\n", item_index);
        return NULL;
    }

    /*step2: get max item*/
    for (i = 0; i < 8 * sizeof(test_header->test_item); i++) {
        if ((test_header->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }

    /*step3: find item_index offset from the limit img*/
    item_offset = search_for_item_offset(fw, item_cnt, item_index);
    if (item_offset == 0) {
        TPD_INFO("search for item limit offset failed\n");
        return NULL;
    }

    /*step4: check the item magic is support or not*/
    item_header = (struct auto_test_item_header *)(fw->data + item_offset);
    if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
        TPD_INFO("test item: %d magic number(%4x) is wrong\n", item_index, item_header->item_magic);
        return NULL;
    }

    /*step5: get the parameter from the limit img*/
    if (item_header->para_num == 0) {
        TPD_INFO("item: %d has %d no parameter\n", item_index, item_header->para_num);
        return NULL;
    } else {
        p_buffer = (int32_t *)(fw->data + item_offset + sizeof(struct auto_test_item_header));
        for (i = 0; i < item_header->para_num; i++) {
            TPD_INFO("item: %d has parameter:%d\n", item_index, p_buffer[i]);
        }
    }
    *para_num = item_header->para_num;
    return p_buffer;
}

/**
 * get_info_for_item - get all infomation from item
 * @fw: pointer to fw
 * @item_index: item index
 * we can using this function to get infomation form index item
 * Returning pointer to test_item_info buffer
 */
struct test_item_info *get_test_item_info(const struct firmware *fw, uint8_t item_index)
{
    uint32_t item_offset = 0;
    int i = 0;
    uint32_t item_cnt = 0;
    struct auto_test_item_header *item_header = NULL;
    struct auto_test_header *test_header = NULL;
    int32_t *p_buffer = NULL;

    /*result: test_item_info */
    struct test_item_info *p = NULL;

    p = kzalloc(sizeof(*p), GFP_KERNEL);
    if (!p)
        return NULL;

    /*step1: check item index is support or not*/
    test_header = (struct auto_test_header *)fw->data;
    if (!(test_header->test_item & (1 << item_index))) {
        TPD_INFO("item_index:%d is not support\n", item_index);
        goto ERROR;
    }
    /*step2: get max item*/
    for (i = 0; i < 8 * sizeof(test_header->test_item); i++) {
        if ((test_header->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }

    /*step3: find item_index offset from the limit img*/
    item_offset = search_for_item_offset(fw, item_cnt, item_index);
    if (item_offset == 0) {
        TPD_INFO("search for item limit offset failed\n");
        goto ERROR;
    }
    /*get item_offset*/
    p->item_offset = item_offset;

    /*step4: check the item magic is support or not*/
    item_header = (struct auto_test_item_header *)(fw->data + item_offset);
    if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
        TPD_INFO("test item: %d magic number(%4x) is wrong\n", item_index, item_header->item_magic);
        goto ERROR;
    }
    /*get item_header*/
    p->item_magic = item_header->item_magic;
    p->item_size = item_header->item_size;
    p->item_bit = item_header->item_bit;
    p->item_limit_type = item_header->item_limit_type;
    p->top_limit_offset = item_header->top_limit_offset;
    p->floor_limit_offset = item_header->floor_limit_offset;

    /*step5: get the parameter from the limit img*/
    if (item_header->para_num == 0) {
        TPD_INFO("item: %d has %d no parameter\n", item_index, item_header->para_num);
        goto ERROR;
    } else {
        p_buffer = (int32_t *)(fw->data + item_offset + sizeof(struct auto_test_item_header));
        for (i = 0; i < item_header->para_num; i++) {
            TPD_INFO("item: %d has parameter:%d\n", item_index, p_buffer[i]);
        }
    }
    /*get item para number and para buffer*/
    p->para_num = item_header->para_num;
    p->p_buffer = p_buffer;

    return p;

ERROR:
    if (p) {
        kfree(p);
    }
    return NULL;
}

void tp_kfree(void **mem)
{
    if(*mem != NULL) {
        kfree(*mem);
        *mem = NULL;
    }
}

void GetCirclePoints(struct Coordinate *input_points, int number, struct Coordinate *pPnts)
{
    int i = 0;
    int k = 0, j = 0, m = 0, n = 0;
    int max_y, min_y, max_x, min_x;

    max_y = input_points[0].y;
    min_y = input_points[0].y;
    max_x = input_points[0].x;
    min_x = input_points[0].x;

    for (i = 0; i < number; i++) {
        if (input_points[i].y > max_y) {
            max_y = input_points[i].y;
            k = i;
        }
    }
    pPnts[2] = input_points[k];

    for (i = 0; i < number; i++) {
        if (input_points[i].y < min_y) {
            min_y = input_points[i].y;
            j = i;
        }
    }
    pPnts[0] = input_points[j];

    for (i = 0; i < number; i++) {
        if (input_points[i].x > max_x) {
            max_x = input_points[i].x;
            m = i;
        }
    }
    pPnts[3] = input_points[m];

    for (i = 0; i < number; i++) {
        if (input_points[i].x < min_x) {
            min_x = input_points[i].x;
            n = i;
        }
    }
    pPnts[1] = input_points[n];
}

/**
 * ClockWise -  calculate clockwise for circle gesture
 * @p: coordinate array head point.
 * @n: how many points need to be calculated
 * Return 1--clockwise, 0--anticlockwise, not circle, report 2
 */
int ClockWise(struct Coordinate *p, int n)
{
    int i, j, k;
    int count = 0;
    long int z;

    if (n < 3)
        return 1;
    for (i = 0; i < n; i++) {
        j = (i + 1) % n;
        k = (i + 2) % n;
        if ((p[i].x == p[j].x) && (p[j].x == p[j].y))
            continue;
        z = (p[j].x - p[i].x) * (p[k].y - p[j].y);
        z -= (p[j].y - p[i].y) * (p[k].x - p[j].x);
        if (z < 0)
            count--;
        else if (z > 0)
            count++;
    }

    TPD_INFO("ClockWise count = %d\n", count);

    if (count > 0)
        return 1;
    else
        return 0;
}

static ssize_t tp_devices_check_read_func(struct file *file, char __user *page, size_t size, loff_t *ppos)
{
    char pagesize[64] = {0};
    int ret = 0;
    struct touchpanel_data *ts = (struct touchpanel_data *)PDE_DATA(file_inode(file));
    if(!ts)
        return 0;

    ret = sprintf(pagesize, "%d\n", (int)(ts->panel_data.tp_type));
    ret = simple_read_from_buffer(page, size, ppos, pagesize, strlen(pagesize));
    return ret;
}

static const struct file_operations gt1x_devices_check = {
    .owner = THIS_MODULE,
    .read  = tp_devices_check_read_func,
};

static ssize_t proc_health_info_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct goodix_proc_operations *goodix_ops;

    if (!ts)
        return 0;

    goodix_ops = (struct goodix_proc_operations *)ts->private_data;

    if (!goodix_ops->get_health_info_state)
        return 0;

    snprintf(page, PAGESIZE-1, "%d.\n", goodix_ops->get_health_info_state(ts->chip_data));
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_health_info_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[4] = {0};
    int temp = 0;
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct goodix_proc_operations *goodix_ops;

    if (!ts)
        return count;

    goodix_ops = (struct goodix_proc_operations *)ts->private_data;

    if (!goodix_ops->set_health_info_state)
        return count;

    if (count > 2)
        return count;
    if (copy_from_user(buf, buffer, count)) {
        TPD_DEBUG("%s: read proc input error.\n", __func__);
        return count;
    }

    if (kstrtoint(buf, 10, &temp))
    {
        TPD_INFO("%s: kstrtoint error\n", __func__);
        return count;
    }
    if (temp > 2)
        return count;

    mutex_lock(&ts->mutex);
    TPD_INFO("%s: value = %d\n", __func__, temp);
    goodix_ops->set_health_info_state(ts->chip_data, temp);
    mutex_unlock(&ts->mutex);

    return count;
}

static const struct file_operations goodix_health_info_ops =
{
    .read  = proc_health_info_read,
    .write = proc_health_info_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

//proc/touchpanel/Goodix/config_version
static int gt1x_tp_config_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct goodix_proc_operations *goodix_ops;

    if(!ts)
        return 0;
    goodix_ops = (struct goodix_proc_operations *)(ts->private_data);
    if (!goodix_ops) {
        return 0;
    }
    if (!goodix_ops->goodix_config_info_read) {
        seq_printf(s, "Not support auto-test proc node\n");
        return 0;
    }
    disable_irq_nosync(ts->client->irq);
    mutex_lock(&ts->mutex);

    goodix_ops->goodix_config_info_read(s, ts->chip_data);

    mutex_unlock(&ts->mutex);
    enable_irq(ts->client->irq);
    return 0;
}

static int proc_data_config_version_read(struct seq_file *s, void *v)

{
    gt1x_tp_config_read_func(s, v);
    return 0;
}

static int gt1x_data_config_version_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_data_config_version_read, PDE_DATA(inode));
}

static const struct file_operations gt1x_tp_config_version_proc_fops = {
    .owner = THIS_MODULE,
    .open = gt1x_data_config_version_open,
    .read = seq_read,
    .release = single_release,
};

static ssize_t goodix_water_protect_read(struct file *file, char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct goodix_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct goodix_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->goodix_water_protect_read) {
        if(copy_to_user(buff, "Not support auto-test proc node\n", strlen("Not support auto-test proc node\n")))
            TPD_INFO("%s,here:%d\n", __func__, __LINE__);
        return 0;
    }

    ret = syna_ops->goodix_water_protect_read(file, buff, len, pos);
    return ret;
}

static ssize_t goodix_water_protect_write(struct file *file, const char *buff, size_t len, loff_t *pos)
{
    struct touchpanel_data *ts = PDE_DATA(file_inode(file));
    struct goodix_proc_operations *syna_ops;
    ssize_t ret = 0;

    if (!ts)
        return 0;

    syna_ops = (struct goodix_proc_operations *)ts->private_data;
    if (!syna_ops)
        return 0;

    if (!syna_ops->goodix_water_protect_write) {
        TPD_INFO("Not support dd proc node %s %d\n", __func__, __LINE__);
        return 0;
    }

    ret = syna_ops->goodix_water_protect_write(file, buff, len, pos);
    return ret;
}

static struct file_operations goodix_water_protect_debug_ops = {
    .owner = THIS_MODULE,
    .read =  goodix_water_protect_read,
    .write = goodix_water_protect_write,
};

void goodix_limit_read(struct seq_file *s, struct touchpanel_data *ts)
{
    int ret = 0, m = 0, i = 0, j = 0, item_cnt = 0;
    const struct firmware *fw = NULL;
    struct auto_test_header *ph = NULL;
    struct auto_test_item_header *item_head = NULL;
    uint32_t *p_item_offset = NULL;
    int32_t *p_data32 = NULL;

    ret = request_firmware(&fw, ts->panel_data.test_limit_name, ts->dev);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        seq_printf(s, "Request failed, Check the path\n");
        return;
    }

    ph = (struct auto_test_header *)(fw->data);
    p_item_offset = (uint32_t *)(fw->data + 16);
	if ((ph->magic1 != 0x494D494C) || (ph->magic2 != 0x474D4954)) {
        TPD_INFO("limit image is not generated by oplus\n");
        seq_printf(s, "limit image is not generated by oplus\n");
        release_firmware(fw);
        return;
    }

    for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
        if ((ph->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }
    if (!item_cnt) {
        TPD_INFO("limit image has no test item\n");
        seq_printf(s, "limit image has no test item\n");
    }

    for (m = 0; m < item_cnt; m++) {
        item_head = (struct auto_test_item_header *)(fw->data + p_item_offset[m]);
        if (item_head->item_magic != Limit_ItemMagic && item_head->item_magic != Limit_ItemMagic_V2) {
            seq_printf(s, "item: %d limit data has some problem\n", item_head->item_bit);
            continue;
        }

        seq_printf(s, "item %d[size %d, limit type %d, para num %d] :\n", item_head->item_bit, item_head->item_size, item_head->item_limit_type, item_head->para_num);
        if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
            seq_printf(s, "no limit data\n");
        } else if (item_head->item_limit_type == LIMIT_TYPE_CERTAIN_DATA) {
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            seq_printf(s, "top limit data: %d\n", *p_data32);
            p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
            seq_printf(s, "floor limit data: %d\n", *p_data32);
        } else if (item_head->item_limit_type == LIMIT_TYPE_MAX_MIN_DATA) {
            seq_printf(s, "top data: \n");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            for (i = 0 ; i < (ts->hw_res.TX_NUM * ts->hw_res.RX_NUM) ; i++) {
                if (i % ts->hw_res.RX_NUM == 0)
                    seq_printf(s, "\n[%2d] ", (i / ts->hw_res.RX_NUM));
                seq_printf(s, "%4d, ", p_data32[i]);
                TPD_DEBUG("%d, ", p_data32[i]);
            }
            seq_printf(s, "\nfloor data: \n");
            p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
            for (i = 0 ; i < (ts->hw_res.TX_NUM * ts->hw_res.RX_NUM); i++) {
                if (i % ts->hw_res.RX_NUM == 0) {
                    seq_printf(s, "\n[%2d] ", (i / ts->hw_res.RX_NUM));
                }
                seq_printf(s, "%4d, ", p_data32[i]);
                TPD_DEBUG("%d, ", p_data32[i]);
            }
        } else if (item_head->item_limit_type == IMIT_TYPE_DELTA_DATA) {
            seq_printf(s, "delta data: \n");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            for (i = 0 ; i < (ts->hw_res.TX_NUM * ts->hw_res.RX_NUM) ; i++) {
                if (i % ts->hw_res.RX_NUM == 0) {
                    seq_printf(s, "\n[%2d] ", (i / ts->hw_res.RX_NUM));
                }
                seq_printf(s, "%4d, ", p_data32[i]);
                TPD_DEBUG("%d, ", p_data32[i]);
            }
        } else if (item_head->item_limit_type == IMIT_TYPE_SLEFRAW_DATA) {
            seq_printf(s, "top data:\n");
            p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
            for (i = 0 ; i < (ts->hw_res.TX_NUM + ts->hw_res.RX_NUM) ; i++) {
                seq_printf(s, "%4d, ", p_data32[i]);
                TPD_DEBUG("%d, ", p_data32[i]);
            }
            seq_printf(s, "\nfloor data:\n");
            p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
            for (i = 0 ; i < (ts->hw_res.TX_NUM + ts->hw_res.RX_NUM) ; i++) {
                seq_printf(s, "%4d, ", p_data32[i]);
                TPD_DEBUG("%d, ", p_data32[i]);
            }
        }

        p_data32 = (int32_t *)(fw->data + p_item_offset[m] + sizeof(struct auto_test_item_header));
        if (item_head->para_num) {
            seq_printf(s, "parameter:");
            for (j = 0; j < item_head->para_num; j++) {
                seq_printf(s, "%d, ", p_data32[j]);
            }
            seq_printf(s, "\n");
        }
        seq_printf(s, "\n");
    }

    release_firmware(fw);
}

static int tp_auto_test_read_func(struct seq_file *s, void *v)
{
    struct touchpanel_data *ts = s->private;
    struct goodix_proc_operations *goodix_ops;
    const struct firmware *fw = NULL;
    int ret = -1;
    struct auto_test_header *test_head = NULL;
    uint32_t *p_data32 = NULL;

    struct goodix_testdata goodix_testdata = {
        .TX_NUM = 0,
        .RX_NUM = 0,
        .fp = NULL,
        .irq_gpio = -1,
        .TP_FW = 0,
        .fw = NULL,
        .test_item = 0,
    };

    if (!ts)
        return 0;
    goodix_ops = (struct goodix_proc_operations *)ts->private_data;
    if (!goodix_ops)
        return 0;
    if (!goodix_ops->auto_test) {
        seq_printf(s, "Not support auto-test proc node\n");
        return 0;
    }

    //step1:disable_irq && get mutex locked
    if (ts->int_mode == BANNABLE) {
        disable_irq_nosync(ts->irq);
    }
    mutex_lock(&ts->mutex);

    if (ts->esd_handle_support) {
        esd_handle_switch(&ts->esd_info, false);
    }

    //step3:request test limit data from userspace
    ret = request_firmware(&fw, ts->panel_data.test_limit_name, ts->dev);
    if (ret < 0) {
        TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name, ret);
        seq_printf(s, "No limit IMG\n");
        mutex_unlock(&ts->mutex);
        if (ts->int_mode == BANNABLE) {
            enable_irq(ts->irq);
        }
        return 0;
    }

    //step4: decode the limit image
    test_head = (struct auto_test_header *)fw->data;
    p_data32 = (uint32_t *)(fw->data + 16);
    if ((test_head->magic1 != 0x494D494C) || (test_head->magic2 != 0x474D4954)) {
        TPD_INFO("limit image is not generated by oplus\n");
        seq_printf(s, "limit image is not generated by oplus\n");
        goto OUT;
    }
    TPD_INFO("current test item: %llx\n", test_head->test_item);

    //init goodix_testdata
    goodix_testdata.TX_NUM = ts->hw_res.TX_NUM;
    goodix_testdata.RX_NUM = ts->hw_res.RX_NUM;
    goodix_testdata.irq_gpio = ts->hw_res.irq_gpio;
    goodix_testdata.TP_FW = ts->panel_data.TP_FW;
    goodix_testdata.fw = fw;
    goodix_testdata.test_item = test_head->test_item;
    goodix_testdata.TP_FW = ts->panel_data.TP_FW;

    goodix_ops->auto_test(s, ts->chip_data, &goodix_testdata);

OUT:

    release_firmware(fw);

    //step5: return to normal mode
    ts->ts_ops->reset(ts->chip_data);
    operate_mode_switch(ts);

    //step6: unlock the mutex && enable irq trigger
    mutex_unlock(&ts->mutex);
    enable_irq(ts->irq);

    return 0;
}
static int baseline_autotest_open(struct inode *inode, struct file *file)
{
    return single_open(file, tp_auto_test_read_func, PDE_DATA(inode));
}

static const struct file_operations tp_auto_test_proc_fops = {
    .owner = THIS_MODULE,
    .open  = baseline_autotest_open,
    .read  = seq_read,
    .release = single_release,
};

int Goodix_create_proc(struct touchpanel_data *ts, struct goodix_proc_operations *goodix_ops)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_tmp = NULL;
    struct proc_dir_entry *prEntry_gt = NULL;

    ts->private_data = goodix_ops;

    // touchpanel_auto_test interface
    prEntry_tmp = proc_create_data("baseline_test", 0666, ts->prEntry_tp, &tp_auto_test_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_tmp = proc_create_data("TP_AUTO_TEST_ID", 0777, ts->prEntry_tp, &gt1x_devices_check, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_tmp = proc_create_data("health_info_enable", 0777, ts->prEntry_tp, &goodix_health_info_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_gt = proc_mkdir("Goodix", ts->prEntry_tp);
    if (prEntry_gt == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create GT TP proc entry\n", __func__);
    }

    //show config and firmware id interface
    prEntry_tmp = proc_create_data("config_version", 0666, prEntry_gt, &gt1x_tp_config_version_proc_fops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    prEntry_tmp = proc_create_data("water_protect", 0666, prEntry_gt, &goodix_water_protect_debug_ops, ts);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        TPD_INFO("%s: Couldn't create water_protect proc entry, %d\n", __func__, __LINE__);
    }

    return ret;
}

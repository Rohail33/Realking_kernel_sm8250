/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef OPLUS_TP_DEVICES_H
#define OPLUS_TP_DEVICES_H
//device list define
typedef enum tp_dev{
    TP_OFILM,
    TP_BIEL,
    TP_TRULY,
    TP_BOE,
    TP_G2Y,
    TP_TPK,
    TP_JDI,
    TP_TIANMA,
    TP_SAMSUNG,
    TP_DSJM,
    TP_BOE_B8,
    TP_INNOLUX,
    TP_HIMAX_DPT,
    TP_AUO,
    TP_DEPUTE,
    TP_HUAXING,
    TP_HLT,
    TP_DJN,
    TP_BOE_B3,
    TP_CDOT,
    TP_INX,
    TP_LS,
    TP_TXD,
    TP_UNKNOWN,
}tp_dev;

struct tp_dev_name {
    tp_dev type;
    char name[32];
};

#endif


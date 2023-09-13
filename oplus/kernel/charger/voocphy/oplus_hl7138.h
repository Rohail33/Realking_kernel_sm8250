// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Opus. All rights reserved.
 */
#ifndef __HL7138_HEADER__
#define __HL7138_HEADER__

/* Register 01h */
#define HL7138_REG_01					0x01	//Reg01=0x00,default value
#define	HL7138_STAT_CHG_MASK			0x80	//bit7
#define	HL7138_STAT_CHG_SHIFT			7

#define	HL7138_REG_MASK					0x40	//bit7
#define	HL7138_REG_SHIFT				6

#define	HL7138_TS_TEMP_MASK				0x20	//bit7
#define	HL7138_TS_TEMP_SHIFT			5

#define	HL7138_V_OK_MASK				0x10	//bit7
#define	HL7138_V_OK_SHIFT				4

#define	HL7138_CUR_MASK					0x08	//bit7
#define	HL7138_CUR_SHIFT				3

#define	HL7138_SHORT_MASK				0x04	//bit7
#define	HL7138_SHORT_SHIFT				2

#define	HL7138_WDOG_MASK				0x02	//bit7
#define	HL7138_WDOD_SHIFT				1

#define	HL7138_PROTOCOL_MASK			0x01	//bit7
#define	HL7138_PROTOCOL_SHIFT			0

/* Register 02h */
#define HL7138_REG_02					0x02	//Reg02=0xFF,default value
#define	HL7138_STAT_CHG_INT_MASK		0x80	//bit7
#define	HL7138_STAT_CHG_INT_SHIFT		7
#define	HL7138_STAT_CHG_INT_ENABLE		0
#define	HL7138_STAT_CHG_INT_DISABLE		1

#define	HL7138_REG_INT_MASK				0x40	//bit7
#define	HL7138_REG_INT_SHIFT			6
#define	HL7138_REG_INT_ENABLE			0
#define	HL7138_REG_INT_DISABLE			1

#define	HL7138_TS_TEMP_INT_MASK			0x20	//bit7
#define	HL7138_TS_TEMP_INT_SHIFT		5
#define	HL7138_TS_TEMP_INT_ENABLE		0
#define	HL7138_TS_TEMP_INT_DISABLE		1

#define	HL7138_V_OK_INT_MASK			0x10	//bit7
#define	HL7138_V_OK_INT_SHIFT			4
#define	HL7138_V_OK_INT_ENABLE			0
#define	HL7138_V_OK_INT_DISABLE			1

#define	HL7138_CUR_INT_MASK				0x08	//bit7
#define	HL7138_CUR_INT_SHIFT			3
#define	HL7138_CUR_INT_ENABLE			0
#define	HL7138_CUR_INT_DISABLE			1

#define	HL7138_SHORT_INT_MASK			0x04	//bit7
#define	HL7138_SHORT_INT_SHIFT			2
#define	HL7138_SHORT_INT_ENABLE			0
#define	HL7138_SHORT_INT_DISABLE		1

#define	HL7138_WDOG_INT_MASK			0x02	//bit7
#define	HL7138_WDOD_INT_SHIFT			1
#define	HL7138_WDOG_INT_ENABLE			0
#define	HL7138_WDOG_INT_DISABLE			1

#define	HL7138_PROTOCOL_INT_MASK		0x01	//bit7
#define	HL7138_PROTOCOL_INT_SHIFT		0
#define	HL7138_PROTOCOL_INT_ENABLE		0
#define	HL7138_PROTOCOL_INT_DISABLE		1

/* Register 03h */
#define HL7138_REG_03					0x03	//Reg03=0x00,default value
#define	HL7138_STAT_CHG_STS_MASK		0xC0	//bit7,6
#define	HL7138_STAT_CHG_STS_SHIFT		6

#define	HL7138_REG_STS_MASK				0x3C	//bit5,4,3,2
#define	HL7138_REG_STS_SHIFT			2

/* Register 04h */
#define HL7138_REG_04					0x04	//Reg04=0x00,default value
#define	HL7138_V_NOT_OK_STS_MASK		0x10	//bit7,6
#define	HL7138_V_NOT_OK_STS_SHIFT		4

#define	HL7138_CUR_STS_MASK				0x08	//bit7,6
#define	HL7138_CUR_STS_SHIFT			3

#define	HL7138_SHORT_STS_MASK			0x04	//bit7,6
#define	HL7138_SHORT_STS_SHIFT			2

#define	HL7138_WDOG_STS_MASK			0x02	//bit7,6
#define	HL7138_WDOG_STS_SHIFT			1

/* Register 05h */
#define HL7138_REG_05					0x05	//Reg05=0x00,default value
#define	HL7138_VIN_OVP_STS_MASK			0x80
#define	HL7138_VIN_OVP_STS_SHIFT

#define	HL7138_VIN_UVLO_STS_MASK		0x40
#define	HL7138_VIN_UVLO_STS_SHIFT

#define	HL7138_TRACK_OV_STS_MASK		0x20
#define	HL7138_TRACK_OV_STS_SHIFT

#define	HL7138_TRACK_UV_STS_MASK		0x10
#define	HL7138_TRACK_UV_STS_SHIFT

#define	HL7138_VBAT_OVP_STS_MASK		0x08
#define	HL7138_VBAT_OVP_STS_SHIFT

#define	HL7138_VOUT_OVP_STS_MASK		0x04
#define	HL7138_VOUT_OVP_STS_SHIFT		2

#define	HL7138_PMID_QUAL_STS_MASK		0x02
#define	HL7138_PMID_QUAL_STS_SHIFT		1

#define	HL7138_VBUS_UV_STS_MASK			0x01
#define	HL7138_VBUS_UV_STS_SHIFT		0

/* Register 06h */
#define HL7138_REG_06					0x06	//Reg06=0x00,default value
#define	HL7138_IIN_OCP_STS_MASK			0x80
#define	HL7138_IIN_OCP_STS_SHIFT		7

#define	HL7138_IBAT_OCP_STS_MASK		0x40
#define	HL7138_IBAT_OCP_STS_SHIFT		6

#define	HL7138_IIN_UCP_STS_MASK			0x20
#define	HL7138_IIN_UCP_STS_SHIFT		5

#define	HL7138_FET_SHORT_STS_MASK		0x10
#define	HL7138_FET_SHORT_STS_SHIFT		4

#define	HL7138_CFLY_SHORT_STS_MASK		0x08
#define	HL7138_CFLY_SHORT_STS_SHIFT		3

#define	HL7138_DEV_MODE_STS_MASK		0x06
#define	HL7138_DEV_MODE_STS_SHIFT		1

#define	HL7138_THSD_STS_MASK			0x01
#define	HL7138_THSD_STS_SHIFT			0

/* Register 08h */
#define HL7138_REG_08					0x8		//Reg08=0x23,default value
#define	HL7138_BAT_OVP_DIS_MASK			0x80	//bit7,0=en;1=dis;
#define	HL7138_BAT_OVP_DIS_SHIFT		7		//LSB
#define	HL7138_BAT_OVP_ENABLE			0
#define	HL7138_BAT_OVP_DISABLE			1

#define HL7138_BAT_OVP_MASK				0x3F	//Reg_above 111100,it's 4.6V,Max=4.6+Reg11[5:4]
#define HL7138_BAT_OVP_SHIFT			0		//LSB=0,so bit[5:0]is BAT_OVP setting
#define HL7138_BAT_OVP_BASE				4090	//offset=4000+90
#define HL7138_BAT_OVP_LSB				10		//LSB,Reg11[5:4]=80/90/100/110mV

/* Register 09h */
#define HL7138_REG_09					0x09	//Reg09=0x99,default value
#define	HL7138_DEEP_SD_MASK				0x01	//bit1,0=normal;1=en;
#define	HL7138_DEEP_SD_SHIFT			0		//LSB
#define	HL7138_DEEP_SD_ENABLE			1
#define	HL7138_DEEP_SD_NORMAL			0

/* Register 0Ah */
#define HL7138_REG_0A					0x0A	//Reg0A=0x2E,default value
#define	HL7138_BAT_OCP_DIS_MASK			0x80	//bit7,0=en;1=dis
#define	HL7138_BAT_OCP_DIS_SHIFT		7		//LSB
#define	HL7138_BAT_OCP_ENABLE			0
#define	HL7138_BAT_OCP_DISABLE			1

#define	HL7138_BAT_OCP_DEB_MASK			0x40	//bit6,0=80US;1=200US;
#define	HL7138_BAT_OCP_DEB_SHIFT		6		//LSB
#define	HL7138_BAT_OCP_80US				0
#define	HL7138_BAT_OCP_200US			1

#define HL7138_BAT_OCP_MASK			    0x3F	//Reg_above 101110,it's 6.6A,Max=6.6+Reg11[3:2]
#define HL7138_BAT_OCP_SHIFT		    0		//LSB=0,so bit[5:0]is BAT_OVP setting
#define HL7138_BAT_OCP_BASE			    2500	//offset,2000+500
#define HL7138_BAT_OCP_LSB			    100		//LSB,Reg11[3:2]=200/300/400/500mV

/* Register 0Bh */
#define HL7138_REG_0B					0x0B	//Reg0B=0x88,default value
#define HL7138_EXT_NFET_USE_MASK        0x80	//BIT7
#define HL7138_EXT_NFET_USE_SHIFT       7
#define HL7138_EXT_NFET_USE_ENABLE      1		//Default used;
#define HL7138_EXT_NFET_USE_DISABLE     0

#define HL7138_VBUS_PD_EN_MASK          0x40	//BIT6
#define HL7138_VBUS_PD_EN_SHIFT         6
#define HL7138_VBUS_PD_ENABLE           1
#define HL7138_VBUS_PD_DISABLE          0

#define HL7138_AC_OVP_MASK				0x0F	//Reg0B=0x88,bit[3:0],MAX=19000
#define HL7138_AC_OVP_SHIFT				0
#define HL7138_AC_OVP_BASE				4000
#define HL7138_AC_OVP_LSB				1000
#define HL7138_AC_OVP_7P0V				7000
#define HL7138_AC_OVP_12P0V				12000	//Default value

/* Register 0Ch */
#define HL7138_REG_0C                       0x0C	//Reg0C=0x03,default value
#define	HL7138_VBUS_OVP_DIS_MASK            0x10	//bit4,0=en,1=dis;
#define	HL7138_VBUS_OVP_DIS_SHIFT           4
#define	HL7138_VBUS_OVP_ENABLE              0
#define	HL7138_VBUS_OVP_DISABLE             1

#define	HL7138_VBUS_OVP_MASK                0x0F	//CP=BP*2; BP=5100+val*LSB
#define	HL7138_VBUS_OVP_SHIFT               0
#define	HL7138_VBUS_OVP_BASE                5100
#define	HL7138_VBUS_OVP_LSB                 50	//LSB

/* Register 0Eh */
#define HL7138_REG_0E                       0x0E	//Reg0E=0x28,default value
#define	HL7138_IBUS_OCP_DIS_MASK            0x80	//BIT7,0=en,1=dis;
#define	HL7138_IBUS_OCP_DIS_SHIFT           7
#define	HL7138_IBUS_OCP_ENABLE              0
#define	HL7138_IBUS_OCP_DISABLE             1

#define	HL7138_IBUS_OCP_MASK                0x3F	//bit5:0,Bp=Cp*2,Cp=1000+val*LSB
#define	HL7138_IBUS_OCP_SHIFT               0
#define	HL7138_IBUS_OCP_BASE                1100	//offset,1000+100
#define	HL7138_IBUS_OCP_LSB                 50		//LSB,Reg11[3:2]=100/150/200/250mV

/* Register 0Fh */
#define HL7138_REG_0F                       0x0F	//Reg0F=0x00,default value
#define	HL7138_IBUS_OCP_DEB_MASK            0x80	//BIT7,0=en,1=dis;
#define	HL7138_IBUS_OCP_DEB_SHIFT           7
#define	HL7138_IBUS_OCP_80US              	0
#define	HL7138_IBUS_OCP_200US             	1

#define	HL7138_IBUS_OCP_TH_MASK            	0x60	//BIT6:5;
#define	HL7138_IBUS_OCP_TH_SHIFT           	5
#define	HL7138_IBUS_OCP_TH_100MA            0		//Default;
#define	HL7138_IBUS_OCP_TH_150MA            1
#define	HL7138_IBUS_OCP_TH_200MA            2
#define	HL7138_IBUS_OCP_TH_250MA            3

/* Register 10h */
#define HL7138_REG_10                       0x10	//Reg10=0x2C,default value
#define HL7138_TDIE_REG_EN_MASK             0x80
#define HL7138_TDIE_REG_EN_SHIFT            7		//0=EN,1=DIS;
#define HL7138_TDIE_REG_ENABLE              0
#define HL7138_TDIE_REG_DISABLE             1

#define HL7138_VBUS_REG_EN_MASK             0x40
#define HL7138_VBUS_REG_EN_SHIFT            6		//0=EN,1=DIS;
#define HL7138_VBUS_REG_ENABLE              0
#define HL7138_VBUS_REG_DISABLE             1

#define HL7138_TDIE_REG_TH_MASK             0x30	//BIT5:4
#define HL7138_TDIE_REG_TH_SHIFT            4		//0=EN,1=DIS;
#define HL7138_TDIE_REG_TH_80              	0
#define HL7138_TDIE_REG_TH_90             	1
#define HL7138_TDIE_REG_TH_100              2		//Default;
#define HL7138_TDIE_REG_TH_110             	3

/* Register 11h */
#define HL7138_REG_11                       0x11	//Reg11=0x5C,default value
#define HL7138_VBAT_REG_EN_MASK             0x80
#define HL7138_VBAT_REG_EN_SHIFT            7		//0=EN,1=DIS;
#define HL7138_VBAT_REG_ENABLE              0
#define HL7138_VBAT_REG_DISABLE             1

#define HL7138_IBAT_REG_EN_MASK             0x40
#define HL7138_IBAT_REG_EN_SHIFT            6		//BIT6,0=EN;1=DIS;
#define HL7138_IBAT_REG_ENABLE              0
#define HL7138_IBAT_REG_DISABLE             1

#define HL7138_SET_VBATOVP_TH_MASK          0x30	//BIT5:4,above value;
#define HL7138_SET_VBATOVP_TH_SHIFT         4
#define HL7138_SET_VBATOVP_TH_80MV          0
#define HL7138_SET_VBATOVP_TH_90MV          1		//Default;
#define HL7138_SET_VBATOVP_TH_100MV         2
#define HL7138_SET_VBATOVP_TH_110MV         3

#define HL7138_SET_IBATOCP_TH_MASK          0xC0	//BIT3:2,above value;
#define HL7138_SET_IBATOCP_TH_SHIFT         2
#define HL7138_SET_IBATOCP_TH_200MA         0
#define HL7138_SET_IBATOCP_TH_300MA         1
#define HL7138_SET_IBATOCP_TH_400MA         2
#define HL7138_SET_IBATOCP_TH_500MA         3		//Default;

/* Register 12h */
#define HL7138_REG_12                       0x12	//Reg12=0x25,default value
#define HL7138_CHG_EN_MASK                  0x80	//BIT7,0=dis,1=en;
#define HL7138_CHG_EN_SHIFT                 7
#define HL7138_CHG_ENABLE                   1
#define HL7138_CHG_DISABLE                  0

#define HL7138_FSW_SET_MASK                 0x78	//0111 1000,bit[6:3]
#define HL7138_FSW_SET_SHIFT                3
#define HL7138_FSW_SET_500KHZ               0
#define HL7138_FSW_SET_600KHZ               1
#define HL7138_FSW_SET_700KHZ               2
#define HL7138_FSW_SET_800KHZ               3
#define HL7138_FSW_SET_900KHZ               4
#define HL7138_FSW_SET_1000KHZ              5

#define	HL7138_IBUS_UCP_DIS_MASK            0x04	//BIT2,0=dis,1=en;
#define	HL7138_IBUS_UCP_DIS_SHIFT           2
#define	HL7138_IBUS_UCP_ENABLE              1
#define	HL7138_IBUS_UCP_DISABLE             0

#define	HL7138_IBUS_UCP_MASK                0x03	//bit1:0,Bp=Cp*2,Cp=100+val*LSB
#define	HL7138_IBUS_UCP_SHIFT               0
#define	HL7138_IBUS_UCP_BASE                100		//default is 150;
#define	HL7138_IBUS_UCP_LSB                 50

/* Register 13h */
#define HL7138_REG_13                       0x13	//Reg13=0x00,default value
#define	HL7138_IBUS_UCP_DEB_MASK            0x40	//BIT6,debounce time;
#define	HL7138_IBUS_UCP_DEB_SHIFT           6		//bit6,0=10ms,1=100ms;
#define	HL7138_IBUS_UCP_DEB_10ms            0
#define	HL7138_IBUS_UCP_DEB_100ms           1

#define HL7138_VOUT_OVP_DIS_MASK            0x20	//bit5
#define HL7138_VOUT_OVP_DIS_SHIFT           5
#define HL7138_VOUT_OVP_ENABLE              0
#define HL7138_VOUT_OVP_DISABLE             1

#define HL7138_TS_PORT_EN_MASK            	0x10	//bit4
#define HL7138_TS_PORT_EN_SHIFT           	4
#define HL7138_TS_PORT_EN_ENABLE            1
#define HL7138_TS_PORT_EN_DISABLE           0

#define HL7138_AUTO_V_REC_MASK            	0x04	//bit2
#define HL7138_AUTO_V_REC_SHIFT           	2
#define HL7138_AUTO_V_REC_ENABLE            1		//REC 20ms;
#define HL7138_AUTO_V_REC_DISABLE           0

#define HL7138_AUTO_I_REC_MASK            	0x02	//bit1
#define HL7138_AUTO_I_REC_SHIFT           	1
#define HL7138_AUTO_I_REC_ENABLE            1		//REC 20ms;
#define HL7138_AUTO_I_REC_DISABLE           0

#define HL7138_AUTO_UCP_MASK            	0x01	//bit0
#define HL7138_AUTO_UCP_SHIFT           	0
#define HL7138_AUTO_UCP_ENABLE              1		//REC 20ms;
#define HL7138_AUTO_UCP_DISABLE             0

/* Register 14h */
#define HL7138_REG_14                       0x14	//Reg14=0x08,default value
#define HL7138_REG_RESET_MASK               0xC0	//BIT6:7; 11=Reset,others is no reset;
#define HL7138_REG_RESET_SHIFT              6
#define HL7138_NO_REG_RESET                 0
#define HL7138_RESET_REG                    3

#define HL7138_WATCHDOG_MASK                0x0F	//bit3:0; 2:0=timer setting;
#define HL7138_WATCHDOG_SHIFT               0
#define HL7138_WATCHDOG_DIS                 8		//bit3=0=en,bit3=1=dis;
#define HL7138_WATCHDOG_200MS               0
#define HL7138_WATCHDOG_500MS               1
#define HL7138_WATCHDOG_1S                  2
#define HL7138_WATCHDOG_2S                  3
#define HL7138_WATCHDOG_5S                  4
#define HL7138_WATCHDOG_10S                 5
#define HL7138_WATCHDOG_20S                 6
#define HL7138_WATCHDOG_40S                 7

/* Register 15h */
#define HL7138_REG_15                       0x15	//Reg15=0x00,default value
#define HL7138_CHARGE_MODE_MASK             0x80	//Bit7,0=cp,1=bp
#define HL7138_CHARGE_MODE_SHIFT            7
#define HL7138_CHARGE_MODE_2_1              0
#define HL7138_CHARGE_MODE_1_1              1

/* Register 16h */
#define HL7138_REG_16                       0x16	//Reg16=0x24,default value
#define HL7138_PMID2OUT_OVP_EN_MASK         0x80	//bit7
#define HL7138_PMID2OUT_OVP_EN_SHIFT        7
#define HL7138_PMID2OUT_OVP_ENABLE          0
#define HL7138_PMID2OUT_OVP_DISABLE         1

#define HL7138_PMID2OUT_UVP_EN_MASK         0x40	//bit6
#define HL7138_PMID2OUT_UVP_EN_SHIFT        6
#define HL7138_PMID2OUT_UVP_ENABLE          0
#define HL7138_PMID2OUT_UVP_DISABLE         1

#define HL7138_PMID2OUT_OVP_MASK            0x38	//bit5:3
#define HL7138_PMID2OUT_OVP_SHIFT           3
#define HL7138_PMID2OUT_OVP_250MV           0
#define HL7138_PMID2OUT_OVP_300MV           1
#define HL7138_PMID2OUT_OVP_350MV           2
#define HL7138_PMID2OUT_OVP_400MV           3
#define HL7138_PMID2OUT_OVP_450MV           4
#define HL7138_PMID2OUT_OVP_500MV           5
#define HL7138_PMID2OUT_OVP_550MV           6
#define HL7138_PMID2OUT_OVP_600MV           7

#define HL7138_PMID2OUT_UVP_MASK            0x07	//bit2:0
#define HL7138_PMID2OUT_UVP_SHIFT           0
#define HL7138_PMID2OUT_UVP_U_50MV          0
#define HL7138_PMID2OUT_UVP_U_100MV         1
#define HL7138_PMID2OUT_UVP_U_150MV         2
#define HL7138_PMID2OUT_UVP_U_200MV         3
#define HL7138_PMID2OUT_UVP_U_250MV         4
#define HL7138_PMID2OUT_UVP_U_300MV         5
#define HL7138_PMID2OUT_UVP_U_350MV         6
#define HL7138_PMID2OUT_UVP_U_400MV         7

/* Register 17h */
#define HL7138_REG_17                       0x17	//Reg17=0x00

/* Register 40h */
#define HL7138_REG_40                       0x40	//Reg40=0x00,default value
#define HL7138_ADC_EN_MASK                  0x01	//bit0,0=dis,1=en;
#define HL7138_ADC_FORCEDLY_EN_MASK         0x18
#define HL7138_ADC_AUTO_MODE                0x00
#define HL7138_ADC_FORCEDLY_ENABLED         0x08
#define HL7138_ADC_FORCEDLY_DISABLED_10     0x10
#define HL7138_ADC_FORCEDLY_DISABLED_11     0x18
#define HL7138_ADC_EN_SHIFT                 0
#define HL7138_ADC_ENABLE                   1
#define HL7138_ADC_DISABLE                  0

#define HL7138_ADC_AVG_TIME_MASK            0x06	//BIT2:1 0000 0110
#define HL7138_ADC_AVG_TIME_SHIFT           1
#define HL7138_ADC_AVG_TIME_0           	0
#define HL7138_ADC_AVG_TIME_2             	1
#define HL7138_ADC_AVG_TIME_4             	2

#define HL7138_ADC_COPY_MASK           		0xC0	//BIT7:6
#define HL7138_ADC_COPY_SHIFT          		6
#define HL7138_ADC_COPY_AUTO_MODE          	0
#define HL7138_ADC_COPY_MANUAL_MODE         3

/* Register 41h */
#define HL7138_REG_41                       0x41	//ADC_CTRL
#define HL7138_VBUS_ADC_DIS_MASK            0x80
#define HL7138_VBUS_ADC_DIS_SHIFT           7
#define HL7138_VBUS_ADC_ENABLE              0
#define HL7138_VBUS_ADC_DISABLE             1

#define HL7138_IBUS_ADC_DIS_MASK            0x40
#define HL7138_IBUS_ADC_DIS_SHIFT           6
#define HL7138_IBUS_ADC_ENABLE              0
#define HL7138_IBUS_ADC_DISABLE             1

#define HL7138_VBAT_ADC_DIS_MASK            0x20
#define HL7138_VBAT_ADC_DIS_SHIFT           5
#define HL7138_VBAT_ADC_ENABLE              0
#define HL7138_VBAT_ADC_DISABLE             1

#define HL7138_IBAT_ADC_DIS_MASK            0x10
#define HL7138_IBAT_ADC_DIS_SHIFT           4
#define HL7138_IBAT_ADC_ENABLE              0
#define HL7138_IBAT_ADC_DISABLE             1

#define HL7138_TS_ADC_DIS_MASK            	0x08
#define HL7138_TS_ADC_DIS_SHIFT           	3
#define HL7138_TS_ADC_ENABLE              	0
#define HL7138_TS_ADC_DISABLE             	1

#define HL7138_TDIE_ADC_DIS_MASK            0x04
#define HL7138_TDIE_ADC_DIS_SHIFT           2
#define HL7138_TDIE_ADC_ENABLE              0
#define HL7138_TDIE_ADC_DISABLE             1

#define HL7138_VOUT_ADC_DIS_MASK            0x02
#define HL7138_VOUT_ADC_DIS_SHIFT           1
#define HL7138_VOUT_ADC_ENABLE              0
#define HL7138_VOUT_ADC_DISABLE             1

/* Register 42h */
#define HL7138_REG_42                       0x42
#define HL7138_VBUS_POL_H_MASK              0xFF	//High 8bit; low 4bit;
#define HL7138_VBUS_ADC_LSB                 400/100	//4mV_LSB

/* Register 43h */
#define HL7138_REG_43                       0x43
#define HL7138_VBUS_POL_L_MASK              0x0F

/* Register 44h */
#define HL7138_REG_44                       0x44
#define HL7138_IBUS_POL_H_MASK              0xFF
#define HL7138_IBUS_ADC_LSB              	110/100	//1.1mA in cp; 2.15 in BP;
#define HL7138_IBUS_CP_ADC_LSB              110/100	//1.1mA in cp; 2.15 in BP;
#define HL7138_IBUS_BP_ADC_LSB              215/100	//1.1mA in cp; 2.15 in BP;

/* Register 45h */
#define HL7138_REG_45                       0x45
#define HL7138_IBUS_POL_L_MASK              0x0F

/* Register 46h */
#define HL7138_REG_46                       0x46
#define HL7138_VBAT_POL_H_MASK              0xFF
#define HL7138_VBAT_ADC_LSB                 125/100	//1.25mV/LSB

/* Register 47h */
#define HL7138_REG_47                       0x47
#define HL7138_VBAT_POL_L_MASK              0x0F

/* Register 48h */
#define HL7138_REG_48                       0x48
#define HL7138_IBAT_POL_H_MASK              0xFF
#define HL7138_IBAT_ADC_LSB                 220/100	//2.2mA/LSB

/* Register 49h */
#define HL7138_REG_49                       0x49
#define HL7138_IBAT_POL_L_MASK              0x0F

/* Register 4Ah */
#define HL7138_REG_4A                       0x4A
#define HL7138_TS_POL_H_MASK              	0xFF
#define HL7138_TS_ADC_LSB                 	44/100	//0.44mV;

/* Register 4Bh */
#define HL7138_REG_4B                       0x4B
#define HL7138_TS_POL_L_MASK              	0x0F

/* Register 4Ch */
#define HL7138_REG_4C                       0x4C
#define HL7138_VOUT_POL_H_MASK              0xFF
#define HL7138_VOUT_ADC_LSB                 125/100	//1.25mV/LSB

/* Register 4Dh */
#define HL7138_REG_4D                       0x4D
#define HL7138_VOUT_POL_L_MASK              0x0F

/* Register 4Eh */
#define HL7138_REG_4E                       0x4E
#define HL7138_TDIE_POL_H_MASK              0xFF
#define HL7138_TDIE_ADC_LSB                 625/10000	//0.0625 Degrees;

/* Register 4Fh */
#define HL7138_REG_4F                       0x4F
#define HL7138_TDIE_POL_L_MASK              0x0F

/* Register 37h */
#define HL7138_REG_37                       0x37	//2B change to 37;
#define HL7138_VOOC_EN_MASK                 0x80
#define HL7138_VOOC_EN_SHIFT                7
#define HL7138_VOOC_ENABLE                  1
#define HL7138_VOOC_DISABLE                 0

#define HL7138_FEC_SEL_MASK              	0x04
#define HL7138_FEC_SEL_SHIFT             	2
#define HL7138_FEC_SEL_ENDTIME              0	//Default is 0,decide by END time;
#define HL7138_FEC_SEL_CLK              	1

#define HL7138_SOFT_RESET_MASK              0x02
#define HL7138_SOFT_RESET_SHIFT             1
#define HL7138_SOFT_RESET                   1

#define HL7138_SEND_SEQ_MASK                0x01
#define HL7138_SEND_SEQ_SHIFT               0
#define HL7138_SEND_SEQ                     1

/* Register 38h */
#define HL7138_REG_38                       0x38	//2C change to 38;
#define HL7138_TX_WDATA_POL_H_MASK          0x03

/* Register 39h */
#define HL7138_REG_39                       0x39	//2D change to 39;
#define HL7138_TX_WDATA_POL_L_MASK          0xFF

/* Register 3Ah */
#define HL7138_REG_3A                       0x3A	//2E change to 3A;
#define HL7138_RX_RDATA_POL_H_MASK          0xFF

/* Register 3Bh */
#define HL7138_REG_3B                       0x3B	//2F change to 3B;
#define HL7138_PULSE_FILTERED_STAT_MASK     0x80
#define HL7138_PULSE_FILTERED_STAT_SHIFT    7

#define HL7138_NINTH_CLK_ERR_FALG_MASK      0x40
#define HL7138_NINTH_CLK_ERR_FALG_SHIFT     6

#define HL7138_TXSEQ_DONE_FLAG_MASK         0x20
#define HL7138_TXSEQ_DONE_FLAG_SHIFT        5

#define HL7138_ERR_TRANS_DET_FLAG_MASK      0x10
#define HL7138_ERR_TRANS_DET_FLAG_SHIFT     4

#define HL7138_TXDATA_WR_FAIL_FLAG_MASK     0x08
#define HL7138_TXDATA_WR_FAIL_FLAG_SHIFT    3

#define HL7138_RX_START_FLAG_MASK           0x04
#define HL7138_RX_START_FLAG_SHIFT          2

#define HL7138_RXDATA_DONE_FLAG_MASK        0x02
#define HL7138_RXDATA_DONE_FLAG_SHIFT       1

#define HL7138_TXDATA_DONE_FLAG_MASK        0x01
#define HL7138_TXDATA_DONE_FLAG_SHIFT       0

/* Register 3Ch */
#define HL7138_REG_3C                       0x3C	//30 change to 3C;
#define HL7138_PULSE_FILTERED_MASK_MASK      0x80
#define HL7138_PULSE_FILTERED_MASK_SHIFT     7

#define HL7138_NINTH_CLK_ERR_MASK_MASK      0x40
#define HL7138_NINTH_CLK_ERR_MASK_SHIFT     6

#define HL7138_TXSEQ_DONE_MASK_MASK         0x20
#define HL7138_TXSEQ_DONE_MASK_SHIFT        5

#define HL7138_ERR_TRANS_DET_MASK_MASK      0x10
#define HL7138_ERR_TRANS_DET_MASK_SHIFT     4

#define HL7138_TXDATA_WR_FAIL_MASK_MASK     0x08
#define HL7138_TXDATA_WR_FAIL_MASK_SHIFT    3

#define HL7138_RX_START_MASK_MASK           0x04
#define HL7138_RX_START_MASK_SHIFT          2

#define HL7138_RXDATA_DONE_MASK_MASK        0x02
#define HL7138_RXDATA_DONE_MASK_SHIFT       1

#define HL7138_TXDATA_DONE_MASK_MASK        0x01
#define HL7138_TXDATA_DONE_MASK_SHIFT       0

/* Register 3Dh */
#define HL7138_REG_3D                       0x3D	//31 change to 3D;
#define HL7138_PRE_WDATA_POL_H_MASK         0x03

/* Register 3Eh */
#define HL7138_REG_3E                       0x3E	//32 change to 3E;
#define HL7138_PRE_WDATA_POL_L_MASK         0xFF

/* Register 3Fh */
#define HL7138_REG_3F                       0x3F	//33 change to 3F;
#define HL7138_LOOSE_DET_MASK               0x80
#define HL7138_LOOSE_DET_SHIFT              7
#define HL7138_LOOSE_WINDOW_HIGH            0
#define HL7138_LOOSE_WINDOW_LOW             1

#define HL7138_LOW_CHECK_EN_MASK            0x40
#define HL7138_LOW_CHECK_EN_SHIFT           6

#define HL7138_END_TIME_SET_MASK            0x30
#define HL7138_END_TIME_SET_SHIFT           4
#define HL7138_NINE_5_5MS_AFTER_NINE_1_5MS  0
#define HL7138_NINE_6MS_AFTER_NINE_2MS      1
#define HL7138_NINE_6_5MS_AFTER_NINE_2_5MS  2
#define HL7138_NINE_7MS_AFTER_NINE_3MS      3

#define HL7138_RX_SAMPLE_TIME_MASK          0x03
#define HL7138_RX_SAMPLE_TIME_SHIFT         4
#define HL7138_RX_SAMPLE_TIME_5US           0
#define HL7138_RX_SAMPLE_TIME_150US         1
#define HL7138_RX_SAMPLE_TIME_300US         2
#define HL7138_RX_SAMPLE_TIME_450US         3

/* Register 36h */
#define HL7138_REG_36                       0x36
#define HL7138_DEVICE_REV_MASK              0xF0
#define HL7138_DEVICE_ID_MASK               0x0F

/* Register 36h */
#define HL7138_REG_A0                       0xA0
#define HL7138_REG_A7						0xA7

#endif

#ifndef __sy6529_HEADER__
#define __sy6529_HEADER__

/* Register 00h */
#define sy6529_REG_00                       0x00
#define sy6529_REG_RESET_MASK               0x80
#define sy6529_REG_RESET_SHIFT              7
#define sy6529_NO_REG_RESET                 0
#define sy6529_RESET_REG                    1

#define sy6529_CHARGE_MODE_MASK             0x70
#define sy6529_CHARGE_MODE_SHIFT            4
#define sy6529_CHARGE_MODE_2_1              2
#define sy6529_CHARGE_MODE_1_1              1
/*
  * by silergy: sy6529 off mode includes different setting
*/
#define sy6529_CHARGE_MODE_OFF              (0|3|4|5|6|7)

#define sy6529_WATCHDOG_DIS_MASK            0x08
#define sy6529_WATCHDOG_DIS_SHIFT           3
#define sy6529_WATCHDOG_DISABLE             1
#define sy6529_WATCHDOG_ENABLE              0
 /*******  *******/
#define sy6529_WATCHDOG_TIMER_MASK          0x07
#define sy6529_WATCHDOG_TIMER_SHIFT         0
#define sy6529_WATCHDOG_200MS               0
#define sy6529_WATCHDOG_500MS               1
#define sy6529_WATCHDOG_1S                  2
#define sy6529_WATCHDOG_2S                  3
#define sy6529_WATCHDOG_5S                  4
#define sy6529_WATCHDOG_10S                 5
#define sy6529_WATCHDOG_20S                 6
#define sy6529_WATCHDOG_40S                 7

/* Register 01h */
#define sy6529_REG_01                       0x01

#define sy6529_FSW_SET_MASK                 0xE0
#define sy6529_FSW_SET_SHIFT                5
#define sy6529_FSW_SET_250KHZ_1               0
#define sy6529_FSW_SET_250KHZ_2               1
#define sy6529_FSW_SET_300KHZ               2
#define sy6529_FSW_SET_375KHZ               3
#define sy6529_FSW_SET_500KHZ               4
#define sy6529_FSW_SET_750KHZ               5
#define sy6529_FSW_SET_850KHZ               6
#define sy6529_FSW_SET_1000KHZ              7

#define sy6529_FREQ_SHIFT_MASK				0x18
#define sy6529_FREQ_SHIFT_SHIFT				3
#define sy6529_FREQ_SHIFT_NORMINAL			0
#define sy6529_FREQ_SHIFT_POSITIVE10		1
#define sy6529_FREQ_SHIFT_NEGATIVE10		2
#define sy6529_FREQ_SHIFT_SPREAD_SPECTRUM	3

/* Register 02h */
#define sy6529_REG_02                       0x02
 /******* fly-short *******/
#define sy6529_FLY_SHORT_EN_MASK        0x80
#define sy6529_FLY_SHORT_EN_SHIFT       7
#define sy6529_FLY_SHORT_ENABLE         1
#define sy6529_FLY_SHORT_DISABLE        0

#define sy6529_SET_IBAT_SNS_RES_MASK        0x40
#define sy6529_SET_IBAT_SNS_RES_SHIFT       6
#define sy6529_SET_IBAT_SNS_RES_5MHM        0
#define sy6529_SET_IBAT_SNS_RES_2MHM        1
 /******* VBUS_HIGH/LOW ERR *******/

#define sy6529_VBUS_LOW_ERR_EN_MASK        0x20
#define sy6529_VBUS_LOW_ERR_EN_SHIFT       5
#define sy6529_VBUS_LOW_ERR_ENABLE         1
#define sy6529_VBUS_LOW_ERR_DISABLE        0

#define	sy6529_VBUS_LOW_ERR_MASK		0x0C
#define sy6529_VBUS_LOW_ERR_SHIFT       2
#define sy6529_VBUS_LOW_ERR_1P10        0
#define sy6529_VBUS_LOW_ERR_1P15        1
#define sy6529_VBUS_LOW_ERR_1P20        2
#define sy6529_VBUS_LOW_ERR_1P25        3

#define sy6529_VBUS_HIGH_ERR_EN_MASK        0x10
#define sy6529_VBUS_HIGH_ERR_EN_SHIFT       4
#define sy6529_VBUS_HIGH_ERR_ENABLE         1
#define sy6529_VBUS_HIGH_ERR_DISABLE        0

#define	sy6529_VBUS_HIGH_ERR_MASK		 0x03
#define sy6529_VBUS_HIGH_ERR_SHIFT        0
#define sy6529_VBUS_HIGH_ERR_1P10        0
#define sy6529_VBUS_HIGH_ERR_1P15        1
#define sy6529_VBUS_HIGH_ERR_1P20        2
#define sy6529_VBUS_HIGH_ERR_1P25        3

/* Register 03h */

/* Register 04h */
#define sy6529_REG_04					0x04

#define	sy6529_AC_OVP_EN_MASK		    0x10
#define	sy6529_AC_OVP_EN_SHIFT		4

#define sy6529_AC_OVP_MASK				0x08
#define sy6529_AC_OVP_SHIFT				0
#define sy6529_AC_OVP_BASE				4000
#define sy6529_AC_OVP_LSB				1000
#define sy6529_AC_OVP_12V				12000

/* Register 05h */
#define sy6529_REG_05					0x05

#define sy6529_REG_05				   0x05			// reg03->reg05
#define sy6529_VAC_PD_EN_MASK          0x80
#define sy6529_VAC_PD_EN_SHIFT         7
#define sy6529_VAC_PD_ENABLE           1
#define sy6529_VAC_PD_DISABLE          0

#define sy6529_VBUS_PD_EN_MASK              0x40
#define sy6529_VBUS_PD_EN_SHIFT             6
#define sy6529_VBUS_PD_ENABLE               1
#define sy6529_VBUS_PD_DISABLE              0

#define sy6529_VDROP_OVP_DIS_MASK		0x20
#define sy6529_VDROP_OVP_DIS_SHIFT	    5
#define sy6529_VDROP_OVP_ENABLE		    1
#define sy6529_VDROP_OVP_DISABLE		0

#define sy6529_VDROP_DEGLITCH_SET_MASK      0x10
#define sy6529_VDROP_DEGLITCH_SET_SHIFT     4
#define sy6529_VDROP_DEGLITCH_SET_5MS       1
#define sy6529_VDROP_DEGLITCH_SET_10US       0

#define sy6529_VDROP_OVP_THRESHOLD_MASK     0x07
#define sy6529_VDROP_OVP_THRESHOLD_SHIFT    0
#define sy6529_VDROP_OVP_THRESHOLD_BASE     50
#define sy6529_VDROP_OVP_THRESHOLD_LSB      50

/* Register 06h */
#define sy6529_REG_06                       0x06
#define	sy6529_VBUS_OVP_DIS_MASK            0x80
#define	sy6529_VBUS_OVP_DIS_SHIFT           7
#define	sy6529_VBUS_OVP_ENABLE              1
#define	sy6529_VBUS_OVP_DISABLE             0

#define	sy6529_VBUS_OVP_MASK                0x7F
#define	sy6529_VBUS_OVP_SHIFT               0
#define	sy6529_VBUS_OVP_BASE                4000
#define	sy6529_VBUS_OVP_LSB                 100
#define	sy6529_VBUS_OVP_12V					12000			// OVP max setting

/* Register 07h */
#define sy6529_REG_07                       0x07		// ->reg05; UCP_deltich time->reg0x4D
#define	sy6529_IBUS_UCP_DIS_MASK            0x80
#define	sy6529_IBUS_UCP_DIS_SHIFT           7
#define	sy6529_IBUS_UCP_ENABLE              1
#define	sy6529_IBUS_UCP_DISABLE             0
/**** UCP_TH ****/

#define	sy6529_IBUS_OCP_DIS_MASK            0x20
#define	sy6529_IBUS_OCP_DIS_SHIFT           5
#define	sy6529_IBUS_OCP_ENABLE              1
#define	sy6529_IBUS_OCP_DISABLE             0

#define	sy6529_IBUS_OCP_MASK                0x1F
#define	sy6529_IBUS_OCP_SHIFT               0
#define	sy6529_IBUS_OCP_BASE                500
#define	sy6529_IBUS_OCP_LSB                 100

/* Register 08h */
#define sy6529_REG_08					0x08			// reg00->reg08  bit7=1 Disable->Enable
#define	sy6529_BAT_OVP_DIS_MASK			0x80
#define	sy6529_BAT_OVP_DIS_SHIFT		7
#define	sy6529_BAT_OVP_ENABLE			1				// exchange 0/1
#define	sy6529_BAT_OVP_DISABLE			0

#define sy6529_BAT_OVP_MASK				0x3F
#define sy6529_BAT_OVP_SHIFT			0
#define sy6529_BAT_OVP_BASE				3500
#define sy6529_BAT_OVP_LSB				25

/* Register 09h */
#define sy6529_REG_09					0x09			// reg01->reg09
#define	sy6529_BAT_OCP_DIS_MASK			0x80
#define	sy6529_BAT_OCP_DIS_SHIFT		7
#define	sy6529_BAT_OCP_ENABLE			1
#define	sy6529_BAT_OCP_DISABLE			0

#define sy6529_BAT_OCP_MASK			    0x3F
#define sy6529_BAT_OCP_SHIFT		    0
#define sy6529_BAT_OCP_BASE			    2000
#define sy6529_BAT_OCP_LSB			    100

/* Register 0Ah */
#define sy6529_REG_0A                       0x0A

#define sy6529_IBAT_REG_EN_MASK             0x20
#define sy6529_IBAT_REG_EN_SHIFT            5
#define sy6529_IBAT_REG_ENABLE              1
#define sy6529_IBAT_REG_DISABLE             0

#define sy6529_SET_IBATREG_MASK             0x18
#define sy6529_SET_IBATREG_SHIFT            3
#define sy6529_SET_IBATREG_200MA            0
#define sy6529_SET_IBATREG_300MA            1
#define sy6529_SET_IBATREG_400MA            2
#define sy6529_SET_IBATREG_500MA            3

#define sy6529_VBAT_REG_EN_MASK             0x04
#define sy6529_VBAT_REG_EN_SHIFT            2
#define sy6529_VBAT_REG_ENABLE              1
#define sy6529_VBAT_REG_DISABLE             0

#define sy6529_SET_VBATREG_MASK             0x03
#define sy6529_SET_VBATREG_SHIFT            0
#define sy6529_SET_VBATREG_50MV             0
#define sy6529_SET_VBATREG_100MV            1
#define sy6529_SET_VBATREG_150MV            2
#define sy6529_SET_VBATREG_200MV            3

/* Register 0Bh */
#define sy6529_REG_0B                       0x0B

#define	sy6529_AC_OVP_FLAG_MASK		    0x80			// REG0x0B Flag
#define	sy6529_AC_OVP_FALG_SHIFT		7

 /******* VAC/VBUS_PULL_DOWN FLAG *******/
#define	sy6529_VAC_PD_FLAG_MASK      0x40
#define	sy6529_VAC_PD_FALG_SHIFT     6
#define	sy6529_VBUS_PD_FLAG_MASK      0x20
#define	sy6529_VBUS_PD_FALG_SHIFT	  5

#define	sy6529_VDROP_OVP_FLAG_MASK      0x10
#define	sy6529_VDROP_OVP_FALG_SHIFT		4
#define sy6529_VBUS_OVP_FLAG_MASK           0x08
#define sy6529_VBUS_OVP_FLAG_SHIFT          3

#define sy6529_IBUS_OCP_FLAG_MASK           0x04
#define sy6529_IBUS_OCP_FLAG_SHIFT          2

#define sy6529_IBUS_UCP_RISE_FLAG_MASK      0x02
#define sy6529_IBUS_UCP_RISE_FLAG_SHIFT     1

#define sy6529_IBUS_UCP_FALL_FLAG_MASK      0x01
#define sy6529_IBUS_UCP_FALL_FLAG_SHIFT     0

/* Register 0Ch */
#define sy6529_REG_0C                       0x0C
#define	sy6529_AC_OVP_MASK_MASK		    0x80			// REG0x0C MASK
#define	sy6529_AC_OVP_MASK_SHIFT		7

#define	sy6529_VAC_PD_MASK_MASK      0x40
#define	sy6529_VAC_PD_MASK_SHIFT     6

#define	sy6529_VBUS_PD_MASK_MASK      0x20
#define	sy6529_VBUS_PD_MASK_SHIFT	  5

#define	sy6529_VDROP_OVP_MASK_MASK      0x10
#define	sy6529_VDROP_OVP_MASK_SHIFT		4
#define sy6529_VBUS_OVP_MASK_MASK           0x08
#define sy6529_VBUS_OVP_MASK_SHIFT          3

#define sy6529_IBUS_OCP_MASK_MASK           0x04
#define sy6529_IBUS_OCP_MASK_SHIFT          2

#define sy6529_IBUS_UCP_RISE_MASK_MASK      0x02
#define sy6529_IBUS_UCP_RISE_MASK_SHIFT     1

#define sy6529_IBUS_UCP_FALL_MASK_MASK      0x01
#define sy6529_IBUS_UCP_FALL_MASK_SHIFT     0

/* Register 0Dh */
#define sy6529_REG_0D                       0x0D

#define sy6529_VBAT_OVP_FLAG_MASK           0x80
#define sy6529_VBAT_OVP_FLAG_SHIFT          7

#define sy6529_IBAT_OCP_FLAG_MASK           0x40
#define sy6529_IBAT_OCP_FLAG_SHIFT          6

#define sy6529_VBATREG_ACTIVE_FLAG_MASK     0x20
#define sy6529_VBATREG_ACTIVE_FLAG_SHIFT    5

#define sy6529_IBATREG_ACTIVE_FLAG_MASK     0x10
#define sy6529_IBATREG_ACTIVE_FLAG_SHIFT    4

#define sy6529_TSHUT_FLAG_MASK              0x08
#define sy6529_TSHUT_FLAG_SHIFT             3

#define sy6529_VBUS2OUT_UVP_FLAG_MASK       0x04
#define sy6529_VBUS2OUT_UVP_FLAG_SHIFT      2

#define sy6529_VBUS2OUT_OVP_FLAG_MASK       0x02
#define sy6529_VBUS2OUT_OVP_FLAG_SHIFT      1

#define sy6529_CONV_OCP_FLAG_MASK           0x01
#define sy6529_CONV_OCP_FLAG_SHIFT          0


/* Register 0Eh */
#define sy6529_REG_0E                       0x0E

#define sy6529_VBAT_OVP_MASK_MASK           0x80
#define sy6529_VBAT_OVP_MASK_SHIFT          7

#define sy6529_IBAT_OCP_MASK_MASK           0x40
#define sy6529_IBAT_OCP_MASK_SHIFT          6

#define sy6529_VBATREG_ACTIVE_MASK_MASK     0x20
#define sy6529_VBATREG_ACTIVE_MASK_SHIFT    5

#define sy6529_IBATREG_ACTIVE_MASK_MASK     0x10
#define sy6529_IBATREG_ACTIVE_MASK_SHIFT    4

#define sy6529_TSHUT_MASK_MASK              0x08
#define sy6529_TSHUT_MASK_SHIFT             3

#define sy6529_VBUS2OUT_UVP_MASK_MASK       0x04
#define sy6529_VBUS2OUT_UVP_MASK_SHIFT      2

#define sy6529_VBUS2OUT_OVP_MASK_MASK       0x02
#define sy6529_VBUS2OUT_OVP_MASK_SHIFT      1

#define sy6529_CONV_OCP_MASK_MASK           0x01
#define sy6529_CONV_OCP_MASK_SHIFT          0


/* Register 0Fh */
#define sy6529_REG_0F                       0x0F

#define sy6529_VBUS_INSERT_FLAG_MASK        0x80			// adapter insert
#define sy6529_VBUS_INSERT_FLAG_SHIFT       7

#define sy6529_VBAT_INSERT_FLAG_MASK        0x40
#define sy6529_VBAT_INSERT_FLAG_SHIFT       6

#define sy6529_WD_TIMEOUT2_FLAG_MASK        0x20
#define sy6529_WD_TIMEOUT2_FLAG_SHIFT       5

#define sy6529_VAC_UVLO_FLAG_MASK           0x10
#define sy6529_VAC_UVLO_FLAG_SHIFT          4

#define sy6529_VBUS_UVLO_FLAG_MASK          0x08
#define sy6529_VBUS_UVLO_FLAG_SHIFT         3

#define sy6529_IBUS_UCP_TIMEOUT2_FLAG_MASK     0x04
#define sy6529_IBUS_UCP_TIMEOUT2_FLAG_SHIFT    2

#define sy6529_ADC_DONE_FLAG_MASK           0x02
#define sy6529_ADC_DONE_FLAG_SHIFT          1

#define sy6529_PIN_DIAG_FALL_FLAG_MASK      0x01	// ->reg06
#define sy6529_PIN_DIAG_FALL_FLAG_SHIFT     0

/* Register 10h */
#define sy6529_REG_10                       0x10

#define sy6529_VBUS_INSERT_MASK_MASK        0x80
#define sy6529_VBUS_INSERT_MASK_SHIFT       7

#define sy6529_VBAT_INSERT_MASK_MASK        0x40
#define sy6529_VBAT_INSERT_MASK_SHIFT       6

#define sy6529_WD_TIMEOUT2_MASK_MASK        0x20
#define sy6529_WD_TIMEOUT2_MASK_SHIFT       5

#define sy6529_VAC_UVLO_MASK_MASK           0x10
#define sy6529_VAC_UVLO_MASK_SHIFT          4

#define sy6529_VBUS_UVLO_MASK_MASK          0x08
#define sy6529_VBUS_UVLO_MASK_SHIFT         3

#define sy6529_IBUS_UCP_TIMEOUT2_MASK_MASK     0x04
#define sy6529_IBUS_UCP_TIMEOUT2_MASK_SHIFT    2

#define sy6529_ADC_DONE_MASK_MASK           0x02
#define sy6529_ADC_DONE_MASK_SHIFT          1

#define sy6529_PIN_DIAG_FALL_MASK_MASK      0x01	// ->reg06
#define sy6529_PIN_DIAG_FALL_MASK_SHIFT     0

/* Register 11h */
#define sy6529_REG_11                       0x11

#define sy6529_ADC_EN_MASK                  0x80
#define sy6529_ADC_EN_SHIFT                 7
#define sy6529_ADC_ENABLE                   1
#define sy6529_ADC_DISABLE                  0

#define sy6529_ADC_RATE_MASK                0x40
#define sy6529_ADC_RATE_SHIFT               6
#define sy6529_ADC_RATE_CONTINOUS           0
#define sy6529_ADC_RATE_ONESHOT             1

#define sy6529_VBUS_ADC_DIS_MASK            0x20
#define sy6529_VBUS_ADC_DIS_SHIFT           5
#define sy6529_VBUS_ADC_ENABLE              0
#define sy6529_VBUS_ADC_DISABLE             1

#define sy6529_IBUS_ADC_DIS_MASK            0x10
#define sy6529_IBUS_ADC_DIS_SHIFT           4
#define sy6529_IBUS_ADC_ENABLE              0
#define sy6529_IBUS_ADC_DISABLE             1

#define sy6529_VBAT_ADC_DIS_MASK            0x08
#define sy6529_VBAT_ADC_DIS_SHIFT           3
#define sy6529_VBAT_ADC_ENABLE              0
#define sy6529_VBAT_ADC_DISABLE             1

#define sy6529_IBAT_ADC_DIS_MASK            0x04
#define sy6529_IBAT_ADC_DIS_SHIFT           2
#define sy6529_IBAT_ADC_ENABLE              0
#define sy6529_IBAT_ADC_DISABLE             1

#define sy6529_TDIE_ADC_DIS_MASK            0x02
#define sy6529_TDIE_ADC_DIS_SHIFT           1
#define sy6529_TDIE_ADC_ENABLE              0
#define sy6529_TDIE_ADC_DISABLE             1

/* Register 12h */
#define sy6529_REG_12                       0x12 //需要增加sign位

#define sy6529_VBUS_ADC_SIGN_MASK           0x80
#define sy6529_VBUS_ADC_SIGN_SHIFT           7
#define sy6529_VBUS_ADC_POSITIVE            0
#define sy6529_VBUS_ADC_NEGATIVE            1

#define sy6529_VBUS_POL_H_MASK              0x7F
#define sy6529_VBUS_ADC_LSB                 5/10000

/* Register 13h */
#define sy6529_REG_13                       0x13
#define sy6529_VBUS_POL_L_MASK              0xFF

/* Register 14h */
#define sy6529_REG_14                       0x14

#define sy6529_IBUS_ADC_SIGN_MASK           0x80
#define sy6529_IBUS_ADC_SIGN_SHIFT           7
#define sy6529_IBUS_ADC_POSITIVE            0
#define sy6529_IBUS_ADC_NEGATIVE            1

#define sy6529_IBUS_POL_H_MASK              0x7F
#define sy6529_IBUS_ADC_LSB                 5/32000

/* Register 15h */
#define sy6529_REG_15                       0x15
#define sy6529_IBUS_POL_L_MASK              0xFF

/* Register 16h */
#define sy6529_REG_16                       0x16

#define sy6529_VBAT_ADC_SIGN_MASK           0x80
#define sy6529_VBAT_ADC_SIGN_SHIFT           7
#define sy6529_VBAT_ADC_POSITIVE            0
#define sy6529_VBAT_ADC_NEGATIVE            1

#define sy6529_VBAT_POL_H_MASK              0x7F
#define sy6529_VBAT_ADC_LSB                 5/32000

/* Register 17h */
#define sy6529_REG_17                       0x17
#define sy6529_VBAT_POL_L_MASK              0xFF

/* Register 18h */
#define sy6529_REG_18                       0x18

#define sy6529_IBAT_ADC_SIGN_MASK           0x80
#define sy6529_IBAT_ADC_SIGN_SHIFT           7
#define sy6529_IBAT_ADC_POSITIVE            0
#define sy6529_IBAT_ADC_NEGATIVE            1


#define sy6529_IBAT_POL_H_MASK              0x7F
#define sy6529_IBAT_ADC_LSB                 5/16000

/* Register 19h */
#define sy6529_REG_19                       0x19
#define sy6529_IBAT_POL_L_MASK              0xFF

/* Register 1Ah */
#define sy6529_REG_1A                       0x1A

#define sy6529_TDIE_ADC_MASK                0XFF
#define sy6529_TDIE_ADC_SHIFT               0
#define sy6529_TDIE_ADC_BASE                -40
#define sy6529_TDIE_ADC_LSB                 1     // 需要检查TDIE ADC定义是否正确

/* Register 2Bh */
#define sy6529_REG_2B                       0x2B
#define sy6529_VOOC_EN_MASK                 0x80
#define sy6529_VOOC_EN_SHIFT                7
#define sy6529_VOOC_ENABLE                  1
#define sy6529_VOOC_DISABLE                 0

#define sy6529_SOFT_RESET_MASK              0x02
#define sy6529_SOFT_RESET_SHIFT             1
#define sy6529_SOFT_RESET                   1

#define sy6529_SEND_SEQ_MASK                0x01
#define sy6529_SEND_SEQ_SHIFT               0
#define sy6529_SEND_SEQ                     1

/* Register 2Ch */
#define sy6529_REG_2C                       0x2C
#define sy6529_TX_WDATA_POL_H_MASK          0x03

/* Register 2Dh */
#define sy6529_REG_2D                       0x2D
#define sy6529_TX_WDATA_POL_L_MASK          0xFF

/* Register 2Eh */
#define sy6529_REG_2E                       0x2E
#define sy6529_RX_RDATA_POL_H_MASK          0xFF

/* Register 2Fh */
#define sy6529_REG_2F                       0x2F
#define sy6529_PULSE_FILTERED_STAT_MASK     0x80
#define sy6529_PULSE_FILTERED_STAT_SHIFT    7

#define sy6529_NINTH_CLK_ERR_FALG_MASK      0x40
#define sy6529_NINTH_CLK_ERR_FALG_SHIFT     6

#define sy6529_TXSEQ_DONE_FLAG_MASK         0x20
#define sy6529_TXSEQ_DONE_FLAG_SHIFT        5

#define sy6529_ERR_TRANS_DET_FLAG_MASK      0x10
#define sy6529_ERR_TRANS_DET_FLAG_SHIFT     4

#define sy6529_TXDATA_WR_FAIL_FLAG_MASK     0x08
#define sy6529_TXDATA_WR_FAIL_FLAG_SHIFT    3

#define sy6529_RX_START_FLAG_MASK           0x04
#define sy6529_RX_START_FLAG_SHIFT          2

#define sy6529_RXDATA_DONE_FLAG_MASK        0x02
#define sy6529_RXDATA_DONE_FLAG_SHIFT       1

#define sy6529_TXDATA_DONE_FLAG_MASK        0x01
#define sy6529_TXDATA_DONE_FLAG_SHIFT       0

/* Register 30h */
#define sy6529_REG_30                       0x30

#define sy6529_PULSE_FILTERED_MASK_MASK     0x80
#define sy6529_PULSE_FILTERED_MASK_SHIFT    7

#define sy6529_NINTH_CLK_ERR_MASK_MASK      0x40
#define sy6529_NINTH_CLK_ERR_MASK_SHIFT     6

#define sy6529_TXSEQ_DONE_MASK_MASK         0x20
#define sy6529_TXSEQ_DONE_MASK_SHIFT        5

#define sy6529_ERR_TRANS_DET_MASK_MASK      0x10
#define sy6529_ERR_TRANS_DET_MASK_SHIFT     4

#define sy6529_TXDATA_WR_FAIL_MASK_MASK     0x08
#define sy6529_TXDATA_WR_FAIL_MASK_SHIFT    3

#define sy6529_RX_START_MASK_MASK           0x04
#define sy6529_RX_START_MASK_SHIFT          2

#define sy6529_RXDATA_DONE_MASK_MASK        0x02
#define sy6529_RXDATA_DONE_MASK_SHIFT       1

#define sy6529_TXDATA_DONE_MASK_MASK        0x01
#define sy6529_TXDATA_DONE_MASK_SHIFT       0

/* Register 31h */
#define sy6529_REG_31                       0x31
#define sy6529_PRE_WDATA_POL_H_MASK         0x03

/* Register 32h */
#define sy6529_REG_32                       0x32
#define sy6529_PRE_WDATA_POL_L_MASK         0xFF

/* Register 33h */
#define sy6529_REG_33                       0x33
#define sy6529_LOOSE_DET_MASK               0x80
#define sy6529_LOOSE_DET_SHIFT              7
#define sy6529_LOOSE_WINDOW_HIGH            0
#define sy6529_LOOSE_WINDOW_LOW             1

#define sy6529_LOW_CHECK_EN_MASK            0x40
#define sy6529_LOW_CHECK_EN_SHIFT           6

#define sy6529_END_TIME_SET_MASK            0x30
#define sy6529_END_TIME_SET_SHIFT           4
#define sy6529_NINE_5_5MS_AFTER_NINE_1_5MS  0
#define sy6529_NINE_6MS_AFTER_NINE_2MS      1
#define sy6529_NINE_6_5MS_AFTER_NINE_2_5MS  2
#define sy6529_NINE_7MS_AFTER_NINE_3MS      3

#define sy6529_RX_SAMPLE_TIME_MASK          0x03
#define sy6529_RX_SAMPLE_TIME_SHIFT         4
#define sy6529_RX_SAMPLE_TIME_5US           0
#define sy6529_RX_SAMPLE_TIME_150US         1
#define sy6529_RX_SAMPLE_TIME_300US         2
#define sy6529_RX_SAMPLE_TIME_450US         3

/* Register 36h */
#define sy6529_REG_36                       0x36

#define sy6529_DEVICE_VERSION_MASK          0xF0
#define sy6529_DEVICE_VERSION_SHIFT         4

#define sy6529_DEVICE_ID_MASK               0x0F
#define sy6529_DEVICE_ID_SHIFT              0

/* Register 3Ah */
#define sy6529_REG_3A						0x3A

#define sy6529_MSG_END_MASK                 0x80
#define sy6529_MSG_END_SHIFT                7
#define sy6529_MSG_END_ENDTIME              0
#define sy6529_MSG_END_19TH CLOCK FALL      1

/* Register 4Dh */
#define sy6529_REG_4D                       0x4D

#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_MASK   0xC0
#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_SHIFT  6
#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_10US   0
#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_5MS    1
#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_40MS    2
#define sy6529_IBUS_UCP_FALL_DEGLITCH_SET_160MS    3

#define sy6529_ADC_SAMPLE_RATE_MASK                 0x01
#define sy6529_ADC_SAMPLE_RATE_SHIFT                0
#define sy6529_ADC_SAMPLE_RATE_LOW                  0
#define sy6529_ADC_SAMPLE_RATE_HIGH				   	1

#endif


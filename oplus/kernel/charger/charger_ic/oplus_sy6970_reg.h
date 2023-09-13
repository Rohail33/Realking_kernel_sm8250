/*copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __SY6970_HEADER__
#define __SY6970_HEADER__

/* Register 00h */
#define SY6970_REG_00			0x00
#define SY6970_ENHIZ_MASK		    0x80
#define SY6970_ENHIZ_SHIFT		    7
#define SY6970_HIZ_ENABLE          1
#define SY6970_HIZ_DISABLE         0
#define SY6970_ENILIM_MASK		    0x40
#define SY6970_ENILIM_SHIFT		6
#define SY6970_ENILIM_ENABLE       1
#define SY6970_ENILIM_DISABLE      0

#define SY6970_IINLIM_MASK		    0x3F
#define SY6970_IINLIM_SHIFT		0
#define SY6970_IINLIM_BASE         100
#define SY6970_IINLIM_LSB          50

/* Register 01h */
#define SY6970_REG_01			0x01
#ifdef _BQ25890H_
#define	SY6970_DPDAC_MASK			0xE0
#define	SY6970_DPDAC_SHIFT			5
#define SY6970_DP_HIZ				0x00
#define SY6970_DP_0V				0x01
#define SY6970_DP_0P6V				0x02
#define SY6970_DP_1P2V				0x03
#define SY6970_DP_2P0V				0x04
#define SY6970_DP_2P7V				0x05
#define SY6970_DP_3P3V				0x06
#define SY6970_DP_SHORT			0x07

#define	SY6970_DMDAC_MASK			0x1C
#define	SY6970_DMDAC_SHIFT			2
#define SY6970_DM_HIZ				0x00
#define SY6970_DM_0V				0x01
#define SY6970_DM_0P6V				0x02
#define SY6970_DM_1P2V				0x03
#define SY6970_DM_2P0V				0x04
#define SY6970_DM_2P7V				0x05
#define SY6970_DM_3P3V				0x06

#define	SY6970_EN12V_MASK			0x02
#define	SY6970_EN12V_SHIFT			1
#define	SY6970_ENABLE_12V			1
#define	SY6970_DISABLE_12V			0

#define SY6970_VINDPMOS_MASK       0x01
#define SY6970_VINDPMOS_SHIFT      0
#define	SY6970_VINDPMOS_400MV		0
#define	SY6970_VINDPMOS_600MV		1
#else
#define SY6970_VINDPMOS_MASK       0x1F
#define SY6970_VINDPMOS_SHIFT      0
#define	SY6970_VINDPMOS_BASE		0
#define	SY6970_VINDPMOS_LSB		100
#endif


/* Register 0x02 */
#define SY6970_REG_02              0x02
#define SY6970_CONV_START_MASK      0x80
#define SY6970_CONV_START_SHIFT     7
#define SY6970_CONV_START           1
#define SY6970_CONV_RATE_MASK       0x40
#define SY6970_CONV_RATE_SHIFT      6
#define SY6970_ADC_CONTINUE_ENABLE  1
#define SY6970_ADC_CONTINUE_DISABLE 0

#define SY6970_BOOST_FREQ_MASK      0x20
#define SY6970_BOOST_FREQ_SHIFT     5
#define SY6970_BOOST_FREQ_1500K     0
#define SY6970_BOOST_FREQ_500K      1

#define SY6970_ICOEN_MASK          0x10
#define SY6970_ICOEN_SHIFT         4
#define SY6970_ICO_ENABLE          1
#define SY6970_ICO_DISABLE         0
#define SY6970_HVDCPEN_MASK        0x08
#define SY6970_HVDCPEN_SHIFT       3
#define SY6970_HVDCPHV_SHIFT       2
#define SY6970_HVDCP_ENABLE        1
#define SY6970_HVDCP_DISABLE       0
#define SY6970_MAXCEN_MASK         0x04
#define SY6970_MAXCEN_SHIFT        2
#define SY6970_MAXC_ENABLE         1
#define SY6970_MAXC_DISABLE        0

#define SY6970_FORCE_DPDM_MASK     0x02
#define SY6970_FORCE_DPDM_SHIFT    1
#define SY6970_FORCE_DPDM          1
#define SY6970_AUTO_DPDM_EN_MASK   0x01
#define SY6970_AUTO_DPDM_EN_SHIFT  0
#define SY6970_AUTO_DPDM_ENABLE    1
#define SY6970_AUTO_DPDM_DISABLE   0

/* Register 0x03 */
#define SY6970_REG_03              0x03
#define SY6970_BAT_VOKOTG_EN_MASK   0x80
#define SY6970_BAT_VOKOTG_EN_SHIFT  7
#define SY6970_BAT_FORCE_DSEL_MASK  0x80
#define SY6970_BAT_FORCE_DSEL_SHIFT 7

#define SY6970_WDT_RESET_MASK      0x40
#define SY6970_WDT_RESET_SHIFT     6
#define SY6970_WDT_RESET           1

#define SY6970_OTG_CONFIG_MASK     0x20
#define SY6970_OTG_CONFIG_SHIFT    5
#define SY6970_OTG_ENABLE          1
#define SY6970_OTG_DISABLE         0

#define SY6970_CHG_CONFIG_MASK     0x10
#define SY6970_CHG_CONFIG_SHIFT    4
#define SY6970_CHG_ENABLE          1
#define SY6970_CHG_DISABLE         0

#define SY6970_SYS_MINV_MASK       0x0E
#define SY6970_SYS_MINV_SHIFT      1

#define SY6970_SYS_MINV_BASE       3000
#define SY6970_SYS_MINV_LSB        100


/* Register 0x04*/
#define SY6970_REG_04              0x04
#define SY6970_EN_PUMPX_MASK       0x80
#define SY6970_EN_PUMPX_SHIFT      7
#define SY6970_PUMPX_ENABLE        1
#define SY6970_PUMPX_DISABLE       0
#define SY6970_ICHG_MASK           0x7F
#define SY6970_ICHG_SHIFT          0
#define SY6970_ICHG_BASE           0
#define SY6970_ICHG_LSB            64

/* Register 0x05*/
#define SY6970_REG_05              0x05
#define SY6970_IPRECHG_MASK        0xF0
#define SY6970_IPRECHG_SHIFT       4
#define SY6970_ITERM_MASK          0x0F
#define SY6970_ITERM_SHIFT         0
#define SY6970_IPRECHG_BASE        64
#define SY6970_IPRECHG_LSB         64
#define SY6970_ITERM_BASE          64
#define SY6970_ITERM_LSB           64

/* Register 0x06*/
#define SY6970_REG_06              0x06
#define SY6970_VREG_MASK           0xFC
#define SY6970_VREG_SHIFT          2
#define SY6970_BATLOWV_MASK        0x02
#define SY6970_BATLOWV_SHIFT       1
#define SY6970_BATLOWV_2800MV      0
#define SY6970_BATLOWV_3000MV      1
#define SY6970_VRECHG_MASK         0x01
#define SY6970_VRECHG_SHIFT        0
#define SY6970_VRECHG_100MV        0
#define SY6970_VRECHG_200MV        1
#define SY6970_VREG_BASE           3840
#define SY6970_VREG_LSB            16

/* Register 0x07*/
#define SY6970_REG_07              0x07
#define SY6970_EN_TERM_MASK        0x80
#define SY6970_EN_TERM_SHIFT       7
#define SY6970_TERM_ENABLE         1
#define SY6970_TERM_DISABLE        0

#define SY6970_WDT_MASK            0x30
#define SY6970_WDT_SHIFT           4
#define SY6970_WDT_DISABLE         0
#define SY6970_WDT_40S             1
#define SY6970_WDT_80S             2
#define SY6970_WDT_160S            3
#define SY6970_WDT_BASE            0
#define SY6970_WDT_LSB             40

#define SY6970_EN_TIMER_MASK       0x08
#define SY6970_EN_TIMER_SHIFT      3

#define SY6970_CHG_TIMER_ENABLE    1
#define SY6970_CHG_TIMER_DISABLE   0

#define SY6970_CHG_TIMER_MASK      0x06
#define SY6970_CHG_TIMER_SHIFT     1
#define SY6970_CHG_TIMER_5HOURS    0
#define SY6970_CHG_TIMER_8HOURS    1
#define SY6970_CHG_TIMER_12HOURS   2
#define SY6970_CHG_TIMER_20HOURS   3

#define SY6970_JEITA_ISET_MASK     0x01
#define SY6970_JEITA_ISET_SHIFT    0
#define SY6970_JEITA_ISET_50PCT    0
#define SY6970_JEITA_ISET_20PCT    1


/* Register 0x08*/
#define SY6970_REG_08              0x08
#define SY6970_BAT_COMP_MASK       0xE0
#define SY6970_BAT_COMP_SHIFT      5
#define SY6970_VCLAMP_MASK         0x1C
#define SY6970_VCLAMP_SHIFT        2
#define SY6970_TREG_MASK           0x03
#define SY6970_TREG_SHIFT          0
#define SY6970_TREG_60C            0
#define SY6970_TREG_80C            1
#define SY6970_TREG_100C           2
#define SY6970_TREG_120C           3

#define SY6970_BAT_COMP_BASE       0
#define SY6970_BAT_COMP_LSB        20
#define SY6970_VCLAMP_BASE         0
#define SY6970_VCLAMP_LSB          32

/* Register 0x09*/
#define SY6970_REG_09              0x09
#define SY6970_FORCE_ICO_MASK      0x80
#define SY6970_FORCE_ICO_SHIFT     7
#define SY6970_FORCE_ICO           1
#define SY6970_TMR2X_EN_MASK       0x40
#define SY6970_TMR2X_EN_SHIFT      6
#define SY6970_BATFET_DIS_MASK     0x20
#define SY6970_BATFET_DIS_SHIFT    5
#define SY6970_BATFET_OFF          1
#define SY6970_BATFET_ON			0
#define SY6970_BATFET_OFF_IMMEDIATELY		1
#define BQ25890H_BATFET_OFF_IMMEDIATELY		0
#define REG09_SY6970_BATFET_DLY_SHIFT	3
#define SY6970_BATFET_DLY_MASK	   0x08


#define SY6970_JEITA_VSET_MASK     0x10
#define SY6970_JEITA_VSET_SHIFT    4
#define SY6970_JEITA_VSET_N150MV   0
#define SY6970_JEITA_VSET_VREG     1
#define SY6970_BATFET_RST_EN_MASK  0x04
#define SY6970_BATFET_RST_EN_SHIFT 2
#define SY6970_BATFET_RST_EN_DISABLE 0
#define SY6970_PUMPX_UP_MASK       0x02
#define SY6970_PUMPX_UP_SHIFT      1
#define SY6970_PUMPX_UP            1
#define SY6970_PUMPX_DOWN_MASK     0x01
#define SY6970_PUMPX_DOWN_SHIFT    0
#define SY6970_PUMPX_DOWN          1

/* Register 0x0A*/
#define SY6970_REG_0A              0x0A
#define SY6970_BOOSTV_MASK         0xF0
#define SY6970_BOOSTV_SHIFT        4
#define SY6970_BOOSTV_BASE         4550
#define SY6970_BOOSTV_LSB          64

#define	SY6970_PFM_OTG_DIS_MASK	0x08
#define	SY6970_PFM_OTG_DIS_SHIFT	3

#define SY6970_BOOST_LIM_MASK      0x07
#define SY6970_BOOST_LIM_SHIFT     0
#define SY6970_BOOST_LIM_500MA     0x00
#define SY6970_BOOST_LIM_750MA     0x01
#define SY6970_BOOST_LIM_1200MA    0x02
#define SY6970_BOOST_LIM_1400MA    0x03
#define SY6970_BOOST_LIM_1650MA    0x04
#define SY6970_BOOST_LIM_1875MA    0x05
#define SY6970_BOOST_LIM_2150MA    0x06
#define SY6970_BOOST_LIM_2450MA    0x07

/* Register 0x0B*/
#define SY6970_REG_0B              0x0B
#define SY6970_VBUS_STAT_MASK      0xE0
#define SY6970_VBUS_STAT_SHIFT     5
#define SY6970_VBUS_TYPE_NONE		0
#define SY6970_VBUS_TYPE_SDP		1
#define SY6970_VBUS_TYPE_CDP		2
#define SY6970_VBUS_TYPE_DCP		3
#define SY6970_VBUS_TYPE_HVDCP		4
#define SY6970_VBUS_TYPE_UNKNOWN	5
#define SY6970_VBUS_TYPE_NON_STD	6
#define SY6970_VBUS_TYPE_OTG		7

#define SY6970_CHRG_STAT_MASK      0x18
#define SY6970_CHRG_STAT_SHIFT     3
#define SY6970_CHRG_STAT_IDLE      0
#define SY6970_CHRG_STAT_PRECHG    1
#define SY6970_CHRG_STAT_FASTCHG   2
#define SY6970_CHRG_STAT_CHGDONE   3

#define SY6970_PG_STAT_MASK        0x04
#define SY6970_PG_STAT_SHIFT       2
#define SY6970_SDP_STAT_MASK       0x02
#define SY6970_SDP_STAT_SHIFT      1
#define SY6970_VSYS_STAT_MASK      0x01
#define SY6970_VSYS_STAT_SHIFT     0

/* Register 0x0C*/
#define SY6970_REG_0C              0x0c
#define SY6970_FAULT_WDT_MASK      0x80
#define SY6970_FAULT_WDT_SHIFT     7
#define SY6970_FAULT_BOOST_MASK    0x40
#define SY6970_FAULT_BOOST_SHIFT   6
#define SY6970_FAULT_CHRG_MASK     0x30
#define SY6970_FAULT_CHRG_SHIFT    4
#define SY6970_FAULT_CHRG_NORMAL   0
#define SY6970_FAULT_CHRG_INPUT    1
#define SY6970_FAULT_CHRG_THERMAL  2
#define SY6970_FAULT_CHRG_TIMER    3

#define SY6970_FAULT_BAT_MASK      0x08
#define SY6970_FAULT_BAT_SHIFT     3
#define SY6970_FAULT_NTC_MASK      0x07
#define SY6970_FAULT_NTC_SHIFT     0
#define SY6970_FAULT_NTC_TSCOLD    1
#define SY6970_FAULT_NTC_TSHOT     2

#define SY6970_FAULT_NTC_WARM      2
#define SY6970_FAULT_NTC_COOL      3
#define SY6970_FAULT_NTC_COLD      5
#define SY6970_FAULT_NTC_HOT       6

/* Register 0x0D*/
#define SY6970_REG_0D              0x0D
#define SY6970_FORCE_VINDPM_MASK   0x80
#define SY6970_FORCE_VINDPM_SHIFT  7
#define SY6970_FORCE_VINDPM_ENABLE 1
#define SY6970_FORCE_VINDPM_DISABLE 0
#define SY6970_VINDPM_MASK         0x7F
#define SY6970_VINDPM_SHIFT        0

#define SY6970_VINDPM_BASE         2600
#define SY6970_VINDPM_LSB          100

/* Register 0x0E*/
#define SY6970_REG_0E              0x0E
#define SY6970_THERM_STAT_MASK     0x80
#define SY6970_THERM_STAT_SHIFT    7
#define SY6970_BATV_MASK           0x7F
#define SY6970_BATV_SHIFT          0
#define SY6970_BATV_BASE           2304
#define SY6970_BATV_LSB            20

/* Register 0x0F*/
#define SY6970_REG_0F              0x0F
#define SY6970_SYSV_MASK           0x7F
#define SY6970_SYSV_SHIFT          0
#define SY6970_SYSV_BASE           2304
#define SY6970_SYSV_LSB            20

/* Register 0x10*/
#define SY6970_REG_10              0x10
#define SY6970_TSPCT_MASK          0x7F
#define SY6970_TSPCT_SHIFT         0
#define SY6970_TSPCT_BASE          21
#define SY6970_TSPCT_LSB           465//should be 0.465,kernel does not support float

/* Register 0x11*/
#define SY6970_REG_11              0x11
#define SY6970_VBUS_GD_MASK        0x80
#define SY6970_VBUS_GD_SHIFT       7
#define SY6970_VBUSV_MASK          0x7F
#define SY6970_VBUSV_SHIFT         0
#define SY6970_VBUSV_BASE          2600
#define SY6970_VBUSV_LSB           100

/* Register 0x12*/
#define SY6970_REG_12              0x12
#define SY6970_ICHGR_MASK          0x7F
#define SY6970_ICHGR_SHIFT         0
#define SY6970_ICHGR_BASE          0
#define SY6970_ICHGR_LSB           50

/* Register 0x13*/
#define SY6970_REG_13              0x13
#define SY6970_VDPM_STAT_MASK      0x80
#define SY6970_VDPM_STAT_SHIFT     7
#define SY6970_IDPM_STAT_MASK      0x40
#define SY6970_IDPM_STAT_SHIFT     6
#define SY6970_IDPM_LIM_MASK       0x3F
#define SY6970_IDPM_LIM_SHIFT      0
#define SY6970_IDPM_LIM_BASE       100
#define SY6970_IDPM_LIM_LSB        50

/* Register 0x14*/
#define SY6970_REG_14              0x14
#define SY6970_RESET_MASK          0x80
#define SY6970_RESET_SHIFT         7
#define SY6970_RESET               1
#define SY6970_ICO_OPTIMIZED_MASK  0x40
#define SY6970_ICO_OPTIMIZED_SHIFT 6
#define SY6970_PN_MASK             0x38
#define SY6970_PN_SHIFT            3
#define SY6970_TS_PROFILE_MASK     0x04
#define SY6970_TS_PROFILE_SHIFT    2
#define SY6970_DEV_REV_MASK        0x03
#define SY6970_DEV_REV_SHIFT       0

extern void oplus_set_usb_props_type(enum power_supply_type type);
extern bool oplus_pd_without_usb(void);
int oplus_sy6970_charger_unsuspend(void);
void oplus_set_prswap(bool swap);
#endif

#ifndef __SOUTHCHIP_8517_H__
#define __SOUTHCHIP_8517_H__

#define V2X_OVUV_REG            0x00
#define V2X_OVP_OFFSET          9500
#define V2X_OVP_GAIN            500
#define V2X_OVP_START_BIT       5
#define V2X_OVP_LEN_BIT         2
#define V2X_OVP_DISABLE         BIT(7)
#define V2X_OVP_ENABLE          ~V2X_OVP_DISABLE

#define V1X_OVUV_REG            0x01
#define V1X_OVP_OFFSET          4150
#define V1X_OVP_GAIN            25
#define V1X_OVP_START_BIT       2
#define V1X_OVP_LEN_BIT         5
#define V1X_OVP_DISABLE         BIT(7)
#define V1X_OVP_ENABLE          ~V1X_OVP_DISABLE
#define V1X_SCP_DISABLE         BIT(1)
#define V1X_SCP_ENABLE          ~V1X_SCP_DISABLE

#define VAC_OVUV_REG            0x02
#define VAC_OVP_OFFSET          6500
#define VAC_OVP_GAIN            500
#define VAC_OVP_START_BIT       3
#define VAC_OVP_LEN_BIT         4
#define VAC_OVP_DISABLE         BIT(7)
#define VAC_OVP_ENABLE          ~VAC_OVP_DISABLE
#define ACDRV_ENABLE            BIT(0)
#define ACDRV_DISABLE           ~ACDRV_ENABLE

#define RVS_OCP_REG             0x03
#define RVS_OCP_OFFSET          300
#define RVS_OCP_GAIN            25
#define RVS_OCP_START_BIT       4
#define RVS_OCP_LEN_BIT         4

#define FWD_OCP_REG             0x03
#define FWD_OCP_OFFSET          400
#define FWD_OCP_GAIN            25
#define FWD_OCP_START_BIT       0
#define FWD_OCP_LEN_BIT         4

#define TIMEOUT_REG             0x04
#define WD_TIMER_START_BIT      4
#define LNC_SS_TIMER_START_BIT  1

#define STATUS_REG              0x09
#define FLAG_REG                0x0c
#define MASK_REG                0x0f

#define FUNCTION_DISABLE_REG    0x13

#define OTG_REG                 0x15
#define OTG_ENABLE              0x3
#define OTG_DISABLE             0

/* Register 00h */
#define SC8517_REG_00                      0x00


/* Register 01h */
#define SC8517_REG_01                      0x01

/* Register 02h */
#define SC8517_REG_02                       0x2

#define SC8517_CHG_EN_MASK                  BIT(0)
#define SC8517_CHG_EN_SHIFT                 0
#define SC8517_CHG_ENABLE                   1
#define SC8517_CHG_DISABLE                  0

#define SC8517_VAC_INRANGE_EN_MASK                  BIT(1)
#define SC8517_VAC_INRANGE_EN_SHIFT                 1
#define SC8517_VAC_INRANGE_ENABLE                   0
#define SC8517_VAC_INRANGE_DISABLE                  1

/* Register 04h */
#define SC8517_REG_04                      0x04


/* Register 06h */
#define SC8517_REG_06                       0x06

#define SC8517_REG_RESET_MASK               BIT(3)
#define SC8517_REG_RESET_SHIFT              3
#define SC8517_NO_REG_RESET                 0
#define SC8517_RESET_REG                    1

/* Register 07h */
#define SC8517_REG_07                       0x07

/* Register 08h */
#define SC8517_REG_08                      0x08

/* Register 09h~0Eh int flag&status */
#define SC8517_REG_09                       0x09


/* Register 0Bh */
#define SC8517_REG_0B                      0x0B
#define VBUS_INRANGE_STATUS_MASK           (BIT(7)|BIT(6))
#define VBUS_INRANGE_STATUS_SHIFT          6

/* Register 0Dh */
#define SC8517_REG_0D                      0x0D

/* Register 0Eh */
#define SC8517_REG_0E                      0x0E

/* Register 0Fh */
#define SC8517_REG_0F                      0x0F

/* Register 10h */
#define SC8517_REG_10                      0x10

/* Register 15h */
#define SC8517_REG_15                      0x15
#define SC8517_CHG_MODE_MASK               (BIT(1)|BIT(0))
#define SC8517_CHG_FIX_MODE                0
#define SC8517_CHG_AUTO_MODE               3


/* Register 27h */
#define SC8517_REG_27                       0x27
#define SC8517_RX_RDATA_POL_H_MASK          0xFF

/* Register 2Ah */
#define SC8517_REG_2A                       0x2A
#define SC8517_PRE_WDATA_POL_H_MASK         0x03

/* Register 2Bh */
#define SC8517_REG_2B                       0x2B

/* Register 25h */
#define SC8517_REG_25                       0x25
#define SC8517_TX_WDATA_POL_H_MASK          0x03

/* Register 26h */
#define SC8517_REG_26                       0x26

/* Register 29h */
#define SC8517_REG_29                       0x29
#define SC8517_REG_20                       0x20
#define SC8517_REG_21                       0x21
#define SC8517_REG_24                       0x24
#define SC8517_REG_2A                       0x2A

/* Register 24h */
#define SC8517_REG_24                       0x24
#define SC8517_VOOC_EN_MASK                 0x80
#define SC8517_VOOC_EN_SHIFT                7
#define SC8517_VOOC_ENABLE                  1
#define SC8517_VOOC_DISABLE                 0

#define SC8517_SOFT_RESET_MASK              0x02
#define SC8517_SOFT_RESET_SHIFT             1
#define SC8517_SOFT_RESET                   1

/* Register 2Ch */
#define SC8517_REG_2C                      0x2C

/* Register 2Dh */
#define SC8517_REG_2D                      0x2D
#endif

#ifndef OPTIGA_SWI_H
#define OPTIGA_SWI_H

#include "../oplus_optiga.h"

#define SWI_BAUDRATE_DEF  (100000UL)
#define SWI_BAUDRATE_MAX (500000UL)
#define SWI_BAUDRATE_MIN  (10000UL)

/* Transaction Elements */
/* BroadCast */
#define SWI_BC              (0x08u)    /* Bus Command */
#define SWI_EDA             (0x09u)    /* Extended Device Address */
#define SWI_SDA             (0x0Au)    /* Slave Device Address */
#define SWI_MDA             (0x0Bu)    /* Master Device Address */

/* MultiCast */
#define SWI_WD              (0x04u)    /* Write Data */
#define SWI_ERA             (0x05u)    /* Extended Register Address */
#define SWI_WRA             (0x06u)    /* Write Register Address */
#define SWI_RRA             (0x07u)    /* Read Register Address */

/* Unicast */
#define SWI_RD_ACK          (0x0Au)    /* ACK and not End of transmission */
#define SWI_RD_NACK         (0x08u)    /* ACK and not End of transmission */
#define SWI_RD_ACK_EOT      (0x0Bu)    /* ACK and not End of transmission */
#define SWI_RD_NACK_EOT     (0x09u)    /* ACK and not End of transmission */

/* Bus Command */
#define SWI_BRES            (0x00u)    /* Bus Reset */
#define SWI_PDWN            (0x02u)    /* Bus Power down */
#define SWI_EXBC            (0x08u)    /* Extent 00001xxx */
#define SWI_ESSM            (0x08u)    /* Enter Standard Speed Mode */
#define SWI_EHSM            (0x09u)    /* Enter High Speed Mode */
#define SWI_EINT            (0x10u)    /* Enable Interrupt */
#define SWI_RBL0            (0x20u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL1            (0x21u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL2            (0x22u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL3            (0x23u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL4            (0x24u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL5            (0x25u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL6            (0x26u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_RBL7            (0x27u)    /* RBLn Set Read Burst Length 2^n */
#define SWI_DISS            (0x30u)    /* Device ID Search Start */
#define SWI_DIMM            (0x32u)    /* Device ID Search Memory */
#define SWI_DIRC            (0x33u)    /* Device ID Search Recall */
#define SWI_DIE0            (0x34u)    /* Device ID Search Enter 0 */
#define SWI_DIE1            (0x35u)    /* Device ID Search Enter 1 */
#define SWI_DIP0            (0x36u)    /* Device ID Search Probe 0 */
#define SWI_DIP1            (0x37u)    /* Device ID Search Probe 1 */
#define SWI_DI00            (0x38u)    /* DIS Enter 0 Probe 0 (DIE0 + DIP0) */
#define SWI_DI01            (0x39u)    /* DIS Enter 0 Probe 1 (DIE0 + DIP1) */
#define SWI_DI10            (0x3Au)    /* DIS Enter 1 Probe 0 (DIE1 + DIP0) */
#define SWI_DI11            (0x3Bu)    /* DIS Enter 1 Probe 1 (DIE1 + DIP1) */
#define SWI_DASM            (0x40u)    /* Device Activation Stick Mode */
#define SWI_DACL            (0x41u)    /* Device Activation Clear */
#define SWI_ECFG            (0x50u)    /* Enable Configuration Space */
#define SWI_EREG            (0x51u)    /* Enable Register Space */
#define SWI_DMAC            (0x60u)    /* Default Master Activation */
#define SWI_CURD            (0x70u)    /* Call for Un-registered Devices */
#define SWI_DCXX            (0xC0u)    /* Device Specific Commands DC00-DC31 11xxxxxx */

/* Configration Register Address */
#define SWI_CTRL0           (0x00u)    /* Control Register 0 */
#define SWI_CTRL1           (0x01u)    /* Control Register 1 */
#define SWI_CTRL2           (0x02u)    /* Control Register 2 */
#define SWI_CTRL7           (0x07u)   /* Control Register 7 */
#define SWI_CAP0            (0x08u)    /* Capability Register 0 */
#define SWI_CAP1            (0x09u)    /* Capability Register 1 */
#define SWI_CAP4            (0x0Cu)    /* Capability Register 4 */
#define SWI_CAP5            (0x0Du)    /* Capability Register 5 */
#define SWI_CAP6            (0x0Eu)    /* Capability Register 6 */
#define SWI_CAP7            (0x0Fu)    /* Capability Register 7 */
#define SWI_INT0            (0x18u)    /* Interrupt Register 0 */
#define SWI_INT4            (0x1Cu)    /* Interrupt Register 4 */
#define SWI_DADR0           (0x20u)    /* Device Address Register 0 */
#define SWI_DADR1           (0x21u)    /* Device Address Register 1 */
#define SWI_DADR2           (0x22u)    /* Device Address Register 2 */
#define SWI_DADR3           (0x23u)    /* Device Address Register 3 */

#define SWI_STAT8           (0x38u)    /* Status Register 8 */
#define SWI_STAT12          (0x3Cu)    /* Status Register 12 */
#define SWI_UID0            (0x40u)    /* UID Register 0 */
#define SWI_UID1            (0x41u)    /* UID Register 1 */
#define SWI_UID2            (0x42u)    /* UID Register 2 */
#define SWI_UID3            (0x43u)    /* UID Register 3 */
#define SWI_UID4            (0x44u)    /* UID Register 4 */
#define SWI_UID5            (0x45u)    /* UID Register 5 */
#define SWI_UID6            (0x46u)    /* UID Register 6 */
#define SWI_UID7            (0x47u)    /* UID Register 7 */
#define SWI_UID8            (0x48u)    /* UID Register 8 */
#define SWI_UID9            (0x49u)    /* UID Register 9 */
#define SWI_UID10           (0x4Au)    /* UID Register 10 */
#define SWI_UID11           (0x4Bu)    /* UID Register 11 */

#define SWI_VNUM            (0x50u)    /* Version Number Register */

/* OPTIGA Bus Command */
#define SWI_OPTIGA_ECCSTART  (0xC0u)    /* ECC Start Command */
#define SWI_OPTIGA_ECCESTART (0xC1u)    /* ECCE Start Command */

/* OPTIGA Register */
#define SWI_OPTIGA_CTRL_SPACE		(0x0266u)
#define SWI_OPTIGA_NVM_CAL           (0x0268u)  /* NVM_Cal*/
#define SWI_OPTIGA_BG_CAL            (0x0269u)  /* BG_Cal */
#define SWI_OPTIGA_VCO_CAL           (0x026Bu)  /* VCO_Cal */
#define SWI_OPTIGA_PTAT_CAL          (0x026Du)  /* PTAT_Cal */
#define SWI_OPTIGA_CUR_CAL           (0x026Eu)  /* Cur_Cal */

#define SWI_OPTIGA_CTRL0_CLK         (0x0270u)  /* CTRL0_CLK */
#define SWI_OPTIGA_CTRL1_ADC         (0x0271u)  /* CTRL1_ADC */
#define SWI_OPTIGA_CTRL2_NVM         (0x0272u)  /* CTRL2_NVM */
#define SWI_OPTIGA_ST_CTRL           (0x0273u)  /* ST_CTRL */
#define SWI_OPTIGA_NVM_ADDR          (0x0274u)  /* NVM_ADDR */
#define SWI_OPTIGA_NVM_LOCK          (0x0275u)  /* NVM Lock control */
#define SWI_OPTIGA_NVM_LOCK_ST       (0x0276u)  /* NVM lock status */

#define SWI_ECC_BASE                (0x0000u)  /* ECC Area Base Address */
#define SWI_NVM_BASE                (0x0100u)  /* NVM Area Base Address */
#define SWI_CRYPTO_BASE             (0x0300u)  /* CRY Area Base Address */

/* NVM */
#define SWI_NVM_WIP3                (0x0018u)  /* Page Address of WIP3 */
#define SWI_NVM_WIP2                (0x0010u)  /* Page Address of WIP2 */
#define SWI_NVM_WIP1                (0x0028u)  /* Page Address of WIP1 */
#define SWI_NVM_WIP0                (0x0020u)  /* Page Address of WIP0 */


typedef struct _S_OPTIGA_PUID
{
	uint16_t uwVendorId;
	uint16_t uwProductId;
	uint32_t ulIdHigh;
	uint32_t ulIdLow;
}S_OPTIGA_PUID;

/* *** SWI High Level Functions *** */
BOOL Swi_ReadActualSpace( uint8_t * ubp_Data );
BOOL Swi_ReadRegisterSpace( uint16_t uw_Address, uint8_t * ubp_Data);
BOOL Swi_WriteRegisterSpace( uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect, BOOL b_WaitForInterrupt, BOOL b_ImmediateInterrupt, BOOL * bp_IrqDetected);
BOOL Swi_WriteRegisterSpaceNoIrq( uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect );
BOOL Swi_ReadConfigSpace( uint16_t uw_Address, uint8_t * ubp_Data );
BOOL Swi_WriteConfigSpace( uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect );
BOOL Swi_SearchPuid( uint8_t ub_BitsToSearch, S_OPTIGA_PUID * stp_DetectedPuid );
BOOL Swi_SearchMultiplePuid(uint8_t ub_BitsToSearch,  S_OPTIGA_PUID *stp_DetectedPuid, uint8_t * ubp_DevCnt );
BOOL Swi_SelectByPuid( uint8_t ub_BitsToExecute,  S_OPTIGA_PUID * stp_DeviceToSelect, uint16_t uw_AssignDevAdr );
void Swi_SelectByAddress( uint16_t uw_DeviceAddress );
void Swi_SetAddress(uint16_t uw_DeviceAddress);
void Swi_PowerDown( void );
void Swi_PowerUp( void );
void Swi_Reset( void );
void Swi_PowerDownCmd (void);
BOOL Swi_ReadUID(uint8_t* UID);
BOOL Swi_SearchMultiplePuid(uint8_t ub_BitsToSearch,  S_OPTIGA_PUID *p_DetectedPuid, uint8_t * ubp_DevCnt );

/* *** SWI Low Level Functions *** */
/* *** SWI Low Level Functions *** */
void Swi_SendRawWord( uint8_t ub_Code, uint8_t ub_Data, BOOL b_WaitForInterrupt, BOOL b_ImmediateInterrupt, BOOL * bp_IrqDetected );
void Swi_SendRawWordNoIrq( uint8_t ub_Code, uint8_t ub_Data );
BOOL Swi_ReceiveRawWord( uint8_t * up_Byte );
BOOL Swi_TreatInvertFlag(uint8_t Code, uint8_t Data);
void Swi_AbortIrq( void );
void Swi_WaitForIrq( BOOL * bp_IrqDetected, BOOL b_Immediate );


//burst read config
#define SWI_FLOW_CONTROL_ENABLE 1

//burst read

#define INIT_DEV_ADDRESS (0x00u)
#define UID_BYTE_LENGTH (12u) /*!< Number of UID in bytes */
#define UID_BIT_SIZE (96u)    /*!< Number of UID in bits */

#define REGISTER_OVERWRITE (0xFFu) /*!< Register value will be completely overwritten */

#define SWI_FRAME_BIT_SIZE (13u) /*!< SWI consists of 2 training + 2 CMD + 8 DATA + 1 INV bits */


#define BURST_LEN4 (4u)

#define BURST_LEN8 (8u)


#define UID0_REG (0x40u)  /*!< UID LSB Register 0 */
#define UID1_REG (0x41u)  /*!< UID Register 1 */
#define UID2_REG (0x42u)  /*!< UID Register 2 */
#define UID3_REG (0x43u)  /*!< UID Register 3 */
#define UID4_REG (0x44u)  /*!< UID Register 4 */
#define UID5_REG (0x45u)  /*!< UID Register 5 */
#define UID6_REG (0x46u)  /*!< UID Register 6 */
#define UID7_REG (0x47u)  /*!< UID Register 7 */
#define UID8_REG (0x48u)  /*!< UID Register 8 */
#define UID9_REG (0x49u)  /*!< UID Register 9 */
#define UID10_REG (0x4Au) /*!< UID Register 10 */
#define UID11_REG (0x4Bu) /*!< UID MSB Register 11 */

#define CTRL2_NVM_REG (0x0272u)           /*!< CTRL2_NVM_REG */
#define CTRL2_NVM_REG__NVM_ACT (0x80u)    /*!< Bit7 NVM active process */
#define CTRL2_NVM_REG__NVM_WR (0x40u)     /*!< Bit6 NVM write/read select */
#define CTRL2_NVM_REG__DATA_SET (0x20u)   /*!< Bit5 Data register select */
#define CTRL2_NVM_REG__PG_ADDRESS (0x18u) /*!< Bit4 and Bit3 page address, Page 21 is DeviceAddress, Page 23 ODC, Page 24 ODC/PUK */
#define CTRL2_NVM_REG__PG_OFFSET (0x7u)   /*!< Bit2, Bit1 and Bit0 Page offset */


#define NVM_ADDR_REG (0x0274u)             /*!< NVM_ADDR_REG */
#define NVM_ADDR_REG__LSC_DEC_STS (0x80u)  /*!< Bit7 Lifespan Counter decrement status */
#define NVM_ADDR_REG__KILL (0x40u)         /*!< Bit6 kill instruction*/
#define NVM_ADDR_REG__DEC_LFSP (0x20u)     /*!< Bit5 decrease lifespan by 1 command */
#define NVM_ADDR_REG__NVM_DATA_LEN (0x18u) /*!< Bit4 and Bit3 NVM data length select  */
#define NVM_ADDR_REG__BYTE_AD (0x07u)      /*!< Bit2, Bit1 and Bit0 Byte address offset  */

/*!< Work In Progress (WIP2) and WIP3 is used for Data read from NVM */
#define NVM_WIP2_REG (0x0010u) /*!< Base Address of WIP2. Same as ECC RX Response1 */
#define NVM_WIP3_REG (0x0018u) /*!< Base Address of WIP3 */

#define WIP_SIZE (8u) /*!< Each WIP is 8 bytes in size */


/**
 * @brief Defines the order of how the UID will be read and return
 */
typedef enum {
  LOW_BYTE_FIRST = 1, /*!< UID0 will be return first */
  HIGH_BYTE_FIRST     /*!< UID11 will be return first */
} UID_BYTE_ORDER;

/**
 * @brief Defines the speed of SWI baud rate
 */
typedef enum {
  HIGH_SPEED = 0, /*!< High speed SWI baud rate */
  LOW_SPEED       /*!< Low speed SWI baud rate */
} SWI_SPEED;

/**
 * @brief Defines the UID read mode 
 */
typedef enum {
  SINGLE_BYTE_READ = 0, /*!< Single byte read mode */
  BURST_BYTES_READ      /*!< Burst byte read mode */
} SWI_BURST_SPEED;




//ERROR CODE
/**
 * @brief Return codes are divided into several layer to identify the path of return.
 */
#define TRUSTB_L_DEV (0x0000u) /*!< 0x0000 - Device layer */
#define TRUSTB_L_INF (0x1000u) /*!< 0x1000 - Interface layer. */
#define TRUSTB_L_CMD (0x2000u) /*!< 0x2000 - Command layer. */
#define TRUSTB_L_APP (0x3000u) /*!< 0x3000 - Application layer. */


/**
 * @brief Application layer related error code.
 */
#define INF_SWI (TRUSTB_L_INF + 0x0100u) /*!< 0x1100 - SWI Interface return code. */
#define APP_ECC (TRUSTB_L_APP + 0x0100u) /*!< 0x3100 - Application ECC layer return code. */
#define APP_NVM (TRUSTB_L_APP + 0x0200u) /*!< 0x3200 - Application NVM layer return code. */
#define APP_LSC (TRUSTB_L_APP + 0x0300u) /*!< 0x3300 - Application LSC layer return code. */
#define APP_SID (TRUSTB_L_APP + 0x0400u) /*!< 0x3400 - Application search UID layer return code. */
#define APP_CRC (TRUSTB_L_APP + 0x0500u) /*!< 0x3500 - Application CRC layer return code. */

/**
 * @brief Driver return success
 */
#define TRUSTB_SUCCESS (0x0000u) /*!< 0x0000 - Driver return success. */

/**
 * @brief Generic initialization code
 */
#define TRUSTB_INIT (0x00FFu) /*!< 0x00FF - Generic initialization code. */

/**
 * @brief SWI INTERFACE return code
 */
#define INF_SWI_SUCCESS (TRUSTB_SUCCESS)             /*!< 0x0000 - Success. */
#define INF_SWI_E_TIMEOUT (INF_SWI + 0x01u)          /*!< 0x1101 - Receive time out */
#define INF_SWI_E_TRAINING (INF_SWI + 0x02u)         /*!< 0x1102 - Training error  */
#define INF_SWI_E_INV (INF_SWI + 0x03u)              /*!< 0x1103 - Invert bit error */
#define INF_SWI_E_FRAME (INF_SWI + 0x04u)            /*!< 0x1104 - Framing error */
#define INF_SWI_E_NO_BIT_SEL (INF_SWI + 0x05u)       /*!< 0x1105 - No bit selected for writing */
#define INF_SWI_E_READ_ODC_TIMEOUT (INF_SWI + 0x06u) /*!< 0x1106 - Read ODC timeout */
#define INF_SWI_E_READ_ODC_DATA (INF_SWI + 0x07u)    /*!< 0x1107 - Read ODC info error */
#define INF_SWI_E_BURST_TYPE (INF_SWI + 0x08u)       /*!< 0x1108 - Invalid burst type */
#define INF_SWI_E_READ_TYPE (INF_SWI + 0x09u)        /*!< 0x1109 - Invalid read type */
#define INF_SWI_E_WRITE_TYPE (INF_SWI + 0x0Au)       /*!< 0x110A - Invalid write type */
#define INF_SWI_E_TIMING_INIT (INF_SWI + 0x0Bu)      /*!< 0x110B - SWI timing initialized */

#define INF_SWI_INIT INF_SWI + 0xFF /*!< Init Return code */

/**
 * @brief ECC return code
 */
#define APP_ECC_SUCCESS (TRUSTB_SUCCESS)          /*!< 0x0000 - Success */
#define APP_ECC_E_READ_UID (APP_ECC + 0x01u)      /*!< 0x3101 - Read UID error */
#define APP_ECC_E_READ_ODC (APP_ECC + 0x02u)      /*!< 0x3102 - Read ODC error */
#define APP_ECC_E_READ_PK (APP_ECC + 0x03u)       /*!< 0x3103 - Read ECC Public Key error */
#define APP_ECC_E_VERIFY_ODC (APP_ECC + 0x04u)    /*!< 0x3104 - Verify ODC error */
#define APP_ECC_E_RANDOM (APP_ECC + 0x05u)        /*!< 0x3105 - Random number error */
#define APP_ECC_E_CHECKVALUE (APP_ECC + 0x06u)    /*!< 0x3106 - Generate Checkvalue error */
#define APP_ECC_E_SEND_CHG (APP_ECC + 0x07u)      /*!< 0x3107 - Send Challenge error */
#define APP_ECC_E_TIMEOUT (APP_ECC + 0x08u)       /*!< 0x3108 - ECC timeout */
#define APP_ECC_E_READ_RESP (APP_ECC + 0x09u)     /*!< 0x3109 - Get response error */
#define APP_ECC_E_READ_RESP_X (APP_ECC + 0x0Au)   /*!< 0x310A - Get Response X error */
#define APP_ECC_E_READ_RESP_Z (APP_ECC + 0x0Bu)   /*!< 0x310B - Get Response Z error */
#define APP_ECC_E_READ_RESP_Z1 (APP_ECC + 0x0Cu)  /*!< 0x310C - Get Response Z last byte error */
#define APP_ECC_E_VERIFY_RESP (APP_ECC + 0x0Du)   /*!< 0x310D - ECC Verify response error */
#define APP_ECC_E_NO_IRQ (APP_ECC + 0x0Eu)        /*!< 0x310E - No IRQ received */
#define APP_ECC_E_GEN_CHALLENGE (APP_ECC + 0x0Fu) /*!< 0x310F - Generate challenge fail */
#define APP_ECC_E_WRITE_CAP7 (APP_ECC + 0x10u)    /*!< 0x3110 - Write Reg CAP7 error */
#define APP_ECC_E_WRITE_INT0 (APP_ECC + 0x11u)    /*!< 0x3111 - Write Reg INT0 error */
#define APP_ECC_E_CRC (APP_ECC + 0x20u)           /*!< 0x3120 - CRC Error Bit-1: PAGE */

/**
 * @brief ECC return code
 */
#define APP_ECC_SUCCESS (TRUSTB_SUCCESS)          /*!< 0x0000 - Success */
#define APP_ECC_E_READ_UID (APP_ECC + 0x01u)      /*!< 0x3101 - Read UID error */
#define APP_ECC_E_READ_ODC (APP_ECC + 0x02u)      /*!< 0x3102 - Read ODC error */
#define APP_ECC_E_READ_PK (APP_ECC + 0x03u)       /*!< 0x3103 - Read ECC Public Key error */
#define APP_ECC_E_VERIFY_ODC (APP_ECC + 0x04u)    /*!< 0x3104 - Verify ODC error */
#define APP_ECC_E_RANDOM (APP_ECC + 0x05u)        /*!< 0x3105 - Random number error */
#define APP_ECC_E_CHECKVALUE (APP_ECC + 0x06u)    /*!< 0x3106 - Generate Checkvalue error */
#define APP_ECC_E_SEND_CHG (APP_ECC + 0x07u)      /*!< 0x3107 - Send Challenge error */
#define APP_ECC_E_TIMEOUT (APP_ECC + 0x08u)       /*!< 0x3108 - ECC timeout */
#define APP_ECC_E_READ_RESP (APP_ECC + 0x09u)     /*!< 0x3109 - Get response error */
#define APP_ECC_E_READ_RESP_X (APP_ECC + 0x0Au)   /*!< 0x310A - Get Response X error */
#define APP_ECC_E_READ_RESP_Z (APP_ECC + 0x0Bu)   /*!< 0x310B - Get Response Z error */
#define APP_ECC_E_READ_RESP_Z1 (APP_ECC + 0x0Cu)  /*!< 0x310C - Get Response Z last byte error */
#define APP_ECC_E_VERIFY_RESP (APP_ECC + 0x0Du)   /*!< 0x310D - ECC Verify response error */
#define APP_ECC_E_NO_IRQ (APP_ECC + 0x0Eu)        /*!< 0x310E - No IRQ received */
#define APP_ECC_E_GEN_CHALLENGE (APP_ECC + 0x0Fu) /*!< 0x310F - Generate challenge fail */
#define APP_ECC_E_WRITE_CAP7 (APP_ECC + 0x10u)    /*!< 0x3110 - Write Reg CAP7 error */
#define APP_ECC_E_WRITE_INT0 (APP_ECC + 0x11u)    /*!< 0x3111 - Write Reg INT0 error */
#define APP_ECC_E_CRC (APP_ECC + 0x20u)           /*!< 0x3120 - CRC Error Bit-1: PAGE */

/**
 * @brief ECC CRC return code
 */
#define APP_ECC_E_CRC_WRITE (APP_ECC + 0x50u) /*!< 0x3150 - ECC Pass but CRC page writing fail */
/*!< 5x - refer x to APP_CRC_E_x */
#define APP_ECC_INIT (APP_ECC + TRUSTB_INIT) /*!< 0x31FF - Init return code */

/**
 * @brief NVM return code
 */
#define APP_NVM_SUCCESS (TRUSTB_SUCCESS)             /*!< 0x0000 - Success */
#define APP_NVM_E_READ (APP_NVM + 0x01u)             /*!< 0x3201 - NVM Read error */
#define APP_NVM_E_WRITE (APP_NVM + 0x02u)            /*!< 0x3202 - NVM Write error */
#define APP_NVM_E_TIMEOUT (APP_NVM + 0x03u)          /*!< 0x3203 - NVM timeout */
#define APP_NVM_E_BUSY (APP_NVM + 0x04u)             /*!< 0x3204 - NVM busy */
#define APP_NVM_E_READ_PAGE (APP_NVM + 0x05u)        /*!< 0x3205 - Read page error */
#define APP_NVM_E_ILLEGAL_PARA (APP_NVM + 0x06u)     /*!< 0x3206 - Illegal parameter */
#define APP_NVM_E_READ_STATUS (APP_NVM + 0x07u)      /*!< 0x3207 - Read NVM status error */
#define APP_NVM_E_PROG_PAGE (APP_NVM + 0x08u)        /*!< 0x3208 - NVM program page error */
#define APP_NVM_E_READ_DEVICEADDR (APP_NVM + 0x09u)  /*!< 0x3209 - Unable to read device address */
#define APP_NVM_E_WRITE_DEVICEADDR (APP_NVM + 0x0Au) /*!< 0x320A - Unable to write device address */
#define APP_NVM_E_DEVICEADDR (APP_NVM + 0x0Bu)       /*!< 0x320B - Invalid device address */
#define APP_NVM_E_LOCK_PAGE (APP_NVM + 0x0Cu)        /*!< 0x320C - Invalid NVM locked page */
#define APP_NVM_E_INVALID_PAGE (APP_NVM + 0x0Du)     /*!< 0x320D - Invalid NVM page number */
#define APP_NVM_E_READBACK (APP_NVM + 0x0Eu)         /*!< 0x320E - Write and read back verify error */
#define APP_NVM_E_READ_BURST (APP_NVM + 0x0Fu)       /*!< 0x320F - Read burst error */
#define APP_NVM_E_READ_KILL (APP_NVM + 0x10u)        /*!< 0x3210 - Read reading the kill status register */
#define APP_NVM_E_READ_CAP4 (APP_NVM + 0x11u)        /*!< 0x3211 - Read reading the kill capability register */

#define APP_NVM_READY (APP_NVM + 0xF0u)           /*!< 0x32F0 - NVM ready for access */
#define APP_NVM_PAGE_LOCKED (APP_NVM + 0xF1u)     /*!< 0x32F1 - NVM Page locked unable to write */
#define APP_NVM_PAGE_NOT_LOCKED (APP_NVM + 0xF2u) /*!< 0x32F2 - NVM Page not lock */

#define APP_NVM_DEV_KILLED (APP_NVM + 0xF3u)   /*!< 0x32F3 - Device is killed */
#define APP_NVM_DEV_NOT_KILL (APP_NVM + 0xF4u) /*!< 0x32F4 - Device cannot be killed */
#define APP_NVM_KILL_CAP (APP_NVM + 0xF5u)     /*!< 0x32F5 - Device can be killed */
#define APP_NVM_KILL_NOT_CAP (APP_NVM + 0xF6u) /*!< 0x32F6 - Device cannot not killed */

#define APP_NVM_INIT (APP_NVM + TRUSTB_INIT) /*!< 0x32FF - Init return code */


#define HIGH_BYTE_ADDR(Addr)  ((Addr >>8u) & 0xffu)
#define LOW_BYTE_ADDR(Addr)   (Addr & 0xffu)


uint16_t Swi_ReadWIPRegisterBurst(uint16_t uw_Address, uint8_t *ubp_Data); 
uint16_t Swi_ReadUID_burst(uint8_t *UID, UID_BYTE_ORDER order, SWI_BURST_SPEED ReadMode);

#endif

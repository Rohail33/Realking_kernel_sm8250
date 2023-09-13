#ifndef OPTIGA_NVM_H
#define OPTIGA_NVM_H

#include "../oplus_optiga.h"
#include "Optiga_Swi.h"
#include "../Platform/board.h"


typedef enum Nvm_UID
{
	Optiga_UID           = 0x00,
	Oplus_UID            = 0x01,
} UID;

BOOL Nvm_ProgramData( BOOL b_WaitForFinish, BOOL b_VerifyData, uint16_t uw_Address, uint8_t ub_BytesToProgram, uint8_t * ubp_Data );
BOOL Nvm_ReadData( uint16_t uw_Address, uint8_t ub_BytesToRead, uint8_t * ubp_Data );

BOOL Nvm_DecreaseLifeSpanCounter( void );
BOOL Nvm_VerifyLifeSpanCounter( BOOL * bp_IsValid );
BOOL Nvm_ReadLifeSpanCounter( uint32_t * ulp_LifeSpanCounter);
BOOL Nvm_LockNVM( uint8_t ub_PageNumber );
BOOL Nvm_ReadLockNVM( uint8_t * ubp_PageNumber );

void Nvm_SwitchUID( uint32_t id );





//burst read

#define NVM_MAX_USER_LOCK_PAGE (8u) /*!< Max NVM page that can be locked */
#define NVM_MAX_PAGE_COUNT (32u)    /*!< Max number of accessible NVM pages */

#define NVM_ODC_PAGE_COUNT (6u) /*!< Number of ODC pages */
#define NVM_PUK_PAGE_COUNT (3u) /*!< Number of PUK pages */
#define NVM_SINGLE_PAGE (1u)    /*!< Single Page read */

/**
 * @brief Defines the type of NVM read write mode 
 */
typedef enum {
  NVM_BYTE_READ = 0,   /*!< Single byte NVM read mode */
  NVM_BURST_BYTE_READ, /*!< Burst byte NVM read mode */
  NVM_BYTE_WRITE,      /*!< Single byte NVM write mode */
  NVM_BURST_BYTE_WRITE /*!< Burst byte NVM write mode */
} NVM_OPS;

#define NVM_MAX_BYTE_SIZE (64u) /*!< Total number of User NVM bytes (8 pages with 8 bytes each) */
#define NVM_PAGE_SIZE (8u)      /*!< Each page size in bytes */


uint16_t Nvm_ReadODC(uint8_t *ubODC, uint8_t *ubPUBKEY, NVM_OPS ReadMode);


#endif

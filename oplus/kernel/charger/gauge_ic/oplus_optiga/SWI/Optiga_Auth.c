/**
 * @file   Optiga_Auth.c
 * @date   Mar, 2016
 * @brief  Implementation of authentication flow
 *
 */

#include "Optiga_Auth.h"
#include "Optiga_Swi.h"
#include "Optiga_Nvm.h"
#include "../Platform/board.h"
#include <linux/delay.h>


static BOOL Optiga_SendChallenge(uint16_t* challenge, BOOL bPolling, uint8_t cmd);
static BOOL Optiga_ReadResponse(uint16_t* Z_resp, uint16_t* X_resp, uint8_t cmd);


/* ****************************************************************************
   name:      Ecc_ReadODC()

   function:  Read out ODC and public key from OPTIGA

   input:     OUT: gf2n_ODC
                gf2n_t array holding the ODC.
              OUT: gf2n_PublicKey
                gf2n_t array holding the public key.

   output:    bool

   return:    true, if all was ok.
              false, if errors detected.

   date:
 ************************************************************************* */
BOOL Ecc_ReadODC (uint32_t * gf2n_ODC, uint32_t * gf2n_PublicKey)
{
	if(Nvm_ReadData( 0x0100 + (8 * 23), 48, (uint8_t*)gf2n_ODC ) == FALSE)
		return FALSE;

	if(Nvm_ReadData( 0x0100 + (8 * 29), 18, (uint8_t*)gf2n_PublicKey ) == FALSE)
		return FALSE;

	return TRUE;
}

/* ****************************************************************************
   name:      Ecc_SendChallengeAnGetResponse()

   function:  send a calculated challenge to OPTIGA and start ECC engine of
              the OPTIGA IC.
              if OPTIGA indicates calculation finished, the results are read
              from OPTIGA memory space into the arrays gf2n_XResponse and
              gf2n_ZResponse.

   input:     IN: gf2n_Challenge
                gf2n_t array holding the challenge to be issued.
              OUT: gf2n_XResponse
                gf2n_t array holding the x part of the OPTIGA response.
              OUT: gf2n_ZResponse
                gf2n_t array holding the z part of the OPTIGA response.
              IN: bPolling
                polling mode to check OPTIGA engine for calculation finished
                state.
                if 'true', then a wait of 200ms is done before the data is
                read back, else the host waits for SWI IRQ signal.
                NOTE: please use FALSE, as that is the most efficient way
                      to handle the ECC engine.
   output:    bool

   return:    true, if all was ok.
              false, if errors detected.

   date:
 ************************************************************************* */
BOOL Ecc_SendChallengeAndGetResponse( uint16_t * gf2n_Challenge, uint16_t * gf2n_XResponse, uint16_t * gf2n_ZResponse, BOOL bPolling, uint8_t bEccMode )
{
	BOOL ret;
	BOOL bEccIrq;
	unsigned long flags; 
	struct oplus_optiga_chip * optiga_chip_info = oplus_get_optiga_info();

	printk(" Ecc_SendChallengeAndGetResponse enter \n");

	spin_lock_irqsave(&optiga_chip_info->slock, flags);

	ret = Optiga_SendChallenge(gf2n_Challenge, bPolling, bEccMode);
	if(ret == FALSE)
		goto out;

	if( bPolling == TRUE )
	{
		// fixed time wait of at least 50ms
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
		printk(" Ecc_SendChallengeAndGetResponse  spin_unlock_irqrestore\n");
#ifdef BURST_READ_INTERVAL
		usleep_range(34000, 34000);
		printk(" Ecc_SendChallengeAndGetResponse after wait 34ms \n");
#endif
		spin_lock_irqsave(&optiga_chip_info->slock, flags);
	}
	else
	{
		Swi_SendRawWord( SWI_BC, SWI_EINT, TRUE, FALSE, &bEccIrq);
		if( bEccIrq == FALSE )
		{
			ret = FALSE;
			goto out;
		}
		Swi_WriteConfigSpace( SWI_CAP7, 0x00u, 0x80u );
		Swi_WriteConfigSpace( SWI_INT0, 0x00u, 0x01u );
	}

	ret = Optiga_ReadResponse(gf2n_ZResponse, gf2n_XResponse, bEccMode);

out:
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
	printk(" Ecc_SendChallengeAndGetResponse out ret =%s\n", ret == TRUE ?"true":"false");
	if (ret == FALSE) {
		return FALSE;
	} else {
		return TRUE;
	}

}


static BOOL Optiga_SendChallenge(uint16_t * challenge, BOOL bPolling, uint8_t bEccMode)
{
	uint8_t ubCap7Value;
	uint8_t ubInt0Value;
	uint8_t ubIndex;
	uint8_t ubWordIndex;
	uint8_t ubData;

	if(bPolling)
	{
		ubCap7Value = 0x00u;
		ubInt0Value = 0x00u;
	}
	else
	{
		ubCap7Value = 0x80u;
		ubInt0Value = 0x01u;
	}

	if( Swi_WriteConfigSpace( SWI_CAP7, ubCap7Value, 0x80u ) == FALSE )
	{
		return FALSE;
	}
	if( Swi_WriteConfigSpace( SWI_INT0, ubInt0Value, 0x01u ) == FALSE )
	{
		return FALSE;
	}

	/*select device register set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG);

	/* write 16 bytes of challenge */
	for( ubIndex = 0u; ubIndex < 16u; ubIndex++ )
	{
		/* set start each aligned 8 byte addresses */
		if( (ubIndex & 0x07u) == 0u )
		{
			Swi_SendRawWordNoIrq(SWI_ERA, (((0x0040u + ubIndex) >> 8u) & 0xFFu));
			Swi_SendRawWordNoIrq(SWI_WRA, ( (0x0040u + ubIndex)        & 0xFFu));
		}

		ubWordIndex = (ubIndex >> 1u);
		if( (ubIndex & 1u) == 1u )
		{
			ubData = (uint8_t)((challenge[ubWordIndex] >> 8u) & 0x00FFu);
		}
		else
		{
			ubData = (uint8_t)(challenge[ubWordIndex] & 0xFFu);
		}

		/* write data w/o any interruption */
		Swi_SendRawWordNoIrq( SWI_WD, ubData);
	}

	/* write remaining last bytes */
	Swi_SendRawWordNoIrq(SWI_ERA, ((0x0340u >> 8u) & 0xFFu));
	Swi_SendRawWordNoIrq(SWI_WRA, ( 0x0340u        & 0xFFu));
	Swi_SendRawWordNoIrq( SWI_WD, (uint8_t)(challenge[8] & 0xFFu));

	/* start ECC calculation */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_OPTIGA_ECCSTART|bEccMode);

	return TRUE;
}

static BOOL Optiga_ReadResponse(uint16_t * Z_resp, uint16_t * X_resp, uint8_t bEccMode)
{
	uint8_t ubIndex;
	uint8_t ubWordIndex;
	uint8_t ubData;

	/* extract responses */
	/* set burst length to 8 bytes in a row */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0);
	/* select register set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG);

	/* extract ZResponse */
	for( ubIndex = 0u; ubIndex < 16u; ubIndex++ )
	{
		Swi_SendRawWordNoIrq(SWI_ERA, (((0x0030u + ubIndex) >> 8u) & 0xFFu));
		Swi_SendRawWordNoIrq(SWI_RRA, ( (0x0030u + ubIndex)        & 0xFFu));
		if( Swi_ReadActualSpace( &ubData ) == FALSE )
		{
			return FALSE;
		}
		ubWordIndex = (ubIndex >> 1u);
		if( (ubIndex & 1u) == 1u )
		{
			Z_resp[ubWordIndex] |= ((uint16_t)ubData << 8u);
		}
		else
		{
			Z_resp[ubWordIndex] = ((uint16_t)ubData & 0xFFu);
		}
	}

	/* extract XResponse */
	for( ubIndex = 0u; ubIndex < 8u; ubIndex++ )
	{
		Swi_SendRawWordNoIrq(SWI_ERA, (((0x0010u+ubIndex) >> 8u)& 0xFFu));
		Swi_SendRawWordNoIrq(SWI_RRA, ( (0x0010u+ubIndex)       & 0xFFu));

		if( Swi_ReadActualSpace( &ubData ) == FALSE )
		{
			return FALSE;
		}
		ubWordIndex = (ubIndex >> 1u);
		if( (ubIndex & 1u) == 1u )
		{
			X_resp[ubWordIndex] |= ((uint16_t)ubData << 8u);
		}
		else
		{
			X_resp[ubWordIndex] = ((uint16_t)ubData & 0xFFu);
		}
	}
	/* only lower 8 bytes of XResponse needed */
	X_resp[4] = 0u;
	X_resp[5] = 0u;
	X_resp[6] = 0u;
	X_resp[7] = 0u;
	X_resp[8] = 0u;

	/* read remaining ZResponse bits */
	if( Swi_ReadRegisterSpace( 0x0330u, &ubData ) == FALSE )
	{
		return FALSE;
	}
	Z_resp[8] = ((uint16_t)ubData & 0xFFu );

	/* all done correct. */
	return TRUE;
}


//burst read



/**
* @brief Read out ODC and ECC public key from the device.
* @param gf2n_ODC Returns the ODC 
* @param gf2n_PublicKey Returns the ECC Public Key
*/
uint16_t Ecc_ReadODC_Burst(uint32_t *gf2n_ODC, uint32_t *gf2n_PublicKey) 
{
	uint8_t ubODC[ODC_BYTE_SIZE];
	uint8_t ubPUBKEY[PUK_BYTE_SIZE];
	uint16_t ret = APP_ECC_INIT;

	printk(" Ecc_ReadODC_Burst  enter\n");

	memset(ubODC, 0, sizeof(ubODC));
	memset(ubPUBKEY, 0, sizeof(ubPUBKEY));

	ret = Nvm_ReadODC(ubODC, ubPUBKEY, NVM_BURST_BYTE_READ);
	if (ret != INF_SWI_SUCCESS) {
		ret = APP_ECC_E_READ_ODC;
	}

	convert8_to_32(gf2n_ODC, ubODC, ODC_BYTE_SIZE);
	convert8_to_32(gf2n_PublicKey, ubPUBKEY, PUK_BYTE_SIZE);

	printk(" Ecc_ReadODC_Burst  out\n");

	return ret;
}


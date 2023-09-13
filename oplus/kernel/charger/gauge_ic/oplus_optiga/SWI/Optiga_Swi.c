#include "Optiga_Swi.h"
#include "../Platform/board.h"
#include <linux/kernel.h>


#define MULTIPLE_DEVICE

extern uint16_t g_ulBaudLow;				//1 * TAU
extern uint16_t g_ulBaudHigh;				//3 * TAU
extern uint16_t g_ulBaudStop;				//5 * TAU
extern uint16_t g_ulResponseTimeOut;		//10 * TAU
extern uint16_t g_ulResponseTimeOutLong;	//30ms
extern uint16_t g_ulBaudPowerDownTime;		//196us
extern uint16_t g_ulBaudPowerUpTime;		//10ms
extern uint16_t g_ulBaudResetTime;			//1ms

#ifdef MULTIPLE_DEVICE
uint8_t ub_Stack[96];
uint8_t ub_StackPointer = 0;
#endif

BOOL Swi_ReadActualSpace( uint8_t * ubp_Data )
{
	return Swi_ReceiveRawWord( ubp_Data );
}

BOOL Swi_ReadRegisterSpace( uint16_t uw_Address, uint8_t * ubp_Data )
{

	/* select register set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG );

	/* send address to read from */
	Swi_SendRawWordNoIrq(SWI_ERA, ((uw_Address >> 8u) & 0xFFu) );
	
	/* set burst length to one byte */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0 );
	
	Swi_SendRawWordNoIrq(SWI_RRA, ( uw_Address        & 0xFFu) );

	/* read out data */
	return Swi_ReceiveRawWord( ubp_Data );
}


BOOL Swi_WriteRegisterSpace(uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect, BOOL b_WaitForInterrupt, BOOL b_ImmediateInterrupt, BOOL * bp_IrqDetected )
{

	BOOL bIntOccured;
	uint8_t ubReadData = 0x00u;


	/* ub_BitSelect == 0x00 makes no sense! */
	if( ub_BitSelect == 0x00u )
	{
		return FALSE;
	}

	/* read out current data, if bit masking used */
	if( ub_BitSelect != 0xFFu )
	{
		if( Swi_ReadRegisterSpace( uw_Address, &ubReadData ) == FALSE)
		{
			return FALSE;
		}
		ubReadData &= ~ub_BitSelect;
		ub_Data = ubReadData | (ub_Data & ub_BitSelect);
	}

	/* set burst lenght is 1 byte */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0 );
	/*select device register set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG );

	Swi_SendRawWordNoIrq(SWI_ERA, ((uw_Address >> 8u) & 0xFFu) );
	Swi_SendRawWordNoIrq(SWI_WRA, ( uw_Address        & 0xFFu) );

	Swi_SendRawWord( SWI_WD, ub_Data, b_WaitForInterrupt, b_ImmediateInterrupt, &bIntOccured );
	if( (b_WaitForInterrupt == TRUE) || (b_ImmediateInterrupt == TRUE) )
	{
		*bp_IrqDetected = bIntOccured;
	}

	/* all done */
	return TRUE;

}

BOOL Swi_WriteRegisterSpaceNoIrq( uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect )
{
	return Swi_WriteRegisterSpace( uw_Address, ub_Data, ub_BitSelect, FALSE, FALSE, NULL );
}


BOOL Swi_ReadConfigSpace( uint16_t uw_Address, uint8_t * ubp_Data )
{
	//Prioritize the configuration space selection for isolation code improvement
	
	/* select device config set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_ECFG );
	
	/* burst lenght is 1 byte */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0 );

	Swi_SendRawWordNoIrq(SWI_ERA, ((uw_Address >> 8u ) & 0xFFu) );
	Swi_SendRawWordNoIrq(SWI_RRA, ( uw_Address         & 0xFFu) );

	return Swi_ReceiveRawWord( ubp_Data );
}


BOOL Swi_WriteConfigSpace( uint16_t uw_Address, uint8_t ub_Data, uint8_t ub_BitSelect )
{
	uint8_t ubReadData = 0u;

	/* ub_BitSelect == 0x00 makes no sense! */
	if( ub_BitSelect == 0x00u )
	{
		return FALSE;
	}

	/* read out current data, if bit masking used */
	if(ub_BitSelect != 0xFFu)
	{
		if( Swi_ReadConfigSpace(uw_Address, &ubReadData) == FALSE )
		{
			return FALSE;
		}
		ubReadData &= ~ub_BitSelect;
		ub_Data = ubReadData | (ub_Data & ub_BitSelect);
	}

	/* burst lenght is 1 byte */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0 );
	/* select device config set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_ECFG );

	Swi_SendRawWordNoIrq(SWI_ERA, ((uw_Address >> 8u) & 0xFFu) );
	Swi_SendRawWordNoIrq(SWI_WRA, ( uw_Address        & 0xFFu) );

	Swi_SendRawWordNoIrq(SWI_WD, ub_Data );

	/* all done */
	return TRUE;
}

void Swi_SelectByAddress(uint16_t uw_DeviceAddress)
{
	Swi_SendRawWordNoIrq( SWI_EDA, ( (uint8_t)((uw_DeviceAddress >> 8u) & 0xFFu)) );
	Swi_SendRawWordNoIrq( SWI_SDA, ( (uint8_t)( uw_DeviceAddress        & 0xFFu)) );
}

void Swi_SetAddress(uint16_t uw_DeviceAddress)
{
	Swi_WriteConfigSpace(SWI_DADR0,  (uw_DeviceAddress        & 0xFFu), 0xFFu );
    Swi_WriteConfigSpace(SWI_DADR1, ((uw_DeviceAddress >> 8u) & 0xFFu), 0xFFu );
}

void Swi_PowerDown( void )
{
	set_pin_dir(1);
	set_pin(0);
	ic_udelay(g_ulBaudPowerDownTime);
}

void Swi_PowerUp( void )
{
	set_pin_dir(1);
	set_pin(1);
	ic_udelay(g_ulBaudPowerUpTime);
}


void Swi_Reset( void )
{
	Swi_SendRawWordNoIrq( SWI_BC, SWI_BRES );
	ic_udelay(g_ulBaudResetTime);
}

void Swi_PowerDownCmd(void){
	Swi_SendRawWordNoIrq( SWI_BC, SWI_PDWN );
}

BOOL Swi_ReadUID(uint8_t* UID)
{

	uint8_t value=0x0;
	int i;

	for(i=0; i<12; i++)
	{
		if(Swi_ReadConfigSpace(SWI_UID0+11-i, ((uint8_t *)&value)) == FALSE)
		{
			printk("Swi_ReadUID i=:%d  value =%d\n", i, value);
			return FALSE;
		}

		*(UID+i) = value;
	}

	return TRUE;
}


BOOL Swi_TreatInvertFlag(uint8_t ub_Code, uint8_t ub_Data)
{
	uint8_t i;
	uint8_t ubCount=0;
	uint16_t srcData = (uint16_t) ub_Code;
	srcData = (srcData<<8) + ub_Data;
	for (i=0;i<16;i++)
	{
		ubCount += srcData&0x01;
		srcData >>=1;
	}
	/* check, if invert required */
	if( ubCount > 6 )
		return TRUE;
	else
		return FALSE;
}


void Swi_AbortIrq( void )
{
	set_pin_dir(1);

	set_pin(0);
	// delay for 1 tau
	ic_udelay(g_ulBaudLow);
	set_pin(1);	

	set_pin_dir(0);
}


void Swi_WaitForIrq( BOOL* bp_IrqDetected, BOOL b_Immediate)
{
	BOOL bResult = FALSE;
	volatile uint32_t ulTimeOut;

	if( b_Immediate )
		ulTimeOut = g_ulResponseTimeOut;
	else
		ulTimeOut = g_ulResponseTimeOutLong; // wait for 30ms.

	*bp_IrqDetected = FALSE;

	while(ulTimeOut)
	{
		if(!(get_pin()))
		{
			bResult = TRUE;
			*bp_IrqDetected = TRUE;
			break;
		}
		ulTimeOut--;
		ic_udelay(1);
	}

	if(ulTimeOut ==0)
	{
		printk("Swi_WaitForIrq ecc wait timeout\n");
	}


	ic_udelay(g_ulBaudLow);
	ic_udelay(g_ulBaudStop);

	if(!bResult)
		Swi_AbortIrq();

	return;
}

void Swi_SendRawWord( uint8_t ub_Code, uint8_t ub_Data, BOOL b_WaitForInterrupt, BOOL b_ImmediateInterrupt, BOOL * bp_IrqDetected )
{
	uint8_t inversion=0;
	BOOL bInterruptOccured = FALSE;

	if(Swi_TreatInvertFlag(ub_Code, ub_Data) == TRUE)
	{
		ub_Code^=0x03;
		ub_Data=~ub_Data;
		inversion=1;

	}

	/* Send a STOP singal first to have time to receive either IRQ or data! */
	// set bif gpio as output 
	set_pin_dir(1);
	// send a stop command first.
	set_pin(1);
	ic_udelay(g_ulBaudStop);
	// send BCF
	set_pin(0);
	(ub_Code&0x08) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send _BCF
	set_pin(1);
	(ub_Code&0x04) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT9
	set_pin(0);
	(ub_Code&0x02) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT8
	set_pin(1);
	(ub_Code&0x01) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT7
	set_pin(0);
	(ub_Data&0x80) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT6
	set_pin(1);
	(ub_Data&0x40) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT5
	set_pin(0);
	(ub_Data&0x20) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT4
	set_pin(1);
	(ub_Data&0x10) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT3
	set_pin(0);
	(ub_Data&0x08) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT2
	set_pin(1);
	(ub_Data&0x04) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT1
	set_pin(0);
	(ub_Data&0x02) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);
	// send BIT0
	set_pin(1);
	(ub_Data&0x01) ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);

	/* Send Inversion bit */
	set_pin(0);
	inversion ? ic_udelay(g_ulBaudHigh): ic_udelay(g_ulBaudLow);

	// Send Stop Command
	set_pin(1);
	ic_udelay(g_ulBaudStop);

	// set gpio as input
	set_pin_dir(0);

	/* Check for IRQ event, if requested */
	if( b_WaitForInterrupt == TRUE )
	{
		/* wait for interrupt */
		Swi_WaitForIrq( &bInterruptOccured, b_ImmediateInterrupt );
	}

	/* store IRQ data, if requested */
	if( (b_WaitForInterrupt == TRUE) || (b_ImmediateInterrupt == TRUE) )
	{
		*bp_IrqDetected = bInterruptOccured;
	}

}

void Swi_SendRawWordNoIrq(uint8_t ub_Code, uint8_t ub_Data )
{
	Swi_SendRawWord( ub_Code, ub_Data, FALSE, FALSE, NULL );
}

BOOL Swi_ReceiveRawWord( uint8_t * up_Byte )
{
	BOOL bPreviousSwiState;
	uint8_t ubIndex = 12u;
	uint8_t ubBitsToCapture;

	uint32_t ulTimeOut = g_ulResponseTimeOut;

	uint32_t ulTimes[13]={0};
	uint32_t ulMaxTime = 0u;
	uint32_t ulMinTime = ~ulMaxTime;
	uint32_t ulCount;
	uint32_t ulThreshold;

    uint8_t uTimes_temp[13];

	while(get_pin() && ulTimeOut)
	{
		ulTimeOut--;
		ic_udelay(1);
	}
	/* exit with fail, if timeout criteria triggered */
	if( ulTimeOut == 0u )
	{
		printk("Swi_ReceiveRawWord 01");
		return FALSE;
	}
	/* get port state */
	bPreviousSwiState = get_pin();

	/* measure time of high and low phases */
	for( ubBitsToCapture = 13u; ubBitsToCapture != 0u; ubBitsToCapture-- )
	{
		ulCount = 0u;
		ulTimeOut = g_ulResponseTimeOut;
		while( get_pin() == bPreviousSwiState && ulTimeOut )
		{
			ulCount++;
			ulTimeOut--;
			ic_udelay(1);
		}
		ulTimes[ubIndex] = ulCount;
		ubIndex--;

		bPreviousSwiState =  get_pin();
	}

	/* evaluate detected results */
	for( ubIndex = 12u; ubIndex != 0u; ubIndex-- )
	{
		ulCount = ulTimes[ubIndex];
		if( ulCount < ulMinTime)
		{
			ulMinTime = ulCount;
		}
		else if(ulCount > ulMaxTime)
		{
			ulMaxTime = ulCount;
		}
		else
		{
			/*  no change required */
		}
	}

	/* calculate threshold */
	ulThreshold = ((ulMaxTime - ulMinTime) >> 1u);
	ulThreshold += ulMinTime;

    memcpy(uTimes_temp,ulTimes,13);

	*up_Byte = (((ulTimes[ 8] > ulThreshold) ? 1u : 0u)<<7) |
			(((ulTimes[ 7] > ulThreshold) ? 1u : 0u)<<6) |
			(((ulTimes[ 6] > ulThreshold) ? 1u : 0u)<<5) |
			(((ulTimes[ 5] > ulThreshold) ? 1u : 0u)<<4) |
			(((ulTimes[ 4] > ulThreshold) ? 1u : 0u)<<3) |
			(((ulTimes[ 3] > ulThreshold) ? 1u : 0u)<<2) |
			(((ulTimes[ 2] > ulThreshold) ? 1u : 0u)<<1) |
			(((ulTimes[ 1] > ulThreshold) ? 1u : 0u)<<0);

	if(((ulTimes[ 0] > ulThreshold) ? 1u : 0u) == 1)
		*up_Byte = ~(*up_Byte);

	if(((ulTimes[12] > ulThreshold) ? 1u : 0u) == ((ulTimes[11] > ulThreshold) ? 1u : 0u)) {
		for(ubIndex=0;ubIndex<13;ubIndex++)
		{
		   printk("Swi_ReceiveRawWord uTimes_temp[%02x] = %02x\n",ubIndex,uTimes_temp[ubIndex]);
		}
		return FALSE;
	}
	/* all done well */
	return TRUE;
}

#ifdef MULTIPLE_DEVICE
/********************************************************************
*
* Function Name: b_UidSearch_GetDipDoneBit
* Description: This function is to be used in multiple UID search
*
* \param[in]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
*
* \return TRUE if no processing error
********************************************************************/
static BOOL b_UidSearch_GetDipDoneBit( uint8_t ub_BitInfo )
{
    if (ub_BitInfo & 0x01)
		return TRUE;
	else
		return FALSE;
}

/********************************************************************
*
* Function Name: b_UidSearch_SetDipDoneBit
* Description: This function is to be used in multiple UID search
*
* \param[out]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
* \param[in]  b_Bit : TRUE: set
*                     FALSE: clear

* \return  -
********************************************************************/
static void b_UidSearch_SetDipDoneBit( uint8_t * ub_BitInfo , BOOL b_Bit)
{
    if (b_Bit)
		*ub_BitInfo = (*ub_BitInfo) | 0x01; // set
	else
		*ub_BitInfo = (*ub_BitInfo) & 0xfe; // clear
}

/********************************************************************
*
* Function Name: b_UidSearch_GetDIE0Info
* Description: This function is to be used in multiple UID search
*
* \param[in]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
* \param[in]  b_Bit : TRUE: set
*                     FALSE: clear

* \return  TRUE if DIE0(bit2) is set
********************************************************************/
static BOOL b_UidSearch_GetDIE0Info( uint8_t ub_BitInfo )
{
    if (ub_BitInfo & 0x04) return TRUE;
	else return FALSE;
}

/********************************************************************
*
* Function Name: b_UidSearch_SetDIE0Info
* Description: This function is to be used in multiple UID search
*
* \param[out]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
* \param[in]  b_Bit : TRUE: set
*                     FALSE: clear

* \return  -
********************************************************************/
static void b_UidSearch_SetDIE0Info( uint8_t * ub_BitInfo , BOOL b_Data)
{
    if (b_Data)
		*ub_BitInfo = (*ub_BitInfo) | 0x04;	 // set
	else
		*ub_BitInfo  = (*ub_BitInfo) & 0xfb; // clear
}

/********************************************************************
*
* Function Name: b_UidSearch_GetDIE1Info
* Description: This function is to be used in multiple UID search
*
* \param[in]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed
* \param[in]  b_Bit : TRUE: set
*                     FALSE: clear

* \return  TRUE if DIE1(bit1) is set
********************************************************************/
static BOOL b_UidSearch_GetDIE1Info( uint8_t ub_BitInfo )
{
    if (ub_BitInfo & 0x02)
		return TRUE;
	else
		return FALSE;
}

/********************************************************************
*
* Function Name: b_UidSearch_SetDIE1Info
* Description: This function is to be used in multiple UID search
*
* \param[out]  ub_BitInfo
*			  bit 0: 0: DIP needed	; 1: DIP not needed
*             bit 1: 0: DIE1 not needed;  1: DIE1 needed
*             bit 2: 0: DIE0 not needed;  1: DIE0 needed

* \return  -
********************************************************************/
static void b_UidSearch_SetDIE1Info( uint8_t * ub_BitInfo )
{
    *ub_BitInfo = (*ub_BitInfo) | 0x02;
}

/********************************************************************
*
* Function Name: b_pop
* Description: This function is to be used in multiple UID search
*
* \param[out]  ub_Data: read out last data in ub_Stack

* \return  TRUE if no error
********************************************************************/
static BOOL b_pop(uint8_t * ub_Data)
{
    if(ub_StackPointer == 0 )
		return FALSE;	// stack empty
	*ub_Data = ub_Stack[--ub_StackPointer];
	return TRUE;
}

/********************************************************************
*
* Function Name: b_push
* Description: This function is to be used in multiple UID search
*
* \param[out]  ub_Data: push data at the end of  ub_Stack

* \return  TRUE if no error
********************************************************************/
static BOOL b_push( uint8_t ub_Data )
{
    if (ub_StackPointer == 96 ) return FALSE; // stack full
	ub_Stack[ub_StackPointer++] = ub_Data;
	return TRUE;
}

/********************************************************************
*
* Function Name: ub_SizeOfStack
* Description: This function is to be used in multiple UID search
*
* \return  Size of ub_Stack
********************************************************************/
static uint8_t ub_SizeOfStack()
{
    return ub_StackPointer;
}

/********************************************************************
*
* Function Name: bif_MultiUidSearch
* Description: This function is to be used in multiple UID search
* devAddr is assigned sequentially from 1 to N for each searched device
* \param[out]  stp_DetectedPuid
* \param[out]  ubp_DevCnt: slave amount in system

* \return  TRUE if no error
********************************************************************/
BOOL Swi_SearchMultiplePuid(uint8_t ub_BitsToSearch,  S_OPTIGA_PUID *pst_DetectedPuid, uint8_t * ubp_DevCnt )
{
	uint8_t ubBitInfo[96];
	uint8_t ubCurrentIdPtr, ubLastIdPtr =0;
	uint8_t ubSlaveCnt = 0;
	BOOL bFound_0;
  	BOOL bFound_1;
  	uint8_t ubBitCnt = ub_BitsToSearch;
  	uint8_t ubByteIndex;
	BOOL bSearchDone = FALSE;
	uint8_t i;
	uint8_t ubpBytes[12];
	uint8_t *ptr=(uint8_t*)pst_DetectedPuid;

	ub_StackPointer = 0;  // clear stack first
	for (i = 0;i < ubBitCnt;i ++)
	{
		ubBitInfo[i] = 0; // clear all info for all 80 bits
	}

	do
	{
		/* Restore device state */
		if(ubSlaveCnt > 0){
			 Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE0);
		}

		/* start ID search */
		Swi_SendRawWordNoIrq(SWI_BC, SWI_DISS);

		ubCurrentIdPtr = ub_BitsToSearch;
		ubBitCnt = ub_BitsToSearch;
		for ( i = 0;i < 12;i ++){
			ubpBytes[i] = 0;
		}

		for ( ; ubBitCnt > 0; ubBitCnt --)
		{
		    ubCurrentIdPtr --;
			ubByteIndex = 11 - (ubCurrentIdPtr >> 3u);
		    ubpBytes[ubByteIndex] = ubpBytes[ubByteIndex] << 1u;

		    if (b_UidSearch_GetDipDoneBit(ubBitInfo[ubCurrentIdPtr]) == FALSE )	  // need to do DIP
			{
				// DIP0
		 		Swi_SendRawWordNoIrq(SWI_BC, SWI_DIP0);
				Swi_WaitForIrq(&bFound_0, TRUE);
				ic_udelay(g_ulResponseTimeOut);

				// DIP1
		 		Swi_SendRawWordNoIrq(SWI_BC, SWI_DIP1);
				Swi_WaitForIrq(&bFound_1, TRUE);
				ic_udelay(g_ulResponseTimeOut);

				if( (bFound_0 == TRUE) && (bFound_1 == TRUE)) // both DIP0 and DIP1 has positive response
				{
					ubLastIdPtr = ubCurrentIdPtr;
					if( b_push( ubLastIdPtr ) == FALSE)
					{
						return FALSE;
					}

		      		ubpBytes[ubByteIndex] &= 0xFEu;	// DIE0
		  			Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE0);
					b_UidSearch_SetDIE0Info( &ubBitInfo[ubLastIdPtr] , TRUE);
					b_UidSearch_SetDIE1Info( &ubBitInfo[ubCurrentIdPtr] );
				}
				else if ((bFound_0 == TRUE) && (bFound_1 == FALSE))	  // DIE0
				{
		      		ubpBytes[ubByteIndex] &= 0xFEu;
		  			Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE0);
					b_UidSearch_SetDIE0Info( &ubBitInfo[ubCurrentIdPtr], TRUE );
				}
				else if ((bFound_0 == FALSE) && (bFound_1 == TRUE)) // DIE1
				{
		      		ubpBytes[ubByteIndex] |= 0x01u;
		  			Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE1);
		  			b_UidSearch_SetDIE0Info( &ubBitInfo[ubCurrentIdPtr] , FALSE);
					b_UidSearch_SetDIE1Info( &ubBitInfo[ubCurrentIdPtr] );
				}
				else//On bit 19 fails.
				{
					return FALSE;
				}
				b_UidSearch_SetDipDoneBit( &ubBitInfo[ubCurrentIdPtr] , TRUE);


			}
			else  // DIPDone == TRUE
			{
			    if (ubCurrentIdPtr == ubLastIdPtr)
			    {
					b_pop( &ubCurrentIdPtr );
				}

				if ( b_UidSearch_GetDIE0Info( ubBitInfo[ubCurrentIdPtr] ))
				{
		      		ubpBytes[ubByteIndex] &= 0xFEu;
		  			Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE0);
				}
				else if (b_UidSearch_GetDIE1Info( ubBitInfo[ubCurrentIdPtr]))
				{
		      		ubpBytes[ubByteIndex] |= 0x01u;
		  			Swi_SendRawWordNoIrq(SWI_BC, SWI_DIE1);
				}
			}

			if (ubCurrentIdPtr == 0) // clear dip done from last bit index that has two positive response
			{
			    if ( ub_SizeOfStack() == 0)
					bSearchDone = TRUE;
				else
				{
					// refresh ubLastIdPtr
				    b_pop( &ubLastIdPtr );
					b_push( ubLastIdPtr );
				}
				// clear DIP done bit since last id pointer
			    for  (i = 0;i < ubLastIdPtr;i ++){
					b_UidSearch_SetDipDoneBit( &ubBitInfo[i] , FALSE);
			    }

				b_UidSearch_SetDIE0Info( &ubBitInfo[ubLastIdPtr] , FALSE);	// clear

			}  // if (ubCurrentIdPtr == 0)
		}   //for ( ; ubBitCnt > 0; ubBitCnt --)

		for(i=0;i<12;i++)
		{
	    	ptr[i] = ubpBytes[i];
		}

		ubSlaveCnt ++;
		ptr += sizeof(S_OPTIGA_PUID);

		/* Configure device address from 1 to N for each found device */
		(void)Swi_WriteConfigSpace(SWI_DADR0,  (ubSlaveCnt        & 0xFFu), 0xFFu );
        (void)Swi_WriteConfigSpace(SWI_DADR1, ((ubSlaveCnt >> 8u) & 0xFFu), 0xFFu );

	}while(bSearchDone == FALSE);

	*ubp_DevCnt = ubSlaveCnt;
    return TRUE;
}

/*burst read*/
static void Swi_ChangeTauRate(SWI_SPEED speed) {
	if (speed == HIGH_SPEED) {
		Set_SWIHighSpeed();
	} else if (speed == LOW_SPEED) {
		Set_SWILowSpeed();
	}
}

/**
* @brief Read WIP data register in burst mode. Enable the GPIO_SLOW_READ_LIMITATION macro for slow system GPIO that is slow to receive data.
* @param uw_Address WIP register address
* @param ubp_Data Data from the address to be returned.
*/
uint16_t Swi_ReadWIPRegisterBurst(uint16_t uw_Address, uint8_t *ubp_Data)
{
	uint16_t ret = INF_SWI_INIT;
	uint8_t data = 0;
	uint8_t index = 8;
	uint8_t len = 8;

	Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL3);                    /*!< set burst length to 8 byte */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG);                    /*!< select EREG register set */
	Swi_SendRawWordNoIrq(SWI_ERA, HIGH_BYTE_ADDR(uw_Address)); /*!< send high byte of address */

#if (SWI_FLOW_CONTROL_ENABLE == 1)
	Swi_ChangeTauRate(LOW_SPEED); /*!< Lower the speed to make sure burst receive is working */
#endif
	Swi_SendRawWordNoIrq(SWI_RRA, LOW_BYTE_ADDR(uw_Address)); /*!< send low byte of address */

	while (index) {
		ret = Swi_ReceiveRawWord(&data);
		if (ret != TRUE) {
		break;
	}

	ubp_Data[len - index] = data;
	index--;
	}

	if(ret == TRUE)
	{
		ret = INF_SWI_SUCCESS;
	}
	else
	{
		ret = INF_SWI_E_TIMEOUT;
	}
	return ret;
}

/**
* @brief Read data from ECFG space register in burst mode. Set SWI_FLOW_CONTROL_ENABLE macro to 1 to enable slow GPIO reading.
* @param uw_Address ECFG register address.
* @param ubp_Data Data to be read from the ECFG address using burst operation.
* @param len Number of data to be read continueously during the burst operation.
*/
uint16_t Swi_ReadConfigSpaceBurst(uint16_t uw_Address, uint8_t *ubp_Data, uint8_t len)
{
	uint16_t ret = INF_SWI_INIT;
	uint8_t index = 0;
	uint8_t i = 0;

	uint8_t burst8_cnt = len / BURST_LEN8;
	uint16_t remaining_cnt = (uint8_t)(len - (burst8_cnt * BURST_LEN8));

	Swi_SendRawWordNoIrq(SWI_BC, SWI_ECFG); /*!< select ECFG register space */

	while (burst8_cnt != 0) {
		Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL3); /*!< set read burst length to 8 bytes */
		Swi_SendRawWordNoIrq(SWI_ERA, HIGH_BYTE_ADDR(uw_Address));
#if (SWI_FLOW_CONTROL_ENABLE == 1)
		Swi_ChangeTauRate(LOW_SPEED); /*!< Lower the speed to make sure burst receive is working */
#endif
		Swi_SendRawWordNoIrq(SWI_RRA, LOW_BYTE_ADDR(uw_Address));

		for (i = 0; i < BURST_LEN8; i++) {
			ret = Swi_ReceiveRawWord(ubp_Data + index);
			index++;
		}

		burst8_cnt--;
		uw_Address += (uint16_t)BURST_LEN8;
	}

	if (remaining_cnt >= BURST_LEN4) {
		Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL2); /*!< set read burst length to 4 bytes */
		Swi_SendRawWordNoIrq(SWI_ERA, HIGH_BYTE_ADDR(uw_Address));
#if (SWI_FLOW_CONTROL_ENABLE == 1)
		Swi_ChangeTauRate(LOW_SPEED); /*!< Lower the speed to make sure burst receive is working */
#endif
		Swi_SendRawWordNoIrq(SWI_RRA, LOW_BYTE_ADDR(uw_Address));

		for (i = 0; i < (uint8_t)BURST_LEN4; i++) {
			ret = Swi_ReceiveRawWord(ubp_Data + index);
			index++;
		}
		uw_Address += (uint16_t)BURST_LEN4;
		remaining_cnt -= BURST_LEN4;
	}

	if (remaining_cnt != 0) {
		Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL0); /*!< set read burst length to 1 byte */

		while (remaining_cnt != 0) {
			Swi_SendRawWordNoIrq(SWI_ERA, HIGH_BYTE_ADDR(uw_Address));
#if (SWI_FLOW_CONTROL_ENABLE == 1)
			Swi_ChangeTauRate(LOW_SPEED); /*!< Lower the speed to make sure burst receive is working */
#endif
			Swi_SendRawWordNoIrq(SWI_RRA, LOW_BYTE_ADDR(uw_Address));

			ret = Swi_ReceiveRawWord(ubp_Data + index);
			index++;
			remaining_cnt--;
			uw_Address++;
		}
	}
#if (SWI_FLOW_CONTROL_ENABLE == 1)
	Swi_ChangeTauRate(HIGH_SPEED); /*!< Lower the speed to make sure burst receive is working */
#endif

	if(ret == TRUE)
	{
		ret = INF_SWI_SUCCESS;
	}
	else
	{
		ret = INF_SWI_E_TIMEOUT;
	}

	return ret;
}

uint16_t Swi_ReadUID_burst(uint8_t *UID, UID_BYTE_ORDER order, SWI_BURST_SPEED ReadMode) {
  uint8_t burstvalue[UID_BYTE_LENGTH];
  uint8_t value[1] = {0};
  uint8_t i = 0;
  uint16_t ret = INF_SWI_INIT;

  memset(burstvalue, 0, sizeof(burstvalue));

  if (ReadMode == BURST_BYTES_READ) {
    ret = Swi_ReadConfigSpaceBurst(UID0_REG, burstvalue, UID_BYTE_LENGTH);
    if (ret != INF_SWI_SUCCESS) {
      return ret;
    }

    if (order == LOW_BYTE_FIRST) {
      memcpy(UID, burstvalue, UID_BYTE_LENGTH);
    } else if (order == HIGH_BYTE_FIRST) {
      i = UID_BYTE_LENGTH - 1;
      while (i) {
        *(UID + i) = burstvalue[UID_BYTE_LENGTH - 1 - i];
        i--;
      }
      *(UID) = burstvalue[UID_BYTE_LENGTH - 1];
    }
  } else if (ReadMode == SINGLE_BYTE_READ) {
    for (i = 0; i < UID_BYTE_LENGTH; i++) {
      ret = Swi_ReadConfigSpace((uint16_t)(UID0_REG + (UID_BYTE_LENGTH - 1) - i), value);
      if (ret != INF_SWI_SUCCESS) {
        return ret;
      }

      *(UID + i) = value[0];
    }
  } else {
    return INF_SWI_E_READ_TYPE;
  }

  return INF_SWI_SUCCESS;
}

#endif

//#include "GPIO.h"
#include "board.h"
#include "../oplus_optiga.h"
#include <linux/delay.h>

uint32_t g_ulBaudLow;				//1 * BIF_DEFAULT_TIMEBASE
uint32_t g_ulBaudHigh;				//3 * BIF_DEFAULT_TIMEBASE
uint32_t g_ulBaudStop;				//>5 * BIF_DEFAULT_TIMEBASE
uint32_t g_ulResponseTimeOut;		//>20 * BIF_DEFAULT_TIMEBASE
uint32_t g_ulResponseTimeOutLong;	//>32ms for ECC completion
uint32_t g_ulBaudPowerDownTime;		//>196us
uint32_t g_ulBaudPowerUpTime;		//>10ms
uint32_t g_ulBaudResetTime;			//>1ms

uint32_t g_culNvmTimeout;			//5.1ms

void timing_init(void)
{
	g_ulBaudLow 				= 10;		//1 * tau;
	g_ulBaudHigh 				= 30;		//3 * tau;
	g_ulBaudStop				= 50;		//5 * tau;
	g_ulResponseTimeOut 		= 200;		//10 * tau;
	g_ulResponseTimeOutLong		= 60000;	//32ms;

	g_ulBaudPowerDownTime		= 2000;		//2000 = 1ms
	g_ulBaudPowerUpTime			= 10000;	//20000 = 10 ms
	g_ulBaudResetTime			= 2000; 	//2000 = 1ms

	g_culNvmTimeout 			= 10000;	//5.1ms
}

/**
* @brief Set SWI baud rate to highspeed
*/
void Set_SWIHighSpeed(void) {
	g_ulBaudLow 				= 10;		//1 * tau;
	g_ulBaudHigh 				= 30;		//3 * tau;
	g_ulBaudStop				= 50;		//5 * tau;
	g_ulResponseTimeOut 		= 200;		//10 * tau;

}

/**
* @brief Set SWI baud rate to lowspeed
*/
void Set_SWILowSpeed(void) {
//need to updae
	g_ulBaudLow 				= 10;		//1 * tau;
	g_ulBaudHigh 				= 30;		//3 * tau;
	g_ulBaudStop				= 50;		//5 * tau;
	g_ulResponseTimeOut 		= 200;		//10 * tau;
}


uint8_t get_pin()
{
	return get_optiga_pin();
}

void set_pin(uint8_t level)
{
	set_optiga_pin(level);
}

void set_pin_dir(uint8_t dir)
{
	set_optiga_pin_dir(dir);
}

void ic_udelay(volatile uint32_t ul_ticks)
{
	if (ul_ticks > 1000){
		mdelay(ul_ticks/1000);
	}else{
		udelay(ul_ticks);
	}
}

uint32_t Get_NVMTimeOutLoop(void) {
 // SWI_ASSERT(ulNvmRetryLoop != 0); /*! SWI timing is not initialized */
 // return ulNvmRetryLoop;

  return g_culNvmTimeout;
}

/**
* @brief Converts 8-bit data to 32-bit data memory type.
* @param dest Pointer to the 32-bit destination array where the content is to be copied, type-casted to a pointer of type void*.
* @param src Pointer to the 8-bit source of data to be copied, type-casted to a pointer of type const void*.
* @param n Number of 8-bit to convert.
*/
void convert8_to_32(uint32_t *dest, uint8_t *src, uint8_t n) {
  uint8_t ubIndex = 0;
  uint8_t dest_size = 0;

  if (n % 4 == 0) {
    dest_size = n / 4;
  } else {
    dest_size = (n / 4) + 1;
  }

  /*!< Convert uint8_t to uint32_t */
  for (ubIndex = 0; ubIndex < dest_size; ubIndex++) {
    dest[ubIndex] = (uint32_t)src[ubIndex * 4];
    dest[ubIndex] += (uint32_t)src[(ubIndex * 4) + 1] << 8;
    dest[ubIndex] += (uint32_t)src[(ubIndex * 4) + 2] << 16;
    dest[ubIndex] += (uint32_t)src[(ubIndex * 4) + 3] << 24;
  }
}

/**
* @brief Converts 32-bit data to 8-bit data memory type.
* @param dest Pointer to the 8-bit destination array where the content is to be copied, type-casted to a pointer of type void*.
* @param src Pointer to the 32-bit source of data to be copied, type-casted to a pointer of type const void*.
* @param n Number of 32-bit to convert.
*/
void convert32_to_8(uint8_t *dest, uint32_t *src, uint8_t n) {
  uint8_t ubIndex = 0;
  uint8_t dest_size = n;

  for (ubIndex = 0; ubIndex < dest_size; ubIndex++) {
    dest[ubIndex * 4] = (uint8_t)src[ubIndex];
    dest[(ubIndex * 4) + 1] = (uint8_t)(src[ubIndex] >> 8);
    dest[(ubIndex * 4) + 2] = (uint8_t)(src[ubIndex] >> 16);
    dest[(ubIndex * 4) + 3] = (uint8_t)(src[ubIndex] >> 24);
  }
}




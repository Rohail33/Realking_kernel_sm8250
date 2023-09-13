#ifndef _BOARD_H_
#define _BOARD_H_

#define GPIO_INPUT	0
#define GPIO_OUTPUT	1
#include <linux/types.h>

#define BURST_READ

#define BURST_READ_INTERVAL

void timing_init(void);
void Set_SWIHighSpeed(void);
void Set_SWILowSpeed(void);

uint8_t get_pin(void);

void set_pin(uint8_t level);

void set_pin_dir(uint8_t dir);

void ic_udelay(volatile uint32_t ul_Loops);
void convert8_to_32(uint32_t *dest, uint8_t *src, uint8_t n);
uint32_t Get_NVMTimeOutLoop(void);



#endif

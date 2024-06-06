#ifndef __ONEWIRE_GPIO_MASTER_H__
#define __ONEWIRE_GPIO_MASTER_H__

unsigned char ow_reset(void);
unsigned char read_byte(void);
void write_byte(char val);

#endif

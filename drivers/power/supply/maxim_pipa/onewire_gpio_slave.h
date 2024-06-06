#ifndef __ONEWIRE_GPIO_SLAVE_H__
#define __ONEWIRE_GPIO_SLAVE_H__

unsigned char ow_reset_slave(void);
unsigned char read_byte_slave(void);
void write_byte_slave(char val);

#endif

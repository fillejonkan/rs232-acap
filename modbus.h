/*
 * modbus utility functions
 */

#ifndef MODBUS_H
#define MODBUS_H

/****************** INCLUDE FILES SECTION ***********************************/

#include <sys/types.h>
#include <stdint.h>

/****************** CONSTANT AND MACRO SECTION ******************************/

/****************** TYPE DEFINITION SECTION *********************************/

/****************** GLOBAL VARIABLE DECLARATION SECTION *********************/

/****************** EXPORTED FUNCTION DECLARATION SECTION *******************/

/*
 * write modbus cmd message
 */
int modbus_write_message(int fd, unsigned char *msg, size_t size);

/*
 * Add CRC16 in two last bytes of buffer
 */
void modbus_add_crc16(unsigned char *msg, size_t size);

/*
 * Generate modbus RTU CRC16
 */
uint16_t modbus_gen_crc16(const unsigned char* msg, size_t size);

#endif /* MODBUS_H */
/****************** END OF FILE modbus.h *******************************/

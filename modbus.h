/*
 * modbus utility functions
 */

#ifndef MODBUS_H
#define MODBUS_H

/****************** INCLUDE FILES SECTION ***********************************/

#include <sys/types.h>
#include <stdint.h>
#include <termios.h>

/****************** CONSTANT AND MACRO SECTION ******************************/

/****************** TYPE DEFINITION SECTION *********************************/

enum parity {
    PARITY_NONE,
    PARITY_ODD,
    PARITY_EVEN
};

/*
 * Forward declaration of modbus handle.
 */
struct modbus;


/****************** GLOBAL VARIABLE DECLARATION SECTION *********************/

/****************** EXPORTED FUNCTION DECLARATION SECTION *******************/

uint16_t *modbus_parse_input_registers(struct modbus *modbus, size_t *n);

/*
 * Read incoming data and check CRC
 */
unsigned char *modbus_eat_buffer(struct modbus *modbus);


/*
 * Read n input registers from speficied start register.
 */
int modbus_read_input_registers(struct modbus *modbus,
                                uint16_t start,
                                uint16_t n);

/*
 * Retrieve modbus file desciptor
 */
int modbus_get_fd(struct modbus *modbus);

/*
 * Close modbus device
 */
void modbus_close_device(struct modbus **modbus);

/*
 * Initialize modbus device
 */
struct modbus *modbus_init_device(const char *path,
                                  unsigned char device_adress,
                                  enum parity par,
                                  speed_t baud,
                                  int stop_bit);

/*
 * write modbus cmd message
 */
int modbus_write_message(int fd, unsigned char *msg, size_t size);

/*
 * Add CRC16 in two last bytes of buffer
 */
void modbus_add_crc16(unsigned char *msg, size_t size);

/*
 * Check CRC16.
 */
int modbus_check_crc16(unsigned char *msg, size_t size);

/*
 * Generate modbus RTU CRC16
 */
uint16_t modbus_gen_crc16(const unsigned char* msg, size_t size);

#endif /* MODBUS_H */
/****************** END OF FILE modbus.h *******************************/

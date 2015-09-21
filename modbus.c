/*
 * modbus utility functions
 */

/****************** INCLUDE FILES SECTION ***********************************/

#include <glib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "modbus.h"

/****************** CONSTANT AND MACRO SECTION ******************************/

 #define READ_RETRIES (10)
 #define BUFSIZE (1024)
 #define CHECK_CRC

/****************** TYPE DEFINITION SECTION *********************************/

struct modbus {
    int fd;
    unsigned char device_address;
    unsigned char buf[BUFSIZE];
};

/****************** GLOBAL VARIABLE DECLARATION SECTION *********************/

static const uint16_t wCRCTable[] = {
    0X0000, 0XC0C1, 0XC181, 0X0140, 0XC301, 0X03C0, 0X0280, 0XC241,
    0XC601, 0X06C0, 0X0780, 0XC741, 0X0500, 0XC5C1, 0XC481, 0X0440,
    0XCC01, 0X0CC0, 0X0D80, 0XCD41, 0X0F00, 0XCFC1, 0XCE81, 0X0E40,
    0X0A00, 0XCAC1, 0XCB81, 0X0B40, 0XC901, 0X09C0, 0X0880, 0XC841,
    0XD801, 0X18C0, 0X1980, 0XD941, 0X1B00, 0XDBC1, 0XDA81, 0X1A40,
    0X1E00, 0XDEC1, 0XDF81, 0X1F40, 0XDD01, 0X1DC0, 0X1C80, 0XDC41,
    0X1400, 0XD4C1, 0XD581, 0X1540, 0XD701, 0X17C0, 0X1680, 0XD641,
    0XD201, 0X12C0, 0X1380, 0XD341, 0X1100, 0XD1C1, 0XD081, 0X1040,
    0XF001, 0X30C0, 0X3180, 0XF141, 0X3300, 0XF3C1, 0XF281, 0X3240,
    0X3600, 0XF6C1, 0XF781, 0X3740, 0XF501, 0X35C0, 0X3480, 0XF441,
    0X3C00, 0XFCC1, 0XFD81, 0X3D40, 0XFF01, 0X3FC0, 0X3E80, 0XFE41,
    0XFA01, 0X3AC0, 0X3B80, 0XFB41, 0X3900, 0XF9C1, 0XF881, 0X3840,
    0X2800, 0XE8C1, 0XE981, 0X2940, 0XEB01, 0X2BC0, 0X2A80, 0XEA41,
    0XEE01, 0X2EC0, 0X2F80, 0XEF41, 0X2D00, 0XEDC1, 0XEC81, 0X2C40,
    0XE401, 0X24C0, 0X2580, 0XE541, 0X2700, 0XE7C1, 0XE681, 0X2640,
    0X2200, 0XE2C1, 0XE381, 0X2340, 0XE101, 0X21C0, 0X2080, 0XE041,
    0XA001, 0X60C0, 0X6180, 0XA141, 0X6300, 0XA3C1, 0XA281, 0X6240,
    0X6600, 0XA6C1, 0XA781, 0X6740, 0XA501, 0X65C0, 0X6480, 0XA441,
    0X6C00, 0XACC1, 0XAD81, 0X6D40, 0XAF01, 0X6FC0, 0X6E80, 0XAE41,
    0XAA01, 0X6AC0, 0X6B80, 0XAB41, 0X6900, 0XA9C1, 0XA881, 0X6840,
    0X7800, 0XB8C1, 0XB981, 0X7940, 0XBB01, 0X7BC0, 0X7A80, 0XBA41,
    0XBE01, 0X7EC0, 0X7F80, 0XBF41, 0X7D00, 0XBDC1, 0XBC81, 0X7C40,
    0XB401, 0X74C0, 0X7580, 0XB541, 0X7700, 0XB7C1, 0XB681, 0X7640,
    0X7200, 0XB2C1, 0XB381, 0X7340, 0XB101, 0X71C0, 0X7080, 0XB041,
    0X5000, 0X90C1, 0X9181, 0X5140, 0X9301, 0X53C0, 0X5280, 0X9241,
    0X9601, 0X56C0, 0X5780, 0X9741, 0X5500, 0X95C1, 0X9481, 0X5440,
    0X9C01, 0X5CC0, 0X5D80, 0X9D41, 0X5F00, 0X9FC1, 0X9E81, 0X5E40,
    0X5A00, 0X9AC1, 0X9B81, 0X5B40, 0X9901, 0X59C0, 0X5880, 0X9841,
    0X8801, 0X48C0, 0X4980, 0X8941, 0X4B00, 0X8BC1, 0X8A81, 0X4A40,
    0X4E00, 0X8EC1, 0X8F81, 0X4F40, 0X8D01, 0X4DC0, 0X4C80, 0X8C41,
    0X4400, 0X84C1, 0X8581, 0X4540, 0X8701, 0X47C0, 0X4680, 0X8641,
    0X8201, 0X42C0, 0X4380, 0X8341, 0X4100, 0X81C1, 0X8081, 0X4040 };

/****************** EXPORTED FUNCTION DEFINITION SECTION *******************/

uint16_t *modbus_parse_input_registers(struct modbus *modbus, size_t *n)
{
    g_assert(modbus);
    g_assert(n);

    *n = 0;
    unsigned char *resp_buf = modbus_eat_buffer(modbus);

    if (!resp_buf) {
        return NULL;
    }

    unsigned char function_code = resp_buf[1];

    uint16_t *regs = NULL;
    size_t nregs;
    int i = 0;


    switch (function_code) {
        case 0x03: /* holding register */
        case 0x04: /* input register */
            nregs = resp_buf[2] / 2;
            *n = nregs;
            regs = g_new0(uint16_t, nregs);

            for (; i < nregs; i++) {
                size_t low_byte  = 4 + 2 * i;
                size_t high_byte = 3 + 2 * i;
                regs[i] = (resp_buf[high_byte] << 8) | resp_buf[low_byte];
            }
            break;
        default:
            g_message("Unknown function code!");
            return NULL;
    }

    return regs;
}


unsigned char *modbus_eat_buffer(struct modbus *modbus)
{
    g_assert(modbus);

    /* Now read the response */
    int tot_read = 1;
    int fd = modbus->fd;

    int retries = READ_RETRIES;
    while (tot_read < BUFSIZE && retries--) {
        int r = read(fd, &modbus->buf[tot_read], BUFSIZE - tot_read);
        if (r > 0) {
            tot_read += r;
            g_message("read %d (%d) chars", r, tot_read);
        } else {
            usleep(1000);
        }
    }

    if (tot_read < 3) {
        g_message("Too small buffer, discard!");
        return NULL;
    }

    int msg_start = 1;

    /* Check if device address was in response or not */
    if (modbus->buf[1] == modbus->device_address) {
        tot_read -= 1;
    } else {
        msg_start = 0;
        g_message("added missing device address 0x%02x", modbus->device_address);
    }

    printf("Message contents: ");
    int i = 0;
    for (; i < tot_read; i++) {
        printf("%d = 0x%02x, ", i, modbus->buf[msg_start+i]);
    }
    printf("\n");

    /* Verify CRC code */
    #ifdef CHECK_CRC
        if (modbus_check_crc16(&modbus->buf[msg_start], tot_read) < 0) {
            return NULL;
        }
    #endif

    return &modbus->buf[msg_start];
}


/* open serial port for read and write */
static int open_serial_tty(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd < 0) {
        perror("failed to open serial port");
        g_assert(0);
    } else {
        g_message("%s() [%s:%d] - Opened serial port fd=%d",
                        __FUNCTION__, __FILE__, __LINE__, fd);
    }

    return fd;
}

int modbus_get_fd(struct modbus *modbus)
{
    g_assert(modbus);

    return modbus->fd;
}

int modbus_read_input_registers(struct modbus *modbus,
                                uint16_t start,
                                uint16_t n)
{
    g_assert(modbus);
    unsigned char cmd_buf[8] = {0,};

    cmd_buf[0] = modbus->device_address;
    cmd_buf[1] = 0x04; /* Function code for input register */

    cmd_buf[2] = (start >> 8) & 0xFF;
    cmd_buf[3] = start & 0xFF;
    cmd_buf[4] = (n >> 8) & 0xFF;
    cmd_buf[5] = n & 0xFF;

    if (modbus_write_message(modbus->fd, cmd_buf, sizeof(cmd_buf))) {
        return -1;
    }

    return 0;
}

/*
 * Close modbus device
 */
void modbus_close_device(struct modbus **modbus)
{
    if (!modbus) {
        return;
    }

    struct modbus *m = *modbus;

    if (!m) {
        return;
    }

    close(m->fd);
    g_free(m);
    *modbus = NULL;
}

/*
 * Initialize modbus device
 */
struct modbus *modbus_init_device(const char *path,
                                  unsigned char device_address,
                                  enum parity par,
                                  speed_t baud,
                                  int stop_bit)
{
    g_assert(path);

    int fd = open_serial_tty(path);

    /* Create device structure */
    struct modbus *modbus = g_new0(struct modbus, 1);
    modbus->device_address = device_address;
    modbus->fd = fd;

    /* Make first character of receive buffer the device address in case
     * the device address was missing in a response.
     */
    modbus->buf[0] = device_address;

    /* Configure serial port according to desired settings */
    struct termios ts = {0,};

    if (tcgetattr(fd, &ts)) {
        perror("Failed to get serial port settings!");
        g_assert(0);
    }

    /* Set input and output baud rate the same */
    cfsetispeed(&ts, baud);
    cfsetospeed(&ts, baud);

    /* Only local ownership of port, allow read.*/
    ts.c_cflag |= (CLOCAL | CREAD);

    /* Add extra stop bit if requested */
    if (stop_bit) {
        ts.c_cflag |= CSTOPB;
    } else {
        ts.c_cflag &= ~CSTOPB;
    }

    /* Setup parity bit according to input configuration */
    switch (par) {
        case PARITY_NONE:
            ts.c_cflag &= ~PARENB;
            break;
        case PARITY_ODD:
            ts.c_cflag |= (PARENB | PARODD);
            break;
        case PARITY_EVEN:
            ts.c_cflag |= PARENB;
            ts.c_cflag &= ~PARODD;
            break;
        default:
            g_assert(0);
    }

    /* Set 8 bit data size */
    ts.c_cflag &= ~CSIZE; /* Mask the character size bits */
    ts.c_cflag |= CS8;    /* Select 8 data bits */

    /* Make raw */
    ts.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    /* Raw output, no post processing of data */
    ts.c_oflag &= ~OPOST;

    /* Physically commit changes to serial port immediately */
    if (tcsetattr(fd, TCSANOW, &ts)) {
        perror("Failed to configure TTY terminal");
        g_assert(0);
    }

    return modbus;
}

/*
 * write modbus cmd message
 */
int modbus_write_message(int fd, unsigned char *msg, size_t size)
{
    g_assert(size > 2);

    /* Last two bytes are CRC code 8 */
    modbus_add_crc16(msg, size);

    /* Send the command down the line */
    int n = write(fd, msg, size);
    if (n < 0) {
        g_message("write() of %d bytes failed! %s\n", size, strerror(errno));
        return -1;
    } else {
        g_message("Successfully wrote %d characters, CRC= 0x%2x, 0x%2x", n,
            msg[size-2], msg[size-1]);
    }

    return 0;
}

/*
 * Generate modbus RTU CRC16
 */
uint16_t modbus_gen_crc16(const unsigned char* msg, size_t size)
{
    unsigned char nTemp;
    uint16_t wCRCWord = 0xFFFF;

    while (size--)
    {
        nTemp = *msg++ ^ wCRCWord;
        wCRCWord >>= 8;
        wCRCWord ^= wCRCTable[nTemp];
    }

   return wCRCWord;
}

void modbus_add_crc16(unsigned char *msg, size_t size)
{
    g_assert(size > 2);

    /* generate two byte CRC code */
    uint16_t crc = modbus_gen_crc16(msg, size-2);

    /* Populate buffer with CRC code */
    msg[size-2] = crc & 0xFF;
    msg[size-1] = (crc >> 8) & 0xFF;
}

/*
 * Check CRC16.
 */
int modbus_check_crc16(unsigned char *msg, size_t size)
{
    g_assert(size > 2);

    /* generate two byte CRC code */
    uint16_t crc = modbus_gen_crc16(msg, size-2);

    /* Populate buffer with CRC code */
    unsigned char crc1 = crc & 0xFF;
    unsigned char crc2 = (crc >> 8) & 0xFF;

    g_message("Calc CRC 0x%02x 0x%02x input crc 0x%02x 0x%02x",
        crc1, crc2, msg[size-2], msg[size-1]);

    if (crc1 != msg[size-2] || crc2 != msg[size-1]) {
        g_message("BAD CRC");
        return -1;
    }

    return 0;
}


/****************** END OF FILE modbus.c *******************************/

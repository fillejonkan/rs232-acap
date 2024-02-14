/*
* - RS 232 -
*
* Read input from serial port and output to dynamic overlay
*
* cmdline stty command stty -F /dev/ttyS1 300 cread cs8 -cstopb -echo -onlcr
*/

#include <glib.h>
#include <gio/gio.h>
#include <string.h>

/* Serial port includes */
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>

#include "modbus.h"
#include "overlay.h"

#define OVERLAY_BUF_SIZE (64)
#define OVERLAY_STR_SIZE (OVERLAY_BUF_SIZE -1)

/**
* Handle for overlay instance
*/
static overlay_handle ovl_handle = NULL;


/*********************** INTERNAL FUNCTION DECLARATIONS ***********************/

/*
 *
 * Read humidity data from lily temperature sensor using MODBUS protocol.
 */
static float lily_read_humidity_data(struct modbus **modbus);

static int lily_init_modbus(struct modbus **modbus);

/*
 *
 * Timer function used to poll humidity / temperature data
 */
static gboolean
on_timeout(gpointer user_data);


/*********************** INTERNAL FUNCTION DEFINITIONS ************************/

static int lily_init_modbus(struct modbus **modbus)
{
    g_assert(modbus);

    if (*modbus) {
        modbus_close_device(modbus);
    }

    g_message("------------REINIT SERIAL PORT------------------------");

    *modbus = modbus_init_device("/dev/ttyS1",
                                 0x01,
                                 PARITY_EVEN,
                                 B9600,
                                 0 /* No stop bit */);

    return 0;
}

static float lily_read_humidity_data(struct modbus **modbus)
{
    g_assert(modbus);
    g_assert(*modbus);

    struct modbus *m = *modbus;
    /* Read input register starting at 0 and just the first register */
    modbus_read_input_registers(m, 10, 2);

    /* Wait for device to process data */
    usleep(50000);

    static unsigned int n_reads    = 0;
    static unsigned int n_failures = 0;
    uint16_t reg1;
    uint16_t reg2;

    size_t nregs;
    uint16_t *regs = modbus_parse_input_registers(m, &nregs);

    if (regs) {
        reg1 = regs[0];
        g_message("[%d, %d] Got Reg1 0x%04x",
            n_reads % 10, n_failures % 5, reg1);

        int bit1 = (reg1 & 0x01) != 0;
        int bit2 = (reg1 & (0x01 << 1)) != 0;
        int bit3 = (reg1 & (0x01 << 2)) != 0;
        int bit4 = (reg1 & (0x01 << 3)) != 0;

        /* Finally update the dynamic overlay with the humidity data */
        char str[OVERLAY_BUF_SIZE];
        g_snprintf(str, sizeof(str), "[%d] REG1-bits: %d%d%d%d", (++n_reads) % 10, bit1, bit2, bit3, bit4);

        if (nregs > 1) {
            reg2 = regs[1];
            g_message("[%d] Got Reg2 0x%04x", n_reads % 10,
                reg2);
            
            bit1 = (reg2 & 0x01) != 0;
            bit2 = (reg2 & (0x01 << 1)) != 0;
            bit3 = (reg2 & (0x01 << 2)) != 0;
            bit4 = (reg2 & (0x01 << 3)) != 0;
            int bit5 = (reg2 & (0x01 << 4)) != 0;
            int bit6 = (reg2 & (0x01 << 5)) != 0;
            int bit7 = (reg2 & (0x01 << 6)) != 0;
            int bit8 = (reg2 & (0x01 << 7)) != 0;


            snprintf(&str[strlen(str)], sizeof(str), " REG2-bits: %d%d%d%d%d%d%d%d",
                bit1, bit2, bit3, bit4, bit5, bit6, bit7, bit8);
        }

        overlay_set_data(ovl_handle, NULL, "RS232", str);
        g_free(regs);
    } else {
        n_failures++;
        /* Re-init serial port in case something went wrong */
        if (n_failures && n_failures % 5) {
            lily_init_modbus(modbus);
        }
    }

    return 0;
}

/*
 * This function will increase our timer and print the current value to stdout.
 */
static gboolean
on_timeout(gpointer user_data)
{
    g_assert(user_data);

    struct modbus **modbus = user_data;

    lily_read_humidity_data(modbus);

    /* Return FALSE if the event source should be removed */
    return TRUE;
}

/*
 * Our main function
 */
int
main(void)
{
    GMainLoop *loop;
    guint timer = 0;

    loop    = g_main_loop_new(NULL, FALSE);

    struct modbus *modbus = NULL;

    lily_init_modbus(&modbus);
    ovl_handle = overlay_init();

    /* Periodically call 'on_timeout()' every second */
    g_timeout_add(500, on_timeout, &modbus);

    /* start the main loop */
    g_main_loop_run(loop);

    /* free up resources */
    modbus_close_device(&modbus);
    g_main_loop_unref(loop);

    return 0;
}
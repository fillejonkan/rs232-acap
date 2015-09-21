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
#include <axsdk/axhttp.h>

/* Serial port includes */
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>

#include <statuscache.h>
#include "modbus.h"

#define OVERLAY_BUF_SIZE (64)
#define OVERLAY_STR_SIZE (OVERLAY_BUF_SIZE -1)


/*********************** INTERNAL FUNCTION DECLARATIONS ***********************/

/*
 * Update dynamic overlay with input string. Max string length 31 characters
 * plus NUL character. Uses statuscache library.
 */
static void update_dynamic_overlay(char *s);

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
                                 0x11,
                                 PARITY_NONE,
                                 B9600,
                                 0 /* No stop bit */);

    return 0;
}

static void update_dynamic_overlay(char *s)
{
    if (strlen(s) > OVERLAY_STR_SIZE) {
        g_message("Overlay string to large %d > %d", strlen(s),
            OVERLAY_STR_SIZE);
        return;
    }

    /* Ignore return value if group is already created */
    sc_create_group("DYNAMIC_TEXT_IS1", 512, 0);

    struct sc_param sc_par = { .name="DYNAMIC_TEXT",
                               .size=OVERLAY_BUF_SIZE,
                               .data=s,
                               .type=SC_STRING};

    /* NULL-terminated list of pointers to param structs. we need just one */
    struct sc_param *arr[2] = {&sc_par, 0};

    sc_set_group("DYNAMIC_TEXT_IS1", arr, SC_CREATE);
}

static float lily_read_humidity_data(struct modbus **modbus)
{
    g_assert(modbus);
    g_assert(*modbus);

    struct modbus *m = *modbus;
    /* Read input register starting at 0 and just the first register */
    modbus_read_input_registers(m, 0, 2);

    /* Wait for device to process data */
    usleep(50000);

    static unsigned int n_reads    = 0;
    static unsigned int n_failures = 0;

    size_t nregs;
    uint16_t *regs = modbus_parse_input_registers(m, &nregs);

    if (regs) {
        uint16_t humidity = regs[0];
        float hum_f = humidity / 10.0;
        g_message("[%d, %d] Got humidity 0x%04x=%2.1f%%",
            n_reads % 10, n_failures % 5, humidity,
            hum_f);

        /* Finally update the dynamic overlay with the humidity data */
        char str[OVERLAY_BUF_SIZE];
        g_snprintf(str, sizeof(str), "[%d] H=%2.1f%%", (++n_reads) % 10, hum_f);

        if (nregs > 1) {
            uint16_t temperature = regs[1];
            float temp_f = temperature / 10.0;
            g_message("[%d] Got temperature 0x%04x=%2.1fC", n_reads % 10,
                temperature,
                temp_f);
            snprintf(&str[strlen(str)], sizeof(str), " T=%2.1fC", temp_f);
        }

        update_dynamic_overlay(str);
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
 * This function will be called when our CGI is accessed.
 */
static void
request_handler(const gchar *path,
        const gchar *method,
        const gchar *query,
        GHashTable *params,
        GOutputStream *output_stream,
        gpointer user_data)
{
    GDataOutputStream *dos;
    guint *timer = (guint*) user_data;
    gchar msg[128];

    dos = g_data_output_stream_new(output_stream);

    /* Send out the HTTP response status code */
    g_data_output_stream_put_string(dos,"Content-Type: text/plain\r\n", NULL, NULL);
    g_data_output_stream_put_string(dos,"Status: 200 OK\r\n\r\n", NULL, NULL);

    /* Our custom message */
    g_snprintf(msg, sizeof(msg),"You have accessed '%s'\n", path ? path:"(NULL)");
    g_data_output_stream_put_string(dos, msg, NULL, NULL);

    g_snprintf(msg, sizeof(msg), "\n%s() [%s:%d] \n\n"
                                                             "-  Hello World! -\n"
                                                             "I've been running for %d seconds\n",
                                                             __FUNCTION__, __FILE__, __LINE__, *timer );
    g_data_output_stream_put_string(dos, msg, NULL, NULL);

    g_object_unref(dos);
}

/*
 * Our main function
 */
int
main(void)
{
    GMainLoop *loop;
    AXHttpHandler *handler;
    guint timer = 0;

    loop    = g_main_loop_new(NULL, FALSE);
    handler = ax_http_handler_new(request_handler, &timer);

    g_message("Created a HTTP handler: %p", handler);

    struct modbus *modbus = NULL;

    lily_init_modbus(&modbus);

    /* Periodically call 'on_timeout()' every second */
    g_timeout_add(2000, on_timeout, &modbus);

    /* start the main loop */
    g_main_loop_run(loop);

    /* free up resources */
    modbus_close_device(&modbus);
    g_main_loop_unref(loop);
    ax_http_handler_free(handler);

    return 0;
}
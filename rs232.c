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

#define OVERLAY_BUF_SIZE (32)
#define OVERLAY_STR_SIZE (OVERLAY_BUF_SIZE -1)

/*********************** INTERNAL FUNCTION DECLARATIONS ***********************/

/*
 * Update dynamic overlay with input string. Max string length 31 characters
 * plus NUL character. Uses statuscache library.
 */
static void update_dynamic_overlay(char *s);

/*
 * Open serial TTY port
 */
static int open_serial_tty();

/*
 * Configure serial TTY port using termios API.
 */
static void configure_serial_tty(int fd);

/*
 * Read from serial port (max OVERLAY_STR_SIZE char)
 * and output to dynamic overlay
 */
static gboolean echo_serial_tty(gpointer user_data);

/*
 *
 * Read humidity data from lily temperature sensor using MODBUS protocol.
 */
static float lily_read_humidity_data(int fd);

/*
 *
 * Timer function used to poll humidity / temperature data
 */
static gboolean
on_timeout(gpointer user_data);


/*********************** INTERNAL FUNCTION DEFINITIONS ************************/

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

/* open serial port for read and write */
static int open_serial_tty()
{
    int fd = open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd < 0) {
        perror("failed to open serial port");
        g_assert(0);
    } else {
        g_message("%s() [%s:%d] - Opened serial port fd=%d",
                        __FUNCTION__, __FILE__, __LINE__, fd);
    }

    return fd;
}

/* Configure serial port */
static void configure_serial_tty(int fd)
{
    struct termios ts = {0,};

    if (tcgetattr(fd, &ts)) {
        perror("Failed to get serial port settings!");
        g_assert(0);
    }

    /* Set input and output baud rate to 300, hardcoded for now */
    cfsetispeed(&ts, B9600);
    cfsetospeed(&ts, B9600);

    /* Only local ownership of port, allow read and one extra stop bit (2) */
    ts.c_cflag |= (CLOCAL | CREAD);
    ts.c_cflag &= ~(CSTOPB | PARENB);

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
}


static float lily_read_humidity_data(int fd)
{
    unsigned char cmd_buf[8];

    /* Fill command buffer according to data sheet */
    cmd_buf[0] = 0x11;
    cmd_buf[1] = 0x04;
    cmd_buf[2] = 0x00;
    cmd_buf[3] = 0x00;
    cmd_buf[4] = 0x00;
    cmd_buf[5] = 0x01;
    cmd_buf[6] = 0x33;
    cmd_buf[7] = 0x5A;

    int n = write(fd, cmd_buf, sizeof(cmd_buf));
    if (n < 0)
        fputs("write() of 8 bytes failed!\n", stderr);
    else {
        g_message("Successfully wrote %d characters", n);
    }

    /* Now read the response */
    int tot_read = 0;
    unsigned char resp_buf[6] = {0,};

    /* 7 according to data sheet but seems like device ID is not output */

    while (tot_read < 6) {
        int r = read(fd, &resp_buf[tot_read], 7 - tot_read);
        if (r > 0) {
            tot_read += r;
            g_message("read %d (%d) chars", r, tot_read);
        }
    }

    g_message("Got bytes 0=0x%02x 1=0x%02x 2=0x%02x, 3=0x%02x 4=0x%02x 5=0x%02x", resp_buf[0], resp_buf[1],resp_buf[2],resp_buf[3],resp_buf[4],resp_buf[5]);

    unsigned int humidity = (resp_buf[2] << 8) | resp_buf[3];
    float hum_f = humidity / 10.0;
    g_message("Got humidity 0x%04x=%2.1f%%", humidity, hum_f);

    /* Finally update the dynamic overlay with the humidity data */
    char str[6];
    g_snprintf(str, sizeof(str), "%2.1f%%", hum_f);
    update_dynamic_overlay(str);

    return 0;
}

static gboolean echo_serial_tty(gpointer user_data)
{
    int tot_read = 0;
    int stop = 0;
    char buf[OVERLAY_BUF_SIZE];
    int fd = *((int *) user_data);

    /* Read lines or input characters from tty and print */
    g_message("Wating for serial input (fd=%d)", fd);
    
    /* reset state and ensure zero termination */
    memset(buf, 0, sizeof(buf));
    tot_read = 0;
    stop = 0;

    while (tot_read < (OVERLAY_STR_SIZE) && !stop) {
        int r = read(fd, &buf[tot_read], OVERLAY_STR_SIZE - tot_read);
        if (r > 0) {
            tot_read += r;
            g_message("read %d (%d) chars", r, tot_read);
        }
        /* Stop on new line from echo */
        if (strstr(buf, "\n") != NULL) {
            stop = 1;
            /* replace newline with zero termination */
            buf[tot_read - 1] = '\0';
        }
    }
    g_message("Got serial string: %s", buf);
        update_dynamic_overlay(buf);

    return TRUE;
}

/*
 * This function will increase our timer and print the current value to stdout.
 */
static gboolean
on_timeout(gpointer user_data)
{
    int fd = *((int *) user_data); 

    lily_read_humidity_data(fd);

    return TRUE; /* FALSE removes the event source */
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

    //update_dynamic_overlay("Initial");

    int fd = open_serial_tty();
    configure_serial_tty(fd);

    /* Periodically call 'on_timeout()' every second */
    //lily_read_humidity_data(fd);
    //g_idle_add(echo_serial_tty, &fd);
    g_timeout_add(2000, on_timeout, &fd);

    //echo_serial_tty();
    /* start the main loop */
    g_main_loop_run(loop);

    /* free up resources */
    close(fd);
    g_main_loop_unref(loop);
    ax_http_handler_free(handler);

    return 0;
}
/*
* - Hello Glib -
*
* This application is a basic 'hello world' type of application.
* It will set up a CGI interface on http://<cam-ip>/local/hello_glib/example.cgi
* -The CGI-path is specified by the HTTPCGIPATH variable in package.conf,
* which in this case points to the file "cgi.txt"
*
* The application will also register a timer, which on timeout calls a
* function. In this case 'on_timeout()' ever second. The current timer value
* is printed when example.cgi is accessed.
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

static int fd = -1;

void update_dynamic_overlay(const char *s)
{
  /* Ignore return value if group is already created */
  sc_create_group("DYNAMIC_TEXT_IS1", 512, 0);

  struct sc_param sc_par = {.name="DYNAMIC_TEXT", .size=32, .data=s, .type=SC_STRING};
  struct sc_param *arr[2] = {&sc_par, 0};

  sc_set_group("DYNAMIC_TEXT_IS1", arr, SC_CREATE);
}

/* open serial port for read and write */
int open_serial_tty()
{
  fd = open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NDELAY);

  if (fd < 0) {
    perror("failed to open serial port");
  } else {
    g_message("%s() [%s:%d] - Opended serial port fd=%d",
            __FUNCTION__, __FILE__, __LINE__, fd);
  }

  return fd;
}

/* Configure serial port */
int configure_serial_tty()
{
  struct termios ts = {0,};

  if (tcgetattr(fd, &ts)) {
    perror("Failed to get serial port settings!");
    g_assert(0);
  }

  cfsetispeed(&ts, B300);
  cfsetospeed(&ts, B300);

  ts.c_cflag |= (CLOCAL | CREAD | CSTOPB);

  /* Set 8 bit data size */
  ts.c_cflag &= ~CSIZE; /* Mask the character size bits */
  ts.c_cflag |= CS8;    /* Select 8 data bits */

  /* Make raw */
  ts.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

  if (tcsetattr(fd, TCSANOW, &ts)) {
    perror("Failed to configure TTY terminal");
    g_assert(0);
  }

  return 0;
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

void write_serial_tty()
{
  int n = write(fd, "HEJ\r", 4);
  if (n < 0)
    fputs("write() of 4 bytes failed!\n", stderr);
}

gboolean echo_serial_tty(gpointer user_data)
{
  int tot_read = 0;
  int buf_size = 32;
  int stop = 0;
  char buf[32];

  /* Read lines or 32 input characters from tty and print */
  //while (1) {
    g_message("Wating for serial input");
    
    /* reset state */
    memset(buf, 0, sizeof(buf));
    tot_read = 0;
    stop = 0;

    while (tot_read < buf_size && !stop) {
      int r = read(fd, &buf[tot_read], 32 - tot_read);
      if (r > 0) {
        tot_read += r;
        g_message("read %d (%d) chars", r, tot_read);
      }
      /* Stop on new line from echo */
      if (strstr(buf, "\n\n") != NULL) {
        stop = 1;
      }
    }
    g_message("Got serial string: %s", buf);
    /* DIRTY zero terminate string without trailing newline */
    buf[tot_read - 2] = '\0';
    update_dynamic_overlay(buf);
  //}

  return TRUE;
}

/*
 * This function will increase our timer and print the current value to stdout.
 */
 #if 0
static gboolean
on_timeout(gpointer data)
{
  guint *timer = (guint*) data;

  (*timer)++; /* increase the timer */

  g_message("%s() [%s:%d] - this app has been running for %d secs.",
            __FUNCTION__, __FILE__, __LINE__, *timer);
  //write_serial_tty();

  return TRUE; /* FALSE removes the event source */
}
#endif

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

  (void) open_serial_tty();
  (void) configure_serial_tty();

  /* Periodically call 'on_timeout()' every second */
  g_idle_add(echo_serial_tty, NULL);
  //g_timeout_add(1000, on_timeout, &timer);

  //echo_serial_tty();
  /* start the main loop */
  g_main_loop_run(loop);

  /* free up resources */
  close(fd);
  g_main_loop_unref(loop);
  ax_http_handler_free(handler);

  return 0;
}


#include "terminal.h"
#include "serial.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#include <stdio.h>

typedef struct terminal_device terminal_device;

struct terminal_device
{
    serial_side_api_st api;
    struct termios orig_term;
    int in_fd;
    int  out_fd;
};

static void
terminal_close(serial_side_api_st * const serial)
{
    if (serial == NULL)
    {
        goto done;
    }
    terminal_device * const device = container_of(serial, terminal_device, api);

    tcsetattr(device->in_fd, TCSANOW, &device->orig_term);

done:
    return;
}

static void
terminal_free(serial_side_api_st * const serial)
{
    if (serial == NULL)
    {
        goto done;
    }
    terminal_device * const device = container_of(serial, terminal_device, api);

    terminal_close(serial);
    free(device);

done:
    return;
}

static int
terminal_init_device(terminal_device * const device)
{
    int fd;
    int n_tty = N_TTY;

    if (device == NULL)
    {
        fd = -1;
        goto done;
    }

    fd = device->in_fd;

    if (!isatty(fd))
    {
        /* Not a TTY, so no need to configure. */
        goto done;
    }

    if (ioctl(fd, TIOCSETD, &n_tty))
    {
        close(fd);
        fd = -1;
        goto done;
    }

    struct termios term;

    if (tcgetattr(fd, &device->orig_term) < 0)
    {
        fd = -1;
        goto done;
    }

    term = device->orig_term;
    term.c_lflag &= ~(ECHO|ICANON);
    term.c_iflag &= ~(IGNBRK|ICRNL);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &term) < 0)
    {
        fd = -1;
        goto done;
    }

done:
    if (fd == -1)
    {
        fprintf(stderr, "failed to init device\n");
    }

    return fd;
}

static int
terminal_get_rx_fd(serial_side_api_st * const serial)
{
    int result;

    if (serial == NULL)
    {
        result = -1;
        goto done;
    }
    terminal_device * const device = container_of(serial, terminal_device, api);

    result = device->in_fd;

done:
    return result;
}

static ssize_t
terminal_write(
    serial_side_api_st * const serial,
    void const * const data,
    size_t const data_length)
{
    ssize_t bytes_written;

    if (serial == NULL)
    {
        bytes_written = -1;
        goto done;
    }

    terminal_device * const device = container_of(serial, terminal_device, api);

    if (device->out_fd < 0)
    {
        bytes_written = -1;
        goto done;
    }

    bytes_written = ser_write(device->out_fd, (unsigned char *)data, data_length);

done:
    return bytes_written;
}

static int
terminal_set_flow_control(serial_side_api_st *serial, int status)
{
    return 0;
}

static int
terminal_get_control_lines(serial_side_api_st *serial)
{
    return 0;
}

static int
terminal_set_control_lines(serial_side_api_st *serial, int state)
{
    return 0;
}

static int
terminal_read(serial_side_api_st *serial,  unsigned char *data, int len)
{
    if (serial == NULL)
    {
        return -1;
    }

    terminal_device * const device = container_of(serial, terminal_device, api);

    return ser_read(device->in_fd, data, len);
}

static serial_side_methods_st const terminal_methods =
{
    .close = terminal_close,
    .free = terminal_free,
    .get_rx_fd = terminal_get_rx_fd,
    .write = terminal_write,

    .set_flow_control = terminal_set_flow_control,
    .get_control_lines = terminal_get_control_lines,
    .set_control_lines = terminal_set_control_lines,
    .read = terminal_read
};

serial_side_api_st *
terminal_init(void)
{
    terminal_device * device = calloc(1, sizeof(*device));

    if (device == NULL)
    {
        goto done;
    }

    device->in_fd = STDIN_FILENO;
    device->out_fd  = STDOUT_FILENO;

    if (terminal_init_device(device) < 0) {
        free(device);
        device = NULL;
        goto done;
    }

done:
    serial_side_api_st * serial;

    if (device != NULL)
    {
        serial = &device->api;
        serial->methods = &terminal_methods;
    }
    else
    {
        serial = NULL;
    }

    return serial;
}



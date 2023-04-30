#include "serial_port.h"
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

typedef struct serial_device serial_device;

struct serial_device
{
    serial_side_api_st api;
    int fd;
};

static void
serial_port_close(serial_side_api_st * const serial)
{
    if (serial == NULL)
    {
        goto done;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    if (device->fd >= 0)
    {
        close(device->fd);
        device->fd = -1;
    }

done:
    return;
}

static void
serial_port_free(serial_side_api_st * const serial)
{
    if (serial == NULL)
    {
        goto done;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    serial_port_close(serial);
    free(device);

done:
    return;
}

static int
serial_port_get_rx_fd(serial_side_api_st * const serial)
{
    int result;

    if (serial == NULL)
    {
        result = -1;
        goto done;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    result = device->fd;

done:
    return result;
}

static ssize_t
serial_port_write(
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

    serial_device * const device = container_of(serial, serial_device, api);

    if (device->fd < 0)
    {
        bytes_written = -1;
        goto done;
    }

    bytes_written = ser_write(device->fd, (unsigned char *)data, data_length);

done:
    return bytes_written;
}

static int
serial_port_set_flow_control(serial_side_api_st *serial, int const iflag, int const cflag)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_set_flow_control(device->fd, iflag, cflag);
}

static int serial_port_set_parity_databits(serial_side_api_st *serial, int const cflag)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_set_parity_databits(device->fd, cflag);
}

static int serial_port_set_speed(serial_side_api_st *serial, int const speed)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_set_speed(device->fd, speed);
}

static int
serial_port_get_control_lines(serial_side_api_st *serial)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_get_control_lines(device->fd);
}

static int
serial_port_set_control_lines(serial_side_api_st *serial, int state)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_set_control_lines(device->fd, state);
}

static int
serial_port_read(serial_side_api_st *serial,  unsigned char *data, int len)
{
    if (serial == NULL)
    {
        return -1;
    }

    serial_device * const device = container_of(serial, serial_device, api);

    return ser_read(device->fd, data, len);
}


static serial_side_methods_st const serial_side_methods =
{
    .close = serial_port_close,
    .free = serial_port_free,
    .get_rx_fd = serial_port_get_rx_fd,
    .write = serial_port_write,

    .set_flow_control = serial_port_set_flow_control,
    .set_parity_databits = serial_port_set_parity_databits,
    .set_speed = serial_port_set_speed,
    .get_control_lines = serial_port_get_control_lines,
    .set_control_lines = serial_port_set_control_lines,
    .read = serial_port_read
};

serial_side_api_st *
serial_port_init(char const * const device_name, int const speed, int const stopbits)
{
    struct serial_device * device = NULL;
    int const fd = ser_init_conn(device_name, speed, stopbits);

    if (fd == -1)
    {
        goto done;
    }

    device = calloc(1, sizeof(*device));
    if (device == NULL)
    {
        close(fd);
        goto done;
    }

    device->fd = fd;

done:
    serial_side_api_st * serial_side_ctx;

    if (device != NULL)
    {
        serial_side_ctx = &device->api;
        serial_side_ctx->methods = &serial_side_methods;
    }
    else
    {
        serial_side_ctx = NULL;
    }

    return serial_side_ctx;
}


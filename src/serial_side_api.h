#pragma once

#include <stddef.h>
#include <unistd.h>
#include <stdint.h>

typedef struct serial_side_api_st serial_side_api_st;

typedef void (*serial_side_close_fn)(serial_side_api_st * serial);

typedef void (*serial_side_free_fn)(serial_side_api_st * serial);

typedef int (*serial_side_rx_fd_fn)(serial_side_api_st * serial);

typedef ssize_t (*serial_side_write_fn)(
    serial_side_api_st * serial, void const * data, size_t data_length);

typedef int (* ser_set_flow_control_fn)(serial_side_api_st * serial, int iflag, int cflag);
typedef int (* ser_set_parity_databits_fn)(serial_side_api_st * serial, int cflag);
typedef int (* ser_set_speed_fn)(serial_side_api_st * serial, int speed);
typedef int (* ser_get_control_lines_fn)(serial_side_api_st * serial);
typedef int (* ser_set_control_lines_fn)(serial_side_api_st * serial, int state);
typedef int (* ser_read_fn)(serial_side_api_st * serial,  unsigned char *data, int len);

typedef struct serial_side_methods_st
{
    serial_side_close_fn close;
    serial_side_free_fn free;
    serial_side_rx_fd_fn get_rx_fd;
    serial_side_write_fn write;

    ser_set_flow_control_fn set_flow_control;
    ser_set_parity_databits_fn set_parity_databits;
    ser_set_speed_fn set_speed;
    ser_get_control_lines_fn get_control_lines;
    ser_set_control_lines_fn set_control_lines;
    ser_read_fn read;
} serial_side_methods_st;

struct serial_side_api_st
{
    serial_side_methods_st const * methods;
};



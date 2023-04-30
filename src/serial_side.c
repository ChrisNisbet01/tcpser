#include "serial_side.h"
#include "serial_port.h"
#include "terminal.h"

#include <stdbool.h>
#include <string.h>

#include <stdio.h>

serial_side_api_st *
serial_side_create(char const * const device_name, int const speed, int const stopbits)
{
    serial_side_api_st * pctx = NULL;

    bool const is_terminal = strcmp(device_name, "term") == 0;

    if (is_terminal)
    {
        pctx = terminal_init();
    }
    else
    {
        /* Assume serial device. */
        pctx = serial_port_init(device_name, speed, stopbits);
    }

    return pctx;
}


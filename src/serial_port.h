#pragma once

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "serial_side_api.h"

serial_side_api_st *
serial_port_init(char const * device_name, int speed, int stopbits);



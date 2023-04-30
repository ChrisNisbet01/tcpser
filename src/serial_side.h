#pragma once

#include "serial_side_api.h"

serial_side_api_st *
serial_side_create(char const * device_name, int speed);


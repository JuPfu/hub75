#pragma once

#include "hub75.hpp"

#define HIGH 1
#define LOW 0

void FM6126A_init_register(const Hub75PinConfig &pins);
void FM6126A_write_register(const Hub75PinConfig &pins, uint32_t matrix_panel_width, uint16_t value, uint8_t position);
void FM6126A_setup(const Hub75PinConfig &pins, uint32_t matrix_panel_width);

#pragma once

#include <stdbool.h>
#include <stdint.h>

void status_led_soft_off_start(uint32_t hold_ms);
void status_led_soft_off_stop(bool shutting_down);

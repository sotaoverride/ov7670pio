/**
 * Copyright (c) 2022 Brian Starkey <stark3y@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include <stdio.h>
#include "pico/stdlib.h"

#include "camera.h"
#include "format.h"
#include "ov7670.h"
#include "ov7670.pio.h"
#define CAMERA_PIO      pio0
#define CAMERA_BASE_PIN 8
#define CAMERA_XCLK_PIN 21
#define CAMERA_SDA      0
#define CAMERA_SCL      1

static inline int __i2c_write_blocking(void *i2c_handle, uint8_t addr, const uint8_t *src, size_t len)
{
	return i2c_write_blocking((i2c_inst_t *)i2c_handle, addr, src, len, false);
}

static inline int __i2c_read_blocking(void *i2c_handle, uint8_t addr, uint8_t *dst, size_t len)
{
	return i2c_read_blocking((i2c_inst_t *)i2c_handle, addr, dst, len, false);
}

// From http://www.paulbourke.net/dataformats/asciiart/
const char charmap[] = " .:-=+*#%@";
OV7670_status status;
static inline uint8_t bits_packed_per_word(uint8_t pin_count) {
	// If the number of pins to be sampled divides the shift register size, we
	// can use the full SR and FIFO width, and push when the input shift count
	// exactly reaches 32. If not, we have to push earlier, so we use the FIFO
	// a little less efficiently.
	const uint SHIFT_REG_WIDTH = 32;
	return SHIFT_REG_WIDTH - (SHIFT_REG_WIDTH % pin_count);
}

int main() {
	stdio_init_all();

	// Wait some time for USB serial connection
	sleep_ms(3000);

	const uint LED_PIN = PICO_DEFAULT_LED_PIN;
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);

	i2c_init(i2c0, 100000);
	gpio_set_function(CAMERA_SDA, GPIO_FUNC_I2C);
	gpio_set_function(CAMERA_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(CAMERA_SDA);
	gpio_pull_up(CAMERA_SCL);

	struct camera camera;
	struct camera_platform_config platform = {
		.i2c_write_blocking = __i2c_write_blocking,
		.i2c_read_blocking = __i2c_read_blocking,
		.i2c_handle = i2c0,

		.pio = CAMERA_PIO,
		.xclk_pin = CAMERA_XCLK_PIN,
		.xclk_divider = 9,
		.base_pin = CAMERA_BASE_PIN,
		.base_dma_channel = -1,
	};

	int ret = camera_init(&camera, &platform);
	if (ret) {
		printf("camera_init failed: %d\n", ret);
		return 1;
	}


	pio_clear_instruction_memory (CAMERA_PIO);

	const uint16_t width = CAMERA_WIDTH_DIV8;
	const uint16_t height = CAMERA_HEIGHT_DIV8;
	uint8_t dma_chan;

	uint8_t capture_buf [width*height];

	uint8_t pattern[] =  " .:!()/|}-=+*#%@";
	int sm = 0;
	uint8_t offset = pio_add_program(CAMERA_PIO, &ov7670_program);
	dma_channel_config cd = dma_channel_get_default_config(dma_chan);
	pio_sm_config c = pio_get_default_sm_config();
	pio_sm_init(CAMERA_PIO, sm, offset, &c);
	pio_sm_set_enabled(CAMERA_PIO, sm, true);
	pio_sm_clear_fifos(CAMERA_PIO, sm);
	pio_sm_restart(CAMERA_PIO, sm);
	channel_config_set_dreq(&cd, pio_get_dreq(CAMERA_PIO, sm, false));
	memset(capture_buf, 0, width*height*sizeof(uint8_t));
	dma_channel_configure(dma_chan, &cd,
			capture_buf,        // Destination pointer
			&pio0->rxf[sm],      // Source pointer
			(height*width)/2, // Number of transfers
			true                // Start immediately
			);

	printf ("%s\n", "dump bytes read from d0 to d7*....");
	sleep_ms(1000);
	gpio_put(LED_PIN, 1);
	pio_sm_put_blocking(CAMERA_PIO, sm, width);
	pio_sm_put_blocking(CAMERA_PIO, sm, height);
	sleep_ms(3000);
	gpio_put(LED_PIN, 0);
	sleep_ms(3000);
	int i;
	for(i=0;i<height;i++){

		for (int j=0;j<width;j++) {
			printf("%c", pattern[capture_buf[(i*width)+j] /26] );
			//printf(" byte %d = value: %d\n", i , capture_buf[i]);
		}
		printf("%s\n", "");
	}
	dma_unclaim_mask (dma_chan);


}

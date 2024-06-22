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
	//unclaim all DMA chans uncles code might have claimed....

	dma_unclaim_mask (0x0000000f);

	//clear all of PIOs setup by uncle (and their SMs as a result off)

	pio_clear_instruction_memory (CAMERA_PIO);

	const uint16_t width = CAMERA_WIDTH_DIV8;
	const uint16_t height = CAMERA_HEIGHT_DIV8;

	//Now setup your own PIO!!
	uint sm = 0;
	uint dma_chan = 0;
	uint offset = pio_add_program(CAMERA_PIO, &ov7670_program);
	pio_sm_config c = pio_get_default_sm_config();
	sm_config_set_in_pins(&c, CAMERA_BASE_PIN);
	sm_config_set_wrap(&c, offset, offset);
	//sm_config_set_clkdiv(&c, 1.f);
	camera_pio_init_gpios(CAMERA_PIO, sm, CAMERA_BASE_PIN);
	pio_sm_set_enabled(CAMERA_PIO, sm, false);
	uint8_t *capture_buf = malloc(width*height*sizeof(uint8_t));
	hard_assert(capture_buf);
	// Need to clear _input shift counter_, as well as FIFO, because there may be
	// partial ISR contents left over from a previous run. sm_restart does this.
	pio_sm_clear_fifos(CAMERA_PIO, sm);
	pio_sm_restart(CAMERA_PIO, sm);

	dma_channel_config cd = dma_channel_get_default_config(dma_chan);
	channel_config_set_read_increment(&c, false);
	channel_config_set_write_increment(&c, true);
	channel_config_set_dreq(&c, pio_get_dreq(CAMERA_PIO, sm, false));

	dma_channel_configure(dma_chan, &cd,
			capture_buf,        // Destination pointer
			&pio0->rxf[sm],      // Source pointer
			1, // Number of transfers
			true                // Start immediately
			);
	pio_sm_init(CAMERA_PIO, sm, offset, &c);
	pio_sm_set_enabled(CAMERA_PIO, sm, true);

	uint8_t pattern[] =  " .:!()/|}-=+*#%@";
	while (1) {
		printf ("%s\n", "dump bytes read from d0 to d7....");
		sleep_ms(1000);
		gpio_put(LED_PIN, 1);
		pio_sm_put_blocking(CAMERA_PIO, sm, width);
		pio_sm_put_blocking(CAMERA_PIO, sm, height);
		sleep_ms(3000);
		gpio_put(LED_PIN, 0);
		sleep_ms(3000);
		//dma_channel_wait_for_finish_blocking(0);
		int i;
		for(i=0;i<height;i++){

			for (int j=0;j<width;j++) {
				printf("%c", pattern[capture_buf[(i*width)+j] >> 4] );
				//printf(" byte %d = value: %d\n", i , capture_buf[i]);
			}
			printf("%s\n", "");
		}
		pio_sm_clear_fifos(CAMERA_PIO, sm);
		pio_sm_restart(CAMERA_PIO, sm);

		dma_unclaim_mask (0x0000000f);
		dma_channel_config cd = dma_channel_get_default_config(dma_chan);
		channel_config_set_read_increment(&c, false);
		channel_config_set_write_increment(&c, true);
		channel_config_set_dreq(&c, pio_get_dreq(CAMERA_PIO, sm, false));
	dma_channel_configure(dma_chan, &cd,
			capture_buf,        // Destination pointer
			&pio0->rxf[sm],      // Source pointer
			1, // Number of transfers
			true                // Start immediately
			);
	pio_sm_init(CAMERA_PIO, sm, offset, &c);
	pio_sm_set_enabled(CAMERA_PIO, sm, true);
	free(capture_buf);
	capture_buf = malloc(width*height*sizeof(uint8_t));

	}
}

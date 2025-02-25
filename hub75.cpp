#include <cstring>
#include <algorithm>
#include <cmath>
#include "stdarg.h"
#include "stdio.h"

#include "hub75.hpp"

#include "font_5x7.h"

static inline Pixel makePixel (uint32_t px);
static inline Pixel makePixel (uint8_t r, uint8_t g, uint8_t b);

Hub75::Hub75(uint width, uint height, Pixel *buffer, PanelType panel_type, bool inverted_stb, COLOR_ORDER color_order)
 : width(width), height(height), panel_type(panel_type), inverted_stb(inverted_stb), color_order(color_order)
 {
	// Set up allllll the GPIO
	gpio_init(pin_r0); gpio_set_function(pin_r0, GPIO_FUNC_SIO); gpio_set_dir(pin_r0, true); gpio_put(pin_r0, 0);
	gpio_init(pin_g0); gpio_set_function(pin_g0, GPIO_FUNC_SIO); gpio_set_dir(pin_g0, true); gpio_put(pin_g0, 0);
	gpio_init(pin_b0); gpio_set_function(pin_b0, GPIO_FUNC_SIO); gpio_set_dir(pin_b0, true); gpio_put(pin_b0, 0);

	gpio_init(pin_r1); gpio_set_function(pin_r1, GPIO_FUNC_SIO); gpio_set_dir(pin_r1, true); gpio_put(pin_r1, 0);
	gpio_init(pin_g1); gpio_set_function(pin_g1, GPIO_FUNC_SIO); gpio_set_dir(pin_g1, true); gpio_put(pin_g1, 0);
	gpio_init(pin_b1); gpio_set_function(pin_b1, GPIO_FUNC_SIO); gpio_set_dir(pin_b1, true); gpio_put(pin_b1, 0);

	gpio_init(pin_row_a); gpio_set_function(pin_row_a, GPIO_FUNC_SIO); gpio_set_dir(pin_row_a, true); gpio_put(pin_row_a, 0);
	gpio_init(pin_row_b); gpio_set_function(pin_row_b, GPIO_FUNC_SIO); gpio_set_dir(pin_row_b, true); gpio_put(pin_row_b, 0);
	gpio_init(pin_row_c); gpio_set_function(pin_row_c, GPIO_FUNC_SIO); gpio_set_dir(pin_row_c, true); gpio_put(pin_row_c, 0);
	gpio_init(pin_row_d); gpio_set_function(pin_row_d, GPIO_FUNC_SIO); gpio_set_dir(pin_row_d, true); gpio_put(pin_row_d, 0);
	gpio_init(pin_row_e); gpio_set_function(pin_row_e, GPIO_FUNC_SIO); gpio_set_dir(pin_row_e, true); gpio_put(pin_row_e, 0);

	gpio_init(pin_clk); gpio_set_function(pin_clk, GPIO_FUNC_SIO); gpio_set_dir(pin_clk, true); gpio_put(pin_clk, !clk_polarity);
	gpio_init(pin_stb); gpio_set_function(pin_stb, GPIO_FUNC_SIO); gpio_set_dir(pin_stb, true); gpio_put(pin_clk, !stb_polarity);
	gpio_init(pin_oe); gpio_set_function(pin_oe, GPIO_FUNC_SIO); gpio_set_dir(pin_oe, true); gpio_put(pin_clk, !oe_polarity);

	if (buffer == nullptr) {
		back_buffer = new Pixel[width * height];
		managed_buffer = true;
	} else {
		back_buffer = buffer;
		managed_buffer = false;
	}

	if (brightness == 0) {
		if (width >= 64) brightness = 6;
		if (width >= 96) brightness = 3;
		if (width >= 128) brightness = 2;
		if (width >= 160) brightness = 1;
	}
	
	clear();
}

Hub75::~Hub75() {
	if (managed_buffer) {
		delete[] back_buffer;
	}
}


void Hub75::FM6126A_write_register(uint16_t value, uint8_t position) {
	gpio_put(pin_clk, !clk_polarity);
	gpio_put(pin_stb, !stb_polarity);

	uint8_t threshold = width - position;
	for(auto i = 0u; i < width; i++) {
		auto j = i % 16;
		bool b = value & (1 << j);

		gpio_put(pin_r0, b);
		gpio_put(pin_g0, b);
		gpio_put(pin_b0, b);
		gpio_put(pin_r1, b);
		gpio_put(pin_g1, b);
		gpio_put(pin_b1, b);

		// Assert strobe/latch if i > threshold
		// This somehow indicates to the FM6126A which register we want to write :|
		gpio_put(pin_stb, i > threshold);
		gpio_put(pin_clk, clk_polarity);
		sleep_us(10);
		gpio_put(pin_clk, !clk_polarity);
	}
}

void Hub75::FM6126A_setup() {
	// Ridiculous register write nonsense for the FM6126A-based 64x64 matrix
	FM6126A_write_register(0b1111111111111110, 12);
	FM6126A_write_register(0b0000001000000000, 13);
}

void Hub75::start(irq_handler_t handler) {
	if(handler) {
		if (panel_type == PANEL_FM6126A) {
			FM6126A_setup();
		}

		// Claim the PIO so we can clean it upon soft restart
		pio_sm_claim(pio, sm_data);
		pio_sm_claim(pio, sm_row);

		data_prog_offs = pio_add_program(pio, &hub75_data_rgb888_program);
		if (inverted_stb) {
			row_prog_offs = pio_add_program(pio, &hub75_row_inverted_program);
		} else {
			row_prog_offs = pio_add_program(pio, &hub75_row_program);
		}
		hub75_data_rgb888_program_init(pio, sm_data, data_prog_offs, DATA_BASE_PIN, pin_clk);
		hub75_row_program_init(pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, pin_stb);

		// Prevent flicker in Python caused by the smaller dataset just blasting through the PIO too quickly
		pio_sm_set_clkdiv(pio, sm_data, width <= 32 ? 2.0f : 1.0f);

		dma_channel = dma_claim_unused_channel(true);
		dma_channel_config config = dma_channel_get_default_config(dma_channel);
		channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
		channel_config_set_bswap(&config, false);
		channel_config_set_dreq(&config, pio_get_dreq(pio, sm_data, true));
		dma_channel_configure(dma_channel, &config, &pio->txf[sm_data], NULL, 0, false);

		// Same handler for both DMA channels
		irq_add_shared_handler(DMA_IRQ_0, handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

		dma_channel_set_irq0_enabled(dma_channel, true);

		irq_set_enabled(pio_get_dreq(pio, sm_data, true), true);
		irq_set_enabled(DMA_IRQ_0, true);

		row = 0;
		bit = 0;

		hub75_data_rgb888_set_shift(pio, data_prog_offs, bit);
		dma_channel_set_trans_count(dma_channel, width * 2, false);
		dma_channel_set_read_addr(dma_channel, &back_buffer, true);
	}
}

void Hub75::stop(irq_handler_t handler) {

	irq_set_enabled(DMA_IRQ_0, false);
	irq_set_enabled(pio_get_dreq(pio, sm_data, true), false);

	if(dma_channel != -1 &&  dma_channel_is_claimed(dma_channel)) {
		dma_channel_set_irq0_enabled(dma_channel, false);
		irq_remove_handler(DMA_IRQ_0, handler);
		//dma_channel_wait_for_finish_blocking(dma_channel);
		dma_channel_abort(dma_channel);
		dma_channel_acknowledge_irq0(dma_channel);
		dma_channel_unclaim(dma_channel);
	}

	if(pio_sm_is_claimed(pio, sm_data)) {
		pio_sm_set_enabled(pio, sm_data, false);
		pio_sm_drain_tx_fifo(pio, sm_data);
		pio_remove_program(pio, &hub75_data_rgb888_program, data_prog_offs);
		pio_sm_unclaim(pio, sm_data);
	}

	if(pio_sm_is_claimed(pio, sm_row)) {
		pio_sm_set_enabled(pio, sm_row, false);
		pio_sm_drain_tx_fifo(pio, sm_row);
		if (inverted_stb) {
			pio_remove_program(pio, &hub75_row_inverted_program, row_prog_offs);
		} else {
			pio_remove_program(pio, &hub75_row_program, row_prog_offs);
		}
		pio_sm_unclaim(pio, sm_row);
	}

	// Make sure the GPIO is in a known good state
	// since we don't know what the PIO might have done with it
	gpio_put_masked(0b111111 << pin_r0, 0);
	gpio_put_masked(0b11111 << pin_row_a, 0);
	gpio_put(pin_clk, !clk_polarity);
	gpio_put(pin_clk, !oe_polarity);
}

void Hub75::dma_complete() {
	if(dma_channel_get_irq0_status(dma_channel)) {
		dma_channel_acknowledge_irq0(dma_channel);

		// Push out a dummy pixel for each row
		pio_sm_put_blocking(pio, sm_data, 0);
		pio_sm_put_blocking(pio, sm_data, 0);

		// SM is finished when it stalls on empty TX FIFO
		hub75_wait_tx_stall(pio, sm_data);

		// Check that previous OEn pulse is finished, else things WILL get out of sequence
		hub75_wait_tx_stall(pio, sm_row);

		// Latch row data, pulse output enable for new row.
		pio_sm_put_blocking(pio, sm_row, row | (brightness << 5 << bit));

		row++;

		if(row == height / 2) {
			row = 0;
			bit++;
			if (bit == BIT_DEPTH) {
				bit = 0;
			}
			hub75_data_rgb888_set_shift(pio, data_prog_offs, bit);
		}

		dma_channel_set_trans_count(dma_channel, width * 2, false);
		dma_channel_set_read_addr(dma_channel, &back_buffer[row * width * 2], true);
	}
}

void Hub75::clear() {
	#if 1
		memset (back_buffer, 0, width * height * sizeof(*back_buffer));
	#else
		for(auto x = 0u; x < width; x++) {
			for(auto y = 0u; y < height; y++) {
				set_pixel(x, y, 0, 0, 0);
			}
		}
	#endif
}

void Hub75::set_color(uint x, uint y, Pixel c) {
	int offset = 0;
	if (x >= width || y >= height) return;
	// flip x
	//x = width - 1 - x;
	// flip y
	//y = height - 1 - y;
	if (y >= height / 2) {
		y -= height / 2;
		offset = 1;
	}
	offset += (y * width + x) * 2;
	back_buffer[offset] = c;
}

void Hub75::set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) {
	switch(color_order) {
		case COLOR_ORDER::RGB:
			set_color(x, y, makePixel(r, g, b));
			break;
		case COLOR_ORDER::RBG:
			set_color(x, y, makePixel(r, b, g));
			break;
		case COLOR_ORDER::GRB:
			set_color(x, y, makePixel(g, r, b));
			break;
		case COLOR_ORDER::GBR:
			set_color(x, y, makePixel(g, b, r));
			break;
		case COLOR_ORDER::BRG:
			set_color(x, y, makePixel(b, r, g));
			break;
		case COLOR_ORDER::BGR:
			set_color(x, y, makePixel(b, g, r));
			break;
	}
}

void Hub75::show_5x7_char (uint x, uint y, unsigned char c, Pixel fg, Pixel bg) {
	for (int col = 4; col >= 0; --col) {
		uint x2 = x + col;
		if (x2 >= (width - 5)) continue;
		uint8_t mask = 0b01000000;
		for (int row = 6; row >= 0; --row) {
			uint8_t y2 = y + row;
			if (y2 >= (height - 7)) continue;
			bool hasBit = (font_5x7[c][col] & mask) != 0;
			set_color (x + col, y + row, hasBit ? fg : bg);
			mask >>= 1;
		}
	}
}

void Hub75::show_5x7_string (uint x, uint y, const char *s, int len, Pixel fg, Pixel bg) {
	for (int i = 0; i < len; ++i) {
		show_5x7_char (x, y, s[i], fg, bg);
		x += 6;
	}
}

void Hub75::show_5x7_string (uint x, uint y, const char *s, Pixel fg, Pixel bg) {
	show_5x7_string (x, y, s, strlen(s), fg, bg);
}

void Hub75::show_5x7_string (uint x, uint y, const char *format, ...) {
	char msg[256];
	va_list args;
	va_start (args, format);
	vsnprintf (msg, sizeof(msg), format, args);
	va_end (args);
	show_5x7_string (x, y, msg, makePixel(100,100,100), black);
}

void Hub75::updateFromRGB888(void *graphics, bool bigEndian) {
	uint32_t *p = (uint32_t *)graphics;
	for (uint y = 0; y < height; y++) {
		for (uint x = 0; x < width; x++) {
			uint32_t col = *p;
			if (bigEndian) col = __builtin_bswap32(col);
			uint8_t r = (col & 0xff0000) >> 16;
			uint8_t g = (col & 0x00ff00) >>  8;
			uint8_t b = (col & 0x0000ff) >>  0;
			set_pixel(x, y, r, g, b);
			p++;
		}
	}
}

void Hub75::updateFromRGB565(void *graphics, bool bigEndian) {
	uint16_t *p = (uint16_t *)graphics;
	for(uint y = 0; y < height; y++) {
		for(uint x = 0; x < width; x++) {
			uint16_t col = *p;
			if (bigEndian) col = __builtin_bswap16(col);
			uint8_t r = (col & 0b1111100000000000) >> 8;
			uint8_t g = (col & 0b0000011111100000) >> 3;
			uint8_t b = (col & 0b0000000000011111) << 3;
			set_pixel(x, y, r, g, b);
			p++;
		}
	}
}

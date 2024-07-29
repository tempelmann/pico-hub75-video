#include <malloc.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include "elapsed.h"

#include "config.h"
#include "secret-credentials.h"	// create this yourself - should define your WIFI_SSID and WIFI_PASSWORD

#include "persistent_storage.h"

#include "rgbled.hpp"
#include "button.hpp"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "mqtt.h"
#include "hub75.hpp"

#define use_watchdog 1 // auto-reboots if stuck
#define WATCHDOG_TIMEOUT_MS  3000 // max is ~4700
#define BLINK_PERIOD_MS 1000

static struct {
	int boardID;
} persistent_info;

static Button buttonA(Interstate75::BUT_A);

static RGBLED board_led(Interstate75::LED_R, Interstate75::LED_G, Interstate75::LED_B, ACTIVE_LOW, 80);

static Hub75 panel(WIDTH, HEIGHT, nullptr, PANEL_GENERIC, false);

// Interrupt callback required function 
void __isr dma_complete() {
	panel.dma_complete();
}

static void blink(int n, int timeOn, int timeOff) {
	for (int i = 0; n <= 0 || i < n; i++) {
		if (i) sleep_ms(timeOff);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
		sleep_ms(timeOn);
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
	}
}

static int reportState(int n) {
	for (int i = 0; i < 3; i++) {
		sleep_ms(1000);
		blink (n, 159, 150);
	}
	return n;
}

static inline uint32_t unpack_565_888(uint16_t pix) {
	uint32_t r_gamma = (pix & 0xf800u) >> 8;
	uint32_t g_gamma = (pix & 0x07e0u) >> 3;
	uint32_t b_gamma = (pix & 0x001fu) << 3;
	return (b_gamma << 16) | (g_gamma << 8) | (r_gamma << 0);
}

uint32_t getTotalHeap(void) {
   extern char __StackLimit, __bss_end__;
   return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap(void) {
   struct mallinfo m = mallinfo();
   return getTotalHeap() - m.uordblks;
}

static void postToTopic (const char *topic, const char *format, va_list args) {
	char msg[256];
	vsnprintf (msg, sizeof(msg), format, args);
	if (!mqtt_post (topic, msg)) {
		printf("ERROR: mtpp_post failed\n");
	}
}

static void postMsg (const char *format, ...) {
	va_list args;
	va_start(args, format);
	postToTopic ("re/info", format, args);
	va_end(args);
}

static void postError (const char *format, ...) {
	va_list args;
	va_start(args, format);
	postToTopic ("re/error", format, args);
	va_end(args);
}

static int callCounter = 0;

static char imgBuf[WIDTH*HEIGHT*4];
static unsigned long bufOfs = 0;

static Elapsed singleFrame_timer;
static Elapsed second_timer;
static int second_frames = 0;

extern "C"
void process_data (const char *topic, const u8_t *data, u16_t len, bool lastPart) {
	char cmd[64];
	if (len < sizeof(cmd)) {
		strncpy (cmd, (const char *)data, len);
		cmd[len] = 0;
	} else {
		cmd[0] = 0;
	}
	if (strcmp(topic, "c") == 0) {
		if (strcmp(cmd, "mem") == 0) {
			postMsg("Free: %lu", getFreeHeap());
		}
	} if (strcmp(topic, "b") == 0) {	// set brightness
		int v = 0;
		sscanf (cmd, "%d", &v);
		if (v >= 1 && v <= 6) {
			panel.brightness = v;
		} else {
			postError ("brightness value outside 1-6: %d", v);
		}
	} if (strcmp(topic, "t") == 0) {	// show text
		panel.show_5x7_string (1, 10, (const char*)cmd);
	} else if (topic[0] == 'i') {	// i16 or i32
		if ((bufOfs + len) > sizeof(imgBuf)) {
    		board_led.set_rgb(80,0,50);
			printf("OVERFLOW\n");
			return;
		}
		if (bufOfs == 0) {
			singleFrame_timer.reset();
		}
		memcpy ((char*)imgBuf + bufOfs, data, len);
		bufOfs += len;
		if (lastPart) {
			second_frames++;
			bufOfs = 0;
			if (strcmp(topic, "i16") == 0) {
				panel.updateFromRGB565 (imgBuf, true);
			} else {
				panel.updateFromRGB888 (imgBuf, true);
			}
			//printf("took %ld ms\n", singleFrame_timer.elapsed_millis());
			long millis = second_timer.elapsed_millis();
			if (millis > 1000) {
				double framesPerSecond = (double)second_frames / (millis / 1000.0);
				printf("%.1f f/s\n", framesPerSecond);
				second_timer.reset();
				second_frames = 0;
			}
		}
	} else {
		printf("Unexpected topic: %s\n", topic);
	}
	if (use_watchdog) {
		watchdog_update();
	}
}

static void fatalError() {
	board_led.set_rgb(100,0,0);
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
	while(true);	// leads to a reset thanks to watchdog
}

int main() {
	stdio_init_all();
	// Blue
    board_led.set_rgb(0,0,100);

	panel.start(dma_complete);

	// draw frame around the entire screen area
	for (int y = 0; y < HEIGHT; ++y) {
		panel.set_pixel (0, y, 0, 0, 100);
		panel.set_pixel (WIDTH-1, y, 0, 0, 100);
	}
	for (int x = 0; x < WIDTH; ++x) {
		panel.set_pixel (x, 0, 0, 0, 100);
		panel.set_pixel (x, HEIGHT-1, 0, 0, 100);
	}

	panel.show_5x7_string (1, 1, watchdog_enable_caused_reboot() ? "Restart" : "Starting");

	while (cyw43_arch_init_with_country(WIFI_COUNTRY) != 0) {
		printf("ERROR: WiFi failed to initialise - will retry\n");
		busy_wait_ms(1000);
	}
	cyw43_arch_enable_sta_mode();
	busy_wait_ms(1000);
	while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, WIFI_TIMEOUT_MS) != 0) {
		printf("Wifi failed to connect - retrying\n");
		busy_wait_ms(1000);
	}
	printf("Connected to Wifi\n");

	// Yellow
    board_led.set_rgb(100,100,0);
	
	printf("Memory: total %ld, free %ld\n", getTotalHeap(), getFreeHeap());

	int ps = cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1));
	if (ps) printf("ERROR: cyw43_wifi_pm returned: %d\n", ps);
	
	if (use_watchdog) {
		watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
	}

	persistent_read (&persistent_info, sizeof(persistent_info));

	if (!mqtt_setup_client()) {
		fatalError();
	}
	char clientID[32];
	snprintf (clientID, sizeof(clientID), "Panel %d", persistent_info.boardID);
	Elapsed waitForMQTTconnect;
	mqtt_connect(clientID);
	while (!mqtt_ready()) {
		if (use_watchdog) {
			watchdog_update();
		}
		if (waitForMQTTconnect.elapsed_millis() > 3000) {
			waitForMQTTconnect.reset();
			mqtt_connect(clientID);
		}
	}
	printf("Connected to MQTT broker\n");
	
	// subscribe to our own ID
	mqtt_subscribeID (persistent_info.boardID);
	panel.show_5x7_string (1, 1, "Ready %d ", persistent_info.boardID);

	// Green
    board_led.set_rgb(0,100,0);
	
	Elapsed blinkTime;
	bool led_toggle = false;
	while (1) {
		busy_wait_ms(50);
		
		if (blinkTime.elapsed_millis() > BLINK_PERIOD_MS) {
			blinkTime.reset();
			if (use_watchdog) {
				watchdog_update();
			}
			led_toggle = !led_toggle;
			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (int)led_toggle);
		}
		
		if (buttonA.read()) {
			persistent_info.boardID += 1;
			if (persistent_info.boardID >= 4) persistent_info.boardID = 0;
			if (persistent_write (&persistent_info, sizeof(persistent_info))) {
				if (mqtt_subscribeID (persistent_info.boardID)) {
					panel.show_5x7_string (1, 1, "New ID %d  ", persistent_info.boardID);
				} else {
					panel.show_5x7_string (1, 1, "Subsc Err ");
				}
			} else {
				panel.show_5x7_string (1, 1, "Flash Err ");
			}
		}

		if (!mqtt_ready()) {
			printf("Lost MQTT connection\n");
			fatalError();
		}
	}
}

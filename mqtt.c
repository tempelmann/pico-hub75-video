/**
 * Based on:
 * SPDX-FileCopyrightText: 2023 Stephen Merrony
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mqtt.h"

#include "string.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"


static mqtt_client_t *client;
static ip_addr_t broker_addr;
static bool subscribedToMQTT = false;

bool mqtt_setup_client() {
	client = mqtt_client_new();
	if (client == NULL) {
		printf("ERROR: Could not allocate MQTT client\n");
		return false;
	}
	return true;
}

static char topic_id[20];

static void mqtt_incoming_publish_cb( __attribute__((unused)) void *arg, 
									  const char *topic, 
									  __attribute__((unused)) u32_t tot_len) {
	strlcpy (topic_id, strchr(topic, '/') + 1, sizeof(topic_id));
}

static void mqtt_incoming_data_cb(__attribute__((unused)) void *arg, const u8_t *data, u16_t len, u8_t flags) {
	// See MQTT_VAR_HEADER_BUFFER_LEN for defining the mqtt buffer size 
	process_data (topic_id, data, len, (flags & MQTT_DATA_FLAG_LAST) != 0);
}

static void mqtt_sub_request_cb(__attribute__((unused)) void *arg, err_t result) {
	//printf("DEBUG: Subscribe result: %d\n", result);
	subscribedToMQTT = (result == 0);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
	err_t err;
	if (status == MQTT_CONNECT_ACCEPTED) {
		mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, NULL);
		err = mqtt_subscribe(client, TOPIC_ALL, 0, mqtt_sub_request_cb, NULL);
		if (err != ERR_OK) {
			printf("ERROR: mqtt_subscribe return: %d\n", err);
		}
	} else {
		printf("ERROR: MQTT connection CB got code %d\n", status);
	}
}

bool mqtt_connect(const char *id) {
	struct mqtt_connect_client_info_t ci;
	err_t err;
  
	/* Setup an empty client info structure */
	memset(&ci, 0, sizeof(ci));

	ci.client_id = id;
	ci.keep_alive = BROKER_KEEPALIVE;

	if (!ip4addr_aton(BROKER_HOST, &broker_addr)) {
		printf("ERROR: Could not resolve MQTT Broker address\n");
		return false;
	}
	cyw43_arch_lwip_begin(); /* start section for to lwIP access */
	err = mqtt_client_connect(client, &broker_addr, BROKER_PORT, mqtt_connection_cb, NULL, &ci);
	cyw43_arch_lwip_end(); /* end section accessing lwIP */

	return err == ERR_OK;
}

bool mqtt_connected() {
	return mqtt_client_is_connected(client) != 0;
}

bool mqtt_ready() {
	return mqtt_connected() && subscribedToMQTT;
}

static void mqtt_pub_request_cb(void *arg, err_t err) {
	if (err) {
    	printf("ERROR: mqtt_pub_request_cb: err %d\n", err);
    }
}

bool mqtt_post(const char *topic, const char *msg) {
	u8_t qos = 0; /* 0 1 or 2, see MQTT specification. AWS IoT does not support QoS 2 */
	u8_t retain = 0;
	cyw43_arch_lwip_begin();
	err_t  err = mqtt_publish(client, topic, msg, strlen(msg), qos, retain, mqtt_pub_request_cb, NULL);
	cyw43_arch_lwip_end();
	return err == ERR_OK;
}

bool mqtt_subscribeID (int id) {
	static int prevID = -1;
	char topic[32];
	if (prevID >= 0) {
		snprintf (topic, sizeof(topic), TOPIC_BRD, prevID);
		mqtt_unsubscribe (client, topic, mqtt_sub_request_cb, NULL);
		prevID = -1;
	}
	snprintf (topic, sizeof(topic), TOPIC_BRD, id);
	err_t err = mqtt_subscribe(client, topic, 0, mqtt_sub_request_cb, NULL);
	if (err != ERR_OK) {
		printf("ERROR: mqtt_subscribe return: %d\n", err);
		return false;
	}
	prevID = id;
	return true;
}

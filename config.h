#ifndef CONFIG_H
#define CONFIG_H

#define WIDTH  128
#define HEIGHT 64

#define BROKER_HOST "192.168.4.8"
#define BROKER_PORT 1883
#define BROKER_KEEPALIVE 60
#define TOPIC_ALL  "all/#"         // <== This is what we subscribe to for msgs to all boards
#define TOPIC_BRD  "id%d/#"         // <== This is what we subscribe to for each individual board ID

#define WIFI_COUNTRY CYW43_COUNTRY_GERMANY
#define WIFI_TIMEOUT_MS 10000

#endif

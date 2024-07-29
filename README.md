# pico-hub75-video

Drives a set of Hub75 panels on a PicoW or Pimoroni Interstate75W, receiving image frames for animated display

It currently has one problem: If it receives too many frames per second (like 10, which it can easily handle), it will
eventually lose the connection to the MQTT broker. The cause has yet to be found.

Discussion: https://forums.raspberrypi.com/viewtopic.php?p=2238159

cmake_minimum_required(VERSION 3.12)

# Name of executable.
set(NAME pico)

# imports
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# C/C++ boilerplate.
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_BUILD_TYPE Release)

# Initialize the SDK
pico_sdk_init()

# Add all source files
add_executable(${NAME}
	main.cpp
	hub75.cpp
	graphics.c
	mqtt.c
	rgbled.cpp
	button.cpp
	persistent_storage.c
)

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

#include(libraries/pico_graphics/pico_graphics)

# Libraries to link
target_link_libraries(${NAME}
	pico_stdlib
	pico_cyw43_arch_lwip_threadsafe_background
	pico_lwip_mqtt
	hardware_pwm
	hardware_pio
	hardware_dma
	hardware_flash
	hardware_sync
#	pico_multicore
)

target_include_directories(${NAME} PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(${NAME} PRIVATE
	PICO_ENTER_USB_BOOT_ON_EXIT=1
	MQTT_VAR_HEADER_BUFFER_LEN=1500	# for MQTT to receive large payloads (more than TCP_MSS is ignored)
)

# enable usb output
# (also for making reset via picotool work, see https://github.com/raspberrypi/picotool/issues/88#issuecomment-1885758321)
pico_enable_stdio_usb(${NAME} 1)

# Create map/bin/hex/uf2 files 
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)

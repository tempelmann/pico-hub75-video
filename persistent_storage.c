//
//  persistent_storage.c
//  main
//
//  Created by Thomas Tempelmann on 29.07.24.
//
//	See https://kevinboone.me/picoflash.html
//

#include "persistent_storage.h"

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

// FLASH_SECTOR_SIZE is 4096, whereas FLASH_PAGE_SIZE is 256

static const size_t page_offset = 0x200000 - 11 * FLASH_SECTOR_SIZE;	// use the 11th-to-last flash page (which I picked randomly)

static const uint32_t magic_code = 0x3e74743c;	//'<tt>';

typedef struct {
	uint32_t magic;
	uint32_t content_size;
	uint32_t reserved;	// must be 0
} header_t;

size_t persistent_read (void *dest, size_t len) {
	static bool first = true;
	if (first) {
		first = false;
		//printf("flash sizeof int %d, page %d, sector %d\n", (int)sizeof(int), FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE);
	}
	header_t *h = (header_t *) ((char*)XIP_BASE + page_offset);
	size_t n = 0;
	if (h->magic == magic_code && h->reserved == 0) {
		n = h->content_size;
		if (n > (FLASH_PAGE_SIZE - sizeof(header_t)) || n > len) {
			printf("persistent_read: overflow\n");
			n = 0;
		}
	} else {
		printf("persistent_read: no stored data\n");
	}
	if (n > 0) {
		memcpy (dest, (char*)h + sizeof(header_t), n);
		size_t rem = len - n;
		if (rem > 0) {
			memset ((char*)dest + n, 0, rem);
		}
	}
	//printf("persistent_read: got %d bytes\n", n);
	return n;
}

bool persistent_write (void *data, size_t len) {
	static char buffer[FLASH_PAGE_SIZE];
	if (len > (FLASH_PAGE_SIZE - sizeof(header_t))) {
		printf("persistent_write: len too high\n");
		return false;
	}
	memset (buffer, 0, sizeof(buffer));
	memcpy (buffer + sizeof(header_t), data, len);
	header_t *h = (header_t*)buffer;
	h->magic = magic_code;
	h->content_size = len;
	h->reserved = 0;
	
	for (int i = 0; i < 1; ++i) {
		uint32_t ints = save_and_disable_interrupts();
		//multicore_lockout_start_blocking();	// if we use multicore, the other core must call multicore_lockout_victim_init() once at start!
		flash_range_erase (page_offset, FLASH_SECTOR_SIZE);
		flash_range_program (page_offset, buffer, FLASH_PAGE_SIZE);
		//multicore_lockout_end_blocking();
		restore_interrupts (ints);
		if (memcmp (buffer, (char *)XIP_BASE + page_offset, sizeof(buffer)) == 0) {
			return true;
		}
		printf("persistent_write: verify error (%d)\n", i);
	}
	return false;
}

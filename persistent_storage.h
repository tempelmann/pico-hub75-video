//
//  persistent_storage.h
//  main
//
//  Created by Thomas Tempelmann on 29.07.24.
//  Copyright © 2024 Thomas Tempelmann. All rights reserved.
//

#pragma once

#include "pico/time.h"

#ifdef __cplusplus
extern "C" {
#endif


size_t persistent_read (void *dest, size_t len);	// returns amount of actually stored bytes
bool persistent_write (void *data, size_t len);


#ifdef __cplusplus
} // extern "C"
#endif

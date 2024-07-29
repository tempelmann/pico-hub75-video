//
//  elapsed.h
//
// https://github.com/zapta/simple_stepper_motor_analyzer/blob/main/platformio/src/misc/elapsed.h
//

#pragma once

#include <stdint.h>
#include "pico/stdlib.h"

class Elapsed {
 public:
  Elapsed() { reset(); }

  void reset() { start_millis_ = millis_now(); }

  uint32_t elapsed_millis() { return millis_now() - start_millis_; }

  void advance(uint32_t interval_millis) { start_millis_ += interval_millis; }

  void set(uint32_t elapsed_millis) {
    start_millis_ = millis_now() - elapsed_millis;
  }

 private:
 inline uint32_t millis_now() {
   return to_ms_since_boot(get_absolute_time());
 }
  uint32_t start_millis_;
};

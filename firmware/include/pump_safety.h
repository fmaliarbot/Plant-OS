#pragma once
// Project safety rule: pumps always boot OFF.
// Milestones 1 and 2 do not use the pumps, but if they are already wired
// this guarantees both pins are held LOW from the very first instruction.

#include <Arduino.h>
#include "pins.h"

inline void pumps_force_off() {
  digitalWrite(pins::PUMP_BASIL, LOW);      // set the level before enabling the output
  digitalWrite(pins::PUMP_ROSEMARY, LOW);
  pinMode(pins::PUMP_BASIL, OUTPUT);
  pinMode(pins::PUMP_ROSEMARY, OUTPUT);
  digitalWrite(pins::PUMP_BASIL, LOW);
  digitalWrite(pins::PUMP_ROSEMARY, LOW);
}

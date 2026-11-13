#pragma once

/**
 * Receive telemetry from SimHub ("Custom Serial device") over the USB port and write it to
 * vehicle_state. Line protocol, one line per update, any key order, missing keys keep their last value:
 *
 *     R:5200,S:143,G:4,F:62,M:8000,L:6500\n
 *
 *   R = rpm, S = speed km/h, G = gear ("R", "N" or 1..9), F = fuel percent 0..100,
 *   M = rev scale max (rpm, 3000..12000), L = redline start (rpm). M and L are optional; 0 = default.
 *
 * vehicle_state.connected is true while valid lines keep arriving; after SIMHUB_TIMEOUT_MS without one
 * it goes false and rpm/speed/gear are zeroed (the UI then shows NOT CONNECTED).
 */
void simhub_start(void);

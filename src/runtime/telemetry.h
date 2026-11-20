// Telemetry for SimHub, motion rigs and dashboards (GitHub issue 111): every
// game frame the player's car (speed, RPM, gear, orientation, G-forces...) is
// sent by UDP to this PC in the layout of the Codemasters "extradata = 3"
// packet (DiRT Rally, 264 bytes, 66 little-endian floats), which those
// programs already read. Off by default; save/settings.ini:
//   telemetry = 1
//   telemetry_port = 20777
// Only while a race is being driven: the attract, the menus and the replays
// send a car standing still, so a motion rig does not follow the demo.
#pragma once

// Game thread, every vblank (runtime.cpp, after stats_frame)
void telemetry_frame(bool on, int port);

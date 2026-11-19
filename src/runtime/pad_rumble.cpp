// The pads' rumble (pad_rumble.h), once a game frame from runtime.cpp.
//
// In a race it comes from what the player's car does, read from the game's
// RAM (docs/game_ram.md; the same car object as the telemetry):
//   [0x2242B0]  the player's car object
//      +0x00 float x3  position, metres (y up)
//      +0x0C float x3  velocity, metres per game frame (y stays 0)
//      +0x18 s16 x3    pitch, yaw (minus the heading), roll; 65536 = 360 deg
//      +0x64 float x4  wheel speeds, metres per game frame
//   0x22F3A8  float    speed, m/s
//
// Four parts:
//   rough   the body shaking on a rough surface (the bridges, the bumps): the
//           frame-to-frame jolts of its height, pitch and roll
//   slide   the car sliding sideways: the angle between where it points and
//           where it goes, at speed
//   hit     a crash or a hard landing: a jump in the acceleration far above
//           what driving does, then fading
//   spin    the wheels spinning faster than the car goes (pulling away)
// The arcade wheel's own vibration (the drive board's, `arcade`) is kept as a
// small part only: it is what made the pads busy on straights and quiet in
// slides, since that wheel goes light when the car slides.
//
// The triggers: the brake trembles while it slows the car, the accelerator
// while the wheels spin, and both carry the rough surface, the slides and the
// hits whatever the pedals do (more with the pedal pressed). Outside a race
// (the selects, replays, the attract) the triggers are still; the pad itself
// keeps the drive board's jolts of the select screens, as the arcade wheel.
//
// SR2_RUMBLE_LOG prints the parts every frame they are not all zero.
#include "pad_rumble.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "inputs.h"
#include "ppc_rt.h"

std::atomic<float> g_pad_motor[2] = {0.0f, 0.0f};

namespace {

constexpr uint32_t SEQUENCE = 0x22D618;      // 1 from the credit to GAME OVER
constexpr uint32_t PHASE = 0x22D624;         // in a game: 1 selects and races
constexpr uint32_t CAR_STATUS = 0x22CF14 + 0x6C;   // 1 GO: racing
constexpr uint32_t PLAYER_CAR = 0x2242B0;
constexpr uint32_t SPEED = 0x22F3A8;
constexpr float HZ = 60.0f;                  // the game's frames a second
constexpr float G = 9.81f;
constexpr float ANGLE = 6.2831853f / 65536.0f;

float rdf(uint32_t a) {
    uint32_t u = rd32(a);
    float f;
    memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}

void bump(std::atomic<float>& a, float v) {
    if (v > a.load()) a = v;
}

struct State {
    bool have = false;      // last frame read (two frames for the jolts)
    int frames = 0;
    float y = 0, vy = 0;
    float vx = 0, vz = 0;
    int16_t ang[3] = {};
    int16_t dang[3] = {};
    float rough = 0, hit = 0;
};
State s;

}  // namespace

void pad_rumble_frame(float arcade, bool attract) {
    static const bool log = getenv("SR2_RUMBLE_LOG") != nullptr;
    const uint32_t car = rd32(PLAYER_CAR);
    const bool racing = !attract && rd32(SEQUENCE) == 1 && rd32(PHASE) == 1 && car != 0 && car < 0x800000 &&
                        int32_t(rd32(CAR_STATUS)) == 1;
    if (!racing) {
        s = State{};
        bump(g_pad_motor[0], arcade);   // the select screens' jolts, as before
        bump(g_pad_motor[1], 0.6f * arcade);
        return;
    }

    float speed = rdf(SPEED);
    if (speed < 0 || speed > 150) speed = 0;
    const float y = rdf(car + 0x04);
    const float vx = rdf(car + 0x0C) * HZ, vz = rdf(car + 0x14) * HZ;
    int16_t ang[3];
    for (int i = 0; i < 3; i++) ang[i] = int16_t(rd16(car + 0x18 + 2 * i));

    float rough_now = 0, hit_now = 0;
    if (s.have) {
        const float vy = (y - s.y) * HZ;
        int16_t dang[3];
        for (int i = 0; i < 3; i++) dang[i] = int16_t(ang[i] - s.ang[i]);
        if (s.frames >= 2) {
            // Jolts: the change of the vertical speed (m/s per frame) and of the
            // pitch and roll rates (degrees per frame, per frame)
            const float jolt_y = std::fabs(vy - s.vy);
            const float jolt_p = std::fabs(float(int16_t(dang[0] - s.dang[0]))) * ANGLE * 57.3f;
            const float jolt_r = std::fabs(float(int16_t(dang[2] - s.dang[2]))) * ANGLE * 57.3f;
            rough_now = std::min(1.0f, jolt_y / 0.6f + (jolt_p + jolt_r) / 0.8f);
            // A hit: the horizontal acceleration well past what grip gives
            // (about 1.5 G) or a landing (the vertical speed stopping hard)
            const float ax = (vx - s.vx) * HZ, az = (vz - s.vz) * HZ;
            const float a = std::sqrt(ax * ax + az * az) / G;
            const float land = std::max(0.0f, vy - s.vy) * HZ / G;   // pushed up
            hit_now = std::clamp(std::max((a - 2.0f) / 4.0f, (land - 3.0f) / 6.0f), 0.0f, 1.0f);
        }
        s.vy = vy;
        memcpy(s.dang, dang, sizeof dang);
    }
    // Rough rises at once and lingers a few frames, so single jolts are felt
    s.rough = std::max(rough_now, s.rough * 0.75f);
    s.hit = std::max(hit_now, s.hit * 0.88f);

    // Slide: where the car points against where it goes
    float slide = 0;
    if (speed > 6.0f) {
        const float heading = -float(ang[1]) * ANGLE;
        const float fx = std::sin(heading), fz = std::cos(heading);
        const float v = std::sqrt(vx * vx + vz * vz);
        if (v > 1.0f) {
            const float side = std::fabs(fx * vz - fz * vx) / v;   // sin of the slip angle
            slide = std::clamp((side - 0.09f) / 0.35f, 0.0f, 1.0f) * std::min(1.0f, speed / 20.0f);
        }
    }

    // Wheelspin: the fastest wheel against the car's speed
    float wheel = 0;
    for (int w = 0; w < 4; w++) wheel = std::max(wheel, std::fabs(rdf(car + 0x64 + 4 * w)) * HZ);
    // (only pulling away with the throttle down: in the air the wheels spin free)
    const float gas_now = std::clamp(float(g_inputs.accel) / 255.0f, 0.0f, 1.0f);
    const float spin = gas_now > 0.5f && speed < 25.0f ? std::clamp((wheel - speed - 2.0f) / 10.0f, 0.0f, 1.0f) : 0.0f;

    // Braking: slowing down, m/s per second
    static float decel = 0;
    static float prev_speed = 0;
    if (s.frames == 0) prev_speed = speed;
    decel += 0.3f * (std::clamp((prev_speed - speed) * HZ, -40.0f, 40.0f) - decel);
    prev_speed = speed;

    s.y = y, s.vx = vx, s.vz = vz;
    memcpy(s.ang, ang, sizeof ang);
    s.have = true;
    s.frames++;

    const float rough = s.rough * std::min(1.0f, 0.3f + speed / 15.0f);
    const float hit = s.hit;
    const float road = 0.35f * arcade;

    // The pad: the low motor carries the heavy things (hits, rough ground,
    // slides), the high one the fine buzz
    const float low = std::max({hit, 0.75f * rough, 0.5f * slide, road, 0.35f * spin});
    const float high = std::max({0.8f * hit, 0.55f * rough, 0.25f * slide, 0.6f * road, 0.25f * spin});
    bump(g_pad_motor[0], low);
    bump(g_pad_motor[1], high);

    // The triggers
    const float gas = std::clamp(float(g_inputs.accel) / 255.0f, 0.0f, 1.0f);
    const float brake = std::clamp(float(g_inputs.brake) / 255.0f, 0.0f, 1.0f);
    float left = 0, right = 0;
    if (brake > 0.05f && speed > 3.0f)
        left = brake * std::min(1.0f, speed / 25.0f) * (0.2f + 0.8f * std::clamp(decel / 15.0f, 0.0f, 1.0f));
    right = 0.6f * spin * std::max(gas, 0.3f);
    const float shared = std::max({0.7f * rough, 0.45f * slide, hit});
    left = std::max(left, shared * (0.45f + 0.55f * brake));
    right = std::max(right, shared * (0.45f + 0.55f * gas));
    bump(g_trigger_rumble[0], left);
    bump(g_trigger_rumble[1], right);

    if (log && (low > 0.02f || left > 0.02f || right > 0.02f))
        printf("[rumble] speed %5.1f  rough %.2f  slide %.2f  hit %.2f  spin %.2f  arcade %.2f  -> pad %.2f/%.2f  triggers %.2f/%.2f\n",
               speed, rough, slide, hit, spin, arcade, low, high, left, right);
}

#include "wheel.h"

#include "gamepad.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

namespace {

constexpr const char* INI_FILE = "save/wheel.ini";
// Buttons read from the wheel: Moza and Fanatec bases go past 64 (a Moza HGP
// shifter on the base gives its gears as buttons 112-119, issue #62)
constexpr int MAX_BUTTONS = 128;
constexpr int NO_REST = 100000;            // a pedal's resting value not given in wheel.ini
constexpr int REST_END = 30000;            // a pedal's rest learnt late must be near one end of its axis

struct Config {
    std::string device;   // part of the wheel's name (any case) or VID:PID; "" = choose by itself
    // Pedals, an H shifter or a hand brake that are USB devices of their own
    // (issue #73): part of the name or VID:PID; "" = on the wheel
    std::string pedals_device, shifter_device, handbrake_device;
    int steer_axis = 0;
    float steer_range = 1.0f;     // fraction of the axis' travel (each side) that gives the full lock
    bool steer_invert = false;
    float steer_dead = 0.0f;
    bool combined_pedals = false;   // one axis: accelerator one way, brake the other
    int gas_axis = 1, brake_axis = 2;
    bool combined_gas_negative = true;
    int gas_rest = NO_REST, brake_rest = NO_REST;   // set by hand in wheel.ini; else learnt
    int shift_up = 4, shift_down = 5, view = 3, handbrake = 1, coin = 8, start = 9;
    int gear[4] = {-1, -1, -1, -1};   // H shifter buttons for gears 1..4
    int handbrake_axis = -1;          // an analog hand brake: on when pulled past half its travel
    int menu_accept = 0, menu_back = 2;   // in the menus (with the hat, the paddles and start)
    float ffb_strength = 0.7f;    // the game's motor force
    float ffb_spring = 0.25f;     // centring spring (0 = none)
    bool ffb_invert = false;
};

const char* INI_TEXT =
    "; Sega Rally 2 Native: steering wheel settings (written on first use, edit freely).\n"
    "; Numbers of axes and buttons are SDL's, counted from 0. Run the game with\n"
    "; SR2_WHEEL_LOG=1 (set SR2_WHEEL_LOG=1 in a command prompt, then SegaRally2.exe)\n"
    "; to see which axis or button moves as you use the wheel. -1 = not used.\n"
    "\n"
    "; The wheel: part of its name as the log shows it ([joystick] lines), for when\n"
    "; a shifter, pedals or a button box is taken instead. Empty: the game picks a\n"
    "; device SDL knows as a wheel, then one with force feedback, then the first.\n"
    "device = %s\n"
    "\n"
    "; Pedals, an H shifter or a hand brake that are separate USB devices: part of\n"
    "; their name, or their VID:PID (for example 16D0:0F53), as the [joystick] lines\n"
    "; show it. The axes and buttons below for them (gas_axis, brake_axis, gear1-4,\n"
    "; handbrake, handbrake_axis) are then that device's. Empty: on the wheel.\n"
    "pedals_device = %s\n"
    "shifter_device = %s\n"
    "handbrake_device = %s\n"
    "\n"
    "; Steering: the axis, and the part of its travel (each side) that gives the full lock.\n"
    "; The arcade wheel turns about 270 degrees: with your wheel set to 900 degrees in its\n"
    "; driver use about 0.3; set it to 270 in the driver and leave 1.0.\n"
    "steer_axis = %d\n"
    "steer_range = %.2f\n"
    "steer_invert = %d\n"
    "steer_dead = %.2f\n"
    "\n"
    "; Pedals: one axis each, or combined_pedals = 1 for one axis (accelerator one way,\n"
    "; brake the other: combined_gas_negative = 1 when the accelerator moves it below\n"
    "; the middle). Their resting position is learnt when the wheel is plugged in or the\n"
    "; game starts (or, on wheels that report them late, when they first move), so keep\n"
    "; your feet off them then. If a pedal still reads as pressed when released, give its\n"
    "; resting value (from SR2_WHEEL_LOG) by removing the ';' below, for example\n"
    "; gas_rest = 32767 and brake_rest = 32767 on a Logitech G29.\n"
    "gas_axis = %d\n"
    "brake_axis = %d\n"
    "combined_pedals = %d\n"
    "combined_gas_negative = %d\n"
    "; gas_rest = 32767\n"
    "; brake_rest = 32767\n"
    "\n"
    "; Buttons\n"
    "shift_up = %d\n"
    "shift_down = %d\n"
    "view = %d\n"
    "handbrake = %d\n"
    "; or an analog hand brake: its axis (on when pulled past half its travel)\n"
    "handbrake_axis = %d\n"
    "coin = %d\n"
    "start = %d\n"
    "; Menus and the pause screen: the hat (d-pad) moves, menu_accept and menu_back\n"
    "; choose and go back, start resumes, the shift paddles change the settings tab\n"
    "menu_accept = %d\n"
    "menu_back = %d\n"
    "; H shifter (gears 1-4); with them set, no gear button held is neutral\n"
    "gear1 = %d\n"
    "gear2 = %d\n"
    "gear3 = %d\n"
    "gear4 = %d\n"
    "\n"
    "; Force feedback (0..1): the game's own force (bumps, slides, the road) and a\n"
    "; centring spring. ffb_invert = 1 if the wheel pulls the wrong way.\n"
    "ffb_strength = %.2f\n"
    "ffb_spring = %.2f\n"
    "ffb_invert = %d\n";

void write_ini(const Config& c) {
    SDL_CreateDirectory("save");
    FILE* f = fopen(INI_FILE, "w");
    if (!f) return;
    fprintf(f, INI_TEXT, c.device.c_str(), c.pedals_device.c_str(), c.shifter_device.c_str(), c.handbrake_device.c_str(),
            c.steer_axis, c.steer_range, c.steer_invert, c.steer_dead, c.gas_axis, c.brake_axis, c.combined_pedals,
            c.combined_gas_negative, c.shift_up, c.shift_down, c.view, c.handbrake, c.handbrake_axis, c.coin, c.start,
            c.menu_accept, c.menu_back, c.gear[0], c.gear[1], c.gear[2], c.gear[3], c.ffb_strength, c.ffb_spring, c.ffb_invert);
    fclose(f);
}

Config read_ini() {
    Config c;
    FILE* f = fopen(INI_FILE, "r");
    if (!f) {
        write_ini(c);
        printf("[wheel] settings written to %s\n", INI_FILE);
        return c;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64], text[128];
        auto text_key = [&](const char* name, std::string& out) {
            std::string format = std::string(" ") + name + " = %127[^\r\n]";
            if (sscanf(line, format.c_str(), text) != 1) return false;
            out = text;
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
            return true;
        };
        if (text_key("device", c.device) || text_key("pedals_device", c.pedals_device) ||
            text_key("shifter_device", c.shifter_device) || text_key("handbrake_device", c.handbrake_device))
            continue;
        float v;
        if (line[0] == ';' || sscanf(line, " %63[a-z_0-9] = %f", key, &v) != 2) continue;
        std::string k = key;
        int i = int(std::lround(v));
        if (k == "steer_axis") c.steer_axis = i;
        else if (k == "steer_range") c.steer_range = std::clamp(v, 0.05f, 1.0f);
        else if (k == "steer_invert") c.steer_invert = i != 0;
        else if (k == "steer_dead") c.steer_dead = std::clamp(v, 0.0f, 0.5f);
        else if (k == "gas_axis") c.gas_axis = i;
        else if (k == "brake_axis") c.brake_axis = i;
        else if (k == "combined_pedals") c.combined_pedals = i != 0;
        else if (k == "combined_gas_negative") c.combined_gas_negative = i != 0;
        else if (k == "gas_rest") c.gas_rest = std::clamp(i, -32768, 32767);
        else if (k == "brake_rest") c.brake_rest = std::clamp(i, -32768, 32767);
        else if (k == "shift_up") c.shift_up = i;
        else if (k == "shift_down") c.shift_down = i;
        else if (k == "view") c.view = i;
        else if (k == "handbrake") c.handbrake = i;
        else if (k == "handbrake_axis") c.handbrake_axis = i;
        else if (k == "coin") c.coin = i;
        else if (k == "start") c.start = i;
        else if (k == "menu_accept") c.menu_accept = i;
        else if (k == "menu_back") c.menu_back = i;
        else if (k.size() == 5 && k.rfind("gear", 0) == 0 && k[4] >= '1' && k[4] <= '4') c.gear[k[4] - '1'] = i;
        else if (k == "ffb_strength") c.ffb_strength = std::clamp(v, 0.0f, 1.0f);
        else if (k == "ffb_spring") c.ffb_spring = std::clamp(v, 0.0f, 1.0f);
        else if (k == "ffb_invert") c.ffb_invert = i != 0;
    }
    fclose(f);
    return c;
}

struct Wheel {
    SDL_Joystick* joy = nullptr;
    uint32_t id = 0;
    int axes = 0, buttons = 0;
    int16_t rest[16] = {};
    bool rest_known[16] = {};   // false: rest is still SDL's first value, which can be a stray 0
    int menu_dir = -1;          // the hat in the menus, repeated while held
    double next_repeat = 0;
    bool was_down[MAX_BUTTONS] = {}, down[MAX_BUTTONS] = {};
    int16_t last_axis[16] = {};
    SDL_Haptic* haptic = nullptr;
    int constant = -1, spring = -1;
    int16_t level = 0;
    bool rumble = false;   // no haptic effects: shakes as rumble
    int score = 0;         // how sure we are it is the wheel (pick_score)
};

bool g_ok = false;
Config g_cfg;
Wheel g_wheel;   // one wheel: the first that arrives
// Separate pedals, shifter and hand brake named in wheel.ini (issue #73). One
// device can be in several (pedals with a hand brake): opened once, in the
// first of them, and the others point to it in g_alias
enum Role { PEDALS, SHIFTER, HANDBRAKE, ROLES };
const char* const ROLE_NAMES[ROLES] = {"pedals", "shifter", "hand brake"};
Wheel g_extra[ROLES];
int g_alias[ROLES] = {PEDALS, SHIFTER, HANDBRAKE};
Wheel g_none;   // a device named in wheel.ini but not plugged in: reads nothing
WheelState g_state;
std::atomic<float> g_force{0}, g_shake{0};   // from the game thread (runtime.cpp ffb_frame)
bool g_focused = false;
const bool g_log = getenv("SR2_WHEEL_LOG") != nullptr;

// A joystick that is a wheel, or anything with axes that SDL cannot use as a
// gamepad (pedals and wheels that SDL does not know as such)
bool is_wheel(uint32_t id) {
    SDL_JoystickType t = SDL_GetJoystickTypeForID(id);
    if (t == SDL_JOYSTICK_TYPE_WHEEL) return true;
    return !SDL_IsGamepad(id) && t != SDL_JOYSTICK_TYPE_THROTTLE;
}

const std::string& role_device(int r) {
    return r == PEDALS ? g_cfg.pedals_device : r == SHIFTER ? g_cfg.shifter_device : g_cfg.handbrake_device;
}

// Where a role's axes and buttons are read: its own device when wheel.ini names one
Wheel& source(int r) {
    if (role_device(r).empty()) return g_wheel;
    Wheel& d = g_extra[g_alias[r]];
    return d.joy ? d : g_none;
}

// A device named in wheel.ini: VID:PID (hex), or part of its name in any case
bool matches(uint32_t id, std::string want) {
    if (want.empty()) return false;
    unsigned vid = 0, pid = 0;
    char extra = 0;
    if (sscanf(want.c_str(), "%4x:%4x%c", &vid, &pid, &extra) == 2)
        return SDL_GetJoystickVendorForID(id) == vid && SDL_GetJoystickProductForID(id) == pid;
    const char* n = SDL_GetJoystickNameForID(id);
    std::string name = n ? n : "";
    for (char& c : name) c = char(std::tolower((unsigned char)c));
    for (char& c : want) c = char(std::tolower((unsigned char)c));
    return name.find(want) != std::string::npos;
}

float axis(const Wheel& w, int a) {
    return a >= 0 && a < w.axes ? float(SDL_GetJoystickAxis(w.joy, a)) : 0.0f;
}

// 0..1 from the resting value towards whichever end the pedal goes
float pedal(const Wheel& w, int a) {
    if (a < 0 || a >= w.axes) return 0.0f;
    float v = axis(w, a), r = float(w.rest[std::min(a, 15)]);
    float t;
    if (r < -16000.0f) t = (v - r) / (32767.0f - r);
    else if (r > 16000.0f) t = (r - v) / (r + 32768.0f);
    else t = std::fabs(v - r) / 32767.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.03f ? 0.0f : (t - 0.03f) / 0.97f;
}

void start_haptics(Wheel& w) {
    w.haptic = SDL_OpenHapticFromJoystick(w.joy);
    if (!w.haptic) {
        w.rumble = true;
        printf("[wheel] no force feedback effects (%s): road shakes as rumble if it has motors\n", SDL_GetError());
        return;
    }
    uint32_t f = SDL_GetHapticFeatures(w.haptic);
    if (f & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(w.haptic, 0);   // the spring below instead
    if (f & SDL_HAPTIC_GAIN) SDL_SetHapticGain(w.haptic, 100);
    if (f & SDL_HAPTIC_CONSTANT) {
        SDL_HapticEffect e;
        SDL_zero(e);
        e.type = SDL_HAPTIC_CONSTANT;
        e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        e.constant.direction.dir[0] = 1;
        e.constant.length = SDL_HAPTIC_INFINITY;
        e.constant.level = 0;
        w.constant = SDL_CreateHapticEffect(w.haptic, &e);
        if (w.constant >= 0) SDL_RunHapticEffect(w.haptic, w.constant, 1);
    }
    if ((f & SDL_HAPTIC_SPRING) && g_cfg.ffb_spring > 0) {
        SDL_HapticEffect e;
        SDL_zero(e);
        e.type = SDL_HAPTIC_SPRING;
        e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        e.condition.direction.dir[0] = 1;
        e.condition.length = SDL_HAPTIC_INFINITY;
        uint16_t sat = uint16_t(0xFFFF * g_cfg.ffb_spring);
        int16_t coeff = int16_t(0x7FFF * g_cfg.ffb_spring);
        e.condition.right_sat[0] = e.condition.left_sat[0] = sat;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = coeff;
        w.spring = SDL_CreateHapticEffect(w.haptic, &e);
        if (w.spring >= 0) SDL_RunHapticEffect(w.haptic, w.spring, 1);
    }
    if (w.constant < 0 && w.spring < 0) w.rumble = true;
    printf("[wheel] force feedback: %s%s%s\n", w.constant >= 0 ? "constant force " : "", w.spring >= 0 ? "spring " : "",
           w.rumble ? "rumble" : "");
}

void stop_haptics(Wheel& w) {
    if (!w.haptic) return;
    SDL_StopHapticEffects(w.haptic);
    SDL_CloseHaptic(w.haptic);
    w.haptic = nullptr;
    w.constant = w.spring = -1;
}

void apply_force(Wheel& w) {
    float f = g_focused ? g_force.load() * g_cfg.ffb_strength * (g_cfg.ffb_invert ? -1.0f : 1.0f) : 0.0f;
    if (w.haptic && w.constant >= 0) {
        int16_t level = int16_t(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        if (std::abs(level - w.level) > 300 || (level == 0 && w.level != 0)) {
            SDL_HapticEffect e;
            SDL_zero(e);
            e.type = SDL_HAPTIC_CONSTANT;
            e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
            e.constant.direction.dir[0] = 1;
            e.constant.length = SDL_HAPTIC_INFINITY;
            e.constant.level = level;
            Uint64 from = SDL_GetTicksNS();
            SDL_UpdateHapticEffect(w.haptic, w.constant, &e);
            g_vibration_seconds += double(SDL_GetTicksNS() - from) * 1e-9;
            w.level = level;
        }
    }
    float shake = g_shake.exchange(0.0f);
    if (w.rumble && g_focused && shake > 0.08f) {
        uint16_t m = uint16_t(std::min(1.0f, shake * g_cfg.ffb_strength) * 0xFFFF);
        Uint64 from = SDL_GetTicksNS();
        SDL_RumbleJoystick(w.joy, m, m / 2, 60);
        g_vibration_seconds += double(SDL_GetTicksNS() - from) * 1e-9;
    }
}

}  // namespace

void wheels_init() {
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
        printf("[wheel] SDL could not initialize joysticks: %s\n", SDL_GetError());
        return;
    }
    g_ok = true;   // the wheels already plugged in arrive as SDL_EVENT_JOYSTICK_ADDED
}

bool wheel_is_ours(uint32_t id) {
    if (g_wheel.joy && g_wheel.id == id) return true;
    for (const Wheel& d : g_extra)
        if (d.joy && d.id == id) return true;
    return false;
}

// Every joystick SDL finds, wheel or not, so that a log tells why a wheel
// was not taken (issue #33)
static void log_joystick(uint32_t id) {
    static const char* const TYPES[] = {"unknown", "gamepad", "wheel", "arcade stick", "flight stick",
                                        "dance pad", "guitar", "drum kit", "arcade pad", "throttle"};
    SDL_JoystickType t = SDL_GetJoystickTypeForID(id);
    const char* name = SDL_GetJoystickNameForID(id);
    printf("[joystick] %s (%04X:%04X, %s%s)\n", name ? name : "?", SDL_GetJoystickVendorForID(id),
           SDL_GetJoystickProductForID(id), unsigned(t) < 10 ? TYPES[t] : "?",
           SDL_IsGamepad(id) ? ", mapped as a gamepad" : "");
}

// Which of the joysticks is the wheel, when there are several (a shifter,
// pedals, a button box... issue #33): the one named in wheel.ini, then one
// SDL knows as a wheel, then one with force feedback, then the first
static int pick_score(uint32_t id, SDL_Joystick* j, const char** why) {
    if (matches(id, g_cfg.device)) return *why = "named in wheel.ini", 4;
    if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL) return *why = "a wheel for SDL", 3;
    if (SDL_IsJoystickHaptic(j)) return *why = "it has force feedback", 2;
    return *why = "the first joystick", 1;
}

// Axes, buttons and the axes' resting values of a device just opened
static void init_device(Wheel& w, SDL_Joystick* j, uint32_t id) {
    w = Wheel{};
    w.joy = j;
    w.id = id;
    w.axes = SDL_GetNumJoystickAxes(j);
    w.buttons = std::clamp(SDL_GetNumJoystickButtons(j), 0, MAX_BUTTONS);
    for (int a = 0; a < std::min(w.axes, 16); a++) {
        int16_t v = 0;
        if (!SDL_GetJoystickAxisInitialState(j, a, &v)) v = SDL_GetJoystickAxis(j, a);
        // Some wheels (a Logitech G29's pedals) give 0 here, and keep 0 until
        // their first report, seconds later: a pedal's 0 is checked in wheels_poll
        w.rest[a] = w.last_axis[a] = v;
        w.rest_known[a] = v != 0;
    }
}

// gas_rest / brake_rest from wheel.ini, on whichever device has the pedals
static void set_pedal_rests() {
    Wheel& p = source(PEDALS);
    if (!p.joy) return;
    auto set_rest = [&](int a, int v) {
        if (v == NO_REST || a < 0 || a >= std::min(p.axes, 16)) return;
        p.rest[a] = int16_t(v);
        p.rest_known[a] = true;
    };
    set_rest(g_cfg.gas_axis, g_cfg.gas_rest);
    set_rest(g_cfg.brake_axis, g_cfg.brake_rest);
}

static void not_used(SDL_Joystick* j) {
    const char* name = SDL_GetJoystickName(j);
    printf("[wheel] %s not used (if it is your pedals, shifter or hand brake, name it in %s as pedals_device, "
           "shifter_device or handbrake_device)\n", name ? name : "?", INI_FILE);
}

static void close_wheel() {
    stop_haptics(g_wheel);
    SDL_CloseJoystick(g_wheel.joy);
    g_wheel = Wheel{};
    g_state = WheelState{};
}

void wheels_added(uint32_t id) {
    if (!g_ok) return;
    log_joystick(id);
    if (wheel_is_ours(id)) return;
    g_cfg = read_ini();
    bool extra = false;
    for (int r = 0; r < ROLES; r++) {
        if (g_extra[g_alias[r]].joy || !matches(id, role_device(r))) continue;   // has its device, or not this one
        int q = 0;
        while (q < r && !(g_extra[q].joy && g_extra[q].id == id)) q++;
        if (q < r) {   // opened just now for an earlier role
            g_alias[r] = q;
            printf("[wheel] %s: also the %s\n", SDL_GetJoystickName(g_extra[q].joy), ROLE_NAMES[r]);
            continue;
        }
        SDL_Joystick* j = SDL_OpenJoystick(id);
        if (!j) continue;
        g_alias[r] = r;
        init_device(g_extra[r], j, id);
        printf("[wheel] %s: %d axes, %d buttons, taken as the %s (named in %s)\n", SDL_GetJoystickName(j),
               g_extra[r].axes, g_extra[r].buttons, ROLE_NAMES[r], INI_FILE);
        extra = true;
    }
    if (extra) {
        set_pedal_rests();
        return;
    }
    if (!is_wheel(id)) return;
    SDL_Joystick* j = SDL_OpenJoystick(id);
    if (!j) return;
    int axes = SDL_GetNumJoystickAxes(j);
    if (axes < 1) {   // buttons only: not a wheel
        not_used(j);
        SDL_CloseJoystick(j);
        return;
    }
    const char* why = "";
    int score = pick_score(id, j, &why);
    if (g_wheel.joy && score <= g_wheel.score) {   // the one we have is as good
        not_used(j);
        SDL_CloseJoystick(j);
        return;
    }
    if (g_wheel.joy) {
        printf("[wheel] %s replaces %s\n", SDL_GetJoystickName(j), g_state.name.c_str());
        close_wheel();
    }
    Wheel& w = g_wheel;
    init_device(w, j, id);
    w.score = score;
    set_pedal_rests();
    const char* name = SDL_GetJoystickName(j);
    g_state = WheelState{};
    g_state.connected = true;
    g_state.name = name ? name : "Wheel";
    printf("[wheel] %s: %d axes, %d buttons, taken as the wheel: %s (settings in %s)\n", g_state.name.c_str(), axes,
           w.buttons, why, INI_FILE);
    start_haptics(w);
}

void wheels_removed(uint32_t id) {
    for (int r = 0; r < ROLES; r++) {
        if (!g_extra[r].joy || g_extra[r].id != id) continue;
        SDL_CloseJoystick(g_extra[r].joy);
        g_extra[r] = Wheel{};
        for (int& a : g_alias)
            if (a == r) a = int(&a - g_alias);
        printf("[wheel] %s disconnected\n", ROLE_NAMES[r]);
    }
    if (!g_wheel.joy || g_wheel.id != id) return;
    close_wheel();
    printf("[wheel] disconnected\n");
}

void wheels_poll(bool focused, bool menu, std::vector<PadKey>& keys) {
    bool driving = focused && !menu;
    menu = focused && menu;
    g_focused = driving;
    Wheel& w = g_wheel;
    WheelState& st = g_state;
    st.shift_up = st.shift_down = st.coin = st.start = st.moved = false;
    if (!w.joy) return;
    Wheel& pedals = source(PEDALS);
    Wheel& shifter = source(SHIFTER);
    Wheel& hand = source(HANDBRAKE);
    // A pedal whose rest SDL gave as 0: while it still reads 0 it has not
    // reported yet. If its first reading jumps straight to one end of the axis,
    // that end is its rest; anything else is a pedal that really rests in the
    // middle and is moving. Not with combined pedals, whose ends are full gas
    // or brake
    auto learn_rest = [](Wheel& d, int a) {
        if (a < 0 || a >= std::min(d.axes, 16) || d.rest_known[a]) return;
        int16_t v = SDL_GetJoystickAxis(d.joy, a);
        if (v == 0) return;
        d.rest_known[a] = true;
        if (std::abs(int(v)) < REST_END) return;
        d.rest[a] = v;
        printf("[wheel] axis %d rest %d\n", a, v);
    };
    if (!g_cfg.combined_pedals) {
        learn_rest(pedals, g_cfg.gas_axis);
        learn_rest(pedals, g_cfg.brake_axis);
    }
    learn_rest(hand, g_cfg.handbrake_axis);
    // The wheel and the separate devices: their buttons, and the log
    Wheel* devices[1 + ROLES] = {&w};
    const char* labels[1 + ROLES] = {"wheel"};
    int count = 1;
    for (int r = 0; r < ROLES; r++) {
        if (!g_extra[r].joy) continue;
        devices[count] = &g_extra[r];
        labels[count++] = ROLE_NAMES[r];
    }
    for (int i = 0; i < count; i++) {
        Wheel& d = *devices[i];
        if (g_log) {
            for (int a = 0; a < std::min(d.axes, 16); a++) {
                int16_t v = SDL_GetJoystickAxis(d.joy, a);
                if (std::abs(v - d.last_axis[a]) > 2000) {
                    printf("[%s] axis %d = %d (rest %d)\n", labels[i], a, v, d.rest[a]);
                    d.last_axis[a] = v;
                }
            }
        }
        for (int b = 0; b < d.buttons; b++) {
            d.down[b] = SDL_GetJoystickButton(d.joy, b);
            if (g_log && d.down[b] != d.was_down[b]) printf("[%s] button %d %s\n", labels[i], b, d.down[b] ? "down" : "up");
            st.moved |= d.down[b] != d.was_down[b];
        }
    }
    auto held_on = [](const Wheel& d, int b) { return b >= 0 && b < d.buttons && d.down[b]; };
    auto held = [&](int b) { return held_on(w, b); };
    auto pressed = [&](int b) { return held(b) && !w.was_down[b]; };
    if (driving) {
        float s = axis(w, g_cfg.steer_axis) / 32767.0f / g_cfg.steer_range;
        if (g_cfg.steer_invert) s = -s;
        float m = std::fabs(s);
        m = m < g_cfg.steer_dead ? 0.0f : (m - g_cfg.steer_dead) / (1.0f - g_cfg.steer_dead);
        float steer = std::clamp(std::copysign(m, s), -1.0f, 1.0f);
        st.moved |= std::fabs(steer - st.steer) > 0.01f;
        st.steer = steer;
        if (g_cfg.combined_pedals) {
            float v = (axis(pedals, g_cfg.gas_axis) - float(pedals.rest[std::clamp(g_cfg.gas_axis, 0, 15)])) / 32767.0f;
            if (g_cfg.combined_gas_negative) v = -v;
            st.gas = pedals.joy ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
            st.brake = pedals.joy ? std::clamp(-v, 0.0f, 1.0f) : 0.0f;
        } else {
            st.gas = pedal(pedals, g_cfg.gas_axis);
            st.brake = pedal(pedals, g_cfg.brake_axis);
        }
        st.moved |= st.gas > 0 || st.brake > 0;
        st.view = held(g_cfg.view);
        st.handbrake = held_on(hand, g_cfg.handbrake) || pedal(hand, g_cfg.handbrake_axis) > 0.5f;
        st.shift_up = pressed(g_cfg.shift_up);
        st.shift_down = pressed(g_cfg.shift_down);
        st.coin = pressed(g_cfg.coin);
        st.start = pressed(g_cfg.start);
        bool h_shifter = false;
        st.gear = -1;
        for (int g = 0; g < 4; g++) {
            if (g_cfg.gear[g] < 0) continue;
            h_shifter = true;
            if (held_on(shifter, g_cfg.gear[g])) st.gear = g + 1;
        }
        if (h_shifter && st.gear < 0) st.gear = 0;
    } else {
        st.steer = st.gas = st.brake = 0;
        st.view = st.handbrake = false;
        st.gear = -1;
    }
    if (menu) {
        if (pressed(g_cfg.menu_accept)) keys.push_back(PadKey::Accept);
        if (pressed(g_cfg.menu_back)) keys.push_back(PadKey::Back);
        if (pressed(g_cfg.start)) keys.push_back(PadKey::Start);
        if (pressed(g_cfg.shift_down)) keys.push_back(PadKey::TabPrev);
        if (pressed(g_cfg.shift_up)) keys.push_back(PadKey::TabNext);
        int dir = -1;
        if (SDL_GetNumJoystickHats(w.joy) > 0) {
            uint8_t h = SDL_GetJoystickHat(w.joy, 0);
            dir = h & SDL_HAT_UP ? 0 : h & SDL_HAT_DOWN ? 1 : h & SDL_HAT_LEFT ? 2 : h & SDL_HAT_RIGHT ? 3 : -1;
        }
        double now = double(SDL_GetTicksNS()) * 1e-9;
        if (dir != w.menu_dir || (dir >= 0 && now >= w.next_repeat)) {
            if (dir >= 0) {
                static const PadKey DIRS[4] = {PadKey::Up, PadKey::Down, PadKey::Left, PadKey::Right};
                keys.push_back(DIRS[dir]);
                w.next_repeat = now + (dir != w.menu_dir ? 0.4 : 0.11);
            }
            w.menu_dir = dir;
        }
    } else {
        w.menu_dir = -1;
    }
    for (int i = 0; i < count; i++) std::copy(std::begin(devices[i]->down), std::end(devices[i]->down), std::begin(devices[i]->was_down));
    apply_force(w);
}

void wheels_shutdown() {
    for (Wheel& d : g_extra)
        if (d.joy) wheels_removed(d.id);
    if (g_wheel.joy) wheels_removed(g_wheel.id);
    g_ok = false;
}

const WheelState& wheel_state() { return g_state; }

void wheel_set_force(float force, float shake) {
    g_force = force;
    if (shake > g_shake.load()) g_shake = shake;
}

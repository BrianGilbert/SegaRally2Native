// The port's own code in place of single instructions of the game's
// (tools/game_hooks.py, game_hooks.h).
#include "game_hooks.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "options.h"
#include "ppc_rt.h"

extern Options g_opt;   // app.cpp
extern std::vector<uint8_t> g_vrom;   // runtime.cpp

// ---- SELECT COURSE with a fifth card, FOREST --------------------------------
// The ROM has a fifth stage, FOREST (course id 1, docs/game_ram.md), that no
// select reaches: SELECT COURSE's carousel was written for four cards. With
// FOREST on, its cursor, turns and drawing count five (the hooks in
// game_hooks.py) and the fifth card, after SNOWY, races FOREST. Practice
// only: the championship never shows SELECT COURSE. Never in ARCADE ORIGINAL.
namespace {
constexpr uint32_t CARD_FLAGS = 0x47D424;   // a word per card (the chosen one blinks); room for four
constexpr int FOREST_CARD = 4;
constexpr uint32_t FOREST_COURSE = 1;
uint32_t g_forest_flag = 0;                 // the fifth card's flag, kept here

int card_of(uint32_t ea) { return int(int32_t(ea - CARD_FLAGS) >> 2); }
}  // namespace

int sr2_cards() {
    return g_opt.forest && !g_opt.arcade ? 5 : 4;
}

// The four cards were a quarter turn apart: the side ones at 90 degrees, the
// back one alone. Five at 72 degrees on the same ellipse put the two at the
// back (144) against each other, and the front pair out further than the side
// ones were. Five: the radius times 1 + a (1 - cos) / 2 + b sin^2, 1 at the
// front; a 0.358, b -0.214 put the front pair (72) 7% in and the back pair
// (144) 25% out. SR2_CARD_SPREAD=a,b tries others without a build
double sr2_card_radius(double angle, double radius) {
    if (sr2_cards() == 4) return radius;
    static const double* ab = [] {
        static double v[2] = {0.358, -0.214};
        if (const char* e = getenv("SR2_CARD_SPREAD")) sscanf(e, "%lf,%lf", &v[0], &v[1]);
        return v;
    }();
    double c = std::cos(angle), u = (1 - c) / 2, s2 = 1 - c * c;
    return double(float(radius * (1 + ab[0] * u + ab[1] * s2)));
}

// FOREST's card: the DESERT card's models (708, lit 709, the first of each
// table) with its own pictures. The model number carries a mark (bit 28: the
// model table's offset, number << 4, leaves it out), and the VROM address
// drawn (sr2_model_address) is then a copy of the model in VROM nobody uses
// (from SR2_OWN_VROM: the 64 MB end in zeros from 0xFA6F40), so the renderer
// tells it apart from DESERT's (R3DSceneBuilder::own_texture, app.cpp: the
// picture and name band from res/forest/card.png)
static constexpr uint32_t OWN_MODEL = 0x10000000;   // on the model number

// A model's length in VROM words: its polygons up to the one marked last
// (bit 2 of word 1) or a zero word 6 (real3d_spec.md, as R3DSceneBuilder::model)
static size_t model_words(const uint32_t* v, size_t from, size_t total) {
    size_t p = from;
    for (int guard = 0; guard < 65536 && p + 7 <= total; guard++) {
        const uint32_t* h = v + p;
        if (h[6] == 0) break;
        int n = (h[0] & 0x40) ? 4 : 3, shared = 0;
        for (int i = 0; i < 4; i++) shared += (h[0] >> i) & 1;
        p += 7 + 4 * size_t(n > shared ? n - shared : 0);
        if (h[1] & 4) break;
    }
    return p - from;
}

// The copy of the model at addr in the free VROM (made once), addr itself
// when there is no room
static uint32_t own_copy(uint32_t addr) {
    static std::unordered_map<uint32_t, uint32_t> copies;
    static uint32_t next = SR2_OWN_VROM;
    auto it = copies.find(addr);
    if (it != copies.end()) return it->second;
    uint32_t copy = addr;
    size_t total = g_vrom.size() / 4;
    uint32_t* v = reinterpret_cast<uint32_t*>(g_vrom.data());
    size_t n = addr < total ? model_words(v, addr, total) : 0;
    bool room = n && next + n <= total;
    for (size_t i = 0; room && i < n; i++) room = v[next + i] == 0;
    if (room) {
        memcpy(v + next, v + addr, n * 4);
        copy = next;
        next += uint32_t(n);
        printf("[forest] card model %06x copied to %06x (%zu words)\n", addr, copy, n);
    } else {
        printf("[forest] no room in VROM at %06x for the card model %06x: DESERT's pictures\n", next, addr);
    }
    copies[addr] = copy;
    return copy;
}

uint32_t sr2_card_model(uint32_t table, uint32_t offset, bool) {
    if (offset >> 2 >= uint32_t(FOREST_CARD)) return rd32(table) | OWN_MODEL;
    return rd32(table + offset);
}

uint32_t sr2_model_address(uint32_t ea, uint32_t model) {
    uint32_t addr = rd32(ea);
    return model & OWN_MODEL ? own_copy(addr) : addr;
}

uint32_t sr2_card_flag_load(uint32_t ea) {
    return card_of(ea) == FOREST_CARD && sr2_cards() > FOREST_CARD ? g_forest_flag : rd32(ea);
}

void sr2_card_flag_store(uint32_t ea, uint32_t v) {
    int card = card_of(ea);
    if (card == FOREST_CARD && sr2_cards() > FOREST_CARD) {
        g_forest_flag = v;
        return;
    }
    wr32(ea, v);
}

// The loops that set all the flags on or off count four: the last word they
// write (card 0) goes to FOREST's too
void sr2_card_flag_store_all(uint32_t ea, uint32_t v) {
    wr32(ea, v);
    if (card_of(ea) == 0) g_forest_flag = v;
}

// The palettes are a copy of 0xE97CC (0, 1, 3, 2) on the stack, four words.
// FOREST: DESERT's for now
uint32_t sr2_card_palette(uint32_t table, uint32_t offset) {
    return rd32(offset >> 2 >= uint32_t(FOREST_CARD) ? table : table + offset);
}

uint32_t sr2_course_id(uint32_t table, uint32_t offset) {
    return offset >> 2 == uint32_t(FOREST_CARD) ? FOREST_COURSE : rd32(table + offset);
}

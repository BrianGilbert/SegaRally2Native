// The port's own code in place of single instructions of the game's
// (tools/game_hooks.py, game_hooks.h).
#include "game_hooks.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "options.h"
#include "ppc_rt.h"

extern Options g_opt;   // app.cpp

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
// drawn (sr2_model_address) is then the model's mirror: VROM's 32 MB repeat
// over the 64 MB a node addresses, so the renderer draws the same polygons
// and tells them apart from DESERT's (R3DSceneBuilder::own_texture, app.cpp:
// the picture and name band from res/forest/card.png)
constexpr uint32_t OWN_MODEL = 0x10000000;   // on the model number
constexpr uint32_t VROM_MIRROR = 0x800000;   // on the VROM word address: 32 MB on

uint32_t sr2_card_model(uint32_t table, uint32_t offset, bool) {
    if (offset >> 2 >= uint32_t(FOREST_CARD)) return rd32(table) | OWN_MODEL;
    return rd32(table + offset);
}

uint32_t sr2_model_address(uint32_t ea, uint32_t model) {
    uint32_t addr = rd32(ea);
    return model & OWN_MODEL ? addr | VROM_MIRROR : addr;
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

// The port's own code in place of single instructions of the game's
// (tools/game_hooks.py lists them by address; recomp.py puts the calls in the
// generated code). Each one does what the instruction did unless its change
// is on.
#pragma once
#include <cstdint>

// SELECT COURSE (practice) with a fifth card, FOREST (game_hooks.cpp)
int sr2_cards();   // 4 as the arcade, 5 with FOREST
double sr2_card_radius(double angle, double radius);                  // how far out a card is drawn
uint32_t sr2_card_model(uint32_t table, uint32_t offset, bool lit);   // card offset / 4 drawn
uint32_t sr2_card_flag_load(uint32_t ea);                             // a card's flag (0x47D424 + 4 x card)
void sr2_card_flag_store(uint32_t ea, uint32_t v);
void sr2_card_flag_store_all(uint32_t ea, uint32_t v);                // the loops that set them all (four)
uint32_t sr2_card_palette(uint32_t table, uint32_t offset);           // the 2D picture's palette for a card
uint32_t sr2_course_id(uint32_t table, uint32_t offset);              // the race set-up: card -> course id

"""C++ that tools/recomp.py puts in place of single instructions' translations,
by address. Each one calls src/runtime/game_hooks.cpp, so a change of the game
can be switched on and off while it runs, where a patch of the ROM could not.

Only plain instructions (no branches): the code after each one runs as the
game's own.
"""

# SELECT COURSE with a fifth card, FOREST: the stage the ROM has (course id 1)
# that no select reaches (docs/game_ram.md). The carousel was written for
# four cards: the count, the wrap of the cursor and the quarter turns between
# cards are constants in the code, and its tables have room for four. With
# sr2_cards() at 4 (FOREST off) every hook does what the instruction did.
#
#   cursor 0x1371C0, carousel angle 0x1371B8 (cards at 2 pi / cards apart)
#   0x47408 (step 5): the cards come in one by one up to the count (0x22436C)
#   0x5914...0x5FD0 (step 6): turning, the cursor read back from the angle
#   0x5FF4: the cards drawn, model by card (lit when its flag is on)
#   0x46AAC / 0x4835C: the race set-up, card -> course id (table 0xF1F20)

N = "sr2_cards()"
TURN = "double(6.28318548f / float(sr2_cards()))"   # 2 pi / cards: pi / 2 for four, as the ROM has

HOOKS = {}


def _set(addrs, code):
    for a in addrs:
        HOOKS[a] = code


# The count of cards the carousel comes in with (step 5): cmpwi r0, 3 / li r0, 4
_set([0x47420], [f"cmps(c, 1, int32_t(c.r[0]), {N} - 1);"])
_set([0x47430], [f"c.r[0] = uint32_t({N});"])

# The cursor read back from the angle: li rX, 4 (counted down from the last card)
_set([0x5CE8, 0x5DBC], [f"c.r[11] = uint32_t({N});"])
_set([0x5ECC], [f"c.r[10] = uint32_t({N});"])
# and its wrap: cmpwi rX, 4 (past the last card: the first) ...
_set([0x599C, 0x5D44, 0x5E20], [f"cmps(c, 1, int32_t(c.r[11]), {N});"])
_set([0x5F28], [f"cmps(c, 1, int32_t(c.r[10]), {N});"])
# ... and rlwinm r9, r9, 0, 30, 31 (before the first: the last; r9 is 0 or ~0)
_set([0x59BC, 0x5D64, 0x5E44, 0x5F50], [f"c.r[9] = c.r[9] & uint32_t({N} - 1);"])
# The angle a card is at: (cards - cursor) turns (subfic r0, r0 / r10, 4)
_set([0x56F8], [f"c.r[0] = addca(c, ~c.r[0], uint32_t({N}), 1);"])
_set([0x5F70], [f"c.r[0] = addca(c, ~c.r[10], uint32_t({N}), 1);"])
# One turn: lfs fX, -0x6814(r9) (0xE97EC, pi / 2)
_set([0x5728, 0x5FA0], [f"c.f[0].d = {TURN};"])
_set([0x5CD8, 0x5D24, 0x5DAC, 0x5DF8, 0x5EBC, 0x5F08], [f"c.f[12].d = {TURN};"])

# Drawing (0x5FF4): card i at 2 pi i x 0.25 (lfs f13, 0.25: 0xE9814) -> 1 / cards
_set([0x60C0], [f"c.f[13].d = double(1.0f / float({N}));"])
# and its model: lwzx r3, r9, r29 (lit, 0xE9790) / lwzx r3, r9, r30 (0xE9780)
_set([0x6210], ["c.r[3] = sr2_card_model(c.r[29], c.r[9], true);"])
_set([0x6224], ["c.r[3] = sr2_card_model(c.r[30], c.r[9], false);"])

# The cards' flags (0x47D424, a word each: the chosen card blinks) have room
# for four; the fifth's word is somebody else's (0x47D434)
_set([0x61F8], ["c.r[0] = sr2_card_flag_load(c.r[9] + c.r[28]);"])            # lwzx r0, r9, r28
_set([0x538C, 0x59F4, 0x47574, 0x46298], ["sr2_card_flag_store(c.r[11] + c.r[9], c.r[0]);"])   # stwx r0, r11, r9
_set([0x5964], ["sr2_card_flag_store_all(c.r[9] + c.r[0], c.r[11]);"])            # stwx r11, r9, r0 (all off)
_set([0x58FC], ["sr2_card_flag_store_all(c.r[11] + c.r[7], c.r[8]);"])            # stwx r8, r11, r7 (all off)
_set([0x512C], ["sr2_card_flag_store_all(c.r[7] + c.r[0], c.r[8]);"])             # stwx r8, r7, r0 (all on)

# The 2D picture under the cards, a palette per card (a copy of 0xE97CC on the
# stack: 0, 1, 3, 2): lwzx r6, rX, r0
_set([0x52E8], ["c.r[6] = sr2_card_palette(c.r[31], c.r[0]);"])
_set([0x540C, 0x57E0, 0x5AE8], ["c.r[6] = sr2_card_palette(c.r[9], c.r[0]);"])

# The race set-up: the card's course id, lwzx r0, r9, r0 (0xF1F20: 3, 2, 4, 5)
_set([0x46AF8, 0x48480], ["c.r[0] = sr2_course_id(c.r[9], c.r[0]);"])

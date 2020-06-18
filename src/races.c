/**************************************************************************
 *  File: races.c                                      Extension of tbaMUD *
 *  Usage: Source file for race-specific code                              *
 *  Original Author: Brian Williams for CircleMUD                          *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 *  Race patch updated for tbaMUD-3.60 by Xiuhtecuhtli on 10.14.09         *
 ***************************************************************************/

/** Help buffer the global variable definitions */
#define __RACES_C__

/* This file attempts to concentrate most of the code which must be changed
 * in order for new races to be added.  If you're adding a new race, you
 * should go through this entire file from beginning to end and add the
 * appropriate new special cases for your new race. */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "db.h"
#include "interpreter.h"
#include "spells.h"

/* Names first */
const char *race_abbrevs[] = {"Hum", "Elf", "Gno", "Dwa", "Hlf", "\n"};

const char *pc_race_types[] = {"Human", "Elf",      "Gnome",
                               "Dwarf", "Halfling", "\n"};

/* The menu for choosing a race in interpreter.c: */
const char *race_menu = "\r\n"
                        "Select a race:\r\n"
                        "  [H] - Human\r\n"
                        "  [E] - Elf\r\n"
                        "  [G] - Gnome\r\n"
                        "  [W] - Dwarf\r\n"
                        "  [L] - Halfling\r\n";

/** Check if the class & race combo is allowed. */
int classRaceAllowed[NUM_RACES][NUM_CLASSES] = {
    /*  M    C    T    W                  */
    {YES, YES, YES, YES}, /* Human */
    {YES, YES, YES, YES}, /* Elf      */
    {YES, YES, NO, YES},  /* Gnome    */
    {NO, YES, NO, YES},   /* Dwarf    */
    {NO, YES, YES, YES}   /* Halfling */
};

/* The code to interpret a race letter -- used in interpreter.c when a new
 * character is selecting a race and by 'set race' in act.wizard.c. */
int parse_race(char arg) {
  arg = LOWER(arg);

  switch (arg) {
  case 'h':
    return RACE_HUMAN;
  case 'e':
    return RACE_ELF;
  case 'g':
    return RACE_GNOME;
  case 'w':
    return RACE_DWARF;
  case 'l':
    return RACE_HALFLING;
  default:
    return RACE_UNDEFINED;
  }
}

/* bitvectors (i.e., powers of two) for each race, mainly for use in do_who
 * and do_users.  Add new races at the end so that all races use sequential
 * powers of two (1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, etc.) up to
 * the limit of your bitvector_t, typically 0-31. */
bitvector_t find_race_bitvector(const char *arg) {
  size_t rpos, ret = 0;

  for (rpos = 0; rpos < strlen(arg); rpos++)
    ret |= (1 << parse_race(arg[rpos]));

  return (ret);
}

/* invalid_race is used by handler.c to determine if a piece of equipment is
 * usable by a particular race, based on the ITEM_ANTI_{race} bitvectors. */
int invalid_race(struct char_data *ch, struct obj_data *obj) {
  if (OBJ_FLAGGED(obj, ITEM_ANTI_HUMAN) && IS_HUMAN(ch))
    return TRUE;
  if (OBJ_FLAGGED(obj, ITEM_ANTI_ELF) && IS_ELF(ch))
    return TRUE;
  if (OBJ_FLAGGED(obj, ITEM_ANTI_GNOME) && IS_GNOME(ch))
    return TRUE;
  if (OBJ_FLAGGED(obj, ITEM_ANTI_DWARF) && IS_DWARF(ch))
    return TRUE;
  if (OBJ_FLAGGED(obj, ITEM_ANTI_HALFLING) && IS_HALFLING(ch))
    return TRUE;

  return FALSE;
}

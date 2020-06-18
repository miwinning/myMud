/**
* @file races.h
* Race Patch version 3.1
*
* Header file for race specific functions and variables. For a list of changes,
* past authors, or instructions on patching this into your source code, check
* out races.README.
*
* Extension of the tbaMUD source code distribution, which is a derivative
* of, and continuation of, CircleMUD.
*
* All rights reserved.  See license for complete information.
* Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University
* CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.
*
*/
#ifndef _RACES_H_
#define _RACES_H_

/* Functions available through class.c */
bitvector_t find_race_bitvector(const char *arg);
int invalid_race(struct char_data *ch, struct obj_data *obj);
int parse_race(char arg);

/* Global variables */

#ifndef __RACES_C__
extern const char *race_abbrevs[];
extern const char *pc_race_types[];
extern const char *race_menu;
extern       int  classRaceAllowed[NUM_RACES][NUM_CLASSES];

#endif /* __RACES_C__ */

#endif /* _RACES_H_*/

/**************************************************************************
 *  File: boards.c                                          Part of tbaMUD *
 *  Usage: Handling of multiple bulletin boards.                           *
 *                                                                         *
 *  All rights reserved.  See license for complete information.            *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

/* FEATURES & INSTALLATION INSTRUCTIONS
 * - Arbitrary number of boards handled by one set of generalized routines.
 *   Adding a new board is as easy as adding another entry to an array.
 * - Safe removal of messages while other messages are being written.
 *
 * TO ADD A NEW BOARD, simply follow our easy 4-step program:
 * 1 - Create a new board object in the object files.
 * 2 - Increase the NUM_OF_BOARDS constant in boards.h.
 * 3 - Add a new line to the board_info array below.  The fields are:
 * 	Board's virtual number.
 * 	Min level one must be to look at this board or read messages on it.
 * 	Min level one must be to post a message to the board.
 * 	Min level one must be to remove other people's messages from this
 * 	  board (but you can always remove your own message).
 * 	Filename of this board, in quotes.
 * 	Last field must always be 0.
 * 4 - In spec_assign.c, find the section which assigns the special procedure
 *     gen_board to the other bulletin boards, and add your new one in a
 *     similar fashion. */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "boards.h"
#include "interpreter.h"
#include "handler.h"
#include "improved-edit.h"
#include "modify.h"
#include <sys/types.h>
#include <sys/stat.h>

extern struct descriptor_data *descriptor_list;
extern struct index_data *obj_index;

struct board_info *bboards = NULL; /* our global board structure */

void init_boards(void) {
  int i, j, board_vnum;
  struct xap_dir xd;
  struct board_info *tmp_board;

  if (!insure_directory(BOARD_DIRECTORY, 0)) {
    log("Unable to open/create directory '%s' - Exiting", BOARD_DIRECTORY);
    exit(1);
  }

  if ((i = xdir_scan(BOARD_DIRECTORY, &xd)) <= 0) {
    log("Funny, no board files found.\n");
    return;
  }

  /* otherwise they do exist */
  for (j = 0; j < i; j++) {
    if (strcmp("..", xdir_get_name(&xd, j)) &&
        strcmp(".", xdir_get_name(&xd, j))) {
      sscanf(xdir_get_name(&xd, j), "%d", &board_vnum);
      if ((tmp_board = load_board(board_vnum)) != NULL) {
        tmp_board->next = bboards;
        bboards = tmp_board;
      }
    }
  }

  /* just logs some summary data about the boards */
  look_at_boards();
}

struct board_info *create_new_board(int board_vnum) {
  char buf[512];
  FILE *fl;
  struct board_info *temp = NULL, *backup;
  struct obj_data *obj = NULL;

  /* object exists, but no board file (yet) */

  if ((fl = fopen(buf, "r"))) {
    fclose(fl);
    log("Preexisting board file when attempting to create new board [vnum: "
        "%d]. Attempting to correct.",
        board_vnum);

    /* unlink file, clear existing board */
    unlink(buf);

    for (temp = bboards, backup = NULL; temp && !backup; temp = temp->next) {
      if (BOARD_VNUM(temp) == board_vnum) {
        backup = temp;
      }
    }
    if (backup) {
      REMOVE_FROM_LIST(backup, bboards, next);
      clear_one_board(backup);
    }
  }

  CREATE(temp, struct board_info, 1);
  if (real_object(board_vnum) == NOTHING) {
    log("Creating board [vnum: %d] though no associated object with that vnum "
        "can be found. Using defaults.",
        board_vnum);
    READ_LVL(temp) = LVL_IMMORT;
    WRITE_LVL(temp) = LVL_IMMORT;
    REMOVE_LVL(temp) = LVL_IMMORT;
  } else {
    obj = &(obj_proto[real_object(board_vnum)]);
    READ_LVL(temp) = GET_OBJ_VAL(obj, 0);
    WRITE_LVL(temp) = GET_OBJ_VAL(obj, 1);
    REMOVE_LVL(temp) = GET_OBJ_VAL(obj, 2);
  }

  BOARD_VNUM(temp) = board_vnum;
  BOARD_MNUM(temp) = 0;
  temp->next = NULL;
  BOARD_MESSAGES(temp) = NULL;

  if (!save_board(temp)) {
    log("Hm. Error while creating new board file [vnum: %d]. Unable to create "
        "new file.",
        board_vnum);
    free(temp);
    return NULL;
  }

  return temp;
}

int save_board(struct board_info *ts) {
  struct board_msg *message;
  struct board_memory *memboard;
  FILE *fl;
  char buf[512];
  int i = 1;

  sprintf(buf, "%s/%d", BOARD_DIRECTORY, BOARD_VNUM(ts));

  if (!(fl = fopen(buf, "wb"))) {
    log("Hm. Error while creating attempting to save board [vnum: %d].  Unable "
        "to create file '%s'",
        BOARD_VNUM(ts), buf);
    return 0;
  }

  fprintf(fl, "Board File\n%d %d %d %d\n", READ_LVL(ts), WRITE_LVL(ts),
          REMOVE_LVL(ts), BOARD_MNUM(ts));

  for (message = BOARD_MESSAGES(ts); message; message = MESG_NEXT(message)) {
    fprintf(fl,
            "#%d\n"
            "%ld\n"
            "%ld\n"
            "%s\n"
            "%s~\n",
            i++, MESG_POSTER(message), MESG_TIMESTAMP(message),
            MESG_SUBJECT(message), MESG_DATA(message));
  }

  /* now write out the saved info.. */
  for (i = 0; i != 301; i++) {
    memboard = BOARD_MEMORY(ts, i);
    while (memboard) {
      fprintf(fl, "S%d %d %d\n", i, MEMORY_READER(memboard),
              MEMORY_TIMESTAMP(memboard));
      memboard = MEMORY_NEXT(memboard);
    }
  }
  fclose(fl);

  return 1;
}

/* a fairly messy function                         */
/* see accompanying document for board file format */

struct board_info *load_board(int board_vnum) {
  struct board_info *temp_board;
  struct board_msg *bmsg;
  struct obj_data *obj = NULL;
  struct stat st;
  struct board_memory *memboard, *list;
  int t[4], mnum, poster, timestamp, msg_num;
  char filebuf[512], buf[512];
  FILE *fl;
  int sflag, error_flag = 0;

  sprintf(filebuf, "%s/%d", BOARD_DIRECTORY, board_vnum);
  if (!(fl = fopen(filebuf, "r"))) {
    log("Request to open board [vnum %d] failed - unable to open file '%s'.",
        board_vnum, filebuf);
    return NULL;
  }

  /* this won't be the most graceful thing you've ever seen .. */
  get_line(fl, buf);

  if (strcasecmp("Board File", buf)) {
    log("Invalid board file '%s' [vnum: %d] - failed to load.", filebuf,
        board_vnum);
    return NULL;
  }

  CREATE(temp_board, struct board_info, 1);
  temp_board->vnum = board_vnum;
  get_line(fl, buf);

  /* oddly enough, most errors in board files can be ignored, setting defaults
   */

  if (sscanf(buf, "%d %d %d %d", t, t + 1, t + 2, t + 3) != 4) {
    log("Parse error on board [vnum: %d], file '%s' - attempting to correct.",
        board_vnum, filebuf);
    t[0] = t[1] = t[2] = LVL_IMMORT;
    t[3] = -1;
    error_flag = 1;
  }

  /* if the objcet exists, the object trumps the board file settings */

  if (real_object(board_vnum) == NOTHING) {
    log("No associated object exists when attempting to create a board [vnum "
        "%d].",
        board_vnum);
    /* previously we just erased it, but lets do a tiny bit of checking, just in
     * case           */
    /* auto delete only if the file has hasn't been modified in the last 7 days
     */

    stat(filebuf, &st);
    if (time(NULL) - st.st_mtime > (60 * 60 * 24 * 7)) {
      log("Deleting old board file '%s' [vnum %d].  7 days without "
          "modification & no associated object.",
          filebuf, board_vnum);
      unlink(filebuf);
      free(temp_board);
      return NULL;
    }

    READ_LVL(temp_board) = t[0];
    WRITE_LVL(temp_board) = t[1];
    REMOVE_LVL(temp_board) = t[2];
    BOARD_MNUM(temp_board) = t[3];
  } else {
    obj = &(obj_proto[real_object(board_vnum)]);

    /* double check one or two things */
    if (t[0] != GET_OBJ_VAL(obj, 0) || t[1] != GET_OBJ_VAL(obj, 1) ||
        t[2] != GET_OBJ_VAL(obj, 2)) {
      log("Mismatch in board <-> object read/write/remove settings for board "
          "[vnum: %d]. Correcting.",
          board_vnum);
      error_flag = 1;
    }

    READ_LVL(temp_board) = GET_OBJ_VAL(obj, 0);
    WRITE_LVL(temp_board) = GET_OBJ_VAL(obj, 1);
    REMOVE_LVL(temp_board) = GET_OBJ_VAL(obj, 2);
    BOARD_MNUM(temp_board) = t[3];
  }

  BOARD_NEXT(temp_board) = NULL;
  BOARD_MESSAGES(temp_board) = NULL;

  /* now loop and parse messages and memory */
  msg_num = 0;
  while (get_line(fl, buf)) {
    if (*buf == 'S') {
      if (sscanf(buf, "S %d %d %d ", &mnum, &poster, &timestamp) == 3) {
        CREATE(memboard, struct board_memory, 1);
        MEMORY_READER(memboard) = poster;
        MEMORY_TIMESTAMP(memboard) = timestamp;

        /* now, validate the memory => insure that for this slot, id, and
           timestamp there is a valid message, and poster.  Memory is deleted
           for mundane reasons; character deletions, message deletions, etc.
           'Failures' will not be logged */

        if (get_name_by_id(poster) == NULL) {
          free(memboard);
          error_flag = 1;
        } else {

          /* locate specific message this pertains to - therefore, messages MUST
           * be loaded first! */

          for (bmsg = BOARD_MESSAGES(temp_board), sflag = 0; bmsg && !sflag;
               bmsg = MESG_NEXT(bmsg)) {
            if (MESG_TIMESTAMP(bmsg) == MEMORY_TIMESTAMP(memboard) &&
                (mnum ==
                 ((MESG_TIMESTAMP(bmsg) % 301 + MESG_POSTER(bmsg) % 301) %
                  301))) {
              sflag = 1;
            }
          }

          if (sflag) {
            if (BOARD_MEMORY(temp_board, mnum)) {
              list = BOARD_MEMORY(temp_board, mnum);
              BOARD_MEMORY(temp_board, mnum) = memboard;
              MEMORY_NEXT(memboard) = list;
            } else {
              BOARD_MEMORY(temp_board, mnum) = memboard;
              MEMORY_NEXT(memboard) = NULL;
            }
          } else {
            free(memboard);
          }
        }
      } else {
        error_flag = 1;
      }
    } else if (*buf == '#') {
      if (parse_message(fl, temp_board)) {
        msg_num++;
      }
    }
  }

  /* now we've completely parsed our file */
  fclose(fl);

  if (msg_num != BOARD_MNUM(temp_board)) {
    log("Board [vnum: %d] message count (%d) not equal to actual message count "
        "(%d). Correcting.",
        BOARD_VNUM(temp_board), BOARD_MNUM(temp_board), msg_num);
    BOARD_MNUM(temp_board) = msg_num;
    error_flag = 1;
  }

  /* if the error flag is set, we need to save the board again */
  if (error_flag) {
    save_board(temp_board);
  }
  return temp_board;
}

int parse_message(FILE *fl, struct board_info *temp_board) {
  struct board_msg *tmsg, *t2msg;
  char subject[81];
  char buf[MAX_MESSAGE_LENGTH + 1]; /* arbitrairy max message length */

  CREATE(tmsg, struct board_msg, 1);

  /* what about our error checking? */
  if (fscanf(fl, "%ld\n", &(MESG_POSTER(tmsg))) != 1 ||
      fscanf(fl, "%ld\n", &(MESG_TIMESTAMP(tmsg))) != 1) {
    log("Parse error in message for board [vnum: %d].  Skipping.",
        BOARD_VNUM(temp_board));
    free(tmsg);
    return 0;
  }

  get_line(fl, subject);

  MESG_SUBJECT(tmsg) = strdup(subject);
  MESG_DATA(tmsg) = fread_string(fl, buf);
  MESG_NEXT(tmsg) = NULL;

  /* always add to the END of the list. */
  MESG_NEXT(tmsg) = MESG_PREV(tmsg) = NULL;
  if (BOARD_MESSAGES(temp_board)) {
    t2msg = BOARD_MESSAGES(temp_board);
    while (MESG_NEXT(t2msg)) {
      t2msg = MESG_NEXT(t2msg);
    }
    MESG_NEXT(t2msg) = tmsg;
    MESG_PREV(tmsg) = t2msg;
  } else {
    MESG_PREV(tmsg) = NULL;
    BOARD_MESSAGES(temp_board) = tmsg;
  }

  return 1;
}

void look_at_boards() {
  int counter, messages = 0;
  struct board_info *tboard = bboards;
  struct board_msg *msg;

  for (counter = 0; tboard; counter++) {
    msg = BOARD_MESSAGES(tboard);
    while (msg) {
      messages++;
      msg = MESG_NEXT(msg);
    }
    tboard = BOARD_NEXT(tboard);
  }

  log("There are %d boards located; %d messages", counter, messages);
}

void clear_boards() {
  struct board_info *tmp, *tmp2;

  for (tmp = bboards; tmp; tmp = tmp2) {
    tmp2 = tmp->next;
    clear_one_board(tmp);
  }
}

void clear_one_board(struct board_info *tmp) {
  struct board_msg *m1, *m2;
  struct board_memory *mem1, *mem2;
  int i;

  /* before we clear this board, we need to disconnect anyone writing/etc to it
   */
  /* xapxapxap take care of this later */

  /* clear the messages */
  for (m1 = BOARD_MESSAGES(tmp); m1; m1 = m2) {
    m2 = m1->next;
    free(m1->subject);
    free(m1->data);
    free(m1);
  }

  /* clear the memory */
  for (i = 0; i < 301; i++) {
    for (mem1 = BOARD_MEMORY(tmp, i); mem1; mem1 = mem2) {
      mem2 = mem1->next;
      free(mem1);
    }
  }
  free(tmp);
  tmp = NULL;
}

void show_board(int board_vnum, struct char_data *ch) {
  struct board_info *thisboard;
  struct board_msg *message;
  char *tmstr;
  int msgcount = 0, yesno = 0;
  char buf[MAX_STRING_LENGTH];
  char name[127];
  /* board locate */
  if (IS_NPC(ch)) {
    send_to_char(
        ch, "Gosh.. now .. if only mobs could read.. you'd be doing good.\r\n");
    return;
  }
  thisboard = locate_board(board_vnum);

  if (!thisboard) {
    log("Creating new board - board #%d", board_vnum);
    thisboard = create_new_board(board_vnum);
    thisboard->next = bboards;
    bboards = thisboard;
  }
  if (GET_LEVEL(ch) < READ_LVL(thisboard)) {
    send_to_char(ch, "You try but fail to understand the holy words.\r\n");
    return;
  }

  /* send the standard board boilerplate */

  sprintf(
      buf,
      "This is a bulletin board.\r\n"
      "Usage:READ/REMOVE <messg #>, RESPOND <messg #>, WRITE <header>.\r\n");
  if (!BOARD_MNUM(thisboard) || !BOARD_MESSAGES(thisboard)) {
    strcat(buf, "The board is empty.\r\n");
    send_to_char(ch, buf);
    return;
  } else {
    sprintf(buf, "%sThere %s %d %s on the board.\r\n", buf,
            (BOARD_MNUM(thisboard) == 1) ? "is" : "are", BOARD_MNUM(thisboard),
            (BOARD_MNUM(thisboard) == 1) ? "message" : "messages");
  }

  message = BOARD_MESSAGES(thisboard);

  //  if (PRF_FLAGGED(ch, PRF_VIEWORDER)) {
  //    while (MESG_NEXT(message)) {
  //      message = MESG_NEXT(message);
  //    }
  //  }

  while (message) {
    tmstr = (char *)asctime(localtime(&MESG_TIMESTAMP(message)));
    *(tmstr + strlen(tmstr) - 1) = '\0';
    yesno = mesglookup(message, ch, thisboard);

    sprintf(name, "%s", get_name_by_id(MESG_POSTER(message)));

    sprintf(buf + strlen(buf), "[%s] (%2d) : %6.10s (%-10s) :: %s \r\n",
            yesno ? "x" : " ", ++msgcount, tmstr, CAP(name),
            MESG_SUBJECT(message) ? MESG_SUBJECT(message) : "No Subject");

    //    if (PRF_FLAGGED(ch, PRF_VIEWORDER)) {
    //    message = MESG_PREV(message);
    //    } else {
    message = MESG_NEXT(message);
    //  }
  }

  page_string(ch->desc, buf, 1);
  return;
}

void board_display_msg(int board_vnum, struct char_data *ch, int arg) {
  struct board_info *thisboard = bboards;
  struct board_msg *message;
  char *tmstr;
  int msgcount, mem, sflag;
  char name[127];
  struct board_memory *mboard_type, *list;
  char buf[MAX_STRING_LENGTH + 1];

  if (IS_NPC(ch)) {
    send_to_char(ch, "Silly mob - reading is for pcs!\r\n");
    return;
  }

  /* guess we'll have to locate the board now in the list */

  thisboard = locate_board(board_vnum);

  if (!thisboard) {
    log("Creating new board - board #%d", board_vnum);
    thisboard = create_new_board(board_vnum);
  }

  if (GET_LEVEL(ch) < READ_LVL(thisboard)) {
    send_to_char(ch, "You try but fail to understand the holy words.\r\n");
    return;
  }

  if (!BOARD_MESSAGES(thisboard)) {
    send_to_char(ch, "The board is empty!\r\n");
    return;
  }

  /* now we locate the message.*/
  message = BOARD_MESSAGES(thisboard);
  if (arg < 1) {
    send_to_char(ch, "You must specify the (positive) number of the message to "
                     "be read!\r\n");
    return;
  }

  for (msgcount = arg; message && msgcount != 1; msgcount--) {
    message = MESG_NEXT(message);
  }

  if (!message) {
    send_to_char(ch, "That message exists only in your imagination.\r\n");
    return;
  }
  /* Have message, let's add the fact that this player read the mesg */

  mem = ((MESG_TIMESTAMP(message) % 301 + MESG_POSTER(message) % 301) % 301);

  /*make the new node */
  CREATE(mboard_type, struct board_memory, 1);
  MEMORY_READER(mboard_type) = GET_IDNUM(ch);
  MEMORY_TIMESTAMP(mboard_type) = MESG_TIMESTAMP(message);
  MEMORY_NEXT(mboard_type) = NULL;

  /* Let's make sure that we don't already have this memory recorded */

  list = BOARD_MEMORY(thisboard, mem);
  sflag = 1;
  while (list && sflag) {
    if (MEMORY_READER(list) == MEMORY_READER(mboard_type) &&
        MEMORY_TIMESTAMP(list) == MEMORY_TIMESTAMP(mboard_type)) {
      /* nope, slot, reader, and timestamp equal, so already saved */
      sflag = 0;
    }
    list = MEMORY_NEXT(list);
  }

  if (sflag) {
    list = BOARD_MEMORY(thisboard, mem);
    BOARD_MEMORY(thisboard, mem) = mboard_type;
    MEMORY_NEXT(mboard_type) = list;
  } else {
    if (mboard_type) {
      free(mboard_type);
    }
  }

  /* before we print out the message, we may as well restore a human
                                  readable timestamp. */
  tmstr = (char *)asctime(localtime(&MESG_TIMESTAMP(message)));
  *(tmstr + strlen(tmstr) - 1) = '\0';
  sprintf(name, "%s", get_name_by_id(MESG_POSTER(message)));
  sprintf(buf, "Message %d : %6.10s (%s) :: %s\r\n\r\n%s\r\n", arg, tmstr,
          CAP(name),
          MESG_SUBJECT(message) ? MESG_SUBJECT(message) : "No Subject",
          MESG_DATA(message) ? MESG_DATA(message)
                             : "Looks like this message is empty.");
  page_string(ch->desc, buf, 1);

  /* really it's not so important to save after each view even if something WAS
   * updated */
  /* might be better to just save them with zone resets? */

  /* for now if sflag is triggered, we know that we added new memory */

  if (sflag) {
    save_board(thisboard);
  }
  return;
}

int mesglookup(struct board_msg *message, struct char_data *ch,
               struct board_info *board) {
  int mem;
  struct board_memory *mboard_type;

  mem = ((MESG_TIMESTAMP(message) % 301 + MESG_POSTER(message) % 301) % 301);

  /* now, we check the mem slot. If its null, we return no, er.. 0..
     if its full, we double check against the timestamp and reader -
     mislabled as poster, but who cares... if they're not true, we
     go to the linked next slot, and repeat */

  mboard_type = BOARD_MEMORY(board, mem);
  while (mboard_type) {

    if (MEMORY_READER(mboard_type) == GET_IDNUM(ch) &&
        MEMORY_TIMESTAMP(mboard_type) == MESG_TIMESTAMP(message)) {
      return 1;
    } else {
      mboard_type = MEMORY_NEXT(mboard_type);
    }
  }
  return 0;
}

void write_board_message(int board_vnum, struct char_data *ch, char *arg) {
  struct board_info *thisboard = bboards;
  struct board_msg *message;

  if (IS_NPC(ch)) {
    send_to_char(ch,
                 "Orwellian police thwart your attempt at free speech.\r\n");
    return;
  }

  thisboard = locate_board(board_vnum);

  if (!thisboard) {
    send_to_char(ch, "Error: Your board could not be found. Please report.\n");
    log("Error in write_board_msg - board #%d", board_vnum);
    return;
  }

  if (GET_LEVEL(ch) < WRITE_LVL(thisboard)) {
    send_to_char(ch, "You are not holy enough to write on this board.\r\n");
    return;
  }

  if (!*arg || !arg) {
    sprintf(arg, "No Subject");
  }

  skip_spaces(&arg);
  delete_doubledollar(arg);
  arg[81] = '\0';

  CREATE(message, struct board_msg, 1);
  MESG_POSTER(message) = GET_IDNUM(ch);
  MESG_TIMESTAMP(message) = time(0);
  MESG_SUBJECT(message) = strdup(arg);
  MESG_NEXT(message) = NULL;
  MESG_DATA(message) = NULL;
  BOARD_MNUM(thisboard) = MAX(BOARD_MNUM(thisboard) + 1, 1);

  MESG_NEXT(message) = BOARD_MESSAGES(thisboard);
  if (BOARD_MESSAGES(thisboard)) {
    MESG_PREV(BOARD_MESSAGES(thisboard)) = message;
  }
  BOARD_MESSAGES(thisboard) = message;

  send_to_char(ch, "Write your message.  (/s saves /h for help)\r\n");
  act("$n starts to write a message.", TRUE, ch, 0, 0, TO_ROOM);

  SET_BIT_AR(PLR_FLAGS(ch), PLR_WRITING);
  string_write(ch->desc, &(MESG_DATA(message)), MAX_MESSAGE_LENGTH,
               board_vnum + BOARD_MAGIC, NULL);
  return;
}

void board_respond(int board_vnum, struct char_data *ch, int mnum) {
  struct board_info *thisboard = bboards;
  struct board_msg *message, *other;
  char number[MAX_STRING_LENGTH], buf[MAX_STRING_LENGTH];
  int gcount = 0;

  thisboard = locate_board(board_vnum);

  if (!thisboard) {
    send_to_char(ch, "Error: Your board could not be found. Please report.\n");
    log("Error in board_respond - board #%d", board_vnum);
    return;
  }

  if (GET_LEVEL(ch) < WRITE_LVL(thisboard)) {
    send_to_char(ch, "You are not holy enough to write on this board.\r\n");
    return;
  }

  if (GET_LEVEL(ch) < READ_LVL(thisboard)) {
    send_to_char(ch, "You are not holy enough to respond to this board.\r\n");
    return;
  }

  if (mnum < 0 || mnum > BOARD_MNUM(thisboard)) {
    send_to_char(ch, "You can only respond to an actual message.\r\n");
    return;
  }
  other = BOARD_MESSAGES(thisboard);

  /*locate message to be repsponded to */

  for (gcount = 0; other && gcount != (mnum - 1); gcount++)
    other = MESG_NEXT(other);

  CREATE(message, struct board_msg, 1);
  MESG_POSTER(message) = GET_IDNUM(ch);
  MESG_TIMESTAMP(message) = time(0);
  sprintf(buf, "Re: %s", MESG_SUBJECT(other));
  MESG_SUBJECT(message) = strdup(buf);
  MESG_NEXT(message) = MESG_PREV(message) = NULL;
  MESG_DATA(message) = NULL;
  BOARD_MNUM(thisboard) = BOARD_MNUM(thisboard) + 1;

  MESG_NEXT(message) = BOARD_MESSAGES(thisboard);
  if (BOARD_MESSAGES(thisboard)) {
    MESG_PREV(BOARD_MESSAGES(thisboard)) = message;
  }
  BOARD_MESSAGES(thisboard) = message;

  send_to_char(ch, "Write your message.  (/s saves /h for help)\r\n\r\n");
  act("$n starts to write a message.", TRUE, ch, 0, 0, TO_ROOM);

  if (!IS_NPC(ch)) {
    SET_BIT_AR(PLR_FLAGS(ch), PLR_WRITING);
  }

  /* don't need number anymore, so we'll reuse it. */
  sprintf(
      number,
      "\t------- Quoted message -------\r\n%s\t------- End Quote -------\r\n",
      MESG_DATA(other));

  MESG_DATA(message) = strdup(number);
  ch->desc->backstr = strdup(number);
  write_to_output(ch->desc, number);

  string_write(ch->desc, &(MESG_DATA(message)), MAX_MESSAGE_LENGTH,
               board_vnum + BOARD_MAGIC, NULL);
  return;
}

struct board_info *locate_board(int board_vnum) {
  struct board_info *thisboard = bboards;

  while (thisboard) {
    if (BOARD_VNUM(thisboard) == board_vnum) {
      return thisboard;
    }
    thisboard = BOARD_NEXT(thisboard);
  }
  return NULL;
}

void remove_board_msg(int board_vnum, struct char_data *ch, int arg) {
  struct board_info *thisboard;
  struct board_msg *cur;
  struct descriptor_data *d;
  int msgcount;
  char buf[MAX_STRING_LENGTH + 1];

  if (IS_NPC(ch)) {
    send_to_char(
        ch, "Nuts.. looks like you forgot your eraser back in mobland...\r\n");
    return;
  }

  thisboard = locate_board(board_vnum);

  if (!thisboard) {
    send_to_char(ch, "Error: Your board could not be found. Please report.\n");
    log("Error in Board_remove_msg - board #%d", board_vnum);
    return;
  }

  cur = BOARD_MESSAGES(thisboard);

  if (arg < 1) {
    send_to_char(ch, "You must specify the (positive) number of the message to "
                     "be read!\r\n");
    return;
  }

  //  if (PRF_FLAGGED(ch, PRF_VIEWORDER)) {
  //    arg = BOARD_MNUM(thisboard) - arg + 1;
  //  }
  for (msgcount = arg; cur && msgcount != 1; msgcount--) {
    cur = MESG_NEXT(cur);
  }

  if (!cur) {
    send_to_char(ch, "That message exists only in your imagination.\r\n");
    return;
  }

  if (GET_IDNUM(ch) != MESG_POSTER(cur) &&
      GET_LEVEL(ch) < REMOVE_LVL(thisboard)) {
    send_to_char(
        ch, "You are not holy enough to remove other people's messages.\r\n");
    return;
  }

  /* perform check for mesg in creation */

  for (d = descriptor_list; d; d = d->next) {
    if (!d->connected && d->str == &(MESG_DATA(cur))) {
      send_to_char(
          ch,
          "At least wait until the author is finished before removing it!\r\n");
      return;
    }
  }

  /* everything else is peachy, kill the message */

  if (BOARD_MESSAGES(thisboard) == cur) {
    if (MESG_NEXT(cur) != NULL) {
      BOARD_MESSAGES(thisboard) = MESG_NEXT(cur);
    } else {
      BOARD_MESSAGES(thisboard) = NULL;
    }
  }

  /* due to the way these things work, these will never dereference nulls
     because the unknown is based on cur */

  if (MESG_PREV(cur)) {
    MESG_NEXT(MESG_PREV(cur)) = MESG_NEXT(cur);
  }

  if (MESG_NEXT(cur)) {
    MESG_PREV(MESG_NEXT(cur)) = MESG_PREV(cur);
  }
  free(cur);
  cur = NULL;
  BOARD_MNUM(thisboard) = BOARD_MNUM(thisboard) - 1;
  send_to_char(ch, "Message removed.\r\n");
  sprintf(buf, "$n just removed message %d.", arg);
  act(buf, FALSE, ch, 0, 0, TO_ROOM);
  save_board(thisboard);
  return;
}

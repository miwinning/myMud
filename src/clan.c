/*
 clan.c
 wintermute mud
 circlemud 3.0bpl12
 ascii pfile system
 taerin and riverwind 5/29/99

 Adapted for tbamud (and changed somewhat) by Welcor@2018-01-13
 */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "interpreter.h"
#include "handler.h"
#include "screen.h"
#include "clan.h"

extern struct descriptor_data *descriptor_list;

/** Matches CLAN_RANK_s */
const char *clan_rank_default_names[] = {"",       "Initiate", "Apprentice",
                                         "Member", "Master",   "Leader"};

/* must match array above */
#define CLAN_RANK_NON_MEMBER 0
#define CLAN_RANK_INITIATE 1
#define CLAN_RANK_APPRENTICE 2
#define CLAN_RANK_MEMBER 3
#define CLAN_RANK_MASTER 4
#define CLAN_RANK_LEADER 5

#define NUM_CLAN_RANKS 6

/* Current members of the clan */
struct clan_member_data {
  char *name;
  int rank;
  struct clan_member_data *next;
};

struct clan_data {
  char *name;
  char *rank_names[NUM_CLAN_RANKS];
  struct clan_member_data *members;
  struct clan_data *next;
};

/*
 * The clans are arranged in a linked list, with clan members as
 * linked sublists.
 */
struct clan_data *clan_list;

/* saved clan data file */
#define CLAN_FILE LIB_ETC "clans"

/* minimum level to create or disband clans*/
#define LVL_CREATE_CLAN LVL_GRGOD

#define GET_CLAN(ch) ((ch)->player_specials->clan)

/* arbitrary length limit to avoid display issues */
#define MAX_CLAN_RANK_LENGTH 20
#define MAX_CLAN_NAME_LENGTH 20

#define CLAN_FUNC(x) static void(x)(struct char_data * ch, char *arg)

/* ------- Locally defined functions --------- */
CLAN_FUNC(perform_clan_create);
CLAN_FUNC(perform_clan_demote);
CLAN_FUNC(perform_clan_disband);
CLAN_FUNC(perform_clan_dismiss);
CLAN_FUNC(perform_clan_enlist);
CLAN_FUNC(perform_clan_info);
CLAN_FUNC(perform_clan_leave);
CLAN_FUNC(perform_clan_list);
CLAN_FUNC(perform_clan_promote);
CLAN_FUNC(perform_clan_rank);
CLAN_FUNC(perform_clan_rename);
CLAN_FUNC(perform_clan_tell);
CLAN_FUNC(perform_clan_who);

static struct clan_data *get_clan_by_name(char *name);
static struct clan_member_data *find_clan_member(struct clan_data *clan,
                                                 char *name);
static char *get_rank_name(int rank, struct clan_data *clan);
static void free_clan(struct clan_data *clan);
static void save_clans();
static int get_clan_rank(struct char_data *ch);
static void remove_member(struct clan_data *clan,
                          struct clan_member_data *member);

/* ------- clan command entry point --------- */
struct clan_command_info {
  const char *command;
  void (*command_pointer)(struct char_data *ch, char *argument);
  sh_int minimum_level;
  int min_clan_rank;
};

/*
 * Checked in order - so "clan d" means "clan demote" if demote is the first
 * with d.
 */
cpp_extern const struct clan_command_info clan_cmd_info[] = {
    {"create", perform_clan_create, LVL_CREATE_CLAN, CLAN_RANK_NON_MEMBER},
    {"enlist", perform_clan_enlist, 0, CLAN_RANK_MASTER},
    {"demote", perform_clan_demote, 0, CLAN_RANK_MASTER},
    {"dismiss", perform_clan_dismiss, 0, CLAN_RANK_MASTER},
    {"disband", perform_clan_disband, LVL_CREATE_CLAN, CLAN_RANK_NON_MEMBER},
    {"info", perform_clan_info, 0, CLAN_RANK_NON_MEMBER},
    {"list", perform_clan_list, 0, CLAN_RANK_NON_MEMBER},
    {"leave", perform_clan_leave, 0, CLAN_RANK_INITIATE},
    {"promote", perform_clan_promote, 0, CLAN_RANK_MASTER},
    {"rank", perform_clan_rank, 0, CLAN_RANK_LEADER},
    {"rename", perform_clan_rename, 0, CLAN_RANK_LEADER},
    {"tell", perform_clan_tell, 0, CLAN_RANK_INITIATE},
    {"who", perform_clan_who, 0, CLAN_RANK_INITIATE},
    {NULL, NULL, 0, CLAN_RANK_NON_MEMBER}};
/*
 * Lists the commands a player may currently use.
 * So members see "tell" and "leave", for example.
 */
static void show_usage(struct char_data *ch) {
  int written = 0, i;
  send_to_char(ch, "Usage: clan <");
  for (i = 0; clan_cmd_info[i].command != NULL; i++) {
    if (GET_LEVEL(ch) < clan_cmd_info[i].minimum_level)
      continue;

    if (!GET_CLAN(ch) && clan_cmd_info[i].min_clan_rank > CLAN_RANK_NON_MEMBER)
      continue;

    if (get_clan_rank(ch) < clan_cmd_info[i].min_clan_rank)
      continue;

    if (written != 0) {
      send_to_char(ch, " | ");
    }
    written++;
    send_to_char(ch, "%s", clan_cmd_info[i].command);
  }

  send_to_char(ch, ">\r\n");
}

/*
 * Main entry point for dealing with clans.
 * Validates the subcommand against the requirements.
 */
ACMD(do_clan) {
  int i;
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];

  half_chop(argument, arg1, arg2);

  if (!*arg1) {
    show_usage(ch);
    return;
  }

  for (i = 0; clan_cmd_info[i].command != NULL; i++) {
    if (is_abbrev(arg1, clan_cmd_info[i].command)) {
      if (GET_LEVEL(ch) < clan_cmd_info[i].minimum_level) {
        send_to_char(ch, "Huh!?");
        return;
      }
      if (!GET_CLAN(ch) &&
          clan_cmd_info[i].min_clan_rank > CLAN_RANK_NON_MEMBER) {
        send_to_char(ch, "Only members of a clan can do that.");
        return;
      }
      if (get_clan_rank(ch) < clan_cmd_info[i].min_clan_rank) {
        send_to_char(ch, "Only a clan leader can do that.");
        return;
      }
      (clan_cmd_info[i].command_pointer)(ch, arg2);
      return;
    }
  }

  show_usage(ch);
}
/*
 * Utility wrapper - makes it simpler to call clan tell directly from
 * interpreter.c
 */
ACMD(do_clan_tell) { perform_clan_tell(ch, argument); }

/* ------- Clan command sub-command handlers --------- */

CLAN_FUNC(perform_clan_create) {
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];
  struct char_data *victim;
  int i;
  struct clan_data *clan = clan_list;

  half_chop(arg, arg1, arg2);

  if (!*arg2 || !(victim = get_char_vis(ch, arg1, NULL, FIND_CHAR_WORLD))) {
    send_to_char(ch, "Usage: clan create <player> <clan name>\r\n");
    return;
  }
  if (strlen(arg2) > MAX_CLAN_NAME_LENGTH) {
    send_to_char(ch, "Name too long!\r\n");
    return;
  }

  if (GET_CLAN(victim)) {
    send_to_char(ch, "They are already in a clan.\r\n");
    return;
  }
  clan = get_clan_by_name(arg2);
  if (clan) {
    send_to_char(ch, "A clan by that name already exists.\r\n");
    return;
  }

  log("CLAN: %s created clan %s", GET_NAME(ch), arg2);

  CREATE(clan, struct clan_data, 1);
  clan->name = strdup(arg2);

  for (i = 0; i < NUM_CLAN_RANKS; i++)
    clan->rank_names[i] = (char *)clan_rank_default_names[i];

  CREATE(clan->members, struct clan_member_data, 1);
  clan->members->name = strdup(GET_NAME(victim));
  clan->members->rank = CLAN_RANK_LEADER;

  GET_CLAN(victim) = clan;

  clan->next = clan_list;
  clan_list = clan;
  save_clans();
  send_to_char(ch, "Ok.\r\n");
  send_to_char(victim,
               "Congratulations.\r\n\r\nYou are now in charge of the clan "
               "%s.\r\nSee 'help clan' for more...",
               arg2);
}

CLAN_FUNC(perform_clan_demote) {
  char lbuf[1024];
  struct clan_data *clan = GET_CLAN(ch);
  struct clan_member_data *member = find_clan_member(clan, arg);

  if (!member) {
    send_to_char(ch, "They are not members of your clan!\r\n");
    log("TEMP: [%s]", arg);
    return;
  }
  if (member->rank == CLAN_RANK_INITIATE || member->rank > get_clan_rank(ch)) {
    send_to_char(ch, "You can not demote this person any further.\r\n");
    return;
  }

  member->rank--;

  log("CLAN: %s demoted %s to rank %d in %s", GET_NAME(ch), member->name,
      member->rank, clan->name);

  save_clans();

  snprintf(lbuf, sizeof(lbuf), "%s%s has been demoted in rank to %s!%s",
           CCRED(ch, C_NRM), member->name, clan->rank_names[member->rank],
           CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);
}

CLAN_FUNC(perform_clan_disband) {
  struct clan_data *clan, *temp;
  struct descriptor_data *d = descriptor_list;

  if (!*arg) {
    send_to_char(ch, "Usage: clan disband <name>\r\n");
    return;
  }
  clan = get_clan_by_name(arg);
  if (!clan) {
    send_to_char(ch, "Usage: clan disband <name>\r\n");
    return;
  }

  log("CLAN: %s disbanded clan %s", GET_NAME(ch), clan->name);

  while (d) {
    if (d->character && GET_CLAN(d->character) == clan) {
      if (STATE(d) == CON_PLAYING)
        send_to_char(d->character, "Your clan has been disbanded!");

      GET_CLAN(d->character) = NULL;
    }
    d = d->next;
  }

  REMOVE_FROM_LIST(clan, clan_list, next);
  free_clan(clan);

  save_clans();
  send_to_char(ch, "Ok.\r\n");
}

CLAN_FUNC(perform_clan_dismiss) {
  char lbuf[1024];
  struct char_data *victim;
  struct clan_data *clan = GET_CLAN(ch);
  struct clan_member_data *member;

  if (!(victim = get_char_room_vis(ch, arg, NULL))) {
    send_to_char(ch, "That person is not available.\r\n");
    return;
  }

  if (GET_CLAN(victim) != GET_CLAN(ch)) {
    send_to_char(ch, "They are not in your clan!\r\n");
    return;
  }

  member = find_clan_member(clan, GET_NAME(victim));
  if (!member) {
    send_to_char(ch, "They are not in your clan!\r\n");
    return;
  }

  if (member->rank >= get_clan_rank(ch)) {
    send_to_char(ch, "Nice try!");
    return;
  }

  log("CLAN: %s dismissed %s from %s", GET_NAME(ch), member->name, clan->name);
  remove_member(clan, member);
  GET_CLAN(victim) = NULL;
  save_clans();

  snprintf(lbuf, sizeof(lbuf), "%s%s has been dismissed from the clan!%s",
           CCRED(ch, C_NRM), GET_NAME(victim), CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);

  snprintf(lbuf, sizeof(lbuf), "%s$n has dismissed you from your guild.%s",
           CCRED(victim, C_NRM), CCNRM(victim, C_NRM));
  act(lbuf, FALSE, ch, 0, victim, TO_VICT | TO_SLEEP);
}

CLAN_FUNC(perform_clan_enlist) {
  char lbuf[1024];
  struct char_data *victim;
  struct clan_data *clan = GET_CLAN(ch);
  struct clan_member_data *member;

  if (!(victim = get_char_room_vis(ch, arg, NULL))) {
    send_to_char(ch, "You don't see that person anywhere.\r\n");
    return;
  }

  if (GET_CLAN(victim)) {
    send_to_char(ch, "That person already belongs to a clan..\r\n");
    return;
  }

  if (GET_LEVEL(victim) >= LVL_IMMORT) {
    send_to_char(ch, "Well, aren't YOU funny\r\n");
    return;
  }

  log("CLAN: %s enlisting %s to %s", GET_NAME(ch), GET_NAME(victim),
      clan->name);

  CREATE(member, struct clan_member_data, 1);
  member->name = strdup(GET_NAME(victim));
  member->rank = CLAN_RANK_INITIATE;
  member->next = clan->members;
  clan->members = member;

  GET_CLAN(victim) = clan;

  save_clans();

  snprintf(lbuf, sizeof(lbuf), "%sWelcome the newest initiate of %s, %s!%s",
           CCRED(ch, C_NRM), clan->name, GET_NAME(victim), CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);
}

CLAN_FUNC(perform_clan_info) {
  struct clan_data *clan = NULL;
  struct clan_member_data *member;
  int i;

  if (*arg)
    clan = get_clan_by_name(arg);
  else if (GET_CLAN(ch))
    clan = GET_CLAN(ch);

  if (!clan) {
    send_to_char(ch, "Info about which clan?\r\n");
    return;
  }
  send_to_char(ch,
               "  %s%s%s\r\n-----------------------\r\n Current "
               "members:\r\n-----------------------\r\n",
               CCCYN(ch, C_NRM), clan->name, CCNRM(ch, C_NRM));

  member = clan->members;
  while (member) {
    send_to_char(ch, "[%-15s] %s\r\n", clan->rank_names[member->rank],
                 member->name);
    member = member->next;
  }

  send_to_char(ch, "-----------------------\r\n Current rank "
                   "names:\r\n-----------------------\r\n");
  for (i = NUM_CLAN_RANKS - 1; i > CLAN_RANK_NON_MEMBER; --i)
    send_to_char(ch, "%d. %s\r\n", i, clan->rank_names[i]);
}

CLAN_FUNC(perform_clan_leave) {
  char lbuf[1024];
  struct clan_data *clan = GET_CLAN(ch), *temp;
  struct clan_member_data *member, *it;

  int leadercount = 0, max_rank = 0;

  member = find_clan_member(clan, GET_NAME(ch));

  snprintf(lbuf, sizeof(lbuf), "%s%s has left the clan!%s", CCRED(ch, C_NRM),
           GET_NAME(ch), CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);

  log("CLAN: %s left clan [%s]", GET_NAME(ch), GET_CLAN(ch)->name);

  if (member->rank == CLAN_RANK_LEADER) {
    it = clan->members;
    while (it) {
      if (it->rank == CLAN_RANK_LEADER)
        leadercount++;
      it = it->next;
    }
    // if the only leader left
    if (leadercount < 2) {
      if (clan->members == member && member->next == NULL) {
        // last member left - disband it.
        log("CLAN: %s left %s as last member. Disbanding it.", GET_NAME(ch),
            clan->name);
        GET_CLAN(ch) = NULL;
        REMOVE_FROM_LIST(clan, clan_list, next);
        free_clan(clan);

        save_clans();
        return;
      }

      // determine highest rank
      it = clan->members;
      while (it) {
        if (it->rank != CLAN_RANK_LEADER && it->rank > max_rank)
          max_rank = it->rank;
        it = it->next;
      }
      // promote one of the highest ranked members to new leader
      it = clan->members;
      while (it) {
        if (it->rank == max_rank)
          break;
        it = it->next;
      }
      snprintf(lbuf, sizeof(lbuf), "%s%s is the new clan leader!%s",
               CCRED(ch, C_NRM), it->name, CCNRM(ch, C_NRM));
      perform_clan_tell(ch, lbuf);

      log("CLAN: due to %s leaving, %s has been appointed new leader of %s",
          GET_NAME(ch), it->name, clan->name);

      it->rank = CLAN_RANK_LEADER;
    }
  }
  remove_member(clan, member);
  GET_CLAN(ch) = NULL;

  save_clans();
}

CLAN_FUNC(perform_clan_list) {
  int count = 0, member_count;
  struct clan_data *clan = clan_list;
  struct clan_member_data *member;

  send_to_char(ch, "Current clans:\r\n");
  if (!clan) {
    send_to_char(ch, "  None!\r\n");
    return;
  }

  while (clan) {
    count++;
    member_count = 0;
    member = clan->members;
    while (member) {
      member_count++;
      member = member->next;
    }
    send_to_char(ch, "  %-4d %s\r\n", member_count, clan->name);
    clan = clan->next;
  }

  send_to_char(ch, "    %d clans in list.\r\n", member_count);
}

CLAN_FUNC(perform_clan_promote) {
  char lbuf[1024];
  struct clan_data *clan = GET_CLAN(ch);
  struct clan_member_data *member = find_clan_member(clan, arg);

  if (!member) {
    send_to_char(ch, "They are not members of your clan!\r\n");
    log("TEMP: [%s]", arg);
    return;
  }
  if (member->rank == CLAN_RANK_LEADER || member->rank >= get_clan_rank(ch)) {
    send_to_char(ch, "You can not promote this person any further.\r\n");
    return;
  }

  member->rank++;

  log("CLAN: %s promoted %s to rank %d in %s", GET_NAME(ch), member->name,
      member->rank, clan->name);

  save_clans();

  snprintf(lbuf, sizeof(lbuf), "%s%s has been promoted in rank to %s!%s",
           CCRED(ch, C_NRM), member->name, clan->rank_names[member->rank],
           CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);
}

CLAN_FUNC(perform_clan_rank) {
  char lbuf[1024];
  struct clan_data *clan = GET_CLAN(ch);
  char arg1[MAX_INPUT_LENGTH];
  char arg2[MAX_INPUT_LENGTH];
  int ranknum;

  half_chop(arg, arg1, arg2);

  if (!*arg1 || !*arg2) {
    send_to_char(ch, "Usage: clan rank <number> <new name>");
    return;
  }
  if (strlen(arg2) > MAX_CLAN_RANK_LENGTH) {
    send_to_char(ch, "Rank name too long");
    return;
  }
  ranknum = atoi(arg1);
  if (ranknum <= CLAN_RANK_NON_MEMBER || ranknum >= NUM_CLAN_RANKS) {
    send_to_char(ch, "Usage: clan rank <number> <new name>");
    return;
  }
  log("CLAN: %s renamed rank %d from %s to %s in clan %s", GET_NAME(ch),
      ranknum, clan->rank_names[ranknum], arg2, clan->name);

  snprintf(lbuf, sizeof(lbuf),
           "%sThe rank %s is dead! Long live the rank %s!%s", CCRED(ch, C_NRM),
           clan->rank_names[ranknum], arg2, CCNRM(ch, C_NRM));
  perform_clan_tell(ch, lbuf);

  if (clan->rank_names[ranknum] != clan_rank_default_names[ranknum])
    free(clan->rank_names[ranknum]);

  clan->rank_names[ranknum] = strdup(arg2);
  save_clans();
}

CLAN_FUNC(perform_clan_rename) {
  struct clan_data *clan = GET_CLAN(ch);

  if (!*arg) {
    send_to_char(ch, "It must have a NAME!?!\r\n");
    return;
  }
  if (get_clan_by_name(arg)) {
    send_to_char(ch, "Already taken!\r\n");
    return;
  }
  if (strlen(arg) > MAX_CLAN_NAME_LENGTH) {
    send_to_char(ch, "Name too long!\r\n");
    return;
  }
  log("CLAN: %s renaming clan [%s] to [%s]", GET_NAME(ch), clan->name, arg);
  free(clan->name);
  clan->name = strdup(arg);

  save_clans();
}

CLAN_FUNC(perform_clan_tell) {
  struct descriptor_data *d;
  char buf[MAX_STRING_LENGTH];

  if (!GET_CLAN(ch)) {
    send_to_char(ch, "You need to be an initiate or higher in a clan.\r\n");
    return;
  }

  for (d = descriptor_list; d; d = d->next) {
    if (d->connected)
      continue;

    if (GET_CLAN(d->character) == GET_CLAN(ch)) {
      *buf = '\0';
      if (ch == d->character)
        send_to_char(d->character, "%sYou tell the clan, '%s'%s\r\n",
                     CCCYN(ch, C_NRM), arg, CCNRM(ch, C_NRM));
      else
        send_to_char(d->character, "%s%s tells the clan, '%s'%s\r\n",
                     CCCYN(ch, C_NRM), GET_NAME(ch), arg, CCNRM(ch, C_NRM));
    }
  }
}

CLAN_FUNC(perform_clan_who) {
  struct descriptor_data *d;

  send_to_char(
      ch, "\r\n%s%s clan members online\r\n====/=====================-%s\r\n",
      CCBLU(ch, C_NRM), GET_CLAN(ch)->name, CCNRM(ch, C_NRM));
  for (d = descriptor_list; d; d = d->next) {
    if (d->connected)
      continue;

    if (GET_CLAN(d->character) == GET_CLAN(ch))
      send_to_char(ch, "%s\r\n", GET_NAME(d->character));
  }
}

/* ------- Functions called externally --------- */

/*
 * For a given player, fetch the clan name. Or an empty string.
 */
char *get_clan_name(struct char_data *ch) {
  if (!GET_CLAN(ch))
    return "";

  return GET_CLAN(ch)->name;
}

/*
 * For a given player, fetch the clan rank name. Or an empty string.
 */
char *get_clan_rank_name(struct char_data *ch) {
  if (!GET_CLAN(ch))
    return "";

  return get_rank_name(get_clan_rank(ch), GET_CLAN(ch));
}

/*
 * If a clan exists with a member with this players name, associate this player
 * with that clan. Or do nothing, if no match is found.
 *
 * Designed to run when the player logs in.
 */
void add_existing_clan(struct char_data *ch) {
  struct clan_data *clan = clan_list;
  while (clan) {
    if (find_clan_member(clan, GET_NAME(ch)))
      break;

    clan = clan->next;
  }
  GET_CLAN(ch) = clan;
}

/*
 * frees all memory used to hold clan data.
 */
void free_clans() {
  struct clan_data *current_clan_data = clan_list, *next_item;
  while (current_clan_data) {
    next_item = current_clan_data->next;
    free_clan(current_clan_data);
    current_clan_data = next_item;
  }
  clan_list = NULL;
}

/*
 * Load the clans from file. Must be called during startup.
 *
 * Note: depends on reading in the same order that is written by save_clans().
 */
void load_clans() {
  FILE *db_file;
  int i;
  char line[MAX_INPUT_LENGTH + 1], tag[6];
  struct clan_data *current_clan_data = NULL;
  struct clan_member_data *current_clan_member = NULL;

  if (!(db_file = fopen(CLAN_FILE, "r"))) {
    log("SYSERR: opening clan file '%s': %s", CLAN_FILE, strerror(errno));
    return;
  }

  while (get_line(db_file, line)) {
    tag_argument(line, tag);
    if (!strcmp(tag, "Name")) {
      CREATE(current_clan_data, struct clan_data, 1);
      current_clan_data->next = clan_list;
      clan_list = current_clan_data;

      for (i = 0; i < NUM_CLAN_RANKS; i++)
        current_clan_data->rank_names[i] = (char *)clan_rank_default_names[i];

      current_clan_data->name = strdup(line);
    } else if (!strcmp(tag, "Ran0"))
      current_clan_data->rank_names[0] = strdup(line);
    else if (!strcmp(tag, "Ran1"))
      current_clan_data->rank_names[1] = strdup(line);
    else if (!strcmp(tag, "Ran2"))
      current_clan_data->rank_names[2] = strdup(line);
    else if (!strcmp(tag, "Ran3"))
      current_clan_data->rank_names[3] = strdup(line);
    else if (!strcmp(tag, "Ran4"))
      current_clan_data->rank_names[4] = strdup(line);
    else if (!strcmp(tag, "Ran5"))
      current_clan_data->rank_names[5] = strdup(line);
    else if (!strcmp(tag, "MNam")) {
      CREATE(current_clan_member, struct clan_member_data, 1);
      current_clan_member->next = current_clan_data->members;
      current_clan_data->members = current_clan_member;

      current_clan_member->name = strdup(line);
    } else if (!strcmp(tag, "MRan"))
      current_clan_member->rank = atoi(line);
  }

  fclose(db_file);
}

/* ------- internal helper functions --------- */

/*
 * Saves the clans to file as tagged ASCII.
 *
 * Note: changes here need to be adapted in load_clans()
 */
static void save_clans() {
  FILE *db_file;
  int i;
  struct clan_data *current_clan_data = clan_list;
  struct clan_member_data *current_clan_member;

  if (!(db_file = fopen(CLAN_FILE, "w"))) {
    log("SYSERR: opening clan file '%s': %s", CLAN_FILE, strerror(errno));
    return;
  }

  while (current_clan_data) {

    fprintf(db_file, "Name: %s\n", current_clan_data->name);
    for (i = 0; i < NUM_CLAN_RANKS; i++)
      if (current_clan_data->rank_names[i] != clan_rank_default_names[i])
        fprintf(db_file, "Ran%d: %s\n", i, current_clan_data->rank_names[i]);

    current_clan_member = current_clan_data->members;
    while (current_clan_member) {
      fprintf(db_file, "MNam: %s\n", current_clan_member->name);
      fprintf(db_file, "MRan: %d\n", current_clan_member->rank);
      current_clan_member = current_clan_member->next;
    }

    current_clan_data = current_clan_data->next;
  }

  fclose(db_file);

  log("clans saved");
}

/*
 * frees memory allocated for a single clan
 */
static void free_clan(struct clan_data *clan) {
  int i;
  struct clan_member_data *member, *next_member;
  free(clan->name);
  for (i = 0; i < NUM_CLAN_RANKS; i++)
    if (clan->rank_names[i] != clan_rank_default_names[i])
      free(clan->rank_names[i]);

  member = clan->members;
  while (member) {
    next_member = member->next;
    free(member->name);
    free(member);
    member = next_member;
  }
  free(clan);
}

/*
 * return the current rank of the player. Or CLAN_RANK_NON_MEMBER if not a
 * member.
 */
static int get_clan_rank(struct char_data *ch) {
  struct clan_member_data *member;
  if (!GET_CLAN(ch))
    return CLAN_RANK_NON_MEMBER;

  member = find_clan_member(GET_CLAN(ch), GET_NAME(ch));
  return member->rank;
}
/*
 * returns the name of a given rank for the given clan
 */
static char *get_rank_name(int rank, struct clan_data *clan) {
  if (!clan || rank >= NUM_CLAN_RANKS)
    return "";

  return clan->rank_names[rank];
}

/*
 * Try to find the clan by case-insensitive search. Or NULL if not found.
 */
static struct clan_data *get_clan_by_name(char *name) {
  struct clan_data *clan = clan_list;

  while (clan) {
    if (!str_cmp(clan->name, name))
      return clan;

    clan = clan->next;
  }
  return NULL;
}
/*
 * In a clan, attempt to find the member with the given name. Or NULL if not
 * found.
 */
static struct clan_member_data *find_clan_member(struct clan_data *clan,
                                                 char *name) {
  struct clan_member_data *member = clan->members;
  while (member) {
    if (!str_cmp(member->name, name))
      break;

    member = member->next;
  }
  return member;
}
/*
 * Removes a member from a clan, freeing the memory
 */
static void remove_member(struct clan_data *clan,
                          struct clan_member_data *member) {
  struct clan_member_data *temp;

  REMOVE_FROM_LIST(member, clan->members, next);
  free(member->name);
  free(member);
}

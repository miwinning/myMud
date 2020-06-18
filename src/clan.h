/*
 * clan.h
 *
 *  Created on: Jan 10, 2018
 *      Author: thomas
 */

#ifndef CLAN_H_
#define CLAN_H_

void load_clans();
void free_clans();

char *get_clan_name(struct char_data *ch);
char *get_clan_rank_name(struct char_data *ch);
void add_existing_clan(struct char_data *ch);

ACMD(do_clan);
ACMD(do_clan_tell);

#endif /* CLAN_H_ */

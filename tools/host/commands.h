#ifndef NJEMU_HOST_COMMANDS_H
#define NJEMU_HOST_COMMANDS_H

int command_compare_frames(int argc, char **argv);
int command_dip_metadata(int argc, char **argv);
int command_font(int argc, char **argv);
int command_game_database(int argc, char **argv);
int command_game_metadata(int argc, char **argv);
int command_rominfo_validate(int argc, char **argv);
int command_validate_cps2_cache(int argc, char **argv);

#endif

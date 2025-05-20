#ifndef SHELL_H
#define SHELL_H

#include "command.h"
#include "single_command.h"

void print_prompt();

extern command_t *g_current_command;
extern single_command_t *g_current_single_command;
extern int g_clear_buffer;
extern char * g_path;
extern int g_return;
extern int g_pid;
#endif

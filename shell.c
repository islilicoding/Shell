#include "shell.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

command_t *g_current_command = NULL;
single_command_t *g_current_single_command = NULL;
char * g_path = "";
bool g_first_time = true;
bool g_source_prev = false;
int g_clear_buffer = 0;
int yyparse(void);
extern int * g_zombies;
extern int g_zombie_num;
extern bool g_source;

/*
 *  Handles ctrl C by setting the clear buffer barrier
 *  and prints prompt
 */

void sig(int s) {
  s = s;
  g_clear_buffer = 1;
  if (isatty(0)) {
    printf("\n");
    print_prompt();
  }
} /* sig() */

/*
 *  Handles zombies by using a while loop to remove
 *  all of them and also prints exited after.
 */

void z_sig(int s) {
  s = s;
  int pid = waitpid(-1, NULL, 0);
  while ((pid = waitpid(-1, NULL, 0)) > 0) {
    /* go through each zombie and print its pid */

    for (int i = 0; i < g_zombie_num; i++) {
      if (pid == g_zombies[i]) {
        printf("[%d] exited.\n", pid);
        g_zombies[i] = 0;
        break;
      }
    }
  }
} /* z_sig() */

/*
 *  Prints shell prompt
 */

void print_prompt() {
  if (isatty(0) && !g_source) {
    printf("myshell>");
    fflush(stdout);
  } else if (g_source) {
    g_source = false;
  }
} /* print_prompt() */


/*
 *  This main is simply an entry point for the program which sets up
 *  memory for the rest of the program and the turns control over to
 *  yyparse and never returns
 */

int main(int argc, char *argv[]) {
  /* set source variable */

  g_source_prev = g_source;
  if (argc > 0) {
    g_path = argv[0];
  }

  /* allocate space for commands and create commands */

  g_current_command = (command_t *)malloc(sizeof(command_t));
  g_current_single_command = (single_command_t *)
    malloc(sizeof(single_command_t));
  create_command(g_current_command);
  create_single_command(g_current_single_command);

  /* handle if ctrl + c is typed */

  struct sigaction sa = {0};
  sa.sa_handler = sig;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa, NULL)) {
    perror("sigaction");
    exit(-1);
  }

  /* Handle zombies */

  struct sigaction z_sa = {0};
  z_sa.sa_handler = z_sig;
  sigemptyset(&z_sa.sa_mask);
  z_sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &z_sa, NULL)) {
    free(g_zombies);
    perror("sigaction");
    exit(-1);
  }
  if (isatty(0)) {
    print_prompt();
    g_source_prev = false;
  }
  yyparse();
} /* main() */

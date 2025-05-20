
#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"

#define EXIT_1  (1)
#define EXIT_0  (0)
#define RETURN  (-1)
#define SUCCESS  (2)

extern char **environ;
int g_return = 0;
int g_p_pid = 0;
int g_pid = 0;
char * g_arg = "";
int g_environ_num = 0;
int g_zombie_num = 0;
int * g_zombies;

/*
 *  Initialize a command_t
 */

void create_command(command_t *command) {
  command->single_commands = NULL;

  command->out_file = NULL;
  command->in_file = NULL;
  command->err_file = NULL;

  command->append_out = false;
  command->append_err = false;
  command->background = false;

  command->num_single_commands = 0;
  g_current_command->num_out_redirect = 0;
  g_current_command->num_in_redirect = 0;
} /* create_command() */

/*
 *  Insert a single command into the list of single commands in a command_t
 */

void insert_single_command(command_t *command, single_command_t *simp) {
  if (simp == NULL) {
    return;
  }

  command->num_single_commands++;
  int new_size = command->num_single_commands * sizeof(single_command_t *);
  command->single_commands = (single_command_t **)
                              realloc(command->single_commands,
                                      new_size);
  command->single_commands[command->num_single_commands - 1] = simp;
} /* insert_single_command() */

/*
 *  Free a command and its contents
 */

void free_command(command_t * command) {
  for (int i = 0; i < command->num_single_commands; i++) {
    free_single_command(command->single_commands[i]);
  }

  if (command->out_file) {
    free(command->out_file);
    command->out_file = NULL;
  }

  if (command->in_file) {
    free(command->in_file);
    command->in_file = NULL;
  }

  if (command->err_file) {
    free(command->err_file);
    command->err_file = NULL;
  }

  command->append_out = false;
  command->append_err = false;
  command->background = false;

  free(command);
} /* free_command() */

/*
 *  Print the contents of the command in a pretty way
 */

void print_command(command_t * command) {
  printf("\n\n");
  printf("              COMMAND TABLE                \n");
  printf("\n");
  printf("  #   single Commands\n");
  printf("  --- ----------------------------------------------------------\n");

  /* iterate over the single commands and print them nicely */

  for (int i = 0; i < command->num_single_commands; i++) {
    printf("  %-3d ", i );
    print_single_command(command->single_commands[i]);
  }

  printf( "\n\n" );
  printf( "  Output       Input        Error        Background\n" );
  printf( "  ------------ ------------ ------------ ------------\n" );
  printf( "  %-12s %-12s %-12s %-12s\n",
            command->out_file?command->out_file:"default",
            command->in_file?command->in_file:"default",
            command->err_file?command->err_file:"default",
            command->background?"YES":"NO");
  printf( "\n\n" );
} /* print_command() */

/*
 *  Check if there are no single commands. Return if so.
 */

bool no_single_commands(command_t * command) {
  if (command->single_commands == NULL) {

    /* set to null and print prompt */

    if(isatty(0)){
      print_prompt();
    }
    command = NULL;
    g_return = 0;
    return false;
  }
  return true;
} /* no_single_commands() */

/*
 *  Exit the shell if "exit" is typed.
 */

bool exit_shell(command_t * command) {
  if(!strcmp(command->single_commands[0]->arguments[0], "exit")) {
    free(g_zombies);
    g_zombies = NULL;
    g_return = 0;
    return false;
  }
  return true;
} /* exit_shell() */

/*
 *  Set the environment if "setenv" is typed.
 */

int set_environ(command_t * command) {
  if (!strcmp(command->single_commands[0]->arguments[0], "setenv")) {
    if (command->single_commands[0]->num_args != 3) {
      fprintf(stderr, "setenv should take two arguments\n");
      g_return = 1;
      return EXIT_0;
    }

    /* set environment and communicate if error */

    int env_error = setenv(command->single_commands[0]->arguments[1],
      command->single_commands[0]->arguments[2], 1);
    if (env_error) {
      fprintf(stderr, "setenv");
      g_return = 1;
      return EXIT_0;
    }
    if(command) {
      free_command(command);
      command = NULL;
    }
    if(isatty(0)) {
      print_prompt();
    }
    return RETURN;
  }
  return SUCCESS;
} /* set_environ() */

/*
 *  Unset the environment variable if "unsetenv" is typed.
 */

bool unset_environ(command_t * command) {
  if (!strcmp(command->single_commands[0]->arguments[0], "unsetenv")) {
    /* unset environment variable and check if there is an error */

    int un_env_error = unsetenv(command->single_commands[0]->arguments[1]);

    if (un_env_error) {
      fprintf(stderr, "unsetenv");
      g_return = 1;
    }
    if(command) {
      free_command(command);
      command = NULL;
    }
    if(isatty(0)) {
      print_prompt();
    }
    return false;
  }
  return true;
} /* unset_env() */

/*
 *  Change the directory if "cd" is typed.
 */

bool change_dir(command_t * command) {
  if (!strcmp(command->single_commands[0]->arguments[0], "cd")) {
    /*get home directory and change it, check if there is an error */

    int error;
    if (command->single_commands[0]->num_args == 1) {
      error = chdir(getenv("HOME"));
    } else {
      error = chdir(command->single_commands[0]->arguments[1]);
    }
    if (error < 0) {
      fprintf(stderr, "cd: can't cd to notfound\n");
      g_return = 1;
    }
    if(command) {
      free_command(command);
      command = NULL;
    }
    if(isatty(0)) {
      print_prompt();
    }
    return false;
  }
  return true;
} /* change_dir() */

/*
 *  Set the input to given file or stdin.
 */

int set_input(command_t * command, int tmpin) {
  int fdin;
  if (command->in_file) {
    /* if in file is provided, set input to it */

    fdin = open(command->in_file, O_RDONLY);
    if (fdin < 0) {
      fprintf(stderr, "/bin/sh: 1: cannot open %s: No such file\n",
        command->in_file);
      g_return = 1;
      if (isatty(0)) {
        print_prompt();
      }
    }
  } else {
    /* Use default input */

    fdin = dup(tmpin);
  }
  return fdin;
} /* set_input() */

/*
 *  Set the error output to a provided file or stderr.
 */

int set_error(command_t * command, int tmperr) {
  int fderr;
  if (command->err_file) {
    /* set output to provided file */

    if (command->append_err == true) {
      fderr = open(command->err_file, O_CREAT|O_WRONLY|O_APPEND, S_IRWXU);
    } else {
      fderr = open(command->err_file, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
    }
  } else {
    /* set output to default */

    fderr = dup(tmperr);
  }
  return fderr;
} /* set_error() */

/*
 *  Set the output to a provided file or stdout.
 */

int set_output(command_t * command, int tmpout) {
  int fdout;
  if (command->out_file){
    /* set to file and specify append / overwrite */

    if (command->append_out == true) {
      fdout = open(command->out_file, O_CREAT|O_WRONLY|O_APPEND, S_IRWXU);
    } else {
      fdout = open(command->out_file, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
    }
  } else {
    /* Use default output */

    fdout = dup(tmpout);
  }
  return fdout;
} /* set_output() */

/*
 *  This waits for the last command or, if background is set,
 *  adds command to global zombie list.
 */

void last_command_or_zombies(command_t * command, int ret) {
  if (!command->background) {
    /* Wait for last command and get exit status of it */

    int num;
    waitpid(ret, &num, 0);
    if (WIFEXITED(num)) {
      g_return = WEXITSTATUS(num);
    }
  } else {
    /* add command to zombie list */

    g_pid = ret;
    if (g_zombies == NULL) {
      g_zombies = (int *)malloc(1 * sizeof(int));
      g_zombies[0] = ret;
      g_zombie_num = 1;
    } else {
      int size = g_zombie_num;
      g_zombies = (int *)realloc(g_zombies, (size + 1) * sizeof(int));
      g_zombies[size] = ret;
      g_zombie_num++;
    }
  }
} /* last_command_or_zombies() */

/*
 *  This checks if there is an ambiguous redirect, and 
 *  reports it if so.
 */

void ambiguous_redirect() {
  if ((g_current_command->num_out_redirect > 1) ||
    (g_current_command->num_in_redirect > 1)) {
    printf("Ambiguous output redirect.\n");
    fflush(stdout);
  }
} /* ambiguous_redirect() */

/*
 *  Handle all the cases for which execution/forking
 *  is not necessary.
 */

int no_execute(command_t * command) {
  /* -1 means return, 1 means exit(1), 0 means exit(0) */

  bool cont = true;

  /* Don't do anything if there are no single commands */

  cont = no_single_commands(command);
  if (!cont) {
    return RETURN;
  }

  /* Exit myshell */

  cont = exit_shell(command);
  if (!cont) {
    return EXIT_0;
  }

  /* Set environment variable environ */

  int env = set_environ(command);
  if (env == EXIT_0) {
    return EXIT_0;
  } else if (env == RETURN) {
    return RETURN;
  }

  /* Unset environmant variable environ */

  cont = unset_environ(command);
  if (!cont) {
    return RETURN;
  }

  /* Change directory */

  cont = change_dir(command);
  if (!cont) {
    return RETURN;
  }
  return SUCCESS;
} /* no_execute() */

/*
 *  Print the environment variables.
 */

bool print_environ(command_t * command, int i) {
  if(!strcmp(command->single_commands[i]->arguments[0], "printenv")) {
    /* print each variable by indexing through environ */

    for (int i = 0; environ[i] != NULL; i++)  {
      printf("%s\n", environ[i]);
    }
    fflush(stdout);
    return false;
  }
  return true;
} /* print_environ() */

/*
 *  Execute a command
 */

void execute_command(command_t * command) {
  int execute = no_execute(command);
  if (execute == EXIT_0) {
    exit(0);
  } else if (execute == EXIT_1) {
    exit(1);
  } else if (execute == RETURN) {
    return;
  }

  /* save input/output fds for later, set input and error */

  int tmpin = dup(0);
  int tmpout = dup(1);
  int tmperr = dup(2);
  int fdin = set_input(command, tmpin);
  int fderr = set_error(command, tmperr);
  dup2(fderr, 2);
  close(fderr);

  int ret;
  int fdout;
  int i;
  for (i = 0; i < command->num_single_commands; i++) {
    dup2(fdin, 0);
    close(fdin);

    /* if last single command, set the global argument */

    if (i == command->num_single_commands - 1){
      g_arg = (char *)malloc(sizeof(char) * (strlen(command->single_commands[i]->
        arguments[command->single_commands[i]->num_args - 1]) + 1));
      g_arg = strdup(command->single_commands[i]->arguments[command->
        single_commands[i]->num_args - 1]);
      fdout = set_output(command, tmpout);
    } else {
      /* Not last single command, create pipe */

      int fdpipe[2];
      pipe(fdpipe);
      fdout = fdpipe[1];
      fdin = fdpipe[0];
    }
    dup2(fdout, 1);
    close(fdout);

    bool cont = print_environ(command, i);
    if (!cont) {
      continue;
    }

    /* Create child process since successful execvp returns nothing */

    ret = fork();
    if (ret == 0) {
        command->single_commands[i]->
            arguments[command->single_commands[i]->num_args] = NULL;
        execvp(command->single_commands[i]->executable,
            command->single_commands[i]->arguments);
        perror("execvp");
        g_return = 2;
        exit(2);
    } else if (ret < 0) {
      perror("fork");
      free(g_zombies);
      free_command(command);
      g_return = 1;
      exit(1);
      return;
    }
  }
  dup2(tmpin, 0);
  dup2(tmpout, 1);
  dup2(tmperr, 2);
  close(tmpin);
  close(tmpout);
  close(tmperr);

  last_command_or_zombies(command, ret);

  ambiguous_redirect();

  if(command) {
    free_command(command);
    command = NULL;
  }

  if(isatty(0)) {
    print_prompt();
  }
} /* execute_command() */

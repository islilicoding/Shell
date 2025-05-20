/*
 * CS-252
 * shell.y: parser for shell
 *
 * This parser compiles the following grammar:
 *
 *      cmd [arg]* [> filename]
 *
 * you must extend it to understand the complete shell grammar
 *
 */

%code requires
{
  #include <stdio.h>
  #include <stdlib.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <string.h>
  #include <stdlib.h>
  #include <dirent.h>
  #include <regex.h>
  #include "command.h"
  #include "shell.h"

}

%union
{
  char * string;
}

%token <string> WORD PIPE
%token TILDE NOTOKEN NEWLINE GREAT LESS AMPERSAND 
%token GREATGREAT GREATAMPERSAND GREATGREATAMPERSAND TWOGREAT

%{


void yyerror(const char * s);
int compare_strings(const void * a, const void * b);
void wildcards(char * argument);
void expand_wildcards(char * prefix, char * argument);
void split_string(const char * input, char * abs_path, char * search);
int yylex();
%}

%%

goal:
  entire_command_list
  ;

entire_command_list:

  /* execute command */

  entire_command_list entire_command {
    execute_command(g_current_command);
    g_current_command = (command_t *)malloc(sizeof(command_t));
    create_command(g_current_command);
  }
  |  entire_command {
    execute_command(g_current_command);
    g_current_command = (command_t *)malloc(sizeof(command_t));
    create_command(g_current_command);
  }
  ;

entire_command:
  single_command_list io_modifier_list background NEWLINE
    | NEWLINE

     /* accept empty command line */

    ;

single_command_list:
  single_command_list PIPE single_command
    |  single_command
    ;

single_command:

  /* insert single command */

  executable argument_list {
    insert_single_command(g_current_command,
      g_current_single_command);
    g_current_single_command = NULL;
  }
  ;

argument_list:
  argument_list argument
    |

    /* can be empty */

    ;

argument:

  /* insert argument (and expand wildcards) */

  WORD {
    if ((strcmp(g_current_single_command->arguments[0], "echo") == 0) &&
        (strchr($1, '?'))) {
      insert_argument(g_current_single_command, $1);
    } else {
      wildcards($1);
    }
  }
  ;

executable:

  /* insert argument */

  WORD {
    g_current_single_command =
      (single_command_t *)malloc(sizeof(single_command_t));
    create_single_command(g_current_single_command);
    insert_argument(g_current_single_command, $1);
    g_current_single_command->executable = $1;
  }
  ;

io_modifier_list:
  io_modifier_list io_modifier
    | io_modifier
    |

    /* can be empty */

    ;

io_modifier:
  GREATGREAT WORD {
    /* redirect output and append */

    g_current_command->out_file = $2;
    g_current_command->append_out = true;
  }
  | GREAT WORD {
    /* redirect output and overwrite*/

    g_current_command->out_file = $2;
    g_current_command->num_out_redirect =
      g_current_command->num_out_redirect + 1;
  }
  | GREATGREATAMPERSAND WORD {
    /* redirect output and error and append */

    g_current_command->out_file = $2;
    g_current_command->err_file = strdup($2);
    g_current_command->append_out = true;
    g_current_command->append_err = true;
  }
  | GREATAMPERSAND WORD {
    /* redirct output and error */

    g_current_command->out_file = $2;
    g_current_command->err_file = strdup($2);
  }
  | LESS WORD {
    /* redirect input and overwrite */

    g_current_command->in_file = $2;
    g_current_command->num_in_redirect =
      g_current_command->num_in_redirect + 1;
  }
  | TWOGREAT WORD {
    /* redirect error */

    g_current_command->err_file = $2;
  }
  ;

background:
  AMPERSAND {
    /* redirect background */

    g_current_command->background = true;
  }
  |

   /* can be empty */

  ;

%%

int g_str_num = 0;
char ** g_strs = NULL;

/*
 * Compares strings for wildcard function
 * to see if one matches a regex.
 */

int compare_strings(const void * a, const void * b) {
  return strcmp(*(const char **)a, *(const char **)b);
} /* compare_strings() */


/*
 * Checks if a string has wildcards in it
 * and expands wildcards if necessary. Otherwise,
 * it inserts the given argument as is.
 */

void wildcards(char * argument) {
  g_strs = (char **)malloc(sizeof(char *));
  g_str_num = 0;
  if ((strchr(argument, '*')) || (strchr(argument, '?'))) {
    /* Expand wildcards and insert sorted arguments */

    expand_wildcards(NULL, argument);
    qsort(g_strs, g_str_num, sizeof(char *), compare_strings);
    for (int i = 0; i < g_str_num; i++) {
      insert_argument(g_current_single_command, g_strs[i]);
    }
  } else {
    /* if no wildcards, insert current command */

    insert_argument(g_current_single_command, argument);
  }
  free(g_strs);
  return;
} /* wildcards() */

/*
 * Compare entries in the directory to the regular expression.
 * If one matches, add it to the list. Also expand wildcards.
 */

void search_entries(char * prefix, char * temp, regex_t regex,
    struct dirent * entry, char * open_directory, char * argument) {
  if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0) {
    /* If there are characters in temp, add onto it */

    if (*temp) {
      if (entry->d_type == DT_DIR) {
        char * new_search = NULL;
        /* Handle different directory cases */

        if (!strcmp(open_directory, ".")) {
          new_search = (char *)malloc(sizeof(char)
            * (strlen(entry->d_name) + 1));
          new_search = strdup(entry->d_name);
        } else if (!strcmp(open_directory, "/")) {
          /* add path to new_search */

          new_search = (char *)malloc(sizeof(char)
            * (strlen(open_directory)
            + strlen(entry->d_name) + 1));
          sprintf(new_search, "%s%s", open_directory, entry->d_name);
        } else {
          /* add path to new_search */

          new_search = (char *)malloc(sizeof(char)
            * (strlen(open_directory)
            + strlen(entry->d_name) + 1));
          sprintf(new_search, "%s/%s", open_directory, entry->d_name);
        }
        /* expand wildcards again if necessary */

        if (*temp == '/') {
          temp++;
          expand_wildcards(new_search, temp);
        }
        if (new_search != NULL) {
          free(new_search);
        }
      }
    } else {
      /* make new argument, add path to argument, and add to strs */

      char * this_argument = (char *) malloc(100);
      this_argument[0] = '\0';
      if (prefix) {
        sprintf(this_argument, "%s/%s", prefix, entry->d_name);
      }
      if (entry->d_name[0] == '.') {
        if (argument[0] == '.') {
          if (this_argument[0] != '\0') {
            g_strs = (char **)realloc(g_strs, sizeof(char *)
              * (g_str_num + 1));
            g_strs[g_str_num++] = strdup(this_argument);
          } else {
            g_strs = (char **)realloc(g_strs, sizeof(char *)
              * (g_str_num + 1));
            g_strs[g_str_num++] = strdup(entry->d_name);
          }
        }
      } else {
        if (this_argument[0] != '\0') {
          g_strs = (char **)realloc(g_strs, sizeof(char *)
            * (g_str_num + 1));
          g_strs[g_str_num++] = strdup(this_argument);
        } else {
          g_strs = (char **)realloc(g_strs, sizeof(char *)
            * (g_str_num + 1));
          g_strs[g_str_num++] = strdup(entry->d_name);
        }
      }
      if (this_argument != NULL) {
        free(this_argument);
      }
    }
  }
} /* search_entries() */

/*
 * Expands wildcards recursively. This is to
 * handle wildcards in paths. Also checks if
 * files in directory match regex expression
 * made from wildcards and given string.
 */

void expand_wildcards(char * prefix, char * argument) {
  /* Split text up into word and directory path */

  char * temp = argument;
  char * search = (char *) malloc(strlen(argument) + 10);
  char * directory = search;
  if (temp[0] == '/') {
    *(search++) = *(temp++);
  }
  while ((*temp != '/') && (*temp)) {
    *(search++) = *(temp++);
  }
  *search = '\0';

  /* check if there is a wildcard in the directory */

  if ((strchr(directory, '*')) || (strchr(directory, '?'))) {
    if ((!prefix) && (argument[0] == '/')) {
      prefix = strdup("/");
      directory++;
    }

    /* form given text, make regex to compare to */

    char * pre_regex = (char *) malloc(2 * strlen(argument) + 10);
    char * p_directory = directory;
    char * p_regex = pre_regex;
    *(p_regex++) = '^';
    while (*p_directory) {
      if (*p_directory == '*') {
        *(p_regex++) = '.';
        *(p_regex++) = '*';
      } else if (*p_directory == '?') {
        *(p_regex++) = '.';
      } else if (*p_directory == '.') {
        *(p_regex++) = '\\';
        *(p_regex++) = '.';
      } else {
        *(p_regex++) = *p_directory;
      }
      p_directory++;
    }
    *(p_regex++) = '$';
    *p_regex = '\0';

    /* compile regex to match words to*/

    regex_t regex;
    int er = regcomp(&regex, pre_regex, REG_EXTENDED | REG_NOSUB);
    if (er != 0) {
      printf("ERROR: %d\n", er);
      perror("regcomp");
    }
    if (pre_regex != NULL) {
      free(pre_regex);
    }

    /* find directory and open it to compare entries to regex */

    char * open_directory = NULL;
    if (prefix) {
      open_directory = strdup(prefix);
    } else {
      open_directory = ".";
    }
    DIR * dir = opendir(open_directory);
    if (dir == NULL) {
      perror("opendir");
      return;
    }
    struct dirent * entry;
    while ((entry = readdir(dir)) != NULL) {
      search_entries(prefix, temp, regex, entry, open_directory, argument);
    }
    closedir(dir);
  } else {
    /* find new prefix in subdirectory, add to existing string */

    char * send_prefix = NULL;
    if (prefix) {
      send_prefix = (char *)malloc(sizeof(char) * (strlen(prefix)
        + strlen(directory) + 1));
      sprintf(send_prefix, "%s/%s", prefix, directory);
    } else {
      send_prefix = (char *)malloc(sizeof(char) * (strlen(directory) + 1));
      send_prefix = strdup(directory);
    }

    /* expand wildcards again if needed */

    if (*temp) {
      expand_wildcards(send_prefix, ++temp);
    }
    if (send_prefix != NULL) {
      free(send_prefix);
      send_prefix = NULL;
    }
  }
} /* expand_wildcards() */


/*
 * Prints string to stderr if there is an error.
 */

void
yyerror(const char * s)
{
  fprintf(stderr, "%s", s);
} /* yyerror() */

#if 0
main()
{
  yyparse();
}
#endif

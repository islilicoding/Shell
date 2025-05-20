/*
 *
 * CS-252
 * shell.l: lexical analyzer for shell
 * You have to extend it.
 *
 * Update more when escaping, etc.
 */

%{

#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "y.tab.h"
#include "shell.h"


extern int g_return;
extern int g_pid;
extern char * g_arg;
extern char ** g_history;
extern int g_history_length;
extern char * read_line();
bool g_source = false;


/*
 *  Gets a character, uses read_line().
 */

int mygetc(FILE * f) {
  /* get a character */

  static char * p = NULL;
  char ch = '\0';
  if (!isatty(0)) {

  /* stdin is not a tty. Call real getc */

    return getc(f);
  }

  /* stdin is a tty. Call our read_line. */

  if ((p == NULL) || (*p == 0)) {
    char * s = read_line();
    p = s;
  }
  ch = *p;
  p++;
  return ch;
} /* mygetc() */

#undef getc
#define getc(f) mygetc(f)


/*
 *  Unputs a character.
 */

static  void yyunput(int c, char * buf_ptr  ); /* yyunput() */


/*
 *  Unputs a charcter.
 */

void myunputc(int c) {
  /* unput a character */

  unput(c);
} /* myunputc() */

%}

%option noyywrap

%%

\n {
  /* newline character */

  return NEWLINE;
}

[ \t] {
  /* Discard spaces and tabs */

}

">" {
  /* STDOUT */

  return GREAT;
}

"<" {
  /* STDIN */

  return LESS;
}

"|" {
  /* pipe */

  return PIPE;
}

"&" {
  /* BACKGROUND */

  return AMPERSAND;
}

">>" {
  /* APPEND */

  return GREATGREAT;
}

">&" {
  /* redirects stdout and error to file, overwrites file */

  return GREATAMPERSAND;
}

">>&" {
  /* redirects error and out to file, appends to file */

  return GREATGREATAMPERSAND;
}

"2>" {
  /* redirects stderr to specified file */

  return TWOGREAT;
}

source[ ][^\n\t]+ {
  /* source */

  char * str = strdup(yytext);
  yyin = fopen(str + 7, "r");
  if (!yyin) {
    perror("fopen");
  }

  /* push lines of file to buffer */

  yypush_buffer_state(yy_create_buffer(yyin, YY_BUF_SIZE));
  BEGIN(INITIAL);
} /* source */

<<EOF>> {
  /* end of file */
  /* get buffer state, terminate if necessary */

  yypop_buffer_state();
  if (!YY_CURRENT_BUFFER) {
    yyterminate();
  }
}


\$\(([^)]+)\) {
  /* subshell */
  /* save in and out file descriptors, format text */

  int tmpin = dup(0);
  int tmpout = dup(1);
  char * str = strdup(yytext);
  str = str + 2;
  str[strlen(str) - 1] = '\0';

  /* create pipes between subshell and parent shell */

  int pipe_in[2];
  int pipe_out[2];
  pipe(pipe_in);
  pipe(pipe_out);
  dup2(pipe_in[0], 0);
  close(pipe_in[0]);
  dup2(pipe_out[1], 1);
  close(pipe_out[1]);
  write(pipe_in[1], str, strlen(str));
  write(pipe_in[1], "\n", 1);
  write(pipe_in[1], "exit", 4);
  write(pipe_in[1], "\n", 1);
  close(pipe_in[1]);

  /* fork for command since execvp returns nothing */

  int ret = fork();
  if (ret == 0) {
    /* child process */

    char ** args = (char **)malloc(sizeof(char *) * 2);
    args[0] = "/proc/self/exe";
    args[1] = NULL;
    execvp(args[0], args);
    perror("execvp");
    free(args);
    args = NULL;
    exit(1);
  } else if (ret < 0) {
    /* fork error */

    perror("fork");
    exit(1);
  } else {
    /* redirect file descriptors, get file lines */

    dup2(tmpin, 0);
    dup2(tmpout, 1);
    close(tmpin);
    close(tmpout);
    char buff = '\0';
    char * buffer = (char *)malloc(sizeof(char) * 4096);
    int n = 0;
    while (read(pipe_out[0], &buff, 1)) {
      if ((buff == '\n') || (buff == '\0')) {
        buffer[n++] = ' ';
      } else {
        buffer[n++] = buff;
      }
    }

    /* unput each char of buffer, backwards */

    buffer[n] = '\0';
    for (n = strlen(buffer); n >= 0; n--) {
      if (buffer[n] != '\0') {
        unput(buffer[n]);
      }
    }
    free(buffer);
    buffer = NULL;
    close(pipe_out[0]);
  }
}

(.*\$\{!!\}[^\n\t ]*)* {
  /* run the previous command, if there is one */

  if (g_history_length - 2 < 0) {
    break;
  }
  printf("%s\n", g_history[g_history_length - 2]);
  fflush(stdout);
  char * str = g_history[g_history_length - 2];

  /* save in and out file descriptors, format text */

  int tmpin = dup(0);
  int tmpout = dup(1);

  /* create pipes between subshell and parent shell */

  int pipe_in[2];
  int pipe_out[2];
  pipe(pipe_in);
  pipe(pipe_out);
  dup2(pipe_in[0], 0);
  close(pipe_in[0]);
  dup2(pipe_out[1], 1);
  close(pipe_out[1]);
  write(pipe_in[1], str, strlen(str));
  write(pipe_in[1], "\n", 1);
  write(pipe_in[1], "exit", 4);
  write(pipe_in[1], "\n", 1);
  close(pipe_in[1]);

  /* fork for command since execvp returns nothing */

  int ret = fork();
  if (ret == 0) {
    /* child process */

    char ** args = (char **)malloc(sizeof(char *) * 2);
    args[0] = "/proc/self/exe";
    args[1] = NULL;
    execvp(args[0], args);
    perror("execvp");
    free(args);
    args = NULL;
    exit(1);
  } else if (ret < 0) {
    /* fork error */

    perror("fork");
    exit(1);
  } else {
    /* redirect file descriptors, get file lines */

    dup2(tmpin, 0);
    dup2(tmpout, 1);
    close(tmpin);
    close(tmpout);
    char buff = '\0';
    char * buffer = (char *)malloc(sizeof(char) * 4096);
    int n = 0;
    while (read(pipe_out[0], &buff, 1)) {
      if ((buff == '\n') || (buff == '\0')) {
        buffer[n++] = ' ';
      } else {
        buffer[n++] = buff;
      }
    }

    /* print buffer */

    buffer[n] = '\0';
    printf("%s\n", buffer);
    free(buffer);
    buffer = NULL;
    close(pipe_out[0]);
  }
}


[ ](.*\$\{[^}]+\}[^\n\t ]*)* {
  /* expand environment variables */
  /* get given text and initialize variables */

  char * ch = strdup(yytext);
  bool unused_env = true;
  bool stop = false;
  char * result = (char *)malloc(sizeof(char) * 1);
  bool env_var = false;
  char * temp = (char *)malloc(sizeof(char) * 2000);
  bool unused_temp = false;
  int j = 1;
  int temp_i = 0;
  char * env = NULL;
  while ((ch[j] != '\0') && (j < strlen(ch))) {
    /* collect environment variable if exists */

    if ((ch[j] == '$') && (ch[j + 1] == '{')) {
      temp_i = 0;
      env_var = true;
    } else if ((env_var) && (ch[j] != '}') && (ch[j] != '{')) {
      temp[temp_i++] = ch[j];
    } else if ((env_var) && (ch[j] == '}')) {
      /* temp = environment variable to find */

      temp[temp_i] = '\0';
      if (temp[0] == '$') {
        /* dollar sign --> get shell process PID */

        unused_env = false;
        int s = snprintf(NULL, 0, "%d", getpid());
        env = (char *)malloc(sizeof(char) * (s + 1));
        sprintf(env, "%d", getpid());
        env[strlen(env)] = '\0';
      } else if ((temp[0] == 'S') && (temp[1] == 'H')
          && (temp[2] == 'E') && (temp[3] == 'L') && (temp[4] == 'L')) {
        /* SHELL --> get path of shell executable */

        char * path = (char *)malloc(sizeof(char) * PATH_MAX);
        realpath(g_path, path);
        env = path;
      } else if (temp[0] == '?') {
        /* ? --> get the return code of the last executed simple command */

        unused_temp = true;
        env = (char *)malloc(sizeof(char) * 2);
        unused_env = false;
        sprintf(env, "%d", g_return);
      } else if (temp[0] == '!') {
        /* ! --> get PID of last background process */

        int s = snprintf(NULL, 0, "%d", g_pid);
        env = (char *)malloc(sizeof(char) * (s + 1));
        sprintf(env, "%d", g_pid);
        env[strlen(env)] = '\0';
        unused_env = false;
      } else if (temp[0] == '_') {
        /* _ --> get last argument in the fully expanded previous command */

        result = NULL;
        env = (char *)malloc(sizeof(char) * (strlen(g_arg) + 1));
        env = strdup(g_arg);
        unused_env = false;
        env[strlen(env)] = '\0';
      } else {
        /* otherwise --> get environment variable */

        env = getenv(temp);
      }
      if (env != NULL) {
        /* if environment variable, append it to result */

        if ((result != NULL) && (strlen(result) > 0)) {
          result = (char *)realloc(result, sizeof(char)
            * (strlen(result) + strlen(env) + 1));
          sprintf(result + strlen(result), "%s", env);
        } else {
          result = (char *)malloc(sizeof(char) * (strlen(env) + 1));
          result = strdup(env);
        }
        result[strlen(result)] = '\0';
      }
      env_var = false;
    } else if (!env_var) {
      /* if no environment variable, add char to temp */

      int i_len = 0;
      if ((result != NULL) && (strlen(result) > 0)) {
        i_len = strlen(result);
        result = (char *)realloc(result, sizeof(char) * (i_len + 2));
        sprintf(result + strlen(result), "%c", ch[j]);
      } else {
        result = (char *)malloc(sizeof(char) * 2);
        sprintf(result, "%c", ch[j]);
      }
      result[strlen(result)] = '\0';
    }

    /* increment indexer for text, go through loop again */

    j++;
  }
  if (!unused_temp) {
    free(temp);
  }
  if (!unused_env) {
    free(env);
  }
  if (!stop) {
    yylval.string = strdup(result);
    free(result);
    return WORD;
  }
}


~([^ \n\t]*)? {
  /* expand tilde */

  bool unused_name = false;
  char * word = strdup(yytext) + 1;
  char * new_path = NULL;
  if ((strlen(word)) == 0 || (word[0] == '/')) {
    char * user = getenv("HOME");
    int len = snprintf(NULL, 0, "%s", user);
    if (word[0] == '/') {
      /* make new path with home in front */

      char * next = word + 1;
      new_path = (char *)malloc(sizeof(char) * (len + strlen(next) + 2));
      sprintf(new_path, "%s/%s", user, next);
    } else {
      /* make new path without home in front */

      new_path = (char *)malloc(sizeof(char) * (len + 1));
      sprintf(new_path, "%s", user);
    }
  } else {
    /* find next directory switch in string */

    char * slash = strchr(word, '/');
    char * name = NULL;
    if (slash != NULL) {
      int len = slash - word;
      name = (char*)malloc(len + 1);
      strncpy(name, word, len);
    } else {
      name = word;
      unused_name = true;
    }
    if ((slash != NULL) && (strlen(slash) > 1)) {
      /* get path to that directory and append it */

      if (getpwnam(name) != NULL) {
        yylval.string = strcat(strdup(getpwnam(name)->pw_dir), slash);
        if (!unused_name) {
          free(name);
          name = NULL;
        }
        return WORD;
      }
    }

    /* get path to that directory */

    if(getpwnam(name) != NULL) {
      yylval.string = strdup(getpwnam(name)->pw_dir);
      if (!unused_name) {
        free(name);
        name = NULL;
      }
      return WORD;
    }
  }
  yylval.string = strdup(new_path);
  free(new_path);
  new_path = NULL;
  return WORD;
}


\"[^\n\"]*\" {
  /* double quotes */

  const char * str = strdup(yytext);
  int original_length = strlen(str);
  if (original_length >= 2) {
    /* Create a new substring excluding first and last characters */

    const char * sub_string = str + 1;
    char result[original_length - 1];
    strncpy(result, sub_string, original_length - 2);
    result[original_length - 2] = '\0';
    char * string = (char *) result;
    int result_index = 0;
    for (int i = 0; i < strlen(string); i++){
      if (string[i] != '\\') {
        string[result_index] = string[i];
        result_index++;
      }
    }
    string[result_index] = '\0';
    int len = strlen(string);
    for (int i = 0; i < len; i++) {
      if (string[i] == '\\') {
        for (int j = i; j < len; j++) {
          string[j] = string[j + 1];
        }
      }
      len--;
      i--;
    }
    yylval.string = strdup(string);
  } else {
    yylval.string = strdup("");
  }
  return WORD;
}


[^ \t\n|><&]*\\[^ \t\n]* {
  /* escape characters */
  char * str = strdup(yytext);
  char * slashes = (char *)malloc(300);
  int i = 0;

  /* ignore slashes and convert to actual character */

  while (*str) {
    if (*str == '\\') {
      if (*(str + 1) == '\\') {
        str = str + 2;
        slashes[i++] = '\\';
      } else {
        slashes[i++] = *(++str);
      }
    } else {
      slashes[i++] = *str;
    }
    str++;
  }
  slashes[i] = '\0';

  yylval.string = strdup(slashes);
  if (slashes != NULL) {
    free(slashes);
  }
  return WORD;
}


[^ \&\|>\t\n][^ \&\|>\t\n]* {
  /* normal word */
  /* Assume that file names have only alpha chars */

  yylval.string = strdup(yytext);
  return WORD;
}

#include "read_line.h"
#include "shell.h"
#include "tty_raw_mode.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>


extern int g_first_time;
int g_line_length = 0;
char g_line_buffer[MAX_BUFFER_LINE];
int g_line_index = 0;

int g_other_length = 0;
char g_other_buffer[MAX_BUFFER_LINE];
int g_other_index = 0;

int g_ctrl_r_index = -1;
char * g_ctrl_r_string = NULL;

/* History array -- keeps lines in history */

int g_history_index = 0;
char ** g_history = NULL;
int g_history_length = 0;

/*
 *  Prints usage for read_line
 */

void read_line_print_usage() {
  char * usage = "\n"
    " ctrl-?       Print usage\n"
    " ctrl-A       Move to the beginning of line\n"
    " ctrl-E       Move to the end of line\n"
    " ctrl-R       Search command history\n"
    " Delete       Removes the character to the cursor's right\n"
    " Backspace    Deletes last character\n"
    " left arrow   Move cursor left\n"
    " right arrow  Move cursor right\n"
    " down arrow   See next command in the history\n"
    " up arrow     See last command in the history\n";

  write(1, usage, strlen(usage));
} /* read_line_print_usage() */

/*
 *  Prints parameter sig to stderr
 */

extern void disp(int sig) {
  fprintf(stderr, "\nsig: %d\n", sig);
} /* disp() */


/*
 *  Finds word for ctr-r search. Writes it to the
 *  command line. Ensures command is not suggested
 *  twice.
 */

void ctrl_r_find(bool ctrl_r_prev) {
  /* refwrite prompt with reverse-i-search string */

  bool found = false;
  if (ctrl_r_prev) {
    write(1, "", 0);
  }

  for (int i = 0; i < 100; i++) {
    write(1, "\033[1D", 5);
  }
  write(1, "\033[K", 4);
  write(1, "(reverse-i-search)'", 19);
  write(1, g_line_buffer, strlen(g_line_buffer));
  write(1, "`: ", 3);
  write(1, g_line_buffer, strlen(g_line_buffer));
  write(1, " ", 1);

  if (g_ctrl_r_string == NULL && g_history_length > 0) {
    g_ctrl_r_string = strdup(g_history[g_history_length - 1]);
  }

  /* get correct string from history that matches text written*/

  for (int i = g_history_length - 1; i >= 0; i--) {
    char * result = strstr(g_history[i], g_line_buffer);
    if (result) {
      if (!ctrl_r_prev || ((g_ctrl_r_index == -1)
        || (g_ctrl_r_index > i))) {
        g_ctrl_r_index = i;
        g_ctrl_r_string = strdup(g_history[i]);

        write(1, " ", 1);
        write(1, g_ctrl_r_string, strlen(g_ctrl_r_string));
        for (int i = 0; i < (int)strlen(g_ctrl_r_string) + 2; i++) {
          write(1, "\033[1D", 5);
        }
        found = true;
        break;
      }
    }
  }

  /* if not found, write failed */

  if (!found) {
    for (int i = 0; i < 100; i++) {
      write(1, "\033[1D", 5);
    }

    write(1, "\033[K", 4);
    write(1, "(failed reverse-i-search)'", 26);
    write(1, g_line_buffer, strlen(g_line_buffer));
    write(1, "`: ", 3);
    write(1, g_line_buffer, strlen(g_line_buffer));
    write(1, " ", 1);
    write(1, g_ctrl_r_string, strlen(g_ctrl_r_string));
    strcpy(g_line_buffer, strdup(g_ctrl_r_string));
    g_line_buffer[strlen(g_ctrl_r_string)] = '\0';
  }

 } /* ctrl_r_find() */

/*
 *  If g_clear_buffer is set, clear the buffer.
 */

void clear_buffer() {
  if (g_clear_buffer ==  1) {
    for (int i = 0; i < MAX_BUFFER_LINE; i++) {
      g_line_buffer[i] = 0;
    }
  }
} /* clear_buffer() */

/*
 *  Run command "source .shellrc" when shell first
 *  starts.
 */

bool first_source() {
  /* open file, and if it existed prior try to run commands*/

  if (g_first_time) {
    FILE * file = fopen(".shellrc", "r");
    if (file == NULL) {
      file = fopen(".shellrc", "w");
      fprintf(file, " ");
      fclose(file);
    } else {
      fclose(file);
      for (int i = 0; i < 100; i++) {
        write(1, "\033[5D", 4);
      }
      write(1, "\033[K", 4);
      char * cmd = "source .shellrc";
      for (int i = 0; i < (int)strlen(cmd); i++) {
        g_line_buffer[i] = cmd[i];
        g_line_length++;
      }

      /* clear global line buffer */

      if (!(g_line_buffer[0] == ' ') && !(g_line_length == 1)) {
        for (int i = 0; i  < (int)strlen(cmd); i++) {
          g_line_buffer[i] = '\0';
        }
        g_line_length = 0;
        fflush(stdout);
        return false;
      } else {
        return false;
      }
    }
  }
  return true;
} /* first_source() */

/*
 *  Handles case that normal character is typed.
 *  Writes it to command line and saves it in
 *  the global line buffer.
 */

bool normal_character(char ch, bool ctrl_r, int ctrl_r_prev) {
  write(1, &ch, 1);

  /* If max number of character reached return */

  if (g_line_length == (MAX_BUFFER_LINE - 2)) {
    return false;
  }
  g_line_buffer[g_line_length] = ch;
  g_line_length++;

  if (g_other_length > 0) {
    for (int i = g_other_length - 1; i >= 0; i--) {
      ch = g_other_buffer[i];
      write(1, &ch, 1);
    }
  }
  for (int i = 0; i < g_other_length; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  if (ctrl_r) {
    ctrl_r_find(ctrl_r_prev);
  }
  return true;
} /* normal_character() */

/*
 *  Handles the case when enter is pressed.
 *  Extra steps are taken if ctrl_r has been
 *  pressed. This finalizes the global line buffer
 *  variable and adds it to the global history list.
 */

void enter(char ch, bool ctrl_r) {
  /* if reverse search is active, rewrite prompt */

  if (ctrl_r) {
    for (int i = 0; i < 100; i++) {
      write(1, "\b", 1);
    }
    write(1, "\033[K", 4);
    write(1, "myshell>", 8);
    write(1, g_ctrl_r_string, strlen(g_ctrl_r_string));
  }

  /* if string on right side of cursor, add it to buffer */

  if (g_other_length > 0 && !ctrl_r) {
    for (int i = g_other_length - 1; i >= 0; i--) {
      g_line_buffer[g_line_length] = g_other_buffer[i];
      g_line_length++;
    }
  } else if (ctrl_r) {
    g_line_length = 0;
    for (int i = 0; i < (int)strlen(g_ctrl_r_string); i++) {
      g_line_buffer[g_line_length++] = g_ctrl_r_string[i];
    }
    g_line_buffer[g_line_length] = '\0';
  }

  /* add string to global history list after allocating space */

  g_history_length++;
  if (g_history_length == 1) {
    g_history = (char **)malloc(sizeof(char*) * g_history_length);
    g_history[g_history_length - 1] =
      (char*)malloc(sizeof(char)*(strlen(g_line_buffer) + 1));
  } else {
    g_history = (char **)realloc(g_history, sizeof(char*)
      * (g_history_length + 1));
    g_history[g_history_length - 1] =
      (char*)malloc(sizeof(char)*(strlen(g_line_buffer) + 1));
  }
  g_history[g_history_length - 1] = strdup(g_line_buffer);
  g_history_index = g_history_length - 1;
  write(1, &ch, 1);
} /* enter() */

/*
 *  Handles case that that delete is typed.
 *  Deletes one character of the global line
 *  buffer.
 */

void delete(char ch) {
  if (g_line_length != 0) {
    /* write non-deleted characters, delete character */

    for (int i = g_other_length - 2; i >= 0; i--) {
      ch = g_other_buffer[i];
      write(1, &ch, 1);
    }
    ch = ' ';
    write(1, &ch, 1);
    for (int i = 0; i < g_other_length; i++) {
      ch = 8;
      write(1, &ch, 1);
    }
    g_other_length--;
  }
} /* delete() */

/*
 *  Handles case that ctrl + e is typed.
 *  Goes to the end of the line.
 */

void end() {
  /* move cursor to end of line */

  for (int i = g_other_length - 1; i >= 0; i--) {
    write(1, "\033[1C", 5);
    g_other_length--;
    g_line_buffer[g_line_length] = g_other_buffer[g_other_length];
    g_line_length++;
  }
} /* end() */

/*
 *  Handles case that ctrl + a is typed.
 *  Goes to the start of the line.
 */

void start(char ch) {
  /* move cursor to start of line */

  int original_line_length = g_line_length;
  for (int i = 0; i < original_line_length; i++) {
    ch = 8;
    write(1, &ch, 1);
    g_line_length--;
    g_other_buffer[g_other_length] = g_line_buffer[g_line_length];
    g_other_length++;
  }
} /* start() */

/*
 *  Handles case that ctrl + r is typed. Initiates
 *  the search of the global history list for a
 *  command matching that typed by the user.
 */

bool reverse_search(char ch, bool ctrl_r) {
  /* print reverse search prompt if triggered */

  if (g_history_length == 0) {
    return false;
  }
  if (ctrl_r) {
    for (int i = 0; i < (int)(23 + strlen(g_ctrl_r_string)); i++) {
      write(1, "\b", 1);
    }
  } else {
    for (int i = 0; i < 8; i++) {
      ch = 8;
      write(1, &ch, 1);
    }
  }
  write(1, "(reverse-i-search)'`: ", 22);
  if (ctrl_r) {
    write(1, "\033[K", 4);
    ctrl_r_find(ctrl_r);
  }
  return true;
} /* reverse_search() */

/*
 *  Handles case that backspace is typed. Removes
 *  a character from the global line buffer and
 *  searches for a match if ctrl + r has been
 *  pressed.
 */

bool backspace(char ch, bool ctrl_r, bool ctrl_r_prev) {
  if (g_line_length == 0) {
    return false;
  }

  /* Go back one character */

  ch = 8;
  write(1, &ch, 1);

  /* Write remaining characters */

  for (int i = g_other_length - 1; i >= 0; i--) {
    ch = g_other_buffer[i];
    write(1, &ch, 1);
  }

  /* Write a space to erase the last character read */

  ch = ' ';
  write(1, &ch, 1);

  /* Go back one character */

  for (int i = 0; i < g_other_length + 1; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  /* Remove one character from buffer */

  g_line_length--;
  g_line_buffer[g_line_length] = '\0';
  if (ctrl_r) {
    ctrl_r_find(ctrl_r_prev);
  }
  return true;
} /* backspace() */

/*
 *  Handles case that the up arrow is pressed.
 *  Gets previous line in history.
 */

void up_arrow(char ch) {
  /* Erase old line, print backspaces */

  int i = 0;
  for (i = 0; i < g_line_length; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  /* Print spaces on top */

  for (i = 0; i < g_line_length; i++) {
    ch = ' ';
    write(1, &ch, 1);
  }

  /* Print backspaces */

  for (i = 0; i < g_line_length; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  /* Copy line from history and echo it */

  if (g_history_length > 0 && g_history_index >= 0) {
    strcpy(g_line_buffer, g_history[g_history_index]);
    g_line_length = strlen(g_line_buffer);
    g_history_index = (g_history_index + 1) % (g_history_length);
    if (g_history_index == -1) {
      g_history_index = g_history_length - 1;
    }
  }
  write(1, g_line_buffer, g_line_length);
} /* up_arrow() */

/*
 *  Handles case that the down arrow is pressed.
 *  Gets next line relative to current line in
 *  history.
 */

void down_arrow(char ch) {
  /* Erase old line, print backspaces */

  int i = 0;
  for (i = 0; i < g_line_length; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  /* Print spaces on top */

  for (i = 0; i < g_line_length; i++) {
    ch = ' ';
    write(1, &ch, 1);
  }

  /* Print backspaces */

  for (i = 0; i < g_line_length; i++) {
    ch = 8;
    write(1, &ch, 1);
  }

  /* Get previous command from history */

  if ((g_history_length > 0) &&
      (g_history_index <= g_history_length - 1)) {
    strcpy(g_line_buffer, g_history[g_history_index]);
    g_history_index++;
  } else if (g_history_index == g_history_length) {
    g_history_index = g_history_length - 1;
    strcpy(g_line_buffer, "");
  }
  g_line_length = strlen(g_line_buffer);
  write(1, g_line_buffer, g_line_length);
} /* down_arrow() */

/*
 *  Handles the reverse search if an escape character
 *  is typed. Quits the reverse search mode.
 */

bool ctrl_r_escape(char ch, bool ctrl_r) {
  /* leave reverse-i-search */

  if (ctrl_r) {
    write(1, "\033[C", 4);
    for (int i = 0; i < 23 + (int)strlen(g_line_buffer); i++) {
      ch = 8;
      write(1, &ch, 1);
      ch = ' ';
      write(1, &ch, 1);
      write(1, "\033[D", 4);
    }
    write(1, "\0", 1);
    g_line_buffer[0] = '\0';
    ctrl_r = false;
    return false;
  }
  return true;
} /* ctrl_r_escape() */

/*
 *  Handles case that the left arrow is pressed.
 *  Moves the cursor one character to the left,
 *  if possible.
 */

void left_arrow() {
  if (g_line_length != 0) {
    write(1, "\033[D", 4);
    g_line_length--;
    g_other_buffer[g_other_length] = g_line_buffer[g_line_length];
    g_other_length++;
  }
} /* left_arrow() */

/*
 *  Handles case that the right arrow is pressed.
 *  Moves the cursor one character to the right,
 *  if possible.
 */

void right_arrow() {
  if (g_other_length != 0) {
    write(1, "\033[1C", 5);
    g_line_buffer[g_line_length] = g_other_buffer[g_other_length - 1];
    g_line_length++;
    g_other_length--;
  }
} /* right_arrow() */

/*
 *  Handles case that an a regular character is typed
 *  or ctrl + _ is typed (but not ctrl + r). Calls the
 *  function associated with handling each specific case.
 */

int char_or_ctrl(char ch, bool ctrl_r, bool ctrl_r_prev) {
  /* go through each possible character case, call helper functions */

  bool cont = true;
  if (ch >= 32 && ch != 127) {
    cont = normal_character(ch, ctrl_r, ctrl_r_prev);
    if (!cont) {
      return false;
    }
  } else if (ch == 10) {
    enter(ch, ctrl_r);
    return false;
  } else if (ch == 31) {
    read_line_print_usage();
    g_line_buffer[0] = 0;
    return false;
  } else if (ch == 4) {
    delete(ch);
  } else if (ch == 5) {
    end();
  } else if (ch == 1) {
    start(ch);
  } else if ((ch == 8) || (ch == 127)) {
    cont = backspace(ch, ctrl_r, ctrl_r_prev);
    if (!cont) {
      return true;
    }
  }
  return true;
} /* char_or_ctrl() */

/*
 *  Handles case that an escape sequence is typed.
 *  Calls functions to handle each case.
 */

bool escape_sequence(char ch, bool ctrl_r) {
  bool cont = ctrl_r_escape(ch, ctrl_r);
  if (!cont) {
    return false;
  }

  /* Escape sequence. Read two chars more */

  char ch1 = '\0';
  char ch2 = '\0';
  read(0, &ch1, 1);
  read(0, &ch2, 1);
  if ((ch1 == 91) && (ch2 == 65)) {
    up_arrow(ch);
  } else if ((ch1 == 91) && (ch2 == 66)) {
    down_arrow(ch);
  } else if ((ch1 == 91) && (ch2 == 68)) {
    left_arrow();
  } else if ((ch1 == 91) && (ch2 == 67)) {
    right_arrow();
  }
  return true;
} /* excape_sequence() */

/*
 *  Input a line with some basic editing.
 */

char * read_line() {
  struct termios attribute;
  tcgetattr(STDIN_FILENO, &attribute);

  /* Set terminal in raw mode */

  tty_raw_mode();
  for (int i = 0; i < g_line_length; i++) {
    g_line_buffer[i] = '\0';
  }
  for (int i = 0; i < g_other_length; i++) {
    g_other_buffer[i] = '\0';
  }
  g_line_length = 0;
  g_other_length = 0;
  int ctrl_r = false;
  g_ctrl_r_index = -1;
  g_ctrl_r_string = NULL;
  bool ctrl_r_prev = false;
  bool cont;

  /* Read one line until enter is typed */

  while (1) {
    clear_buffer();
    cont = first_source();
    if (!cont) {
      break;
    }

    /* Read one character in raw mode. */

    char ch = '\0';
    read(0, &ch, 1);

    if ((ch != 27) && (ch != 18)) {
      cont = char_or_ctrl(ch, ctrl_r, ctrl_r_prev);
      if (!cont) {
        break;
      } else {
        continue;
      }
    } else if (ch == 18) {
      ctrl_r_prev = ctrl_r;
      cont = reverse_search(ch, ctrl_r);
      if (!cont) {
        continue;
      }
      ctrl_r = true;
    } else if (ch == 27) {
      cont = escape_sequence(ch, ctrl_r);
      if (!cont) {
        break;
      }
    }
  }

  /* Add eol and null char at the end of string */

  g_line_buffer[g_line_length] = 10;
  g_line_length++;
  g_line_buffer[g_line_length] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &attribute);
  if (g_first_time) {
    g_first_time = false;
  }
  return g_line_buffer;
} /* read_line() */

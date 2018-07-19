#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <linux/limits.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_echo(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "shows current directory"},
  {cmd_cd, "cd", "Change the directory to dir. If dir is not supplied, the value of the HOME shell variable is the default."},
  {cmd_echo, "echo", "Echo your feelings."},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Prints out current directory */
int cmd_pwd(unused struct tokens *tokens)
{
  char path[PATH_MAX];
  getcwd (path, PATH_MAX);
  printf ("%s\n", path);
  return 0;
}

static void _mychdir (const char *newdir)
{
  char oldpwd[PATH_MAX];
  getcwd (oldpwd, PATH_MAX);
  setenv ("OLDPWD", oldpwd, 1);
  setenv ("PWD", newdir, 1);
  chdir (newdir);
}

/* Changes current directory */
int cmd_cd(struct tokens *tokens)
{
  int count = tokens_get_length (tokens);
  if (count == 1) {
    const char *home = getenv ("HOME");
    _mychdir (home);
  } else if (count == 2) {
    const char *newdir = tokens_get_token (tokens, 1);
    if (strlen (newdir) == 1 && newdir[0] == '-') {
      _mychdir (getenv ("OLDPWD"));
    } else {
      _mychdir (newdir);
    }
  } else {
    fprintf (stderr, "cd: too many arguments\n");
    return 1;
  }
  return 0;
}

/* This is primitive and broken */
static void _interpolate (struct tokens *tokens, int start)
{
  int count = tokens_get_length (tokens);
  char *token, *value;
  for (int i = start; i < count; i++) {
    token = tokens_get_token (tokens, i);
    if (token[0] == '$') {
      value = getenv (++token);
      if (value) printf ("%s", value);
    }
    else
      printf ("%s", token);
    if (i+1 < count) printf (" ");
  }
  printf ("\n");
}

/* Prints out stuff at the screen */
int cmd_echo(struct tokens *tokens)
{
  _interpolate (tokens, 1);
  return 0;
}

char *isprogram (const char *dirname, const char *basename)
{
  struct stat sb;
  char path[PATH_MAX];
  snprintf (path, PATH_MAX, "%s/%s", dirname, basename);

  if (stat (path, &sb) != -1) {
    if (S_ISREG (sb.st_mode) && sb.st_mode & S_IXUSR) {
      return strdup (path);
    }
  }
  return NULL;
}


char *path_lookup (const char *program)
{
  const char *origpath;
  char copypath[PATH_MAX];
  char *match, *tmp = NULL;

  /* Copy the PATH value since strtok will change it inplace if
     needed */
  origpath = getenv ("PATH");
  memset (copypath, sizeof (char), PATH_MAX);
  memcpy (copypath, origpath, strlen (origpath));

  /* Look for program at every directory of PATH */
  tmp = strtok (copypath, ":");
  match = isprogram (tmp, program);
  if (!match) {
    while ((tmp = strtok (0, ":"))) {
      match = isprogram (tmp, program);
      if (match) return match;
    }
  }
  return NULL;
}

/* Find a command in the path */
char *path_resolve (const char *program)
{
  struct stat sb;
  /* Account for paths that can be fully resolved */
  if (stat (program, &sb) != -1) {
    if (S_ISDIR(sb.st_mode)) {
      fprintf (stderr, "%s: Is a directory\n", program);
      return NULL;
    }
    return strdup (program);
  }
  return path_lookup (program);
}

/* Try to run a command */
int run (struct tokens *tokens)
{
  int status;
  const char *program = tokens_get_token (tokens, 0);
  char *full_path = path_resolve (program);
  if (full_path) {
    pid_t pid = fork ();

    if (pid == -1) {
      fprintf (stderr, "Couldn't spawn new process");
      return 1;
    }

    /* Child process */
    if (pid == 0) {
      char **toks = tokens_get_all (tokens);
      execv (full_path, toks);
    } else {
      /* Parent process */
      waitpid (pid, &status, 0);
      return status;
    }
  }
  return 0;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    /* Read the return code of the built in or program we're
     * running */
    char str_code[10];
    int code;
    if (fundex >= 0) {
      code = cmd_table[fundex].fun(tokens);
    } else {
      code = run (tokens);
    }
    sprintf (str_code, "%d", code);
    setenv ("?",  str_code, 1);

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}

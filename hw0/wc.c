#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ❤ */

struct params {
  bool c;                       /* bytes */
  bool m;                       /* chars */
  bool l;                       /* lines */
  bool w;                       /* words */
};

struct totals {
  long bytes;
  long chars;
  long lines;
  long words;
};

long count_lines (char *content, long size)
{
  long lines = 0;
  for (long i = 0; i < size; i++) {
    if (content[i] == '\n') lines++;
  }
  return lines;
}

long count_words (char *content, long size)
{
  long i, words = 0;
  bool at_word = false;
  for (i = 0; i < size; i++) {
    char tmp = content[i];
    if (!isspace (tmp) && !at_word) {
      at_word = true;
      words++;
    } else if (isspace (tmp)) {
      at_word = false;
    }
  }
  return words;
}

int count_things (const char *file_path, struct params *params, struct totals *totals)
{
  FILE *fp;
  long file_size;

  if ((fp = fopen (file_path, "rb")) == NULL) {
    fprintf (stderr, "Can't open file `%s'\n", file_path);
    return EXIT_FAILURE;
  }

  /* Read file size */
  fseek (fp, 0, SEEK_END);
  file_size = ftell (fp);
  fseek (fp, 0, SEEK_SET);

  /* Allocate space for file content in the stack */
  char file_content[file_size];
  if (fread (file_content, sizeof (char), file_size, fp) != (size_t) file_size) {
    fprintf (stderr, "Can't read file `%s'\n", file_path);
    return EXIT_FAILURE;
  }

  /* Count the lines */
  if (params->l) {
    long lines = count_lines (file_content, file_size);
    printf ("%ld\t", lines);
    totals->lines += lines;
  }

  /* Count the bytes (we already did) */
  if (params->c) {
    printf ("%ld\t", file_size);
    totals->bytes += file_size;
  }

  /* Count the words */
  if (params->w) {
    long words = count_words (file_content, file_size);
    printf ("%ld\t", words);
    totals->words += words;
  }

  printf ("%s\n", file_path);

  fclose (fp);
  return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
  argc--;
  argv++;

  struct totals totals = { 0, 0, 0, 0 };

  struct params params = { false, false, false, false };

  for (int i = 0; i < argc; i++) {
    char *arg = argv[i];
    if (arg[0] == '-') {
      switch (arg[1]) {
      case 'c': params.c = true; break;
      case 'm': params.m = true; break;
      case 'l': params.l = true; break;
      case 'w': params.w = true; break;
      default:
        fprintf (stderr, "Invalid option `%c'\n", arg[1]);
        exit (EXIT_FAILURE);
      }
      continue;
    }
    /* Process each file, bubbling up possible errors */
    if (count_things (argv[i], &params, &totals) == EXIT_FAILURE)
      return EXIT_FAILURE;
  }

  if (params.l) printf ("%ld\t", totals.lines);
  if (params.c) printf ("%ld\t", totals.bytes);
  if (params.m) printf ("%ld\t", totals.chars);
  if (params.w) printf ("%ld\t", totals.words);
  printf ("total\n");

  /* printf ("options: c: %d, m: %d, l: %d\n", params.c, params.m, params.l); */

  return EXIT_SUCCESS;
}

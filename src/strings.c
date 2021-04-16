#include <stdlib.h>
#include <string.h>

typedef struct {
  char *input;
  char *output;
} string_replacement_t;

char *string_replace(const char *input, const char *substr, const char *repl) {
  if (input == NULL || substr == NULL || repl == NULL) {
    return NULL;
  }

  char *output = NULL, *offset = NULL, *offseted_input = (char *)input;

  while ((offset = strstr(offseted_input, substr)) != NULL) {
    int segment_length = offset - offseted_input;
    int out_length = (output == NULL ? 0 : strlen(output));

    // Resize ouput to fit the new segment and replacement string.
    output = realloc(output, out_length + segment_length + strlen(repl) + 1);
    
    // Copy the new segment and replacement string into the ouput.
    strncpy(output + out_length, offseted_input, segment_length);
    strncpy(output + out_length + segment_length, repl, strlen(repl));
    output[strlen(output)] = '\0';

    // Shift the input pointer.
    offseted_input += (segment_length + strlen(substr));
  }

  // Copy the remainder of the string.
  int remainder = (input + strlen(input) - offseted_input);
  if (remainder > 0) {
    int out_length = (output == NULL ? 0 : strlen(output));
    output = realloc(output, out_length + remainder + 1);
    strncpy(output + out_length, offseted_input, remainder);
    output[out_length + remainder] = '\0';
  }

  return output;
}

char *string_html_escape(const char *input) {
  if (input == NULL) {
    return NULL;
  }

  string_replacement_t replacements[] = {
    { .input = "&", .output = "&amp;" },
    { .input = "<", .output = "&lt;" },
    { .input = ">", .output = "&rt;" },
    { .input = "\"", .output = "&quot;" }
  };

  char *output = malloc(strlen(input) + 1);
  strcpy(output, input);

  for (int idx = 0; idx < 4; idx++) {
    char *tmp = string_replace(output, replacements[idx].input, replacements[idx].output);
    free(output);
    output = tmp;
  }

  return output;
}

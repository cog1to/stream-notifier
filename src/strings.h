#ifndef _H_STRINGS
#define _H_STRINGS

/**
 * Replaces all occurances of a substring within a string and returns a new 
 * string with those replacements.
 *
 * @param input: Input string.
 * @param substr: Substring to search inside the input string.
 * @param repl: Replacement string for a substring.
 *
 * @return Newly allocated string containing necessary replacements.
 */
char *string_replace(const char *input, const char *substr, const char *repl);

/**
 * Performs basic HTML escaping for a string. Does following escaping:
 *   > -> &gt;
 *   < -> &lt;
 *   & -> &amp;
 *   " -> &quot;
 *
 * @param input Input string to HTML-escape.
 *
 * @return New string that is HTML-escaped.
 */
char *string_html_escape(const char *input);

#endif

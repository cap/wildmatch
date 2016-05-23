/*
 * Copyright (c), 2016 David Aguilar
 * Based on the fnmatch implementation from OpenBSD.
 *
 * Copyright (c) 1989, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "wildmatch.h"

#ifdef __cplusplus
extern "C" {
#endif

#undef  EOS
#define EOS '\0'

#define RANGE_MATCH 1
#define RANGE_NOMATCH 0
#define RANGE_ERROR (-1)

#define check_flag(flags, opts) ((flags) & (opts))

extern int isblank(int);

static int rangematch(const char *, char, int, const char **);

int wildmatch(const char *pattern, const char *string, int flags)
{
    const char *stringstart;
    const char *newp;
    const char *slash;
    char c, test;
    int wild = 0;

    /* WM_WILDSTAR implies WM_PATHNAME and WM_PERIOD. */
    if (check_flag(flags, WM_WILDSTAR)) {
        flags |= WM_PATHNAME;
        /*
        flags |= WM_PERIOD;
        */
    }

    for (stringstart = string;;) {
        switch (c = *pattern++) {
        case EOS:
            if (check_flag(flags, WM_LEADING_DIR) && *string == '/')
                return (0);
            return (*string == EOS ? 0 : WM_NOMATCH);
        case '?':
            if (*string == EOS)
                return (WM_NOMATCH);
            if (*string == '/' && check_flag(flags, WM_PATHNAME))
                return (WM_NOMATCH);
            if (*string == '.' && check_flag(flags, WM_PERIOD) &&
                (string == stringstart ||
                (check_flag(flags, WM_PATHNAME) && *(string - 1) == '/')))
                return (WM_NOMATCH);
            ++string;
            break;
        case '*':
            c = *pattern;
            wild = check_flag(flags, WM_WILDSTAR) && pattern[0] == '*';
            if (wild) {
                /* Collapse multiple stars and slash-** patterns,
                 * e.g. "** / *** / **** / **" (without spaces)
                 * is treated as a single ** wildstar.
                 */
                while (c == '*') {
                    c = *++pattern;
                }
                while (c == '/' && pattern[1] == '*' && pattern[2] == '*') {
                    c = *++pattern;
                    while (c == '*') {
                        c = *++pattern;
                    }
                }
            } else {
                /* Collapse multiple stars. */
                while (c == '*') {
                    c = *++pattern;
                }
            }

            if (!wild && *string == '.' && check_flag(flags, WM_PERIOD) &&
                (string == stringstart ||
                (check_flag(flags, WM_PATHNAME) && *(string - 1) == '/')))
                return (WM_NOMATCH);

            /* Optimize for pattern with * or ** at end or before /. */
            if (c == EOS) {
                if (wild) {
                    return (0);
                }
                if (check_flag(flags, WM_PATHNAME)) {
                    return (check_flag(flags, WM_LEADING_DIR) ||
                        strchr(string, '/') == NULL ?  0 : WM_NOMATCH);
                } else {
                    return (0);
                }
            } else if (c == '/') {
                if (wild) {
                    slash = strchr(stringstart, '/');
                    if (!slash) {
                        return WM_NOMATCH;
                    }
                    while (slash) {
                        if (wildmatch(pattern+1, slash+1, flags) == 0) {
                            return 0;
                        }
                        slash = strchr(slash+1, '/');
                    }
                } else {
                    if (check_flag(flags, WM_PATHNAME)) {
                        if ((string = strchr(string, '/')) == NULL) {
                            return (WM_NOMATCH);
                        }
                    }
                }
            } else if (wild) {
                return WM_NOMATCH;
            }

            /* General case, use recursion. */
            while ((test = *string) != EOS) {
                if (!wildmatch(pattern, string, flags & ~WM_PERIOD))
                    return (0);
                if (test == '/' && check_flag(flags, WM_PATHNAME))
                    break;
                ++string;
            }
            return (WM_NOMATCH);
        case '[':
            if (*string == EOS)
                return (WM_NOMATCH);
            if (*string == '/' && check_flag(flags, WM_PATHNAME))
                return (WM_NOMATCH);
            if (*string == '.' && check_flag(flags, WM_PERIOD) &&
                (string == stringstart ||
                (check_flag(flags, WM_PATHNAME) && *(string - 1) == '/')))
                return (WM_NOMATCH);

            switch (rangematch(pattern, *string, flags, &newp)) {
            case RANGE_ERROR:
                /* not a good range, treat as normal text */
                goto normal;
            case RANGE_MATCH:
                pattern = newp;
                break;
            case RANGE_NOMATCH:
                return (WM_NOMATCH);
            }
            ++string;
            break;
        case '\\':
            if (!check_flag(flags, WM_NOESCAPE)) {
                if ((c = *pattern++) == EOS) {
                    c = '\\';
                    --pattern;
                    if (*(string+1) == EOS) {
                        return WM_NOMATCH;
                    }
                }
            }
            /* FALLTHROUGH */
        default:
        normal:
            if (c != *string && !(check_flag(flags, WM_CASEFOLD) &&
                 (tolower((unsigned char)c) ==
                 tolower((unsigned char)*string))))
                return (WM_NOMATCH);
            ++string;
            break;
        }
    /* NOTREACHED */
    }
}

static int
rangematch(const char *pattern, char test, int flags, const char **newp)
{
    int negate, ok;
    char c, c2;
    char tmp;
    const char *endmarker;
    size_t len;

    /*
     * A bracket expression starting with an unquoted circumflex
     * character produces unspecified results (IEEE 1003.2-1992,
     * 3.13.2).  This implementation treats it like '!', for
     * consistency with the regular expression syntax.
     * J.T. Conklin (conklin@ngai.kaleida.com)
     */
    if ((negate = (*pattern == '!' || *pattern == '^')))
        ++pattern;

    if (check_flag(flags, WM_CASEFOLD))
        test = tolower((unsigned char)test);

    /*
     * A right bracket shall lose its special meaning and represent
     * itself in a bracket expression if it occurs first in the list.
     * -- POSIX.2 2.8.3.2
     */
    ok = 0;
    c = *pattern++;
    do {
        if (c == '\\' && !check_flag(flags, WM_NOESCAPE))
            c = *pattern++;
        if (c == EOS) {
            return (RANGE_ERROR);
        }
        if (c == '/' && check_flag(flags, WM_PATHNAME))
            return (RANGE_NOMATCH);
        if (*pattern == '-'
            && (c2 = *(pattern+1)) != EOS && c2 != ']') {
            pattern += 2;
            if (c2 == '\\' && !check_flag(flags, WM_NOESCAPE))
                c2 = *pattern++;
            if (c2 == EOS)
                return (RANGE_ERROR);

            if (check_flag(flags, WM_CASEFOLD)) {
                c = tolower((unsigned char)c);
                c2 = tolower((unsigned char)c2);
            }
            if (c > c2) {
                tmp = c2;
                c2 = c;
                c = tmp;
            }
            if (c <= test && test <= c2) {
                ok = 1;
            }
        }
        if (c == '[' && *pattern == ':'
            && (c2 = *(pattern+1)) != EOS) {

            ++pattern;
            endmarker = strchr(pattern, ']');
            len = (endmarker && (endmarker > pattern + 1))
                ? (endmarker - pattern - 1) : 0;

            if (len > 0 && pattern[len] == ':') {

                if (!strncmp(pattern, "alnum", len)) {
                    if (isalnum(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "alpha", len)) {
                    if (isalpha(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "blank", len)) {
                    if (isblank(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "cntrl", len)) {
                    if (iscntrl(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "digit", len)) {
                    if (isdigit(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "graph", len)) {
                    if (isgraph(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "lower", len)) {
                    if (islower(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "print", len)) {
                    if (isprint(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "punct", len)) {
                    if (ispunct(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "space", len)) {
                    if (isspace(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "upper", len)) {
                    if (ISSET(flags, WM_CASEFOLD)) {
                        test = toupper((unsigned char)test);
                    }
                    if (isupper(test)) {
                        ok = 1;
                    }
                } else if (!strncmp(pattern, "xdigit", len)) {
                    if (isxdigit(test)) {
                        ok = 1;
                    }
                } else {
                    --pattern;
                    ok = 0;
                    continue;
                }
                /* Move past the character-class name and :] end-marker */
                pattern = endmarker + 1;

            } else {
                /* Regular pattern match */
                --pattern;
                c = '[';
            }
        }
        if (c == test) {
            ok = 1;
        }
    } while ((c = *pattern++) != ']');

    *newp = (const char *)pattern;
    return (ok == negate ? RANGE_NOMATCH : RANGE_MATCH);
}
#ifdef __cplusplus
}
#endif

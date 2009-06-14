/*
 * augtool.c:
 *
 * Copyright (C) 2007, 2008 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Author: David Lutterkort <dlutter@redhat.com>
 */

#include "system.h"

#include "augeas.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <argz.h>
#include <getopt.h>

#include "debug.h"

/* ===== internal.h */

#if !defined(SEP)
#define	SEP	'/'
#endif

#define DATADIR "/usr/share"

/* Define: AUGEAS_LENS_DIR
 * The default location for lens definitions */
#define AUGEAS_LENS_DIR DATADIR "/augeas/lenses"

/* The directory where we install lenses distribute with Augeas */
#define AUGEAS_LENS_DIST_DIR DATADIR "/augeas/lenses/dist"

/* Define: AUGEAS_ROOT_ENV
 * The env var that points to the chroot holding files we may modify.
 * Mostly useful for testing */
#define AUGEAS_ROOT_ENV "AUGEAS_ROOT"

/* Define: AUGEAS_FILES_TREE
 * The root for actual file contents */
#define AUGEAS_FILES_TREE "/files"

/* Define: AUGEAS_META_TREE
 * Augeas reports some information in this subtree */
#define AUGEAS_META_TREE "/augeas"

/* Define: AUGEAS_META_FILES
 * Information about files */
#define AUGEAS_META_FILES AUGEAS_META_TREE AUGEAS_FILES_TREE

/* Define: AUGEAS_META_ROOT
 * The root directory */
#define AUGEAS_META_ROOT AUGEAS_META_TREE "/root"

/* Define: AUGEAS_META_SAVE_MODE
 * How we save files. One of 'backup', 'overwrite' or 'newfile' */
#define AUGEAS_META_SAVE_MODE AUGEAS_META_TREE "/save"

/* Define: AUGEAS_CLONE_IF_RENAME_FAILS
 * Control what save does when renaming the temporary file to its final
 * destination fails with EXDEV or EBUSY: when this tree node exists, copy
 * the file contents. If it is not present, simply give up and report an
 * error.  */
#define AUGEAS_COPY_IF_RENAME_FAILS \
    AUGEAS_META_SAVE_MODE "/copy_if_rename_fails"

/* A hierarchy where we record certain 'events', e.g. which tree
 * nodes actually gotsaved into files */
#define AUGEAS_EVENTS AUGEAS_META_TREE "/events"

#define AUGEAS_EVENTS_SAVED AUGEAS_EVENTS "/saved"

/* Where to put information about parsing of path expressions */
#define AUGEAS_META_PATHX AUGEAS_META_TREE "/pathx"

/* Define: AUGEAS_LENS_ENV
 * Name of env var that contains list of paths to search for additional
   spec files */
#define AUGEAS_LENS_ENV "AUGEAS_LENS_LIB"

/* Define: MAX_ENV_SIZE
 * Fairly arbitrary bound on the length of the path we
 *  accept from AUGEAS_SPEC_ENV */
#define MAX_ENV_SIZE 4096

/* Define: PATH_SEP_CHAR
 * Character separating paths in a list of paths */
#define PATH_SEP_CHAR ':'

/* Constants for setting the save mode via the augeas path at
 * AUGEAS_META_SAVE_MODE */
#define AUG_SAVE_BACKUP_TEXT "backup"
#define AUG_SAVE_NEWFILE_TEXT "newfile"
#define AUG_SAVE_NOOP_TEXT "noop"
#define AUG_SAVE_OVERWRITE_TEXT "overwrite"

/* ===== */

struct command {
    const char *name;
    int minargs;
    int maxargs;
    int(*handler) (char *args[]);
    const char *synopsis;
    const char *help;
};

static const struct command const commands[];

static augeas *aug = NULL;
static const char *const progname = "augtool";
static unsigned int flags = AUG_NONE;
const char *root = NULL;
char *loadpath = NULL;

static char *cleanstr(char *path, const char sep) {
    if (path == NULL || strlen(path) == 0)
        return path;
    char *e = path + strlen(path) - 1;
    while (e >= path && (*e == sep || isspace(*e)))
        *e-- = '\0';
    return path;
}

static char *cleanpath(char *path) {
    return cleanstr(path, SEP);
}

static char *ls_pattern(const char *path) {
    char *q;
    int r;

    if (path[strlen(path)-1] == SEP)
        r = asprintf(&q, "%s*", path);
    else
        r = asprintf(&q, "%s/*", path);
    if (r == -1)
        return NULL;
    return q;
}

static int child_count(const char *path) {
    char *q = ls_pattern(path);
    int cnt;

    if (q == NULL)
        return 0;
    cnt = aug_match(aug, q, NULL);
    free(q);
    return cnt;
}

static int cmd_ls(char *args[]) {
    int cnt;
    char *path = cleanpath(args[0]);
    char **paths;
    int i;

    path = ls_pattern(path);
    if (path == NULL)
        return -1;
    cnt = aug_match(aug, path, &paths);
    for (i=0; i < cnt; i++) {
        const char *val;
        const char *basnam = strrchr(paths[i], SEP);
        int dir = child_count(paths[i]);
        aug_get(aug, paths[i], &val);
        basnam = (basnam == NULL) ? paths[i] : basnam + 1;
        if (val == NULL)
            val = "(none)";
        printf("%s%s= %s\n", basnam, dir ? "/ " : " ", val);
        free(paths[i]);
    }
    if (cnt > 0)
        free(paths);
    free(path);
    return 0;
}

static int cmd_match(char *args[]) {
    int cnt;
    const char *pattern = cleanpath(args[0]);
    char **matches;
    int filter = (args[1] != NULL) && (strlen(args[1]) > 0);
    int result = 0;
    int i;

    cnt = aug_match(aug, pattern, &matches);
    if (cnt < 0) {
        printf("  (error matching %s)\n", pattern);
        result = -1;
        goto done;
    }
    if (cnt == 0) {
        printf("  (no matches)\n");
        goto done;
    }

    for (i=0; i < cnt; i++) {
        const char *val;
        aug_get(aug, matches[i], &val);
        if (val == NULL)
            val = "(none)";
        if (filter) {
            if (!strcmp(args[1], val))
                printf("%s\n", matches[i]);
        } else {
            printf("%s = %s\n", matches[i], val);
        }
    }
 done:
    for (i=0; i < cnt; i++)
        free(matches[i]);
    free(matches);
    return result;
}

static int cmd_rm(char *args[]) {
    int cnt;
    const char *path = cleanpath(args[0]);
    printf("rm : %s", path);
    cnt = aug_rm(aug, path);
    printf(" %d\n", cnt);
    return 0;
}

static int cmd_mv(char *args[]) {
    const char *src = cleanpath(args[0]);
    const char *dst = cleanpath(args[1]);
    int r;

    r = aug_mv(aug, src, dst);
    if (r == -1)
        printf("Failed\n");
    return r;
}

static int cmd_set(char *args[]) {
    const char *path = cleanpath(args[0]);
    const char *val = args[1];
    int r;

    r = aug_set(aug, path, val);
    if (r == -1)
        printf ("Failed\n");
    return r;
}

static int cmd_defvar(char *args[]) {
    const char *name = args[0];
    const char *path = cleanpath(args[1]);
    int r;

    r = aug_defvar(aug, name, path);
    if (r == -1)
        printf ("Failed\n");
    return r;
}

static int cmd_defnode(char *args[]) {
    const char *name = args[0];
    const char *path = cleanpath(args[1]);
    const char *value = args[2];
    int r;

    /* Our simple minded line parser treats non-existant and empty values
     * the same. We choose to take the empty string to mean NULL */
    if (value != NULL && strlen(value) == 0)
        value = NULL;
    r = aug_defnode(aug, name, path, value, NULL);
    if (r == -1)
        printf ("Failed\n");
    return r;
}

static int cmd_clear(char *args[]) {
    const char *path = cleanpath(args[0]);
    int r;

    r = aug_set(aug, path, NULL);
    if (r == -1)
        printf ("Failed\n");
    return r;
}

static int cmd_get(char *args[]) {
    const char *path = cleanpath(args[0]);
    const char *val;

    printf("%s", path);
    if (aug_get(aug, path, &val) != 1) {
        printf(" (o)\n");
    } else if (val == NULL) {
        printf(" (none)\n");
    } else {
        printf(" = %s\n", val);
    }
    return 0;
}

static int cmd_print(char *args[]) {
    return aug_print(aug, stdout, cleanpath(args[0]));
}

static int cmd_save(/*@unused@*/ char *args[]) {
    int r;
    r = aug_save(aug);
    if (r == -1) {
        printf("Saving failed\n");
    } else {
        r = aug_match(aug, "/augeas/events/saved", NULL);
        if (r > 0) {
            printf("Saved %d file(s)\n", r);
        } else if (r < 0) {
            printf("Error during match: %d\n", r);
        }
    }
    return r;
}

static int cmd_load(/*@unused@*/ char *args[]) {
    int r;
    r = aug_load(aug);
    if (r == -1) {
        printf("Loading failed\n");
    } else {
        r = aug_match(aug, "/augeas/events/saved", NULL);
        if (r > 0) {
            printf("Saved %d file(s)\n", r);
        } else if (r < 0) {
            printf("Error during match: %d\n", r);
        }
    }
    return r;
}

static int cmd_ins(char *args[]) {
    const char *label = args[0];
    const char *where = args[1];
    const char *path = cleanpath(args[2]);
    int before;
    int r;

    if (!strcmp(where, "after"))
        before = 0;
    else if (!strcmp(where, "before"))
        before = 1;
    else {
        printf("The <WHERE> argument must be either 'before' or 'after'.");
        return -1;
    }

    r = aug_insert(aug, path, label, before);
    if (r == -1)
        printf ("Failed\n");
    return r;
}

static int cmd_help(/*@unused@*/ char *args[]) {
    const struct command *c;

    printf("Commands:\n\n");
    printf("    exit, quit\n        Exit the program\n\n");
    for (c=commands; c->name != NULL; c++) {
        printf("    %s\n        %s\n\n", c->synopsis, c->help);
    }
    printf("\nEnvironment:\n\n");
    printf("    AUGEAS_ROOT\n        the file system root, defaults to '/'\n\n");
    printf("    AUGEAS_LENS_LIB\n        colon separated list of directories with lenses,\n\
        defaults to " AUGEAS_LENS_DIR "\n\n");
    return 0;
}

static int chk_args(const struct command *cmd, int maxargs, char *args[]) {
    int i;

    for (i=0; i < cmd->minargs; i++) {
        if (args[i] == NULL || strlen(args[i]) == 0) {
            fprintf(stderr, "Not enough arguments for %s\n", cmd->name);
            return -1;
        }
    }
    for (i = cmd->maxargs; i < maxargs; i++) {
        if (args[i] != NULL && strlen(args[i]) > 0) {
            fprintf(stderr, "Too many arguments for %s\n", cmd->name);
            return -1;
        }
    }
    return 0;
}

static char *nexttoken(char **line) {
    char *r, *s;
    char quot = '\0';

    s = *line;

    while (*s && isblank(*s)) s+= 1;
    if (*s == '\'' || *s == '"') {
        quot = *s;
        s += 1;
    }
    r = s;
    while (*s) {
        if ((quot && *s == quot) || (!quot && isblank(*s)))
            break;
        s += 1;
    }
    if (*s)
        *s++ = '\0';
    *line = s;
    return r;
}

static char *parseline(char *line, int maxargs, char *args[]) {
    char *cmd;
    int argc;

    memset(args, 0, maxargs * sizeof(*args));
    cmd = nexttoken(&line);

    for (argc=0; argc < maxargs; argc++) {
        args[argc] = nexttoken(&line);
    }

    if (*line) {
        fprintf(stderr, "Too many arguments: '%s' not used\n", line);
    }
    return cmd;
}

static const struct command const commands[] = {
    { "ls",  1, 1, cmd_ls, "ls <PATH>",
      "List the direct children of PATH"
    },
    { "match",  1, 2, cmd_match, "match <PATH> [<VALUE>]",
      "Find all paths that match the path expression PATH. If VALUE is given,\n"
      "        only the matching paths whose value equals VALUE are printed"
    },
    { "rm",  1, 1, cmd_rm, "rm <PATH>",
      "Delete PATH and all its children from the tree"
    },
    { "mv", 2, 2, cmd_mv, "mv <SRC> <DST>",
      "Move node SRC to DST. SRC must match exactly one node in the tree.\n"
      "        DST must either match exactly one node in the tree, or may not\n"
      "        exist yet. If DST exists already, it and all its descendants are\n"
      "        deleted. If DST does not exist yet, it and all its missing \n"
      "        ancestors are created." },
    { "set", 1, 2, cmd_set, "set <PATH> <VALUE>",
      "Associate VALUE with PATH. If PATH is not in the tree yet,\n"
      "        it and all its ancestors will be created. These new tree entries\n"
      "        will appear last amongst their siblings"
    },
    { "clear", 1, 1, cmd_clear, "clear <PATH>",
      "Set the value for PATH to NULL. If PATH is not in the tree yet,\n"
      "        it and all its ancestors will be created. These new tree entries\n"
      "        will appear last amongst their siblings"
    },
    { "get", 1, 1, cmd_get, "get <PATH>",
      "Print the value associated with PATH"
    },
    { "print", 0, 1, cmd_print, "print [<PATH>]",
      "Print entries in the tree. If PATH is given, printing starts there,\n"
      "        otherwise the whole tree is printed"
    },
    { "ins", 3, 3, cmd_ins, "ins <LABEL> <WHERE> <PATH>",
      "Insert a new node with label LABEL right before or after PATH into\n"
     "        the tree. WHERE must be either 'before' or 'after'."
    },
    { "save", 0, 0, cmd_save, "save",
      "Save all pending changes to disk. For now, files are not overwritten.\n"
      "        Instead, new files with extension .augnew are created"
    },
    { "load", 0, 0, cmd_load, "load",
      "Load files accordig to the transforms in /augeas/load."
    },
    { "defvar", 2, 2, cmd_defvar, "defvar <NAME> <EXPR>",
      "Define the variable NAME to the result of evalutating EXPR. The\n"
      "        variable can be used in path expressions as $NAME. Note that EXPR\n"
      "        is evaluated when the variable is defined, not when it is used."
    },
    { "defnode", 2, 3, cmd_defnode, "defnode <NAME> <EXPR> [<VALUE>]",
      "Define the variable NAME to the result of evalutating EXPR, which\n"
      "        must be a nodeset. If no node matching EXPR exists yet, one\n"
      "        is created and NAME will refer to it. If VALUE is given, this\n"
      "        is the same as 'set EXPR VALUE'; if VALUE is not given, the\n"
      "        node is created as if with 'clear EXPR' would and NAME refers\n"
      "        to that node."
    },
    { "help", 0, 0, cmd_help, "help",
      "Print this help text"
    },
    { NULL, -1, -1, NULL, NULL, NULL }
};

static int run_command(char *cmd, int maxargs, char **args) {
    int r = 0;
    const struct command *c;

    if (!strcmp("exit", cmd) || !strcmp("quit", cmd)) {
        exit(EXIT_SUCCESS);
    }
    for (c = commands; c->name; c++) {
        if (!strcmp(cmd, c->name))
            break;
    }
    if (c->name) {
        r = chk_args(c, maxargs, args);
        if (r == 0) {
            r = (*c->handler)(args);
        }
    } else {
        fprintf(stderr, "Unknown command '%s'\n", cmd);
        r = -1;
    }

    return r;
}

static char *readline_path_generator(const char *text, int state) {
    static int current = 0;
    static char **children = NULL;
    static int nchildren = 0;

    if (state == 0) {
        char *end = strrchr(text, SEP);
        char *path;
        if (end == NULL) {
            if ((path = strdup("/*")) == NULL)
                return NULL;
        } else {
            end += 1;
	    path = xcalloc(1, end - text + 2);
            if (path == NULL)
                return NULL;
            strncpy(path, text, end - text);
            strcat(path, "*");
        }

        for (;current < nchildren; current++)
            free((void *) children[current]);
        free((void *) children);
        nchildren = aug_match(aug, path, &children);
        current = 0;
        free(path);
    }

    while (current < nchildren) {
        char *child = children[current];
        current += 1;
        if (!strncmp(child, text, strlen(text))) {
            if (child_count(child) > 0) {
                char *c = realloc(child, strlen(child)+2);
                if (c == NULL)
                    return NULL;
                child = c;
                strcat(child, "/");
            }
            rl_filename_completion_desired = 1;
            rl_completion_append_character = '\0';
            return child;
        } else {
            free(child);
        }
    }
    return NULL;
}

static char *readline_command_generator(const char *text, int state) {
    static int current = 0;
    const char *name;

    if (state == 0)
        current = 0;

    rl_completion_append_character = ' ';
    while ((name = commands[current].name) != NULL) {
        current += 1;
        if (!strncmp(text, name, strlen(text)))
            return strdup(name);
    }
    return NULL;
}

#define	HAVE_RL_COMPLETION_MATCHES	/* XXX no AutoFu yet */
#ifndef HAVE_RL_COMPLETION_MATCHES
typedef char *rl_compentry_func_t(const char *, int);
static char **rl_completion_matches(/*@unused@*/ const char *text,
                           /*@unused@*/ rl_compentry_func_t *func) {
    return NULL;
}
#endif

static char **readline_completion(const char *text, int start, /*@unused@*/ int end) {
    if (start == 0)
        return rl_completion_matches(text, readline_command_generator);
    else
        return rl_completion_matches(text, readline_path_generator);

    return NULL;
}

static void readline_init(void) {
    rl_readline_name = "augtool";
    rl_attempted_completion_function = readline_completion;
    rl_completion_entry_function = readline_path_generator;
}

__attribute__((noreturn))
static void usage(void) {
    fprintf(stderr, "Usage: %s [OPTIONS] [COMMAND]\n", progname);
    fprintf(stderr, "Load the Augeas tree and modify it. If no COMMAND is given, run interactively\n");
    fprintf(stderr, "Run '%s help' to get a list of possible commands.\n",
            progname);
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr, "  -c, --typecheck    typecheck lenses\n");
    fprintf(stderr, "  -b, --backup       preserve originals of modified files with\n"
                    "                     extension '.augsave'\n");
    fprintf(stderr, "  -n, --new          save changes in files with extension '.augnew',\n"
                    "                     leave original unchanged\n");
    fprintf(stderr, "  -r, --root ROOT    use ROOT as the root of the filesystem\n");
    fprintf(stderr, "  -I, --include DIR  search DIR for modules; can be given mutiple times\n");
    fprintf(stderr, "  --nostdinc         do not search the builtin default directories for modules\n");
    fprintf(stderr, "  --noload           do not load any files into the tree on startup\n");
    fprintf(stderr, "  --noautoload       do not autoload modules from the search path\n");

    exit(EXIT_FAILURE);
}

static void parse_opts(int argc, char **argv) {
    int opt;
    size_t loadpathlen = 0;
    enum {
        VAL_NO_STDINC = CHAR_MAX + 1,
        VAL_NO_LOAD = VAL_NO_STDINC + 1,
        VAL_NO_AUTOLOAD = VAL_NO_LOAD + 1
    };
    struct option options[] = {
        { "help",      0, 0, 'h' },
        { "typecheck", 0, 0, 'c' },
        { "backup",    0, 0, 'b' },
        { "new",       0, 0, 'n' },
        { "root",      1, 0, 'r' },
        { "include",   1, 0, 'I' },
        { "nostdinc",  0, 0, VAL_NO_STDINC },
        { "noload",    0, 0, VAL_NO_LOAD },
        { "noautoload", 0, 0, VAL_NO_AUTOLOAD },
        { 0, 0, 0, 0}
    };
    int idx;

    while ((opt = getopt_long(argc, argv, "hnbcr:I:", options, &idx)) != -1) {
        switch(opt) {
        case 'c':
            flags |= AUG_TYPE_CHECK;
            break;
        case 'b':
            flags |= AUG_SAVE_BACKUP;
            break;
        case 'n':
            flags |= AUG_SAVE_NEWFILE;
            break;
        case 'h':
            usage();
            break;
        case 'r':
            root = optarg;
            break;
        case 'I':
            argz_add(&loadpath, &loadpathlen, optarg);
            break;
        case VAL_NO_STDINC:
            flags |= AUG_NO_STDINC;
            break;
        case VAL_NO_LOAD:
            flags |= AUG_NO_LOAD;
            break;
        case VAL_NO_AUTOLOAD:
            flags |= AUG_NO_MODL_AUTOLOAD;
            break;
        default:
            usage();
            break;
        }
    }
    argz_stringify(loadpath, loadpathlen, PATH_SEP_CHAR);
}

static int main_loop(void) {
    static const int maxargs = 3;
    char *line = NULL;
    char *cmd, *args[maxargs];
    int ret = 0;
    size_t len = 0;

    while(1) {
        char *dup_line;

        if (isatty(fileno(stdin))) {
            line = readline("augtool> ");
        } else if (getline(&line, &len, stdin) == -1) {
            return ret;
        }
        cleanstr(line, '\n');
        if (line == NULL) {
            printf("\n");
            return ret;
        }
        if (line[0] == '#')
            continue;

        dup_line = strdup(line);
        if (dup_line == NULL) {
            fprintf(stderr, "Out of memory\n");
            return -1;
        }

        cmd = parseline(dup_line, maxargs, args);
        if (cmd != NULL && strlen(cmd) > 0) {
            int r;
            r = run_command(cmd, maxargs, args);
            if (r < 0)
                ret = -1;
            if (isatty(fileno(stdin)))
                add_history(line);
        }
        free(dup_line);
    }
}

int main(int argc, char **argv)
{
    int r;

    parse_opts(argc, argv);

    aug = aug_init(root, loadpath, flags);
    if (aug == NULL) {
        fprintf(stderr, "Failed to initialize Augeas\n");
        exit(EXIT_FAILURE);
    }
    readline_init();
    if (optind < argc) {
        // Accept one command from the command line
        r = run_command(argv[optind], argc - optind, argv+optind+1);
    } else {
        r = main_loop();
    }

    return r == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

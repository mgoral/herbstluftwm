/** Copyright 2011-2012 Thorsten Wißmann. All rights reserved.
 *
 * This software is licensed under the "Simplified BSD License".
 * See LICENSE for details */

#include "ipc-protocol.h"
#include "command.h"
#include "utils.h"
#include "settings.h"
#include "layout.h"
#include "key.h"

#include <glib.h>
#include <string.h>
#include <stdlib.h>

static char* completion_directions[]    = { "left", "right", "down", "up",NULL};
static char* completion_focus_args[]    = { "-i", "-e", NULL };
static char* completion_unrule_args[]   = { "-F", "--all", NULL };
static char* completion_keyunbind_args[]= { "-F", "--all", NULL };
static char* completion_flag_args[]     = { "on", "off", "toggle", NULL };
static char* completion_status[]        = { "status", NULL };

static bool no_completion(int argc, char** argv, int pos) {
    return false;
}

static bool first_parameter_is_tag(int argc, char** argv, int pos);
static bool first_parameter_is_flag(int argc, char** argv, int pos);
static bool keybind_parameter_expected(int argc, char** argv, int pos);

/* find out, if a parameter still expects a parameter at a certain index.
 * only if this returns true, than a completion will be searched.
 *
 * if no match is found, then it defaults to "command still expects a
 * parameter".
 */
struct {
    char*   command;    /* the first argument */
    int     min_index;  /* rule will only be considered */
                        /* if current pos >= min_index */
    bool    (*function)(int argc, char** argv, int pos);
} g_parameter_expected[] = {
    { "quit",           0,  no_completion },
    { "reload",         0,  no_completion },
    { "version",        0,  no_completion },
    { "list_monitors",  0,  no_completion },
    { "list_commands",  0,  no_completion },
    { "list_keybinds",  0,  no_completion },
    { "add_monitor",    7,  no_completion },
    { "cycle",          2,  no_completion },
    { "cycle_all",      2,  no_completion },
    { "cycle_layout",   2,  no_completion },
    { "close",          0,  no_completion },
    { "dump",           2,  no_completion },
    { "floating",       3,  no_completion },
    { "floating",       2,  first_parameter_is_tag },
    { "merge_tag",      3,  no_completion },
    { "focus",          3,  no_completion },
    { "focus",          2,  first_parameter_is_flag },
    { "shift",          3,  no_completion },
    { "shift",          2,  first_parameter_is_flag },
    { "split",          3,  no_completion },
    { "fullscreen",     2,  no_completion },
    { "pseudotile",     2,  no_completion },
    { "pad",            6,  no_completion },
    { "keybind",        2,  keybind_parameter_expected },
    { "keyunbind",      2,  no_completion },
    { "monitor_rect",   3,  no_completion },
    { "mousebind",      3,  no_completion },
    { "mouseunbind",    0,  no_completion },
    { "move_monitor",   7,  no_completion },
    { "layout",         2,  no_completion },
    { "load",           3,  no_completion },
    { "load",           2,  first_parameter_is_tag },
    { "move",           2,  no_completion },
    { "move_index",     2,  no_completion },
    { "raise",          2,  no_completion },
    { "rename",         3,  no_completion },
    { "remove",         0,  no_completion },
    { "remove_monitor", 2,  no_completion },
    { "resize",         3,  no_completion },
    { "unrule",         2,  no_completion },
    { "use",            2,  no_completion },
    { "use_index",      2,  no_completion },
    { "add",            2,  no_completion },
    { "get",            2,  no_completion },
    { "toggle",         2,  no_completion },
    { "set",            3,  no_completion },
    { "set_layout",     2,  no_completion },
    { "tag_status",     2,  no_completion },
    { 0 },
};

/* list of completions, if a line matches, then it will be used, the order
 * doesnot matter */
struct {
    char*   command;
    int     index;      /* which parameter to complete */
                        /* command name is index = 0 */
                        /* -1 will match any position */
    /* === various methods, how to complete === */
    /* completion by function */
    void (*function)(int argc, char** argv, int pos, GString** output);
    /* completion by a list of strings */
    char** list;
} g_completions[] = {
    /* name ,       index,  completion method                   */
    { "add_monitor",    2,  .function = complete_against_tags },
    { "dump",           1,  .function = complete_against_tags },
    { "floating",       1,  .function = complete_against_tags },
    { "floating",       1,  .list = completion_flag_args },
    { "floating",       1,  .list = completion_status },
    { "floating",       2,  .list = completion_flag_args },
    { "floating",       2,  .list = completion_status },
    { "focus",          1,  .list = completion_directions },
    { "focus",          1,  .list = completion_focus_args },
    { "focus",          2,  .list = completion_directions },
    { "fullscreen",     1,  .list = completion_flag_args },
    { "layout",         1,  .function = complete_against_tags },
    { "load",           1,  .function = complete_against_tags },
    { "merge_tag",      1,  .function = complete_against_tags },
    { "merge_tag",      2,  .function = complete_merge_tag },
    { "move",           1,  .function = complete_against_tags },
    { "pseudotile",     1,  .list = completion_flag_args },
    { "keybind",       -1,  .function = complete_against_keybind_command },
    { "keyunbind",      1,  .list = completion_keyunbind_args },
    { "keyunbind",      1,  .function = complete_against_keybinds },
    { "rename",         1,  .function = complete_against_tags },
    { "resize",         1,  .list = completion_directions },
    { "shift",          1,  .list = completion_directions },
    { "shift",          1,  .list = completion_focus_args },
    { "shift",          2,  .list = completion_directions },
    { "set",            1,  .function = complete_against_settings },
    { "get",            1,  .function = complete_against_settings },
    { "toggle",         1,  .function = complete_against_settings },
    { "cycle_value",    1,  .function = complete_against_settings },
    { "set_layout",     1,  .list = g_layout_names },
    { "unrule",         1,  .list = completion_unrule_args },
    { "use",            1,  .function = complete_against_tags },
    { 0 },
};

int call_command(int argc, char** argv, GString** output) {
    if (argc <= 0) {
        return HERBST_COMMAND_NOT_FOUND;
    }
    int i = 0;
    CommandBinding* bind = NULL;
    while (g_commands[i].cmd.standard != NULL) {
        if (!strcmp(g_commands[i].name, argv[0])) {
            // if command was found
            bind = g_commands + i;
            break;
        }
        i++;
    }
    if (!bind) {
        return HERBST_COMMAND_NOT_FOUND;
    }
    int status;
    if (bind->has_output) {
        status = bind->cmd.standard(argc, argv, output);
    } else {
        status = bind->cmd.no_output(argc, argv);
    }
    return status;
}

int call_command_no_output(int argc, char** argv) {
    GString* output = g_string_new("");
    int status = call_command(argc, argv, &output);
    g_string_free(output, true);
    return status;
}


int list_commands(int argc, char** argv, GString** output)
{
    int i = 0;
    while (g_commands[i].cmd.standard != NULL) {
        *output = g_string_append(*output, g_commands[i].name);
        *output = g_string_append(*output, "\n");
        i++;
    }
    return 0;
}

void complete_against_list(char* needle, char** list, GString** output) {
    size_t len = strlen(needle);
    while (*list) {
        char* name = *list;
        if (!strncmp(needle, name, len)) {
            *output = g_string_append(*output, name);
            *output = g_string_append(*output, "\n");
        }
        list++;
    }
}

void complete_against_tags(int argc, char** argv, int pos, GString** output) {
    char* needle;
    if (pos >= argc) {
        needle = "";
    } else {
        needle = argv[pos];
    }
    size_t len = strlen(needle);
    for (int i = 0; i < g_tags->len; i++) {
        char* name = g_array_index(g_tags, HSTag*, i)->name->str;
        if (!strncmp(needle, name, len)) {
            *output = g_string_append(*output, name);
            *output = g_string_append(*output, "\n");
        }
    }
}

void complete_merge_tag(int argc, char** argv, int pos, GString** output) {
    char* first = (argc >= 1) ? argv[1] : "";
    char* needle;
    if (pos >= argc) {
        needle = "";
    } else {
        needle = argv[pos];
    }
    size_t len = strlen(needle);
    for (int i = 0; i < g_tags->len; i++) {
        char* name = g_array_index(g_tags, HSTag*, i)->name->str;
        if (!strcmp(name, first)) {
            // merge target must not be equal to tag to remove
            continue;
        }
        if (!strncmp(needle, name, len)) {
            *output = g_string_append(*output, name);
            *output = g_string_append(*output, "\n");
        }
    }
}

void complete_against_settings(int argc, char** argv, int pos, GString** output)
{
    char* needle;
    if (pos >= argc) {
        needle = "";
    } else {
        needle = argv[pos];
    }
    size_t len = strlen(needle);
    bool is_toggle_command = !strcmp(argv[0], "toggle");
    // complete with setting name
    for (int i = 0; i < settings_count(); i++) {
        if (is_toggle_command && g_settings[i].type != HS_Int) {
            continue;
        }
        // only check the first len bytes
        if (!strncmp(needle, g_settings[i].name, len)) {
            *output = g_string_append(*output, g_settings[i].name);
            *output = g_string_append(*output, "\n");
        }
    }
}

void complete_against_keybinds(int argc, char** argv, int pos, GString** output) {
    char* needle;
    if (pos >= argc) {
        needle = "";
    } else {
        needle = argv[pos];
    }
    key_find_binds(needle, output);
}

bool parameter_expected(int argc, char** argv, int pos) {
    if (pos <= 0 || argc < 1) {
        /* no parameter if there is no command */
        return false;
    }
    for (int i = 0; i < LENGTH(g_parameter_expected)
                    && g_parameter_expected[i].command; i++) {
        if (pos < g_parameter_expected[i].min_index) {
            continue;
        }
        if (!strcmp(g_parameter_expected[i].command, argv[0])) {
            return g_parameter_expected[i].function(argc, argv, pos);
        }
    }
    return true;
}

int complete_command(int argc, char** argv, GString** output) {
    // usage: complete POSITION command to complete ...
    if (argc < 2) {
        return HERBST_INVALID_ARGUMENT;
    }
    // index must be between first and als arg of "commmand to complete ..."
    int position = CLAMP(atoi(argv[1]), 0, argc-2);
    (void)SHIFT(argc, argv);
    (void)SHIFT(argc, argv);
    return complete_against_commands(argc, argv, position, output);
}

void complete_against_keybind_command(int argc, char** argv, int position,
                                      GString** output) {
    if (argc <  2 || position < 2) {
        return;
    }
    complete_against_commands(argc - 2, argv + 2, position - 2, output);
}

int complete_against_commands(int argc, char** argv, int position,
                              GString** output) {
    // complete command
    if (position == 0) {
        char* str = (argc >= 1) ? argv[0] : "";
        size_t len = strlen(str);
        int i = 0;
        while (g_commands[i].cmd.standard != NULL) {
            // only check the first len bytes
            if (!strncmp(str, g_commands[i].name, len)) {
                *output = g_string_append(*output, g_commands[i].name);
                *output = g_string_append(*output, "\n");
            }
            i++;
        }
        return 0;
    }
    if (!parameter_expected(argc, argv, position)) {
        return HERBST_NO_PARAMETER_EXPECTED;
    }
    if (argc >= 1) {
        char* cmd_str = (argc >= 1) ? argv[0] : "";
        // complete parameters for commands
        for (int i = 0; i < LENGTH(g_completions); i++) {
            if (!g_completions[i].command
                || (g_completions[i].index != -1
                    && position != g_completions[i].index)
                || strcmp(cmd_str, g_completions[i].command)) {
                continue;
            }
            char* needle = (position < argc) ? argv[position] : "";
            if (!needle) {
                needle = "";
            }
            // try to complete
            if (g_completions[i].function) {
                g_completions[i].function(argc, argv, position, output);
            }
            if (g_completions[i].list) {
                complete_against_list(needle, g_completions[i].list,
                                      output);
            }
        }
    }
    return 0;
}

bool first_parameter_is_tag(int argc, char** argv, int pos) {
    // only complete if first parameter is a valid tag
    if (argc >= 2 && find_tag(argv[1]) && pos == 2) {
        return true;
    } else {
        return false;
    }
}

bool first_parameter_is_flag(int argc, char** argv, int pos) {
    // only complete if first parameter is a flag like -i or -e
    if (argc >= 2 && argv[1][0] == '-' && pos == 2) {
        return true;
    } else {
        return false;
    }
}

bool keybind_parameter_expected(int argc, char** argv, int pos) {
    if (argc < 2 || pos < 2) {
        return true;
    }
    if (pos == 2) {
        // at least a command name always is expected
        return true;
    }
    return parameter_expected(argc - 2, argv + 2, pos - 2);
}


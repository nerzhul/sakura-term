#pragma once

/* Globals for command line parameters */
static const char *option_workdir;
static const char *option_font;
static const char *option_execute;
static gboolean option_xterm_execute=FALSE;
static gchar **option_xterm_args;
static gboolean option_version = FALSE;
static gint option_ntabs = 1;
static gint option_login = FALSE;
static const char *option_title;
static const char *option_icon;
static int option_rows, option_columns;
static gboolean option_hold=FALSE;
static char *option_config_file;
static gboolean option_fullscreen;
static gboolean option_maximize;
static gint option_colorset;

static GOptionEntry entries[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version, N_("Print version number"), NULL },
	{ "font", 'f', 0, G_OPTION_ARG_STRING, &option_font, N_("Select initial terminal font"), NULL },
	{ "ntabs", 'n', 0, G_OPTION_ARG_INT, &option_ntabs, N_("Select initial number of tabs"), NULL },
	{ "working-directory", 'd', 0, G_OPTION_ARG_STRING, &option_workdir, N_("Set working directory"), NULL },
	{ "execute", 'x', 0, G_OPTION_ARG_STRING, &option_execute, N_("Execute command"), NULL },
	{ "xterm-execute", 'e', 0, G_OPTION_ARG_NONE, &option_xterm_execute, N_("Execute command (last option in the command line)"), NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &option_xterm_args, NULL, NULL },
	{ "login", 'l', 0, G_OPTION_ARG_NONE, &option_login, N_("Login shell"), NULL },
	{ "title", 't', 0, G_OPTION_ARG_STRING, &option_title, N_("Set window title"), NULL },
	{ "icon", 'i', 0, G_OPTION_ARG_STRING, &option_icon, N_("Set window icon"), NULL },
	{ "xterm-title", 'T', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &option_title, NULL, NULL },
	{ "columns", 'c', 0, G_OPTION_ARG_INT, &option_columns, N_("Set columns number"), NULL },
	{ "rows", 'r', 0, G_OPTION_ARG_INT, &option_rows, N_("Set rows number"), NULL },
	{ "hold", 'h', 0, G_OPTION_ARG_NONE, &option_hold, N_("Hold window after execute command"), NULL },
	{ "maximize", 'm', 0, G_OPTION_ARG_NONE, &option_maximize, N_("Maximize window"), NULL },
	{ "fullscreen", 's', 0, G_OPTION_ARG_NONE, &option_fullscreen, N_("Fullscreen mode"), NULL },
	{ "config-file", 0, 0, G_OPTION_ARG_FILENAME, &option_config_file, N_("Use alternate configuration file"), NULL },
	{ "colorset", 0, 0, G_OPTION_ARG_INT, &option_colorset, N_("Select initial colorset"), NULL },
	{ NULL }
};

void sakura_init();
void sakura_add_tab();
void sakura_sanitize_working_directory();

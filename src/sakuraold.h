#pragma once

#include <vte/vte.h>
#include "gettext.h"
#include "sakura.h"

class Terminal;
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



extern Sakura *sakura;

extern GQuark term_data_id;

static const gint FORWARD = 1;

#define  sakura_set_config_string(key, value) do {\
	g_key_file_set_value(sakura->cfg, cfg_group, key, value);\
	sakura->config_modified=true;\
	} while(0);

#define  sakura_set_config_integer(key, value) do {\
	g_key_file_set_integer(sakura->cfg, cfg_group, key, value);\
	sakura->config_modified=true;\
	} while(0);

#define  sakura_set_config_boolean(key, value) do {\
	g_key_file_set_boolean(sakura->cfg, cfg_group, key, value);\
	sakura->config_modified=true;\
	} while(0);

static const char cfg_group[] = "sakura";

void     sakura_config_done();
// Callbacks
void     sakura_increase_font (GtkWidget *, void *);
void     sakura_decrease_font (GtkWidget *, void *);
void sakura_set_tab_label_text(const gchar *, gint page);
void     sakura_conf_changed (GtkWidget *, void *);
//static gboolean sakura_notebook_focus_in (GtkWidget *, void *);

void search(VteTerminal *vte, const char *pattern, bool reverse);
void     sakura_fullscreen (GtkWidget *, void *);

/* Menuitem callbacks */
void sakura_color_dialog(GtkWidget *, void *);
void sakura_set_title_dialog(GtkWidget *, void *);
void sakura_open_url(GtkWidget *, void *);
void sakura_show_first_tab(GtkWidget *widget, void *data);
void sakura_tabs_on_bottom(GtkWidget *widget, void *data);
void sakura_less_questions(GtkWidget *widget, void *data);
void sakura_show_close_button(GtkWidget *widget, void *data);
void sakura_disable_numbered_tabswitch(GtkWidget *, void *);
void sakura_use_fading(GtkWidget *, void *);
void sakura_setname_entry_changed(GtkWidget *, void *);
void sakura_urgent_bell(GtkWidget *widget, void *data);
void sakura_audible_bell(GtkWidget *widget, void *data);
void sakura_blinking_cursor(GtkWidget *widget, void *data);
void sakura_allow_bold(GtkWidget *widget, void *data);
void sakura_stop_tab_cycling_at_end_tabs(GtkWidget *widget, void *data);
void sakura_set_cursor(GtkWidget *widget, void *data);
void sakura_set_palette(GtkWidget *widget, void *data);
void sakura_open_mail(GtkWidget *widget, void *data);
void sakura_copy_url(GtkWidget *widget, void *data);
/* Misc */
void sakura_error(const char *, ...);

/* Callbacks */
gboolean sakura_button_press(GtkWidget *, GdkEventButton *, gpointer);
void sakura_child_exited(GtkWidget *, void *);
void sakura_eof(GtkWidget *, void *);
void sakura_title_changed(GtkWidget *, void *);
void sakura_closebutton_clicked(GtkWidget *, void *);
void sakura_spawn_callback(VteTerminal *vte, GPid pid, GError *error, gpointer user_data);
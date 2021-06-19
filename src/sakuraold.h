#pragma once

#include <vte/vte.h>
#include "gettext.h"
#include "sakura.h"

class Terminal;
/* Globals for command line parameters */
static const char *option_workdir;
static const char *option_font;
static const char *option_execute;
static gboolean option_xterm_execute = FALSE;
static gchar **option_xterm_args;
static gboolean option_version = FALSE;
static gint option_ntabs = 1;
static gint option_login = FALSE;
static const char *option_title;
static const char *option_icon;
static int option_rows, option_columns;
static gboolean option_hold = FALSE;
static char *option_config_file;
static gboolean option_fullscreen;
static gboolean option_maximize;
static gint option_colorset;

extern GOptionEntry entries[];

extern Sakura *sakura;

extern GQuark term_data_id;

static const gint FORWARD = 1;

#define sakura_set_config_string(key, value)                                                       \
	do {                                                                                       \
		g_key_file_set_value(sakura->cfg, cfg_group, key, value);                          \
		sakura->config_modified = true;                                                    \
	} while (0);

#define sakura_set_config_integer(key, value)                                                      \
	do {                                                                                       \
		g_key_file_set_integer(sakura->cfg, cfg_group, key, value);                        \
		sakura->config_modified = true;                                                    \
	} while (0);

#define sakura_set_config_boolean(key, value)                                                      \
	do {                                                                                       \
		g_key_file_set_boolean(sakura->cfg, cfg_group, key, value);                        \
		sakura->config_modified = true;                                                    \
	} while (0);

static const char cfg_group[] = "sakura";

void sakura_config_done();
// Callbacks
void sakura_set_tab_label_text(const gchar *, gint page);
void sakura_conf_changed(GtkWidget *, void *);
// static gboolean sakura_notebook_focus_in (GtkWidget *, void *);

void search(VteTerminal *vte, const char *pattern, bool reverse);
void sakura_fullscreen(GtkWidget *, void *);

/* Menuitem callbacks */
void sakura_color_dialog(GtkWidget *, void *);
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
/* Misc */
void sakura_error(const char *, ...);

/* Callbacks */
gboolean sakura_button_press(GtkWidget *, GdkEventButton *, gpointer);
void sakura_child_exited(GtkWidget *, void *);
void sakura_eof(GtkWidget *, void *);
void sakura_title_changed(GtkWidget *, void *);
void sakura_closebutton_clicked(GtkWidget *, void *);
void sakura_spawn_callback(VteTerminal *vte, GPid pid, GError *error, gpointer user_data);
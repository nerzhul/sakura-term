#pragma once

#include <vte/vte.h>
#include "gettext.h"

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

#define NUM_COLORSETS 6

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

struct terminal {
	GtkWidget *hbox;
	GtkWidget *vte;     /* Reference to VTE terminal */
	GPid pid;          /* pid of the forked process */
	GtkWidget *scrollbar;
	GtkWidget *label;
	gchar *label_text;
	bool label_set_byuser;
	GtkBorder padding;   /* inner-property data */
	int colorset;
};

static struct {
	GtkWidget *main_window;
	GtkWidget *notebook;
	GtkWidget *menu;
	PangoFontDescription *font;
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	const GdkRGBA *palette;
	char *current_match;
	guint width;
	guint height;
	glong columns;
	glong rows;
	gint scroll_lines;
	gint label_count;
	VteCursorShape cursor_type;
	bool first_tab;
	bool show_scrollbar;
	bool show_closebutton;
	bool tabs_on_bottom;
	bool less_questions;
	bool urgent_bell;
	bool audible_bell;
	bool blinking_cursor;
	bool stop_tab_cycling_at_end_tabs;
	bool allow_bold;
	bool fullscreen;
	bool keep_fc;                    /* Global flag to indicate that we don't want changes in the files and columns values */
	bool config_modified;            /* Configuration has been modified */
	bool externally_modified;        /* Configuration file has been modified by another process */
	bool resized;
	bool disable_numbered_tabswitch; /* For disabling direct tabswitching key */
	bool focused;                    /* For fading feature */
	bool first_focus;                /* First time gtkwindow recieve focus when is created */
	bool faded;			 /* Fading state */
	bool use_fading;
	bool scrollable_tabs;
	GtkWidget *item_copy_link;       /* We include here only the items which need to be hidden */
	GtkWidget *item_open_link;
	GtkWidget *item_open_mail;
	GtkWidget *open_link_separator;
	GKeyFile *cfg;
	GtkCssProvider *provider;
	char *configfile;
	char *icon;
	char *word_chars;                /* Exceptions for word selection */
	gchar *tab_default_title;
	gint last_colorset;
	gint add_tab_accelerator;
	gint del_tab_accelerator;
	gint switch_tab_accelerator;
	gint move_tab_accelerator;
	gint copy_accelerator;
	gint scrollbar_accelerator;
	gint open_url_accelerator;
	gint font_size_accelerator;
	gint set_tab_name_accelerator;
	gint search_accelerator;
	gint set_colorset_accelerator;
	gint add_tab_key;
	gint del_tab_key;
	gint prev_tab_key;
	gint next_tab_key;
	gint copy_key;
	gint paste_key;
	gint scrollbar_key;
	gint set_tab_name_key;
	gint search_key;
	gint fullscreen_key;
	gint increase_font_size_key;
	gint decrease_font_size_key;
	gint set_colorset_keys[NUM_COLORSETS];
	VteRegex *http_vteregexp, *mail_vteregexp;
	char *argv[3];
} sakura;

static GQuark term_data_id = 0;

#define  sakura_get_page_term( sakura, page_idx )  \
    (struct terminal*)g_object_get_qdata(  \
            G_OBJECT( gtk_notebook_get_nth_page( (GtkNotebook*)sakura.notebook, page_idx ) ), term_data_id);

void sakura_init();
void sakura_add_tab();
void sakura_sanitize_working_directory();


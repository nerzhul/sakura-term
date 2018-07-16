#pragma once

#include "config.h"

namespace Gtk {
class Window;
}

#define DEFAULT_COLUMNS 80
#define DEFAULT_ROWS 24

class Sakura {
public:
	Sakura();
	~Sakura();
	static void init();
	void destroy(GtkWidget *);

	gboolean on_key_press(GtkWidget *widget, GdkEventKey *event);
	gboolean on_focus_in(GtkWidget *widget, GdkEvent *event);
	gboolean on_focus_out(GtkWidget *widget, GdkEvent *event);
	void on_child_exited(GtkWidget *widget);
	void on_eof(GtkWidget *widget);
	void on_page_removed(GtkWidget *widget);

	void beep(GtkWidget *);
	void close_tab(GtkWidget *);
	void del_tab(gint, bool exit_if_needed = false);
	void toggle_fullscreen(GtkWidget *);
	void toggle_numbered_tabswitch_option(GtkWidget *widget);
	gboolean notebook_scoll(GtkWidget *widget, GdkEventScroll *event);

	void set_colors();

	Gtk::Window *main_window = nullptr;
	GtkWidget *notebook;
	GtkWidget *menu;
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	char *current_match;
	guint width;
	guint height;
	glong columns = DEFAULT_COLUMNS;
	glong rows = DEFAULT_ROWS;
	gint label_count;
	bool keep_fc;                    /* Global flag to indicate that we don't want changes in the files and columns values */
	bool config_modified = false;            /* Configuration has been modified */
	bool externally_modified;        /* Configuration file has been modified by another process */
	bool resized;
	bool focused;                    /* For fading feature */
	bool first_focus;                /* First time gtkwindow recieve focus when is created */
	bool faded;			 /* Fading state */
	GtkWidget *item_copy_link;       /* We include here only the items which need to be hidden */
	GtkWidget *item_open_link;
	GtkWidget *item_open_mail;
	GtkWidget *open_link_separator;
	GKeyFile *cfg;
	GtkCssProvider *provider;
	gchar *tab_default_title;
	VteRegex *http_vteregexp, *mail_vteregexp;
	char *argv[3];
	Config config;

private:
	bool m_fullscreen = false;
};

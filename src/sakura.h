#pragma once

#include "config.h"
#include <gtkmm.h>

class SakuraWindow;
class Terminal;

#define DEFAULT_COLUMNS 80
#define DEFAULT_ROWS 24

class Sakura {
public:
	Sakura();
	~Sakura();
	bool destroy(GdkEventAny *);
	void init_popup();

	void copy();
	void paste();

	// Some old static callbacks to refactor
	static void increase_font(GtkWidget *, void *);
	static void decrease_font(GtkWidget *, void *);

	void copy_url();
	void open_url();
	void open_mail();
	void open_title_dialog();

	gboolean on_key_press(GtkWidget *widget, GdkEventKey *event);
	void on_child_exited(GtkWidget *widget);
	void on_eof(GtkWidget *widget);

	void beep(GtkWidget *);
	void close_tab();
	void del_tab(gint, bool exit_if_needed = false);
	void toggle_numbered_tabswitch_option(GtkWidget *widget);

	void show_search_dialog();

	void set_colors();

	void fade_in();
	void fade_out();
	void set_size();

	void set_name_dialog();
	void set_font();

	std::unique_ptr<SakuraWindow> main_window;
	Gtk::Menu *menu;
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	char *current_match;
	int width;
	int height;
	glong columns = DEFAULT_COLUMNS;
	glong rows = DEFAULT_ROWS;
	gint label_count = 1;
	bool keep_fc = false;                    /* Global flag to indicate that we don't want changes in the files and columns values */
	bool config_modified = false;            /* Configuration has been modified */
	bool externally_modified = false;        /* Configuration file has been modified by another process */
	bool faded = false;			 /* Fading state */
	Gtk::MenuItem *item_copy_link;       /* We include here only the items which need to be hidden */
	Gtk::MenuItem *item_open_link;
	Gtk::MenuItem *item_open_mail;
	Gtk::SeparatorMenuItem *open_link_separator;
	GKeyFile *cfg;
	Glib::RefPtr<Gtk::CssProvider> provider;
	VteRegex *http_vteregexp, *mail_vteregexp;
	char *argv[3];
	Config config;
private:
	void set_color_set(int cs);

	void show_font_dialog();

};

#pragma once

#include "config.h"

class Sakura {
public:
	static void init();
	static void destroy();

	gboolean on_key_press(GtkWidget *widget, GdkEventKey *event);

	void beep(GtkWidget *);

	static void del_tab(gint);

	GtkWidget *main_window;
	GtkWidget *notebook;
	GtkWidget *menu;
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	char *current_match;
	guint width;
	guint height;
	glong columns;
	glong rows;
	gint label_count;
	bool fullscreen;
	bool keep_fc;                    /* Global flag to indicate that we don't want changes in the files and columns values */
	bool config_modified;            /* Configuration has been modified */
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
};

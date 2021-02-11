#pragma once

#include <gtk/gtk.h>
#include <gtkmm/label.h>
#include <gtkmm/box.h>

class Terminal
{
public:
	Terminal();
	~Terminal();

	/**
	 * Called by g_object_set_qdata_full on terminal removal
	 */
	static void free(Terminal *term);

	Gtk::Box hbox;
	GtkWidget *vte;     /* Reference to VTE terminal */
	GPid pid = 0;          /* pid of the forked process */
	GtkWidget *scrollbar;
	Gtk::Label label;
	gchar *label_text;
	bool label_set_byuser = false;
	GtkBorder padding;   /* inner-property data */
	int colorset;
	gulong bg_image_callback_id = 0;
	GdkPixbuf *bg_image = nullptr;

	static gchar *tab_default_title;
private:
};

#pragma once

#include <gtk/gtk.h>

#define  sakura_get_page_term( sakura, page_idx )  \
    (Terminal *)g_object_get_qdata(  \
            G_OBJECT( gtk_notebook_get_nth_page(sakura->main_window->notebook->gobj(), page_idx )), term_data_id);

class Terminal
{
public:
	Terminal();
	~Terminal();

	/**
	 * Called by g_object_set_qdata_full on terminal removal
	 */
	static void free(Terminal *term);

	GtkWidget *hbox;
	GtkWidget *vte;     /* Reference to VTE terminal */
	GPid pid = 0;          /* pid of the forked process */
	GtkWidget *scrollbar;
	GtkWidget *label;
	gchar *label_text;
	bool label_set_byuser = false;
	GtkBorder padding;   /* inner-property data */
	int colorset;
	gulong bg_image_callback_id = 0;
	GdkPixbuf *bg_image = nullptr;

	static gchar *tab_default_title;
private:
};

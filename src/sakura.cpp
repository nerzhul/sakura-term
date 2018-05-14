#include <gtk/gtk.h>
#include <vte/vte.h>
#include "sakura.h"
#include "debug.h"
#include "sakuraold.h"

void Sakura::destroy()
{
	SAY("Destroying sakura");

	/* Delete all existing tabs */
	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) >= 1) {
		del_tab(-1);
	}

	g_key_file_free(sakura.cfg);

	pango_font_description_free(sakura.font);

	free(sakura.configfile);

	gtk_main_quit();

}

/* Delete the notebook tab passed as a parameter */
void Sakura::del_tab(gint page)
{
	struct terminal *term;
	gint npages;

	term = sakura_get_page_term(sakura, page);
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* When there's only one tab use the shell title, if provided */
	if (npages==2) {
		const char *title;

		term = sakura_get_page_term(sakura, 0);
		title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title!=NULL)
			gtk_window_set_title(GTK_WINDOW(sakura.main_window), title);
	}

	term = sakura_get_page_term(sakura, page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if ( npages == 2) {
		if (sakura.first_tab) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);
		}
		sakura.keep_fc=true;
	}

	gtk_widget_hide(term->hbox);
	gtk_notebook_remove_page(GTK_NOTEBOOK(sakura.notebook), page);

	/* Find the next page, if it exists, and grab focus */
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		term = sakura_get_page_term(sakura, page);
		gtk_widget_grab_focus(term->vte);
	}
}

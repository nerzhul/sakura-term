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

gboolean Sakura::on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (event->type!=GDK_KEY_PRESS) return FALSE;

	unsigned int topage = 0;

	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Add/delete tab keybinding pressed */
	if ( (event->state & sakura.add_tab_accelerator)==sakura.add_tab_accelerator &&
		 keycode==sakura_tokeycode(sakura.keymap.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ( (event->state & sakura.del_tab_accelerator)==sakura.del_tab_accelerator &&
				keycode==sakura_tokeycode(sakura.keymap.del_tab_key) ) {
		/* Delete current tab */
		sakura_close_tab(NULL, NULL);
		return TRUE;
	}

	/* Switch tab keybinding pressed (numbers or next/prev) */
	/* In cases when the user configured accelerators like these ones:
		switch_tab_accelerator=4  for ctrl+next[prev]_tab_key
		move_tab_accelerator=5  for ctrl+shift+next[prev]_tab_key
	   move never works, because switch will be processed first, so it needs to be fixed with the following condition */
	if ( ((event->state & sakura.switch_tab_accelerator) == sakura.switch_tab_accelerator) &&
		 ((event->state & sakura.move_tab_accelerator) != sakura.move_tab_accelerator) ) {

		if ((keycode>=sakura_tokeycode(GDK_KEY_1)) && (keycode<=sakura_tokeycode( GDK_KEY_9))) {

			/* User has explicitly disabled this branch, make sure to propagate the event */
			if(sakura.disable_numbered_tabswitch) return FALSE;

			if      (sakura_tokeycode(GDK_KEY_1) == keycode) topage = 0;
			else if (sakura_tokeycode(GDK_KEY_2) == keycode) topage = 1;
			else if (sakura_tokeycode(GDK_KEY_3) == keycode) topage = 2;
			else if (sakura_tokeycode(GDK_KEY_4) == keycode) topage = 3;
			else if (sakura_tokeycode(GDK_KEY_5) == keycode) topage = 4;
			else if (sakura_tokeycode(GDK_KEY_6) == keycode) topage = 5;
			else if (sakura_tokeycode(GDK_KEY_7) == keycode) topage = 6;
			else if (sakura_tokeycode(GDK_KEY_8) == keycode) topage = 7;
			else if (sakura_tokeycode(GDK_KEY_9) == keycode) topage = 8;
			if (topage <= npages)
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), topage);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.prev_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))==0) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), npages-1);
			} else {
				gtk_notebook_prev_page(GTK_NOTEBOOK(sakura.notebook));
			}
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.next_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))==(npages-1)) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);
			} else {
				gtk_notebook_next_page(GTK_NOTEBOOK(sakura.notebook));
			}
			return TRUE;
		}
	}

	/* Move tab keybinding pressed */
	if ( ((event->state & sakura.move_tab_accelerator) == sakura.move_tab_accelerator)) {
		if (keycode==sakura_tokeycode(sakura.keymap.prev_tab_key)) {
			sakura_move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.next_tab_key)) {
			sakura_move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ( (event->state & sakura.copy_accelerator)==sakura.copy_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.copy_key)) {
			sakura_copy(NULL, NULL);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.paste_key)) {
			sakura_paste(NULL, NULL);
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ( (event->state & sakura.scrollbar_accelerator)==sakura.scrollbar_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.scrollbar_key)) {
			sakura_show_scrollbar(NULL, NULL);
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ( (event->state & sakura.set_tab_name_accelerator)==sakura.set_tab_name_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.set_tab_name_key)) {
			sakura_set_name_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ( (event->state & sakura.search_accelerator)==sakura.search_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.search_key)) {
			sakura_search_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ( (event->state & sakura.font_size_accelerator)==sakura.font_size_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.increase_font_size_key)) {
			sakura_increase_font(NULL, NULL);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.decrease_font_size_key)) {
			sakura_decrease_font(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode==sakura_tokeycode(sakura.keymap.fullscreen_key)){
		sakura_fullscreen(NULL, NULL);
		return TRUE;
	}

	/* Change in colorset */
	if ( (event->state & sakura.set_colorset_accelerator)==sakura.set_colorset_accelerator ) {
		int i;
		for(i=0; i<NUM_COLORSETS; i++) {
			if (keycode==sakura_tokeycode(sakura.keymap.set_colorset_keys[i])){
				sakura_set_colorset(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}


/* Delete the notebook tab passed as a parameter */
void Sakura::del_tab(gint page)
{
	gint npages;
	auto *term = sakura_get_page_term(sakura, page);
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = sakura_get_page_term(sakura, 0);
		const char * title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title)
			gtk_window_set_title(GTK_WINDOW(sakura.main_window), title);
	}

	term = sakura_get_page_term(sakura, page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		if (sakura.first_tab) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);
		}
		sakura.keep_fc = true;
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

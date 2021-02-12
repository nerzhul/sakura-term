/*******************************************************************************
 *  Filename: sakura->c
 *  Description: VTE-based terminal emulator
 *
 *           Copyright (C) 2006-2012  David Gómez <david@pleyades.net>
 *           Copyright (C) 2008       Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#include <cstdio>
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <cwchar>
#include <cmath>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <clocale>
#include <libintl.h>
#include <gtkmm.h>
#include <X11/Xlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include "debug.h"
#include "gettext.h"
#include "notebook.h"
#include "palettes.h"
#include "sakura.h"
#include "sakuraold.h"
#include "terminal.h"
#include "window.h"

#define HIG_DIALOG_CSS                                                                             \
	"* {\n"                                                                                    \
	"-GtkDialog-action-area-border : 12;\n"                                                    \
	"-GtkDialog-button-spacing : 12;\n"                                                        \
	"}"

#define FONT_MINIMAL_SIZE (PANGO_SCALE * 6)
#define TAB_MAX_SIZE 40
#define TAB_MIN_SIZE 6
#define FADE_PERCENT 60

#define ERROR_BUFFER_LENGTH 256

/* Functions */
static void sakura_set_tab_label_text(const gchar *, gint page);

void search(VteTerminal *vte, const char *pattern, bool reverse)
{
	GError *error = NULL;
	VteRegex *regex;

	vte_terminal_search_set_wrap_around(vte, TRUE);

	regex = vte_regex_new_for_search(
			pattern, (gssize)strlen(pattern), PCRE2_MULTILINE | PCRE2_CASELESS, &error);
	if (!regex) { /* Ubuntu-fucking-morons (17.10 and 18.04) package a broken VTE without PCRE2,
			 and search fails */
		sakura_error(error->message);
		g_error_free(error);
	} else {
		vte_terminal_search_set_regex(vte, regex, 0);

		if (!vte_terminal_search_find_next(vte)) {
			vte_terminal_unselect_all(vte);
			vte_terminal_search_find_next(vte);
		}

		if (regex)
			vte_regex_unref(regex);
	}
}

gboolean sakura_button_press(
		GtkWidget *widget, GdkEventButton *button_event, gpointer user_data)
{
	Terminal *term;
	gint page, tag;

	if (button_event->type != GDK_BUTTON_PRESS)
		return FALSE;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	/* Find out if cursor it's over a matched expression...*/
	sakura->current_match = vte_terminal_match_check_event(
			VTE_TERMINAL(term->vte), (GdkEvent *)button_event, &tag);

	/* Left button with accelerator: open the URL if any */
	if (button_event->button == 1 &&
			((button_event->state & sakura->config.open_url_accelerator) ==
					sakura->config.open_url_accelerator) &&
			sakura->current_match) {

		sakura_open_url(NULL, NULL);

		return TRUE;
	}

	/* Right button: show the popup menu */
	if (button_event->button == 3) {
		GtkMenu *menu = GTK_MENU(widget);

		if (sakura->current_match) {
			/* Show the extra options in the menu */

			char *matches;
			/* Is it a mail address? */
			if (vte_terminal_event_check_regex_simple(VTE_TERMINAL(term->vte),
					    (GdkEvent *)button_event, &sakura->mail_vteregexp, 1, 0,
					    &matches)) {
				sakura->item_open_mail->show();
				sakura->item_open_link->hide();
			} else {
				sakura->item_open_link->show();
				sakura->item_open_mail->hide();
			}
			sakura->item_copy_link->show();
			sakura->open_link_separator->show();

			g_free(matches);
		} else {
			/* Hide all the options */
			sakura->item_open_mail->hide();
			sakura->item_open_link->hide();
			sakura->item_copy_link->hide();
			sakura->open_link_separator->hide();
		}

		gtk_menu_popup_at_pointer(menu, (GdkEvent *)button_event);

		return TRUE;
	}

	return FALSE;
}

/* Handler for notebook focus-in-event */
// static gboolean
// sakura_notebook_focus_in(GtkWidget *widget, void *data)
//{
//	Terminal *term;
//	int index;
//
//	index = sakura->main_window->notebook->get_current_page();
//	term = sakura->get_page_term(index);
//
//	/* If term is found stop event propagation */
//	if(term != NULL) {
//		gtk_widget_grab_focus(term->vte);
//		return TRUE;
//	}
//
//	return FALSE;
//}

void sakura_increase_font(GtkWidget *widget, void *data)
{
	gint new_size;

	/* Increment font size one unit */
	new_size = pango_font_description_get_size(sakura->config.font) + PANGO_SCALE;

	pango_font_description_set_size(sakura->config.font, new_size);
	sakura_set_font();
	sakura_set_size();
	sakura_set_config_string("font", pango_font_description_to_string(sakura->config.font));
}

void sakura_decrease_font(GtkWidget *widget, void *data)
{
	gint new_size;

	/* Decrement font size one unit */
	new_size = pango_font_description_get_size(sakura->config.font) - PANGO_SCALE;

	/* Set a minimal size */
	if (new_size >= FONT_MINIMAL_SIZE) {
		pango_font_description_set_size(sakura->config.font, new_size);
		sakura_set_font();
		sakura_set_size();
		sakura_set_config_string(
				"font", pango_font_description_to_string(sakura->config.font));
	}
}

void sakura_child_exited(GtkWidget *widget, void *data)
{
	//auto obj = (Sakura *)data;
	// Strangely the obj pointer is null here... use the globally defined pointed
	// instead
	sakura->on_child_exited(widget);
}

void sakura_eof(GtkWidget *widget, void *data)
{
	auto obj = (Sakura *)data;
	obj->on_eof(widget);
}

/* This handler is called when window title changes, and is used to change window and notebook pages
 * titles */
void sakura_title_changed(GtkWidget *widget, void *data)
{
	Terminal *term;
	const char *title;
	gint n_pages;
	gint modified_page;
	VteTerminal *vte_term = (VteTerminal *)widget;

	modified_page = sakura->main_window->notebook->find_tab(vte_term);
	n_pages = sakura->main_window->notebook->get_n_pages();
	term = sakura->get_page_term(modified_page);

	title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));

	/* User set values overrides any other one, but title should be changed */
	if (!term->label_set_byuser)
		sakura_set_tab_label_text(title, modified_page);

	if (option_title == NULL) {
		if (n_pages == 1) {
			/* Beware: It doesn't work in Unity because there is a Compiz bug: #257391
			 */
			sakura->main_window->set_title(title);
		} else
			sakura->main_window->set_title("sakura");
	} else {
		sakura->main_window->set_title(option_title);
	}
}

/* Save configuration */
void sakura_config_done()
{
	GError *gerror = NULL;
	gsize len = 0;

	gchar *cfgdata = g_key_file_to_data(sakura->cfg, &len, &gerror);
	if (!cfgdata) {
		fprintf(stderr, "%s\n", gerror->message);
		exit(EXIT_FAILURE);
	}

	/* Write to file IF there's been changes */
	if (sakura->config_modified) {

		bool overwrite = true;

		if (sakura->externally_modified) {
			GtkWidget *dialog;
			gint response;

			dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()),
					GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
					_("Configuration has been modified by another process. "
					  "Overwrite?"));

			response = gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);

			overwrite = response == GTK_RESPONSE_YES;
		}

		if (overwrite) {
			sakura->config.write();
		}
	}

	free(cfgdata);
}

void sakura_window_show_event(GtkWidget *widget, gpointer data)
{
	// set size when the window is first shown
	sakura_set_size();
}

void sakura_font_dialog(GtkWidget *widget, void *data)
{
	GtkWidget *font_dialog;
	gint response;

	font_dialog = gtk_font_chooser_dialog_new(
			_("Select font"), GTK_WINDOW(sakura->main_window->gobj()));
	gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(font_dialog), sakura->config.font);

	response = gtk_dialog_run(GTK_DIALOG(font_dialog));

	if (response == GTK_RESPONSE_OK) {
		pango_font_description_free(sakura->config.font);
		sakura->config.font = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(font_dialog));
		sakura_set_font();
		sakura_set_size();
		sakura_set_config_string(
				"font", pango_font_description_to_string(sakura->config.font));
	}

	gtk_widget_destroy(font_dialog);
}

void sakura_set_name_dialog(GtkWidget *widget, void *data)
{
	GtkWidget *input_dialog, *input_header;
	GtkWidget *entry, *label;
	GtkWidget *name_hbox; /* We need this for correct spacing */
	gint response;
	gint page;
	Terminal *term;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	input_dialog = gtk_dialog_new_with_buttons(_("Set tab name"),
			GTK_WINDOW(sakura->main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Apply"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	input_header = gtk_dialog_get_header_bar(GTK_DIALOG(input_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(input_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(input_dialog), GTK_RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	sakura->provider->load_from_data(std::string(css));
	GtkStyleContext *context = gtk_widget_get_style_context(input_dialog);

	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura->provider->gobj()),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	name_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	entry = gtk_entry_new();
	label = gtk_label_new(_("New text"));
	/* Set tab label as entry default text (when first tab is not displayed, get_tab_label_text
	   returns a null value, so check accordingly */
	auto text = sakura->main_window->notebook->get_tab_label_text(term->hbox);
	if (text.empty()) {
		gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
	}
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(name_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(name_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(input_dialog))),
			name_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed),
			input_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(input_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(name_hbox);

	response = gtk_dialog_run(GTK_DIALOG(input_dialog));

	if (response == GTK_RESPONSE_ACCEPT) {
		sakura_set_tab_label_text(gtk_entry_get_text(GTK_ENTRY(entry)), page);
		term->label_set_byuser = true;
	}

	gtk_widget_destroy(input_dialog);
}

void sakura_set_colorset(int cs)
{
	gint page;
	Terminal *term;

	if (cs < 0 || cs >= NUM_COLORSETS)
		return;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);
	term->colorset = cs;

	sakura_set_config_integer("last_colorset", term->colorset + 1);

	sakura->set_colors();
}

/* Callback from the color change dialog. Updates the contents of that
 * dialog, passed as 'data' from user input. */
static void sakura_color_dialog_changed(GtkWidget *widget, void *data)
{
	int selected = -1;
	GtkDialog *dialog = (GtkDialog *)data;
	GtkColorButton *fore_button =
			(GtkColorButton *)g_object_get_data(G_OBJECT(dialog), "buttonfore");
	GtkColorButton *back_button =
			(GtkColorButton *)g_object_get_data(G_OBJECT(dialog), "buttonback");
	GtkColorButton *curs_button =
			(GtkColorButton *)g_object_get_data(G_OBJECT(dialog), "buttoncurs");
	GtkComboBox *set = (GtkComboBox *)g_object_get_data(G_OBJECT(dialog), "set_combo");
	GtkSpinButton *opacity_spin =
			(GtkSpinButton *)g_object_get_data(G_OBJECT(dialog), "opacity_spin");
	GdkRGBA *temp_fore_colors = (GdkRGBA *)g_object_get_data(G_OBJECT(dialog), "fore");
	GdkRGBA *temp_back_colors = (GdkRGBA *)g_object_get_data(G_OBJECT(dialog), "back");
	GdkRGBA *temp_curs_colors = (GdkRGBA *)g_object_get_data(G_OBJECT(dialog), "curs");
	selected = gtk_combo_box_get_active(set);

	/* if we come here as a result of a change in the active colorset,
	 * load the new colorset to the buttons.
	 * Else, the colorselect buttons or opacity spin have gotten a new
	 * value, store that. */
	if ((GtkWidget *)set == widget) {
		/* Spin opacity is a percentage, convert it*/
		gint new_opacity = (int)(temp_back_colors[selected].alpha * 100);
		gtk_color_chooser_set_rgba(
				GTK_COLOR_CHOOSER(fore_button), &temp_fore_colors[selected]);
		gtk_color_chooser_set_rgba(
				GTK_COLOR_CHOOSER(back_button), &temp_back_colors[selected]);
		gtk_color_chooser_set_rgba(
				GTK_COLOR_CHOOSER(curs_button), &temp_curs_colors[selected]);
		gtk_spin_button_set_value(opacity_spin, new_opacity);
	} else {
		gtk_color_chooser_get_rgba(
				GTK_COLOR_CHOOSER(fore_button), &temp_fore_colors[selected]);
		gtk_color_chooser_get_rgba(
				GTK_COLOR_CHOOSER(back_button), &temp_back_colors[selected]);
		gtk_color_chooser_get_rgba(
				GTK_COLOR_CHOOSER(curs_button), &temp_curs_colors[selected]);
		gtk_spin_button_update(opacity_spin);
		temp_back_colors[selected].alpha = gtk_spin_button_get_value(opacity_spin) / 100;
	}
}

void sakura_color_dialog(GtkWidget *widget, void *data)
{
	GtkWidget *color_dialog;
	GtkWidget *color_header;
	GtkWidget *label1, *label2, *label3, *set_label, *opacity_label;
	GtkWidget *buttonfore, *buttonback, *buttoncurs, *set_combo, *opacity_spin;
	GtkAdjustment *spinner_adj;
	GtkWidget *hbox_fore, *hbox_back, *hbox_curs, *hbox_sets, *hbox_opacity;
	gint response;
	Terminal *term;
	gint page;
	int cs;
	int i;
	gchar combo_text[3];
	GdkRGBA temp_fore[NUM_COLORSETS];
	GdkRGBA temp_back[NUM_COLORSETS];
	GdkRGBA temp_curs[NUM_COLORSETS];

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	color_dialog = gtk_dialog_new_with_buttons(_("Select colors"),
			GTK_WINDOW(sakura->main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	color_header = gtk_dialog_get_header_bar(GTK_DIALOG(color_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(color_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(color_dialog), GTK_RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	sakura->provider->load_from_data(std::string(css));
	GtkStyleContext *context = gtk_widget_get_style_context(color_dialog);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura->provider->gobj()),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	/* Add the drop-down combobox that selects current colorset to edit. */
	hbox_sets = gtk_box_new((GtkOrientation)FALSE, 12);
	set_label = gtk_label_new(_("Colorset"));
	set_combo = gtk_combo_box_text_new();
	for (cs = 0; cs < NUM_COLORSETS; cs++) {
		g_snprintf(combo_text, 2, "%d", cs + 1);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(set_combo), NULL, combo_text);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(set_combo), term->colorset);

	/* Foreground and background and cursor color buttons */
	hbox_fore = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	hbox_back = gtk_box_new((GtkOrientation)FALSE, 12);
	hbox_curs = gtk_box_new((GtkOrientation)FALSE, 12);
	label1 = gtk_label_new(_("Foreground color"));
	label2 = gtk_label_new(_("Background color"));
	label3 = gtk_label_new(_("Cursor color"));
	buttonfore = gtk_color_button_new_with_rgba(&sakura->forecolors[term->colorset]);
	buttonback = gtk_color_button_new_with_rgba(&sakura->backcolors[term->colorset]);
	buttoncurs = gtk_color_button_new_with_rgba(&sakura->curscolors[term->colorset]);

	/* Opacity control */
	hbox_opacity = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	spinner_adj = gtk_adjustment_new(
			(sakura->backcolors[term->colorset].alpha) * 100, 0.0, 99.0, 1.0, 5.0, 0);
	opacity_spin = gtk_spin_button_new(GTK_ADJUSTMENT(spinner_adj), 1.0, 0);
	opacity_label = gtk_label_new(_("Opacity level (%)"));
	gtk_box_pack_start(GTK_BOX(hbox_opacity), opacity_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(hbox_opacity), opacity_spin, FALSE, FALSE, 12);

	gtk_box_pack_start(GTK_BOX(hbox_fore), label1, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(hbox_fore), buttonfore, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(hbox_back), label2, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(hbox_back), buttonback, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(hbox_curs), label3, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(hbox_curs), buttoncurs, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(hbox_sets), set_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(hbox_sets), set_combo, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))),
			hbox_sets, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))),
			hbox_fore, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))),
			hbox_back, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))),
			hbox_curs, FALSE, FALSE, 6);
	gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))),
			hbox_opacity, FALSE, FALSE, 6);

	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog)));

	/* When user switches the colorset to change, the callback needs access
	 * to these selector widgets */
	g_object_set_data(G_OBJECT(color_dialog), "set_combo", set_combo);
	g_object_set_data(G_OBJECT(color_dialog), "buttonfore", buttonfore);
	g_object_set_data(G_OBJECT(color_dialog), "buttonback", buttonback);
	g_object_set_data(G_OBJECT(color_dialog), "buttoncurs", buttoncurs);
	g_object_set_data(G_OBJECT(color_dialog), "opacity_spin", opacity_spin);
	g_object_set_data(G_OBJECT(color_dialog), "fore", temp_fore);
	g_object_set_data(G_OBJECT(color_dialog), "back", temp_back);
	g_object_set_data(G_OBJECT(color_dialog), "curs", temp_curs);

	g_signal_connect(G_OBJECT(buttonfore), "color-set", G_CALLBACK(sakura_color_dialog_changed),
			color_dialog);
	g_signal_connect(G_OBJECT(buttonback), "color-set", G_CALLBACK(sakura_color_dialog_changed),
			color_dialog);
	g_signal_connect(G_OBJECT(buttoncurs), "color-set", G_CALLBACK(sakura_color_dialog_changed),
			color_dialog);
	g_signal_connect(G_OBJECT(set_combo), "changed", G_CALLBACK(sakura_color_dialog_changed),
			color_dialog);
	g_signal_connect(G_OBJECT(opacity_spin), "changed", G_CALLBACK(sakura_color_dialog_changed),
			color_dialog);

	for (i = 0; i < NUM_COLORSETS; i++) {
		temp_fore[i] = sakura->forecolors[i];
		temp_back[i] = sakura->backcolors[i];
		temp_curs[i] = sakura->curscolors[i];
	}

	response = gtk_dialog_run(GTK_DIALOG(color_dialog));

	if (response == GTK_RESPONSE_ACCEPT) {
		/* Save all colorsets to both the global struct and configuration.*/
		for (i = 0; i < NUM_COLORSETS; i++) {
			char name[20];
			gchar *cfgtmp;

			sakura->forecolors[i] = temp_fore[i];
			sakura->backcolors[i] = temp_back[i];
			sakura->curscolors[i] = temp_curs[i];

			sprintf(name, "colorset%d_fore", i + 1);
			cfgtmp = gdk_rgba_to_string(&sakura->forecolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);

			sprintf(name, "colorset%d_back", i + 1);
			cfgtmp = gdk_rgba_to_string(&sakura->backcolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);

			sprintf(name, "colorset%d_curs", i + 1);
			cfgtmp = gdk_rgba_to_string(&sakura->curscolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);
		}

		/* Apply the new colorsets to all tabs
		 * Set the current tab's colorset to the last selected one in the dialog.
		 * This is probably what the new user expects, and the experienced user
		 * hopefully will not mind. */
		term->colorset = gtk_combo_box_get_active(GTK_COMBO_BOX(set_combo));
		sakura_set_config_integer("last_colorset", term->colorset + 1);
		sakura->set_colors();
	}

	gtk_widget_destroy(color_dialog);
}

void sakura_fade_out()
{
	gint page;
	Terminal *term;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	if (!sakura->faded) {
		sakura->faded = true;
		GdkRGBA x = sakura->forecolors[term->colorset];
		// SAY("fade out red %f to %f", x.red, x.red/100.0*FADE_PERCENT);
		x.red = x.red / 100.0 * FADE_PERCENT;
		x.green = x.green / 100.0 * FADE_PERCENT;
		x.blue = x.blue / 100.0 * FADE_PERCENT;
		if ((x.red >= 0 && x.red <= 1.0) && (x.green >= 0 && x.green <= 1.0) &&
				(x.blue >= 0 && x.blue <= 1.0)) {
			sakura->forecolors[term->colorset] = x;
		} else {
			SAY("Forecolor value out of range");
		}
	}
}

void sakura_fade_in()
{
	gint page;
	Terminal *term;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	if (sakura->faded) {
		sakura->faded = false;
		GdkRGBA x = sakura->forecolors[term->colorset];
		// SAY("fade in red %f to %f", x.red, x.red/FADE_PERCENT*100.0);
		x.red = x.red / FADE_PERCENT * 100.0;
		x.green = x.green / FADE_PERCENT * 100.0;
		x.blue = x.blue / FADE_PERCENT * 100.0;
		if ((x.red >= 0 && x.red <= 1.0) && (x.green >= 0 && x.green <= 1.0) &&
				(x.blue >= 0 && x.blue <= 1.0)) {
			sakura->forecolors[term->colorset] = x;
		} else {
			SAY("Forecolor value out of range");
		}
	}
}

void sakura_search_dialog(GtkWidget *widget, void *data)
{
	GtkWidget *title_dialog, *title_header;
	GtkWidget *entry, *label;
	GtkWidget *title_hbox;
	gint response;

	title_dialog = gtk_dialog_new_with_buttons(_("Search"), GTK_WINDOW(sakura->main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Apply"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	title_header = gtk_dialog_get_header_bar(GTK_DIALOG(title_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(title_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	sakura->provider->load_from_data(std::string(css));
	GtkStyleContext *context = gtk_widget_get_style_context(title_dialog);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura->provider->gobj()),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	entry = gtk_entry_new();
	label = gtk_label_new(_("Search"));
	title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(title_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(title_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(title_dialog))),
			title_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed),
			title_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(title_hbox);

	response = gtk_dialog_run(GTK_DIALOG(title_dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		gint page;
		Terminal *term;
		page = sakura->main_window->notebook->get_current_page();
		term = sakura->get_page_term(page);
		search(VTE_TERMINAL(term->vte), gtk_entry_get_text(GTK_ENTRY(entry)), 0);
	}
	gtk_widget_destroy(title_dialog);
}

void sakura_set_title_dialog(GtkWidget *widget, void *data)
{
	GtkWidget *title_dialog, *title_header;
	GtkWidget *entry, *label;
	GtkWidget *title_hbox;
	gint response;

	title_dialog = gtk_dialog_new_with_buttons(_("Set window title"),
			GTK_WINDOW(sakura->main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Apply"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	title_header = gtk_dialog_get_header_bar(GTK_DIALOG(title_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(title_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	sakura->provider->load_from_data(std::string(css));
	GtkStyleContext *context = gtk_widget_get_style_context(title_dialog);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura->provider->gobj()),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	entry = gtk_entry_new();
	label = gtk_label_new(_("New window title"));
	title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	/* Set window label as entry default text */
	gtk_entry_set_text(GTK_ENTRY(entry), gtk_window_get_title(GTK_WINDOW(sakura->main_window->gobj())));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(title_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(title_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(title_dialog))),
			title_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed),
			title_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(title_hbox);

	response = gtk_dialog_run(GTK_DIALOG(title_dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		/* Bug #257391 shadow reachs here too... */
		sakura->main_window->set_title(gtk_entry_get_text(GTK_ENTRY(entry)));
	}
	gtk_widget_destroy(title_dialog);
}

void sakura_copy_url(GtkWidget *widget, void *data)
{
	GtkClipboard *clip;

	clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, sakura->current_match, -1);
	clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text(clip, sakura->current_match, -1);
}

void sakura_open_url(GtkWidget *widget, void *data)
{
	GError *error = NULL;
	gchar *browser = NULL;

	SAY("Opening %s", sakura->current_match);

	browser = g_strdup(g_getenv("BROWSER"));

	if (!browser) {
		if (!(browser = g_find_program_in_path("xdg-open"))) {
			/* TODO: Legacy for systems without xdg-open. This should be removed */
			browser = g_strdup("firefox");
		}
	}

	gchar *argv[] = {browser, sakura->current_match, NULL};
	if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
		sakura_error("Couldn't exec \"%s %s\": %s", browser, sakura->current_match,
				error->message);
		g_error_free(error);
	}

	g_free(browser);
}

void sakura_open_mail(GtkWidget *widget, void *data)
{
	GError *error = NULL;
	gchar *program = NULL;

	if ((program = g_find_program_in_path("xdg-email"))) {
		gchar *argv[] = {program, sakura->current_match, NULL};
		if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
				    &error)) {
			sakura_error("Couldn't exec \"%s %s\": %s", program, sakura->current_match,
					error->message);
		}
		g_free(program);
	}
}

void sakura_show_first_tab(GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura->main_window->notebook->set_show_tabs(true);
		sakura_set_config_string("show_always_first_tab", "Yes");
		sakura->config.first_tab = true;
	} else {
		/* Only hide tabs if the notebook has one page */
		if (sakura->main_window->notebook->get_n_pages() == 1) {
			sakura->main_window->notebook->set_show_tabs(false);
		}
		sakura_set_config_string("show_always_first_tab", "No");
		sakura->config.first_tab = false;
	}
	sakura_set_size();
}

void sakura_tabs_on_bottom(GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		gtk_notebook_set_tab_pos(sakura->main_window->notebook->gobj(), GTK_POS_BOTTOM);
		sakura_set_config_boolean("tabs_on_bottom", TRUE);
	} else {
		gtk_notebook_set_tab_pos(sakura->main_window->notebook->gobj(), GTK_POS_TOP);
		sakura_set_config_boolean("tabs_on_bottom", FALSE);
	}
}

void sakura_less_questions(GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura->config.less_questions = TRUE;
		sakura_set_config_boolean("less_questions", TRUE);
	} else {
		sakura->config.less_questions = FALSE;
		sakura_set_config_boolean("less_questions", FALSE);
	}
}

void sakura_show_close_button(GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura_set_config_boolean("closebutton", TRUE);
	} else {
		sakura_set_config_boolean("closebutton", FALSE);
	}
}

void sakura_urgent_bell(GtkWidget *widget, void *data)
{
	sakura->config.urgent_bell = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura->config.urgent_bell) {
		sakura_set_config_string("urgent_bell", "Yes");
	} else {
		sakura_set_config_string("urgent_bell", "No");
	}
}

void sakura_audible_bell(GtkWidget *widget, void *data)
{
	gint page;
	Terminal *term;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		vte_terminal_set_audible_bell(VTE_TERMINAL(term->vte), TRUE);
		sakura_set_config_string("audible_bell", "Yes");
	} else {
		vte_terminal_set_audible_bell(VTE_TERMINAL(term->vte), FALSE);
		sakura_set_config_string("audible_bell", "No");
	}
}

void sakura_blinking_cursor(GtkWidget *widget, void *data)
{
	gint page;
	Terminal *term;

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(term->vte), VTE_CURSOR_BLINK_ON);
		sakura_set_config_string("blinking_cursor", "Yes");
	} else {
		vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(term->vte), VTE_CURSOR_BLINK_OFF);
		sakura_set_config_string("blinking_cursor", "No");
	}
}

void sakura_allow_bold(GtkWidget *widget, void *data)
{
	gint page = sakura->main_window->notebook->get_current_page();
	auto term = sakura->get_page_term(page);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		vte_terminal_set_bold_is_bright(VTE_TERMINAL(term->vte), TRUE);
		sakura_set_config_string("allow_bold", "Yes");
	} else {
		vte_terminal_set_bold_is_bright(VTE_TERMINAL(term->vte), FALSE);
		sakura_set_config_string("allow_bold", "No");
	}
}

void sakura_stop_tab_cycling_at_end_tabs(GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura_set_config_string("stop_tab_cycling_at_end_tabs", "Yes");
		sakura->config.stop_tab_cycling_at_end_tabs = true;
	} else {
		sakura_set_config_string("stop_tab_cycling_at_end_tabs", "No");
		sakura->config.stop_tab_cycling_at_end_tabs = false;
	}
}

void sakura_set_cursor(GtkWidget *widget, void *data)
{
	Terminal *term;
	int n_pages, i;

	char *cursor_string = (char *)data;
	n_pages = sakura->main_window->notebook->get_n_pages();

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {

		if (strcmp(cursor_string, "block") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_BLOCK;
		} else if (strcmp(cursor_string, "underline") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_UNDERLINE;
		} else if (strcmp(cursor_string, "ibeam") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_IBEAM;
		}

		for (i = (n_pages - 1); i >= 0; i--) {
			term = sakura->get_page_term(i);
			vte_terminal_set_cursor_shape(
					VTE_TERMINAL(term->vte), sakura->config.cursor_type);
		}

		sakura_set_config_integer("cursor_type", sakura->config.cursor_type);
	}
}

void sakura_set_palette(GtkWidget *widget, void *data)
{
	char *palette = (char *)data;

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		if (strcmp(palette, "linux") == 0) {
			sakura->config.palette = linux_palette;
		} else if (strcmp(palette, "gruvbox") == 0) {
			sakura->config.palette = gruvbox_palette;
		} else if (strcmp(palette, "xterm") == 0) {
			sakura->config.palette = xterm_palette;
		} else if (strcmp(palette, "rxvt") == 0) {
			sakura->config.palette = rxvt_palette;
		} else if (strcmp(palette, "tango") == 0) {
			sakura->config.palette = tango_palette;
		} else if (strcmp(palette, "solarized_dark") == 0) {
			sakura->config.palette = solarized_dark_palette;
		} else {
			sakura->config.palette = solarized_light_palette;
		}

		/* Palette changed so we ¿need? to set colors again */
		sakura->set_colors();

		sakura_set_config_string("palette", palette);
	}
}

/* Retrieve the cwd of the specified term page.
 * Original function was from terminal-screen.c of gnome-terminal, copyright (C) 2001 Havoc
 * Pennington Adapted by Hong Jen Yee, non-linux shit removed by David Gómez */
char *sakura_get_term_cwd(Terminal *term)
{
	char *cwd = NULL;

	if (term->pid >= 0) {
		char *file, *buf;
		struct stat sb;
		int len;

		file = g_strdup_printf("/proc/%d/cwd", term->pid);

		if (g_stat(file, &sb) == -1) {
			g_free(file);
			return cwd;
		}

		buf = (char *)malloc(sb.st_size + 1);

		if (buf == NULL) {
			g_free(file);
			return cwd;
		}

		len = readlink(file, buf, sb.st_size + 1);

		if (len > 0 && buf[0] == '/') {
			buf[len] = '\0';
			cwd = g_strdup(buf);
		}

		g_free(buf);
		g_free(file);
	}

	return cwd;
}

void sakura_setname_entry_changed(GtkWidget *widget, void *data)
{
	GtkDialog *title_dialog = (GtkDialog *)data;

	if (strcmp(gtk_entry_get_text(GTK_ENTRY(widget)), "") == 0) {
		gtk_dialog_set_response_sensitive(
				GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);
	} else {
		gtk_dialog_set_response_sensitive(
				GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, TRUE);
	}
}

void sakura_fullscreen(GtkWidget *, void *data)
{
	auto obj = (SakuraWindow *)data;
	obj->toggle_fullscreen();
}

/* Callback for the tabs close buttons */
void sakura_closebutton_clicked(GtkWidget *widget, void *data)
{
	gint page;
	GtkWidget *hbox = (GtkWidget *)data;
	Terminal *term;
	pid_t pgid;
	GtkWidget *dialog;
	gint npages, response;

	page = gtk_notebook_page_num(sakura->main_window->notebook->gobj(), hbox);
	term = sakura->get_page_term(page);
	npages = sakura->main_window->notebook->get_n_pages();

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!sakura->config.less_questions)) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			sakura->del_tab(page, true);
		}
	} else { /* No processes, hell with tab */
		sakura->del_tab(page, true);
	}
}

/* Callback called when sakura configuration file is modified by an external process */
void sakura_conf_changed(GtkWidget *widget, void *data)
{
	sakura->externally_modified = true;
}

void sakura_disable_numbered_tabswitch(GtkWidget *widget, void *data)
{
	auto obj = (Sakura *)data;
	obj->toggle_numbered_tabswitch_option(widget);
}

void sakura_use_fading(GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura->config.use_fading = true;
		sakura_set_config_boolean("use_fading", TRUE);
	} else {
		sakura->config.use_fading = false;
		sakura_set_config_boolean("use_fading", FALSE);
		sakura_fade_in();
		sakura->set_colors();
	}
}

/******* Functions ********/

void sakura_set_size()
{
	gint pad_x, pad_y;
	gint char_width, char_height;
	gint min_width, natural_width;
	gint page;

	auto term = sakura->get_page_term(0);
	int npages = sakura->main_window->notebook->get_n_pages();

	/* Mayhaps an user resize happened. Check if row and columns have changed */
	if (sakura->main_window->resized) {
		sakura->columns = vte_terminal_get_column_count(VTE_TERMINAL(term->vte));
		sakura->rows = vte_terminal_get_row_count(VTE_TERMINAL(term->vte));
		SAY("New columns %ld and rows %ld", sakura->columns, sakura->rows);
		sakura->main_window->resized = false;
	}

	gtk_style_context_get_padding(gtk_widget_get_style_context(term->vte),
			gtk_widget_get_state_flags(term->vte), &term->padding);
	pad_x = term->padding.left + term->padding.right;
	pad_y = term->padding.top + term->padding.bottom;
	// SAY("padding x %d y %d", pad_x, pad_y);
	char_width = vte_terminal_get_char_width(VTE_TERMINAL(term->vte));
	char_height = vte_terminal_get_char_height(VTE_TERMINAL(term->vte));

	sakura->width = pad_x + (char_width * sakura->columns);
	sakura->height = pad_y + (char_height * sakura->rows);

	if (npages >= 2 || sakura->config.first_tab) {

		/* TODO: Yeah i know, this is utter shit. Remove this ugly hack and set geometry
		 * hints*/
		if (!sakura->config.show_scrollbar)
			// sakura->height += min_height - 10;
			sakura->height += 10;
		else
			// sakura->height += min_height - 47;
			sakura->height += 47;

		sakura->width += 8;
		sakura->width += /* (hb*2)+*/ (pad_x * 2);
	}

	page = sakura->main_window->notebook->get_current_page();
	term = sakura->get_page_term(page);

	gtk_widget_get_preferred_width(term->scrollbar, &min_width, &natural_width);
	// SAY("SCROLLBAR min width %d natural width %d", min_width, natural_width);
	if (sakura->config.show_scrollbar) {
		sakura->width += min_width;
	}

	/* GTK does not ignore resize for maximized windows on some systems,
	so we do need check if it's maximized or not */
	GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(sakura->main_window->gobj()));
	if (gdk_window != NULL) {
		if (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) {
			SAY("window is maximized, will not resize");
			return;
		}
	}

	gtk_window_resize(GTK_WINDOW(sakura->main_window->gobj()), sakura->width, sakura->height);
	SAY("Resized to %d %d", sakura->width, sakura->height);
}

void sakura_set_font()
{
	gint n_pages;
	Terminal *term;
	int i;

	n_pages = sakura->main_window->notebook->get_n_pages();

	/* Set the font for all tabs */
	for (i = (n_pages - 1); i >= 0; i--) {
		term = sakura->get_page_term(i);
		vte_terminal_set_font(VTE_TERMINAL(term->vte), sakura->config.font);
	}
}

static void sakura_set_tab_label_text(const gchar *title, gint page)
{
	Terminal *term;
	gchar *chopped_title;

	term = sakura->get_page_term(page);

	if ((title != NULL) && (g_strcmp0(title, "") != 0)) {
		/* Chop to max size. TODO: Should it be configurable by the user? */
		chopped_title = g_strndup(title, TAB_MAX_SIZE);
		/* Honor the minimum tab label size */
		while (strlen(chopped_title) < TAB_MIN_SIZE) {
			char *old_ptr = chopped_title;
			chopped_title = g_strconcat(chopped_title, " ", NULL);
			free(old_ptr);
		}
		gtk_label_set_text(GTK_LABEL(term->label.gobj()), chopped_title);
		free(chopped_title);
	} else { /* Use the default values */
		gtk_label_set_text(GTK_LABEL(term->label.gobj()), term->label_text);
	}
}

/* Callback for vte_terminal_spawn_async */
void sakura_spawn_callback(VteTerminal *vte, GPid pid, GError *error, gpointer user_data)
{
	auto term = (Terminal *)user_data;
	// term = sakura->get_page_term(page);
	if (pid == -1) { /* Fork has failed */
		SAY("Error: %s", error->message);
	} else {
		term->pid = pid;
	}
}

void sakura_error(const char *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	char *buff;

	va_start(args, format);
	buff = (char *)malloc(sizeof(char) * ERROR_BUFFER_LENGTH);
	vsnprintf(buff, sizeof(char) * ERROR_BUFFER_LENGTH, format, args);
	va_end(args);

	dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",
			buff);
	gtk_window_set_title(GTK_WINDOW(dialog), _("Error message"));
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	free(buff);
}

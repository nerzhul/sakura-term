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

#define TAB_MAX_SIZE 40
#define TAB_MIN_SIZE 6
#define FADE_PERCENT 60

#define ERROR_BUFFER_LENGTH 256

GOptionEntry entries[] = {{"version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
					  N_("Print version number"), NULL},
		{"font", 'f', 0, G_OPTION_ARG_STRING, &option_font,
				N_("Select initial terminal font"), NULL},
		{"ntabs", 'n', 0, G_OPTION_ARG_INT, &option_ntabs,
				N_("Select initial number of tabs"), NULL},
		{"working-directory", 'd', 0, G_OPTION_ARG_STRING, &option_workdir,
				N_("Set working directory"), NULL},
		{"execute", 'x', 0, G_OPTION_ARG_STRING, &option_execute, N_("Execute command"),
				NULL},
		{"xterm-execute", 'e', 0, G_OPTION_ARG_NONE, &option_xterm_execute,
				N_("Execute command (last option in the command line)"), NULL},
		{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &option_xterm_args, NULL,
				NULL},
		{"login", 'l', 0, G_OPTION_ARG_NONE, &option_login, N_("Login shell"), NULL},
		{"title", 't', 0, G_OPTION_ARG_STRING, &option_title, N_("Set window title"), NULL},
		{"icon", 'i', 0, G_OPTION_ARG_STRING, &option_icon, N_("Set window icon"), NULL},
		{"xterm-title", 'T', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &option_title, NULL,
				NULL},
		{"columns", 'c', 0, G_OPTION_ARG_INT, &option_columns, N_("Set columns number"),
				NULL},
		{"rows", 'r', 0, G_OPTION_ARG_INT, &option_rows, N_("Set rows number"), NULL},
		{"hold", 'h', 0, G_OPTION_ARG_NONE, &option_hold,
				N_("Hold window after execute command"), NULL},
		{"maximize", 'm', 0, G_OPTION_ARG_NONE, &option_maximize, N_("Maximize window"),
				NULL},
		{"fullscreen", 's', 0, G_OPTION_ARG_NONE, &option_fullscreen, N_("Fullscreen mode"),
				NULL},
		{"config-file", 0, 0, G_OPTION_ARG_FILENAME, &option_config_file,
				N_("Use alternate configuration file"), NULL},
		{"colorset", 0, 0, G_OPTION_ARG_INT, &option_colorset,
				N_("Select initial colorset"), NULL},
		{NULL}};

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

gboolean sakura_button_press(GtkWidget *widget, GdkEventButton *button_event, gpointer user_data)
{
	if (button_event->type != GDK_BUTTON_PRESS)
		return FALSE;

	gint tag;
	auto term = sakura->main_window->notebook.get_current_tab_term();

	/* Find out if cursor it's over a matched expression...*/
	sakura->current_match = vte_terminal_match_check_event(
			VTE_TERMINAL(term->vte), (GdkEvent *)button_event, &tag);

	/* Left button with accelerator: open the URL if any */
	if (button_event->button == 1 &&
			((button_event->state & sakura->config.open_url_accelerator) ==
					sakura->config.open_url_accelerator) &&
			sakura->current_match) {

		sakura->open_url();

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

void sakura_child_exited(GtkWidget *widget, void *data)
{
	// auto obj = (Sakura *)data;
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
	auto vte_term = (VteTerminal *)widget;

	gint modified_page = sakura->main_window->notebook.find_tab(vte_term);
	gint n_pages = sakura->main_window->notebook.get_n_pages();
	auto term = sakura->main_window->notebook.get_tab_term(modified_page);

	const char *title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));

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
	int cs;
	int i;
	gchar combo_text[3];
	GdkRGBA temp_fore[NUM_COLORSETS];
	GdkRGBA temp_back[NUM_COLORSETS];
	GdkRGBA temp_curs[NUM_COLORSETS];

	auto term = sakura->main_window->notebook.get_current_tab_term();

	auto color_dialog = gtk_dialog_new_with_buttons(_("Select colors"),
			GTK_WINDOW(sakura->main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	auto color_header = gtk_dialog_get_header_bar(GTK_DIALOG(color_dialog));
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
	auto hbox_sets = gtk_box_new((GtkOrientation)FALSE, 12);
	auto set_label = gtk_label_new(_("Colorset"));
	auto set_combo = gtk_combo_box_text_new();
	for (cs = 0; cs < NUM_COLORSETS; cs++) {
		g_snprintf(combo_text, 2, "%d", cs + 1);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(set_combo), NULL, combo_text);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(set_combo), term->colorset);

	/* Foreground and background and cursor color buttons */
	auto hbox_fore = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	auto hbox_back = gtk_box_new((GtkOrientation)FALSE, 12);
	auto hbox_curs = gtk_box_new((GtkOrientation)FALSE, 12);
	auto label1 = gtk_label_new(_("Foreground color"));
	auto label2 = gtk_label_new(_("Background color"));
	auto label3 = gtk_label_new(_("Cursor color"));
	auto buttonfore = gtk_color_button_new_with_rgba(&sakura->forecolors[term->colorset]);
	auto buttonback = gtk_color_button_new_with_rgba(&sakura->backcolors[term->colorset]);
	auto buttoncurs = gtk_color_button_new_with_rgba(&sakura->curscolors[term->colorset]);

	/* Opacity control */
	auto hbox_opacity = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	auto spinner_adj = gtk_adjustment_new(
			(sakura->backcolors[term->colorset].alpha) * 100, 0.0, 99.0, 1.0, 5.0, 0);
	auto opacity_spin = gtk_spin_button_new(GTK_ADJUSTMENT(spinner_adj), 1.0, 0);
	auto opacity_label = gtk_label_new(_("Opacity level (%)"));
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

	if (gtk_dialog_run(GTK_DIALOG(color_dialog)) == GTK_RESPONSE_ACCEPT) {
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

void sakura_show_first_tab(GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura->main_window->notebook.set_show_tabs(true);
		sakura_set_config_string("show_always_first_tab", "Yes");
		sakura->config.first_tab = true;
	} else {
		/* Only hide tabs if the notebook has one page */
		if (sakura->main_window->notebook.get_n_pages() == 1) {
			sakura->main_window->notebook.set_show_tabs(false);
		}
		sakura_set_config_string("show_always_first_tab", "No");
		sakura->config.first_tab = false;
	}
	sakura->set_size();
}

void sakura_tabs_on_bottom(GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		gtk_notebook_set_tab_pos(sakura->main_window->notebook.gobj(), GTK_POS_BOTTOM);
		sakura_set_config_boolean("tabs_on_bottom", TRUE);
	} else {
		gtk_notebook_set_tab_pos(sakura->main_window->notebook.gobj(), GTK_POS_TOP);
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
	auto term = sakura->main_window->notebook.get_current_tab_term();

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
	auto term = sakura->main_window->notebook.get_current_tab_term();

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
	auto term = sakura->main_window->notebook.get_current_tab_term();

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
	char *cursor_string = (char *)data;
	auto n_pages = sakura->main_window->notebook.get_n_pages();

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {

		if (strcmp(cursor_string, "block") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_BLOCK;
		} else if (strcmp(cursor_string, "underline") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_UNDERLINE;
		} else if (strcmp(cursor_string, "ibeam") == 0) {
			sakura->config.cursor_type = VTE_CURSOR_SHAPE_IBEAM;
		}

		for (int i = (n_pages - 1); i >= 0; i--) {
			auto term = sakura->main_window->notebook.get_tab_term(i);
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

void sakura_fullscreen(GtkWidget *, void *data)
{
	auto obj = (SakuraWindow *)data;
	obj->toggle_fullscreen();
}

/* Callback for the tabs close buttons */
void sakura_closebutton_clicked(GtkWidget *widget, void *data)
{
	GtkWidget *hbox = (GtkWidget *)data;

	auto page = gtk_notebook_page_num((*sakura->main_window).notebook.gobj(), hbox);
	auto term = sakura->main_window->notebook.get_tab_term(page);

	/* Only write configuration to disk if it's the last tab */
	if (sakura->main_window->notebook.get_n_pages() == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	auto pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!sakura->config.less_questions)) {
		auto dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()),
				GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		auto response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			sakura->main_window->notebook.del_tab(page, true);
		}
	} else { /* No processes, hell with tab */
		sakura->main_window->notebook.del_tab(page, true);
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
		sakura->fade_in();
		sakura->set_colors();
	}
}

/******* Functions ********/

void sakura_set_tab_label_text(const gchar *title, gint page)
{
	auto term = sakura->main_window->notebook.get_tab_term(page);
	if ((title != NULL) && (g_strcmp0(title, "") != 0)) {
		/* Chop to max size. TODO: Should it be configurable by the user? */
		auto chopped_title = g_strndup(title, TAB_MAX_SIZE);
		/* Honor the minimum tab label size */
		while (strlen(chopped_title) < TAB_MIN_SIZE) {
			char *old_ptr = chopped_title;
			chopped_title = g_strconcat(chopped_title, " ", NULL);
			free(old_ptr);
		}
		term->label.set_text(chopped_title);
		free(chopped_title);
	} else { /* Use the default values */
		term->label.set_text(term->label_text);
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
	va_list args;
	char *buff;

	va_start(args, format);
	buff = (char *)malloc(sizeof(char) * ERROR_BUFFER_LENGTH);
	vsnprintf(buff, sizeof(char) * ERROR_BUFFER_LENGTH, format, args);
	va_end(args);

	auto dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",
			buff);
	gtk_window_set_title(GTK_WINDOW(dialog), _("Error message"));
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	free(buff);
}

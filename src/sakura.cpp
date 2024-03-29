#include <gtk/gtk.h>
#include <vte/vte.h>
#include <glib/gstdio.h>
#include <iostream>
#include <cassert>
#include <libintl.h>
#include <gtkmm/window.h>
#include <gtkmm/notebook.h>
#include "sakura.h"
#include "debug.h"
#include "palettes.h"
#include "notebook.h"
#include "sakuraold.h"
#include "terminal.h"
#include "window.h"

#define FONT_MINIMAL_SIZE (PANGO_SCALE * 6)
#define FADE_PERCENT 60
#define NOTEBOOK_CSS                                                                               \
	"* {\n"                                                                                    \
	"color : rgba(0,0,0,1.0);\n"                                                               \
	"background-color : rgba(0,0,0,1.0);\n"                                                    \
	"border-color : rgba(0,0,0,1.0);\n"                                                        \
	"}"

#define HIG_DIALOG_CSS                                                                             \
	"* {\n"                                                                                    \
	"-GtkDialog-action-area-border : 12;\n"                                                    \
	"-GtkDialog-button-spacing : 12;\n"                                                        \
	"}"

#define HTTP_REGEXP "(ftp|http)s?://[^ \t\n\b()<>{}«»\\[\\]\'\"]+[^.]"
#define MAIL_REGEXP "[^ \t\n\b]+@([^ \t\n\b]+\\.)+([a-zA-Z]{2,4})"

static void sakura_on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto obj = (Sakura *)data;
	obj->on_key_press(widget, event);
}

/* This function is used to fix bug #1393939 */
void sanitize_working_directory()
{
	const gchar *home_directory = g_getenv("HOME");
	if (home_directory == NULL) {
		home_directory = g_get_home_dir();
	}

	if (home_directory) {
		if (chdir(home_directory)) {
			fprintf(stderr, _("Cannot change working directory\n"));
			exit(1);
		}
	}
}

Sakura::Sakura() : cfg(g_key_file_new()), provider(Gtk::CssProvider::create())
{
	// This object is a singleton
	assert(sakura == nullptr);
	sakura = this;

	if (!config.read()) {
		exit(EXIT_FAILURE);
	}

	config.monitor();

	main_window = std::make_unique<SakuraWindow>(Gtk::WINDOW_TOPLEVEL, &config);

	/* set default title pattern from config or NULL */
	Terminal::tab_default_title =
			g_key_file_get_string(cfg, cfg_group, "tab_default_title", NULL);

	term_data_id = g_quark_from_static_string("sakura_term");

	/* Use always GTK header bar*/
	g_object_set(gtk_settings_get_default(), "gtk-dialogs-use-header", TRUE, NULL);

	provider->load_from_data(NOTEBOOK_CSS);
	auto context = main_window->notebook.get_style_context();
	context->add_provider(provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	/* Command line optionsNULL initialization */

	/* Set argv for forked childs. Real argv vector starts at argv[1] because we're
	   using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
	argv[0] = g_strdup(g_getenv("SHELL"));
	if (option_login) {
		argv[1] = g_strdup_printf("-%s", g_getenv("SHELL"));
	} else {
		argv[1] = g_strdup(g_getenv("SHELL"));
	}
	argv[2] = NULL;

	if (option_title) {
		main_window->set_title(option_title);
	}

	if (option_columns) {
		columns = option_columns;
	}

	if (option_rows) {
		rows = option_rows;
	}

	if (option_font) {
		config.font = Pango::FontDescription(Glib::ustring(option_font));
	}

	if (option_colorset && option_colorset > 0 && option_colorset <= NUM_COLORSETS) {
		config.last_colorset = option_colorset;
	}

	/* These options are exclusive */
	if (option_fullscreen) {
		main_window->toggle_fullscreen();
	} else if (option_maximize) {
		main_window->maximize();
	}

	GError *error = nullptr;
	http_vteregexp = vte_regex_new_for_match(HTTP_REGEXP, strlen(HTTP_REGEXP), 0, &error);
	if (!http_vteregexp) {
		SAY("http_regexp: %s", error->message);
		g_error_free(error);
	}

	error = nullptr;
	mail_vteregexp = vte_regex_new_for_match(MAIL_REGEXP, strlen(MAIL_REGEXP), 0, &error);
	if (!mail_vteregexp) {
		SAY("mail_regexp: %s", error->message);
		g_error_free(error);
	}

	init_popup();

	main_window->signal_delete_event().connect(sigc::mem_fun(*this, &Sakura::destroy));
	g_signal_connect(G_OBJECT(main_window->gobj()), "key-press-event",
			G_CALLBACK(sakura_on_key_press), this);
	// g_signal_connect(G_OBJECT(notebook), "focus-in-event",
	// G_CALLBACK(sakura_notebook_focus_in), NULL);

	/* Add initial tabs (1 by default) */
	for (int i = 0; i < option_ntabs; i++) {
		main_window->notebook.add_tab();
	}

	sanitize_working_directory();
}

Sakura::~Sakura()
{
	for (uint8_t i = 0; i < 3; i++) {
		if (argv[i]) {
			free(argv[i]);
		}
	}

	if (http_vteregexp) {
		vte_regex_unref(http_vteregexp);
	}

	if (mail_vteregexp) {
		vte_regex_unref(mail_vteregexp);
	}
}

static const gint BACKWARDS = 2;

static guint sakura_tokeycode(guint key)
{
	GdkKeymap *keymap;
	GdkKeymapKey *keys;
	gint n_keys;
	guint res = 0;

	keymap = gdk_keymap_get_for_display(gdk_display_get_default());

	if (gdk_keymap_get_entries_for_keyval(keymap, key, &keys, &n_keys)) {
		if (n_keys > 0) {
			res = keys[0].keycode;
		}
		g_free(keys);
	}

	return res;
}


void sakura_setname_entry_changed(GtkWidget *widget, void *data)
{
	Gtk::Dialog *title_dialog = (Gtk::Dialog *)data;

	if (strcmp(gtk_entry_get_text(GTK_ENTRY(widget)), "") == 0) {
		title_dialog->set_response_sensitive(Gtk::RESPONSE_ACCEPT, false);
	} else {
		title_dialog->set_response_sensitive(Gtk::RESPONSE_ACCEPT, true);
	}
}

bool Sakura::destroy(GdkEventAny*)
{
	SAY("Destroying sakura");

	g_key_file_free(cfg);

	gtk_main_quit();
	return true;
}


void Sakura::open_url()
{
	GError *error = NULL;

	SAY("Opening %s", current_match);

	gchar *browser = g_strdup(g_getenv("BROWSER"));

	if (!browser) {
		if (!(browser = g_find_program_in_path("xdg-open"))) {
			/* TODO: Legacy for systems without xdg-open. This should be removed */
			browser = g_strdup("firefox");
		}
	}

	gchar *argv[] = {browser, current_match, NULL};
	if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
		sakura_error("Couldn't exec \"%s %s\": %s", browser, current_match,
				error->message);
		g_error_free(error);
	}

	g_free(browser);
}

void Sakura::copy_url()
{
	auto clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, current_match, -1);
	clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text(clip, current_match, -1);
}

void Sakura::init_popup()
{
	item_open_mail = new Gtk::MenuItem(_("Open mail"));
	item_open_link = new Gtk::MenuItem(_("Open link"));
	item_copy_link = new Gtk::MenuItem(_("Copy link"));
	auto item_new_tab = new Gtk::MenuItem(_("New tab"));
	auto item_set_name = new Gtk::MenuItem(_("Set tab name..."));
	auto item_close_tab = new Gtk::MenuItem(_("Close tab"));
	auto item_fullscreen = new Gtk::MenuItem(_("Full screen"));
	auto item_copy = new Gtk::MenuItem(_("Copy"));
	auto item_paste = new Gtk::MenuItem(_("Paste"));
	auto item_select_font = new Gtk::MenuItem(_("Select font..."));
	auto item_select_colors = new Gtk::MenuItem(_("Select colors..."));
	auto item_set_title = new Gtk::MenuItem(_("Set window title..."));

	auto item_options = new Gtk::MenuItem(_("Options"));

	auto item_other_options = new Gtk::MenuItem(_("More"));
	auto item_show_first_tab = new Gtk::CheckMenuItem(_("Always show tab bar"));
	auto item_tabs_on_bottom = new Gtk::CheckMenuItem(_("Tabs at bottom"));
	auto item_show_close_button = new Gtk::CheckMenuItem(_("Show close button on tabs"));
	auto item_toggle_scrollbar = new Gtk::CheckMenuItem(_("Show scrollbar"));
	auto item_less_questions = new Gtk::CheckMenuItem(_("Don't show exit dialog"));
	auto item_urgent_bell = new Gtk::CheckMenuItem(_("Set urgent bell"));
	auto item_audible_bell = new Gtk::CheckMenuItem(_("Set audible bell"));
	auto item_blinking_cursor = new Gtk::CheckMenuItem(_("Set blinking cursor"));
	auto item_allow_bold = new Gtk::CheckMenuItem(_("Enable bold font"));
	auto item_stop_tab_cycling_at_end_tabs =
			new Gtk::CheckMenuItem(_("Stop tab cycling at end tabs"));
	auto item_disable_numbered_tabswitch =
			new Gtk::CheckMenuItem(_("Disable numbered tabswitch"));
	auto item_use_fading = new Gtk::CheckMenuItem(_("Enable focus fade"));
	auto item_cursor = new Gtk::MenuItem(_("Set cursor type"));
	auto item_cursor_block = gtk_radio_menu_item_new_with_label(NULL, _("Block"));
	auto item_cursor_underline = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_cursor_block), _("Underline"));
	auto item_cursor_ibeam = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_cursor_block), _("IBeam"));
	auto item_palette = new Gtk::MenuItem(_("Set palette"));
	auto item_palette_tango = gtk_radio_menu_item_new_with_label(NULL, "Tango");
	auto item_palette_linux = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "Linux");
	auto item_palette_gruvbox = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "Gruvbox");
	auto item_palette_xterm = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "Xterm");
	auto item_palette_rxvt = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "rxvt");
	auto item_palette_solarized_dark = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "Solarized dark");
	auto item_palette_solarized_light = gtk_radio_menu_item_new_with_label_from_widget(
			GTK_RADIO_MENU_ITEM(item_palette_tango), "Solarized light");

	item_show_first_tab->set_active(config.first_tab);
	item_show_close_button->set_active(config.show_closebutton);
	item_tabs_on_bottom->set_active(config.tabs_on_bottom);
	item_less_questions->set_active(config.less_questions);
	item_toggle_scrollbar->set_active(config.show_scrollbar);
	item_disable_numbered_tabswitch->set_active(config.disable_numbered_tabswitch);
	item_use_fading->set_active(config.use_fading);
	item_urgent_bell->set_active(config.urgent_bell);
	item_audible_bell->set_active(config.audible_bell);
	item_blinking_cursor->set_active(config.blinking_cursor);
	item_allow_bold->set_active(config.allow_bold);
	item_stop_tab_cycling_at_end_tabs->set_active(config.stop_tab_cycling_at_end_tabs);

	switch (config.cursor_type) {
	case VTE_CURSOR_SHAPE_BLOCK:
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_block), TRUE);
		break;
	case VTE_CURSOR_SHAPE_UNDERLINE:
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_underline), TRUE);
		break;
	case VTE_CURSOR_SHAPE_IBEAM:
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_ibeam), TRUE);
	}

	if (config.palette_str == "linux") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_linux), TRUE);
	} else if (config.palette_str == "gruvbox") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_gruvbox), TRUE);
	} else if (config.palette_str == "xterm") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_tango), TRUE);
	} else if (config.palette_str == "rxvt") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_rxvt), TRUE);
	} else if (config.palette_str == "tango") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_xterm), TRUE);
	} else if (config.palette_str == "solarized_dark") {
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(item_palette_solarized_dark), TRUE);
	} else {
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(item_palette_solarized_light), TRUE);
	}


	open_link_separator = new Gtk::SeparatorMenuItem();

	/* Add items to popup menu */
	menu = new Gtk::Menu();
	menu->append(*item_open_mail);
	menu->append(*item_open_link);
	menu->append(*item_copy_link);
	menu->append(*open_link_separator);
	menu->append(*item_new_tab);
	menu->append(*item_set_name);
	menu->append(*item_close_tab);

	auto separator1 = new Gtk::SeparatorMenuItem(), separator2 = new Gtk::SeparatorMenuItem();
	menu->append(*separator1);
	menu->append(*item_fullscreen);
	menu->append(*item_copy);
	menu->append(*item_paste);
	menu->append(*separator2);
	menu->append(*item_options);

	auto options_menu = gtk_menu_new();
	auto other_options_menu = gtk_menu_new();
	auto cursor_menu = gtk_menu_new();
	auto palette_menu = gtk_menu_new();

	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), (GtkWidget*)item_set_title->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), (GtkWidget*)item_select_colors->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), (GtkWidget*)item_select_font->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), (GtkWidget*)item_other_options->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_show_first_tab->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_tabs_on_bottom->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_show_close_button->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_toggle_scrollbar->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_less_questions->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_urgent_bell->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_audible_bell->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_disable_numbered_tabswitch->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_use_fading->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_blinking_cursor->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_allow_bold->gobj());
	gtk_menu_shell_append(
			GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_stop_tab_cycling_at_end_tabs->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_cursor->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_block);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_underline);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_ibeam);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), (GtkWidget*)item_palette->gobj());
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_tango);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_linux);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_gruvbox);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_xterm);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_rxvt);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_solarized_dark);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_solarized_light);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_options->gobj()), options_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_other_options->gobj()), other_options_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_cursor->gobj()), cursor_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_palette->gobj()), palette_menu);

	/* ... and finally assign callbacks to menuitems */
	item_new_tab->signal_activate().connect(sigc::mem_fun(&main_window->notebook, &SakuraNotebook::add_tab));
	item_set_name->signal_activate().connect(sigc::mem_fun(*this, &Sakura::set_name_dialog));
	item_close_tab->signal_activate().connect(sigc::mem_fun(&main_window->notebook, &SakuraNotebook::close_tab));
	item_select_font->signal_activate().connect(sigc::mem_fun(*this, &Sakura::show_font_dialog));

	item_copy->signal_activate().connect(sigc::mem_fun(*this, &Sakura::copy));
	item_paste->signal_activate().connect(sigc::mem_fun(*this, &Sakura::paste));
	g_signal_connect(G_OBJECT(item_select_colors->gobj()), "activate", G_CALLBACK(sakura_color_dialog),
			NULL);

	g_signal_connect(G_OBJECT(item_show_first_tab->gobj()), "activate",
			G_CALLBACK(sakura_show_first_tab), NULL);
	g_signal_connect(G_OBJECT(item_tabs_on_bottom->gobj()), "activate",
			G_CALLBACK(sakura_tabs_on_bottom), NULL);
	g_signal_connect(G_OBJECT(item_less_questions->gobj()), "activate",
			G_CALLBACK(sakura_less_questions), NULL);
	g_signal_connect(G_OBJECT(item_show_close_button->gobj()), "activate",
			G_CALLBACK(sakura_show_close_button), NULL);

	item_toggle_scrollbar->signal_activate().connect(sigc::mem_fun(&(main_window.get()->notebook), &SakuraNotebook::show_scrollbar));
	g_signal_connect(G_OBJECT(item_urgent_bell->gobj()), "activate", G_CALLBACK(sakura_urgent_bell),
			NULL);
	g_signal_connect(G_OBJECT(item_audible_bell->gobj()), "activate", G_CALLBACK(sakura_audible_bell),
			NULL);
	g_signal_connect(G_OBJECT(item_blinking_cursor->gobj()), "activate",
			G_CALLBACK(sakura_blinking_cursor), NULL);
	g_signal_connect(
			G_OBJECT(item_allow_bold->gobj()), "activate", G_CALLBACK(sakura_allow_bold), NULL);
	g_signal_connect(G_OBJECT(item_stop_tab_cycling_at_end_tabs->gobj()), "activate",
			G_CALLBACK(sakura_stop_tab_cycling_at_end_tabs), NULL);
	g_signal_connect(G_OBJECT(item_disable_numbered_tabswitch->gobj()), "activate",
			G_CALLBACK(sakura_disable_numbered_tabswitch), sakura);

	g_signal_connect(
			G_OBJECT(item_use_fading->gobj()), "activate", G_CALLBACK(sakura_use_fading), NULL);
	item_set_title->signal_activate().connect(sigc::mem_fun(*this, &Sakura::open_title_dialog));
	g_signal_connect(G_OBJECT(item_cursor_block), "activate", G_CALLBACK(sakura_set_cursor),
			(gpointer) "block");
	g_signal_connect(G_OBJECT(item_cursor_underline), "activate", G_CALLBACK(sakura_set_cursor),
			(gpointer) "underline");
	g_signal_connect(G_OBJECT(item_cursor_ibeam), "activate", G_CALLBACK(sakura_set_cursor),
			(gpointer) "ibeam");
	g_signal_connect(G_OBJECT(item_palette_tango), "activate", G_CALLBACK(sakura_set_palette),
			(gpointer) "tango");
	g_signal_connect(G_OBJECT(item_palette_linux), "activate", G_CALLBACK(sakura_set_palette),
			(gpointer) "linux");
	g_signal_connect(G_OBJECT(item_palette_gruvbox), "activate", G_CALLBACK(sakura_set_palette),
			(gpointer) "gruvbox");
	g_signal_connect(G_OBJECT(item_palette_xterm), "activate", G_CALLBACK(sakura_set_palette),
			(gpointer) "xterm");
	g_signal_connect(G_OBJECT(item_palette_rxvt), "activate", G_CALLBACK(sakura_set_palette),
			(gpointer) "rxvt");
	g_signal_connect(G_OBJECT(item_palette_solarized_dark), "activate",
			G_CALLBACK(sakura_set_palette), (gpointer) "solarized_dark");
	g_signal_connect(G_OBJECT(item_palette_solarized_light), "activate",
			G_CALLBACK(sakura_set_palette), (gpointer) "solarized_light");

	item_open_mail->signal_activate().connect(sigc::mem_fun(*this, &Sakura::open_mail));
	item_open_link->signal_activate().connect(sigc::mem_fun(*this, &Sakura::open_url));
	item_copy_link->signal_activate().connect(sigc::mem_fun(*this, &Sakura::copy_url));
	item_fullscreen->signal_activate().connect(sigc::mem_fun(main_window.get(), &SakuraWindow::toggle_fullscreen));

	menu->show_all();
}

void Sakura::open_mail()
{
	gchar *program = NULL;

	if ((program = g_find_program_in_path("xdg-email"))) {
		gchar *argv[] = {program, sakura->current_match, NULL};
		GError *error = NULL;
		if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
				    &error)) {
			sakura_error("Couldn't exec \"%s %s\": %s", program, sakura->current_match,
					error->message);
		}
		g_free(program);
	}
}

void Sakura::open_title_dialog()
{
	auto title_dialog = gtk_dialog_new_with_buttons(_("Set window title"),
			GTK_WINDOW(main_window->gobj()),
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR),
			_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Apply"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar*/
	auto title_header = gtk_dialog_get_header_bar(GTK_DIALOG(title_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(title_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	provider->load_from_data(std::string(css));
	GtkStyleContext *context = gtk_widget_get_style_context(title_dialog);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider->gobj()),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	auto entry = gtk_entry_new();
	auto label = gtk_label_new(_("New window title"));
	auto title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	/* Set window label as entry default text */
	gtk_entry_set_text(GTK_ENTRY(entry), gtk_window_get_title(GTK_WINDOW(main_window->gobj())));
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

	if (gtk_dialog_run(GTK_DIALOG(title_dialog)) == GTK_RESPONSE_ACCEPT) {
		/* Bug #257391 shadow reachs here too... */
		main_window->set_title(gtk_entry_get_text(GTK_ENTRY(entry)));
	}
	gtk_widget_destroy(title_dialog);
}

static gboolean terminal_screen_image_draw_cb(GtkWidget *widget, cairo_t *cr, void *userdata)
{
	auto obj = (Terminal *)userdata;

	if (!obj->bg_image)
		return FALSE;

	GdkRectangle target_rect;
	GtkAllocation alloc;

	gtk_widget_get_allocation(widget, &alloc);

	target_rect.x = 0;
	target_rect.y = 0;
	target_rect.width = alloc.width;
	target_rect.height = alloc.height;

	cairo_surface_t *child_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
	cairo_t *child_cr = cairo_create(child_surface);

	g_signal_handler_block(widget, obj->bg_image_callback_id);
	gtk_widget_draw(widget, child_cr);
	g_signal_handler_unblock(widget, obj->bg_image_callback_id);

	gdk_cairo_set_source_pixbuf(cr, obj->bg_image, 0, 0);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);

	gdk_cairo_rectangle(cr, &target_rect);
	cairo_fill(cr);

	cairo_set_source_surface(cr, child_surface, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_paint(cr);

	cairo_destroy(child_cr);
	cairo_surface_destroy(child_surface);

	return TRUE;
}

void Sakura::copy()
{
	auto term = main_window->notebook.get_current_tab_term();
	vte_terminal_copy_clipboard_format(VTE_TERMINAL(term->vte), VTE_FORMAT_TEXT);
}

void Sakura::paste()
{
	auto term = main_window->notebook.get_current_tab_term();
	vte_terminal_paste_clipboard(VTE_TERMINAL(term->vte));
}

void Sakura::set_name_dialog()
{
	gint page = main_window->notebook.get_current_page();
	auto term = main_window->notebook.get_tab_term(page);

	auto input_dialog = Gtk::Dialog(_("Set tab name"), Gtk::DIALOG_MODAL | Gtk::DIALOG_USE_HEADER_BAR);
	input_dialog.set_parent(*main_window);
	input_dialog.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
	input_dialog.add_button(_("_Apply"), Gtk::RESPONSE_ACCEPT);

	/* Configure the new gtk header bar*/
	auto input_header = input_dialog.get_header_bar();
	input_header->set_show_close_button(false);
	input_dialog.set_default_response(Gtk::RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	provider->load_from_data(std::string(css));
	auto context = input_dialog.get_style_context();
	context->add_provider(provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	auto name_hbox = Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0);
	auto entry = Gtk::Entry();
	auto label = Gtk::Label(_("New text"));
	/* Set tab label as entry default text (when first tab is not displayed, get_tab_label_text
	   returns a null value, so check accordingly */
	auto text = main_window->notebook.get_tab_label_text(term->hbox);
	if (text.empty()) {
		entry.set_text(text);
	}
	entry.set_activates_default(true);
	name_hbox.pack_start(label, true, true, 12);
	name_hbox.pack_start(entry, true, true, 12);
	input_dialog.get_content_area()->pack_start(name_hbox, false, false, 12);

	/* Disable accept button until some text is entered */
	//entry->signal_changed().connect(sigc::mem_fun(*entry, &Sakura::setname_entry_changed));
	g_signal_connect(G_OBJECT(entry.gobj()), "changed", G_CALLBACK(sakura_setname_entry_changed),
			&input_dialog);

	input_dialog.set_response_sensitive(Gtk::RESPONSE_ACCEPT, false);

	name_hbox.show_all();

	if (input_dialog.run() == Gtk::RESPONSE_ACCEPT) {
		sakura_set_tab_label_text(entry.get_text().c_str(), page);
		term->label_set_byuser = true;
	}
}

void Sakura::set_font()
{
	gint n_pages = main_window->notebook.get_n_pages();

	/* Set the font for all tabs */
	for (int i = (n_pages - 1); i >= 0; i--) {
		auto term = main_window->notebook.get_tab_term(i);
		vte_terminal_set_font(VTE_TERMINAL(term->vte), config.font.gobj());
	}
}

/* Set the terminal colors for all notebook tabs */
void Sakura::set_colors()
{
	int i;
	int n_pages = main_window->notebook.get_n_pages();
	Terminal *term;

	for (i = (n_pages - 1); i >= 0; i--) {
		term = main_window->notebook.get_tab_term(i);

		if (!config.get_background_image().empty()) {
			if (!term->bg_image_callback_id) {
				term->bg_image_callback_id = g_signal_connect(term->hbox.gobj(),
						"draw", G_CALLBACK(terminal_screen_image_draw_cb),
						term);
			}

			g_clear_object(&term->bg_image);
			GError *error = nullptr;
			term->bg_image = gdk_pixbuf_new_from_file(
					config.get_background_image().c_str(), &error);
			if (error) {
				SAY("Failed to load background image %s", error->message);
				g_clear_error(&error);
			}

			term->hbox.queue_draw();
		}

		backcolors[term->colorset].alpha = config.get_background_alpha();

		vte_terminal_set_colors(VTE_TERMINAL(term->vte), &forecolors[term->colorset],
				&backcolors[term->colorset], config.palette, PALETTE_SIZE);
		vte_terminal_set_color_cursor(VTE_TERMINAL(term->vte), &curscolors[term->colorset]);
	}

	/* Main window opacity must be set. Otherwise vte widget will remain opaque */
	sakura->main_window->set_opacity(backcolors[term->colorset].alpha);
}

gboolean Sakura::on_key_press(GtkWidget *widget, GdkEventKey *event)
{
	if (event->type != GDK_KEY_PRESS)
		return FALSE;

	uint32_t topage = 0;

	gint npages = main_window->notebook.get_n_pages();

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in
	 * US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Add/delete tab keybinding pressed */
	if ((event->state & config.add_tab_accelerator) == config.add_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.add_tab_key)) {
		main_window->notebook.add_tab();
		return TRUE;
	} else if ((event->state & sakura->config.del_tab_accelerator) ==
					config.del_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.del_tab_key)) {
		/* Delete current tab */
		main_window->notebook.close_tab();
		return TRUE;
	}

	/* Switch tab keybinding pressed (numbers or next/prev) */
	/* In cases when the user configured accelerators like these ones:
		switch_tab_accelerator=4  for ctrl+next[prev]_tab_key
		move_tab_accelerator=5  for ctrl+shift+next[prev]_tab_key
	   move never works, because switch will be processed first, so it needs to be
	   fixed with the following condition */
	if (((event->state & config.switch_tab_accelerator) ==
			    sakura->config.switch_tab_accelerator) &&
			((event->state & config.move_tab_accelerator) !=
					config.move_tab_accelerator)) {

		if ((keycode >= sakura_tokeycode(GDK_KEY_1)) &&
				(keycode <= sakura_tokeycode(GDK_KEY_9))) {

			/* User has explicitly disabled this branch, make sure to
			 * propagate the event */
			if (config.disable_numbered_tabswitch)
				return FALSE;

			if (sakura_tokeycode(GDK_KEY_1) == keycode)
				topage = 0;
			else if (sakura_tokeycode(GDK_KEY_2) == keycode)
				topage = 1;
			else if (sakura_tokeycode(GDK_KEY_3) == keycode)
				topage = 2;
			else if (sakura_tokeycode(GDK_KEY_4) == keycode)
				topage = 3;
			else if (sakura_tokeycode(GDK_KEY_5) == keycode)
				topage = 4;
			else if (sakura_tokeycode(GDK_KEY_6) == keycode)
				topage = 5;
			else if (sakura_tokeycode(GDK_KEY_7) == keycode)
				topage = 6;
			else if (sakura_tokeycode(GDK_KEY_8) == keycode)
				topage = 7;
			else if (sakura_tokeycode(GDK_KEY_9) == keycode)
				topage = 8;
			if (topage <= npages)
				main_window->notebook.set_current_page(topage);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			if (main_window->notebook.get_current_page() == 0) {
				main_window->notebook.set_current_page(npages - 1);
			} else {
				gtk_notebook_prev_page(main_window->notebook.gobj());
			}
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			if (main_window->notebook.get_current_page() == (npages - 1)) {
				main_window->notebook.set_current_page(0);
			} else {
				main_window->notebook.next_page();
			}
			return TRUE;
		}
	}

	/* Move tab keybinding pressed */
	if (((event->state & config.move_tab_accelerator) == config.move_tab_accelerator)) {
		if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			main_window->notebook.move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			main_window->notebook.move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ((event->state & config.copy_accelerator) == config.copy_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.copy_key)) {
			sakura->copy();
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.paste_key)) {
			sakura->paste();
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ((event->state & config.scrollbar_accelerator) == config.scrollbar_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.scrollbar_key)) {
			main_window->notebook.show_scrollbar();
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ((event->state & config.set_tab_name_accelerator) == config.set_tab_name_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.set_tab_name_key)) {
			set_name_dialog();
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ((event->state & config.search_accelerator) == config.search_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.search_key)) {
			show_search_dialog();
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ((event->state & config.font_size_accelerator) == config.font_size_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.increase_font_size_key)) {
			sakura->increase_font(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.decrease_font_size_key)) {
			sakura->decrease_font(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode == sakura_tokeycode(config.keymap.fullscreen_key)) {
		main_window->toggle_fullscreen();
		return TRUE;
	}

	/* Change in colorset */
	if ((event->state & config.set_colorset_accelerator) == config.set_colorset_accelerator) {
		int i;
		for (i = 0; i < NUM_COLORSETS; i++) {
			if (keycode == sakura_tokeycode(config.keymap.set_colorset_keys[i])) {
				set_color_set(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void Sakura::increase_font(GtkWidget *widget, void *data)
{
	/* Increment font size one unit */
	gint new_size = sakura->config.font.get_size() + PANGO_SCALE;

	sakura->config.font.set_size(new_size);
	sakura->set_font();
	sakura->set_size();
	sakura_set_config_string("font", sakura->config.font.to_string().c_str());
}

void Sakura::decrease_font(GtkWidget *widget, void *data)
{
	/* Decrement font size one unit */
	gint new_size = sakura->config.font.get_size() - PANGO_SCALE;

	/* Set a minimal size */
	if (new_size >= FONT_MINIMAL_SIZE) {
		sakura->config.font.set_size(new_size);
		sakura->set_font();
		sakura->set_size();
		sakura_set_config_string(
				"font", sakura->config.font.to_string().c_str());
	}
}

void Sakura::set_color_set(int cs)
{
	if (cs < 0 || cs >= NUM_COLORSETS)
		return;

	auto term = main_window->notebook.get_current_tab_term();
	term->colorset = cs;

	sakura_set_config_integer("last_colorset", term->colorset + 1);

	set_colors();
}


void Sakura::show_font_dialog()
{
	Gtk::FontChooserDialog font_dialog(_("Select font"));
	font_dialog.set_parent(*sakura->main_window.get());
	font_dialog.set_font_desc(sakura->config.font);

	if (font_dialog.run() == Gtk::RESPONSE_OK) {
		sakura->config.font = font_dialog.get_font_desc();
		sakura->set_font();
		sakura->set_size();
		sakura_set_config_string(
				"font", sakura->config.font.to_string().c_str());
	}
}

void Sakura::fade_in()
{
	if (faded) {
		auto term = main_window->notebook.get_current_tab_term();

		faded = false;
		GdkRGBA x = sakura->forecolors[term->colorset];
		// SAY("fade in red %f to %f", x.red, x.red/FADE_PERCENT*100.0);
		x.red = x.red / FADE_PERCENT * 100.0;
		x.green = x.green / FADE_PERCENT * 100.0;
		x.blue = x.blue / FADE_PERCENT * 100.0;
		if ((x.red >= 0 && x.red <= 1.0) && (x.green >= 0 && x.green <= 1.0) &&
				(x.blue >= 0 && x.blue <= 1.0)) {
			forecolors[term->colorset] = x;
		} else {
			SAY("Forecolor value out of range");
		}
	}
}

void Sakura::fade_out()
{
	if (!faded) {
		auto term = main_window->notebook.get_current_tab_term();

		faded = true;
		GdkRGBA x = forecolors[term->colorset];
		// SAY("fade out red %f to %f", x.red, x.red/100.0*FADE_PERCENT);
		x.red = x.red / 100.0 * FADE_PERCENT;
		x.green = x.green / 100.0 * FADE_PERCENT;
		x.blue = x.blue / 100.0 * FADE_PERCENT;
		if ((x.red >= 0 && x.red <= 1.0) && (x.green >= 0 && x.green <= 1.0) &&
				(x.blue >= 0 && x.blue <= 1.0)) {
			forecolors[term->colorset] = x;
		} else {
			SAY("Forecolor value out of range");
		}
	}
}

void Sakura::set_size()
{
	auto term = main_window->notebook.get_tab_term(0);
	int npages = main_window->notebook.get_n_pages();

	/* Mayhaps an user resize happened. Check if row and columns have changed */
	if (main_window->resized) {
		columns = vte_terminal_get_column_count(VTE_TERMINAL(term->vte));
		rows = vte_terminal_get_row_count(VTE_TERMINAL(term->vte));
		SAY("New columns %ld and rows %ld", columns, rows);
		main_window->resized = false;
	}

	gtk_style_context_get_padding(gtk_widget_get_style_context(term->vte),
			gtk_widget_get_state_flags(term->vte), &term->padding);
	gint pad_x = term->padding.left + term->padding.right;
	gint pad_y = term->padding.top + term->padding.bottom;
	// SAY("padding x %d y %d", pad_x, pad_y);
	gint char_width = vte_terminal_get_char_width(VTE_TERMINAL(term->vte));
	gint char_height = vte_terminal_get_char_height(VTE_TERMINAL(term->vte));

	width = pad_x + (char_width * columns);
	height = pad_y + (char_height * rows);

	if (npages >= 2 || config.first_tab) {

		/* TODO: Yeah i know, this is utter shit. Remove this ugly hack and set geometry
		 * hints*/
		if (!config.show_scrollbar)
			// height += min_height - 10;
			height += 10;
		else
			// height += min_height - 47;
			height += 47;

		width += 8;
		width += /* (hb*2)+*/ (pad_x * 2);
	}

	term = main_window->notebook.get_current_tab_term();

	gint min_width, natural_width;
	gtk_widget_get_preferred_width(term->scrollbar, &min_width, &natural_width);
	// SAY("SCROLLBAR min width %d natural width %d", min_width, natural_width);
	if (config.show_scrollbar) {
		width += min_width;
	}

	/* GTK does not ignore resize for maximized windows on some systems,
	so we do need check if it's maximized or not */
	GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(main_window->gobj()));
	if (gdk_window != NULL) {
		if (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) {
			SAY("window is maximized, will not resize");
			return;
		}
	}

	main_window->resize(width, height);
	SAY("Resized to %d %d", width, height);
}

void Sakura::on_child_exited(GtkWidget *widget)
{
	gint page = gtk_notebook_page_num(
			main_window->notebook.gobj(), gtk_widget_get_parent(widget));
	gint npages = main_window->notebook.get_n_pages();

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	if (option_hold == TRUE) {
		SAY("hold option has been activated");
		return;
	}

	// No more pages, we can just exit
	if (npages == 0) {
		return;
	}

	auto term = main_window->notebook.get_tab_term(page);

	/* Child should be automatically reaped because we don't use G_SPAWN_DO_NOT_REAP_CHILD flag
	 */
	g_spawn_close_pid(term->pid);

	main_window->notebook.del_tab(page, true);
}

void Sakura::on_eof(GtkWidget *widget)
{
	SAY("Got EOF signal");

	gint npages = main_window->notebook.get_n_pages();

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Workaround for libvte strange behaviour. There is not child-exited signal for
	   the last terminal, so we need to kill it here.  Check with libvte authors about
	   child-exited/eof signals */
	if (main_window->notebook.get_current_page() == 0) {

		auto term = main_window->notebook.get_tab_term(0);

		if (option_hold == TRUE) {
			SAY("hold option has been activated");
			return;
		}

		// SAY("waiting for terminal pid (in eof) %d", term->pid);
		// waitpid(term->pid, &status, WNOHANG);
		/* TODO: check wait return */
		/* Child should be automatically reaped because we don't use
		 * G_SPAWN_DO_NOT_REAP_CHILD flag */
		g_spawn_close_pid(term->pid);

		main_window->notebook.del_tab(0, true);
	}
}

void Sakura::toggle_numbered_tabswitch_option(GtkWidget *widget)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		config.disable_numbered_tabswitch = true;
		sakura_set_config_boolean("disable_numbered_tabswitch", TRUE);
	} else {
		config.disable_numbered_tabswitch = false;
		sakura_set_config_boolean("disable_numbered_tabswitch", FALSE);
	}
}

void Sakura::show_search_dialog()
{
	auto title_dialog = Gtk::Dialog(_("Search"), Gtk::DIALOG_MODAL | Gtk::DIALOG_USE_HEADER_BAR);
	title_dialog.set_parent(*sakura->main_window);
	title_dialog.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
	title_dialog.add_button(_("_Apply"), Gtk::RESPONSE_ACCEPT);

	/* Configure the new gtk header bar*/
	auto title_header = title_dialog.get_header_bar();
	title_header->set_show_close_button(false);
	title_dialog.set_default_response(Gtk::RESPONSE_ACCEPT);

	/* Set style */
	gchar *css = g_strdup_printf(HIG_DIALOG_CSS);
	sakura->provider->load_from_data(std::string(css));
	auto context = title_dialog.get_style_context();
	context->add_provider(sakura->provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	auto entry = Gtk::Entry();
	auto label = Gtk::Label(_("Search"));
	auto title_hbox = Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0);
	entry.set_activates_default(true);
	title_hbox.pack_start(label, true, true, 12);
	title_hbox.pack_start(entry, true, true, 12);

	title_dialog.get_content_area()->pack_start(title_hbox, false, false, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(entry.gobj(), "changed", G_CALLBACK(sakura_setname_entry_changed),
			&title_dialog);
	title_dialog.set_response_sensitive(Gtk::RESPONSE_ACCEPT, false);

	title_hbox.show_all();

	if (title_dialog.run() == Gtk::RESPONSE_ACCEPT) {
		auto term = main_window->notebook.get_current_tab_term();
		search(VTE_TERMINAL(term->vte), entry.get_text().c_str(), 0);
	}
}

void Sakura::beep(GtkWidget *widget)
{
	// Remove the urgency hint. This is necessary to signal the window manager
	// that a new urgent event happened when the urgent hint is set after this.
	main_window->set_urgency_hint(false);

	if (config.urgent_bell) {
		main_window->set_urgency_hint(true);
	}
}

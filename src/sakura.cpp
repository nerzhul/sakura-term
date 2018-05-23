#include <gtk/gtk.h>
#include <vte/vte.h>
#include <glib/gstdio.h>
#include <iostream>
#include <cassert>
#include <libintl.h>
#include "sakura.h"
#include "debug.h"
#include "palettes.h"
#include "sakuraold.h"
#include "terminal.h"

#define NOTEBOOK_CSS                                                                               \
	"* {\n"                                                                                    \
	"color : rgba(0,0,0,1.0);\n"                                                               \
	"background-color : rgba(0,0,0,1.0);\n"                                                    \
	"border-color : rgba(0,0,0,1.0);\n"                                                        \
	"}"

#define HTTP_REGEXP "(ftp|http)s?://[^ \t\n\b()<>{}«»\\[\\]\'\"]+[^.]"
#define MAIL_REGEXP "[^ \t\n\b]+@([^ \t\n\b]+\\.)+([a-zA-Z]{2,4})"

Sakura::~Sakura()
{
	for (uint8_t i=0; i < 3; i++) {
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

static void sakura_on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto *obj = (Sakura *)data;
	obj->on_key_press(widget, event);
}

static gboolean sakura_delete_event(GtkWidget *widget, void *data)
{
	Terminal *term;
	GtkWidget *dialog;
	gint response;
	gint npages;
	gint i;
	pid_t pgid;

	if (!sakura->config.less_questions) {
		npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura->notebook));

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to
		 * the shell PGID */
		for (i = 0; i < npages; i++) {

			term = sakura_get_page_term(sakura, i);
			pgid = tcgetpgrp(vte_pty_get_fd(
					vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

			/* If running processes are found, we ask one time and exit */
			if ((pgid != -1) && (pgid != term->pid)) {
				dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window),
						GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
						GTK_BUTTONS_YES_NO,
						_("There are running processes.\n\nDo you really "
						  "want to close Sakura?"));

				response = gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);

				if (response == GTK_RESPONSE_YES) {
					sakura_config_done();
					return FALSE;
				} else {
					return TRUE;
				}
			}
		}
	}

	sakura_config_done();
	return FALSE;
}

static void sakura_destroy_window(GtkWidget *widget, void *data)
{
	auto *obj = (Sakura *)data;
	obj->destroy(widget);
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

void Sakura::init()
{
	int i;

	term_data_id = g_quark_from_static_string("sakura_term");

	/* Config file initialization*/
	sakura->cfg = g_key_file_new();
	sakura->config_modified = false;

	if (!sakura->config.read()) {
		exit(EXIT_FAILURE);
	}

	sakura->config.monitor();

	/* set default title pattern from config or NULL */
	sakura->tab_default_title =
			g_key_file_get_string(sakura->cfg, cfg_group, "tab_default_title", NULL);

	/* Use always GTK header bar*/
	g_object_set(gtk_settings_get_default(), "gtk-dialogs-use-header", TRUE, NULL);

	sakura->provider = gtk_css_provider_new();

	sakura->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(sakura->main_window), "sakura");

	/* Create notebook and set style */
	sakura->notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(
			(GtkNotebook *)sakura->notebook, sakura->config.scrollable_tabs);

	gchar *css = g_strdup_printf(NOTEBOOK_CSS);
	gtk_css_provider_load_from_data(sakura->provider, css, -1, NULL);
	GtkStyleContext *context = gtk_widget_get_style_context(sakura->notebook);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura->provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	/* Adding mask, for handle scroll events */
	gtk_widget_add_events(sakura->notebook, GDK_SCROLL_MASK);

	/* Figure out if we have rgba capabilities. FIXME: Is this really needed? */
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(sakura->main_window));
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual != NULL && gdk_screen_is_composited(screen)) {
		gtk_widget_set_visual(GTK_WIDGET(sakura->main_window), visual);
	}

	/* Command line optionsNULL initialization */

	/* Set argv for forked childs. Real argv vector starts at argv[1] because we're
	   using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
	sakura->argv[0] = g_strdup(g_getenv("SHELL"));
	if (option_login) {
		sakura->argv[1] = g_strdup_printf("-%s", g_getenv("SHELL"));
	} else {
		sakura->argv[1] = g_strdup(g_getenv("SHELL"));
	}
	sakura->argv[2] = NULL;

	if (option_title) {
		gtk_window_set_title(GTK_WINDOW(sakura->main_window), option_title);
	}

	if (option_columns) {
		sakura->columns = option_columns;
	}

	if (option_rows) {
		sakura->rows = option_rows;
	}

	/* Add datadir path to icon name and set icon */
	gchar *icon_path;
	GError *error = nullptr;
	if (option_icon) {
		icon_path = g_strdup_printf("%s", option_icon);
	} else {
		icon_path = g_strdup_printf(DATADIR "/pixmaps/%s", sakura->config.icon.c_str());
	}
	gtk_window_set_icon_from_file(GTK_WINDOW(sakura->main_window), icon_path, &error);
	g_free(icon_path);
	icon_path = NULL;
	if (error)
		g_error_free(error);

	if (option_font) {
		sakura->config.font = pango_font_description_from_string(option_font);
	}

	if (option_colorset && option_colorset > 0 && option_colorset <= NUM_COLORSETS) {
		sakura->config.last_colorset = option_colorset;
	}

	/* These options are exclusive */
	if (option_fullscreen) {
		sakura_fullscreen(nullptr, sakura);
	} else if (option_maximize) {
		gtk_window_maximize(GTK_WINDOW(sakura->main_window));
	}

	sakura->label_count = 1;
	sakura->resized = FALSE;
	sakura->keep_fc = false;
	sakura->externally_modified = false;

	error = nullptr;
	sakura->http_vteregexp =
			vte_regex_new_for_match(HTTP_REGEXP, strlen(HTTP_REGEXP), 0, &error);
	if (!sakura->http_vteregexp) {
		SAY("http_regexp: %s", error->message);
		g_error_free(error);
	}
	error = nullptr;
	sakura->mail_vteregexp =
			vte_regex_new_for_match(MAIL_REGEXP, strlen(MAIL_REGEXP), 0, &error);
	if (!sakura->mail_vteregexp) {
		SAY("mail_regexp: %s", error->message);
		g_error_free(error);
	}

	gtk_container_add(GTK_CONTAINER(sakura->main_window), sakura->notebook);

	/* Adding mask to see wheter sakura window is focused or not */
	// gtk_widget_add_events(sakura->main_window, GDK_FOCUS_CHANGE_MASK);
	sakura->focused = true;
	sakura->first_focus = true;
	sakura->faded = false;

	sakura_init_popup();

	g_signal_connect(G_OBJECT(sakura->main_window), "delete_event",
			G_CALLBACK(sakura_delete_event), NULL);
	g_signal_connect(G_OBJECT(sakura->main_window), "destroy",
			G_CALLBACK(sakura_destroy_window), sakura);
	g_signal_connect(G_OBJECT(sakura->main_window), "key-press-event",
			G_CALLBACK(sakura_on_key_press), sakura);
	g_signal_connect(G_OBJECT(sakura->main_window), "configure-event",
			G_CALLBACK(sakura_resized_window), NULL);
	g_signal_connect(G_OBJECT(sakura->main_window), "focus-out-event",
			G_CALLBACK(sakura_focus_out), sakura);
	g_signal_connect(G_OBJECT(sakura->main_window), "focus-in-event",
			G_CALLBACK(sakura_focus_in), sakura);
	g_signal_connect(G_OBJECT(sakura->main_window), "show",
			G_CALLBACK(sakura_window_show_event), NULL);
	// g_signal_connect(G_OBJECT(sakura->notebook), "focus-in-event",
	// G_CALLBACK(sakura_notebook_focus_in), NULL);
	g_signal_connect(sakura->notebook, "scroll-event", G_CALLBACK(sakura_notebook_scroll),
			sakura);

	/* Add initial tabs (1 by default) */
	for (i = 0; i < option_ntabs; i++)
		sakura_add_tab();

	sanitize_working_directory();
}

void Sakura::destroy(GtkWidget *)
{
	SAY("Destroying sakura");

	/* Delete all existing tabs */
	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) >= 1) {
		del_tab(-1);
	}

	g_key_file_free(cfg);

	pango_font_description_free(config.font);

	gtk_main_quit();
}

static gboolean
terminal_screen_image_draw_cb (GtkWidget *widget, cairo_t *cr, void *userdata)
{
	auto *obj = (Terminal *)userdata;
	GdkRectangle target_rect;
	GtkAllocation alloc;
	cairo_surface_t *child_surface;
	cairo_t *child_cr;

	if (!obj->bg_image)
		return FALSE;

	gtk_widget_get_allocation (widget, &alloc);

	target_rect.x = 0;
	target_rect.y = 0;
	target_rect.width = alloc.width;
	target_rect.height = alloc.height;

	child_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
	child_cr = cairo_create (child_surface);

	g_signal_handler_block (widget, obj->bg_image_callback_id);
	gtk_widget_draw (widget, child_cr);
	g_signal_handler_unblock (widget, obj->bg_image_callback_id);

	gdk_cairo_set_source_pixbuf (cr, obj->bg_image, 0, 0);
	cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);

	gdk_cairo_rectangle (cr, &target_rect);
	cairo_fill (cr);

	cairo_set_source_surface (cr, child_surface, 0, 0);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_paint (cr);

	cairo_destroy (child_cr);
	cairo_surface_destroy (child_surface);

	return TRUE;
}

/* Set the terminal colors for all notebook tabs */
void Sakura::set_colors()
{
	int i;
	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura->notebook));
	Terminal *term;

	for (i = (n_pages - 1); i >= 0; i--) {
		term = sakura_get_page_term(sakura, i);

		config.background_image = "/home/nerzhul/Images/17966.jpg";
		if (!config.background_image.empty()) {
			if (!term->bg_image_callback_id) {
				term->bg_image_callback_id = g_signal_connect (term->hbox, "draw",
					G_CALLBACK(terminal_screen_image_draw_cb), term);
			}

			g_clear_object (&term->bg_image);
			GError *error = nullptr;
			term->bg_image = gdk_pixbuf_new_from_file(config.background_image.c_str(),
				&error);
			if (error) {
				SAY("Failed to load background image %s", error->message);
				g_clear_error(&error);
			}

			gtk_widget_queue_draw(GTK_WIDGET(term->hbox));

			sakura->backcolors[term->colorset].alpha = 0.9;
		}

		vte_terminal_set_colors(VTE_TERMINAL(term->vte),
			&sakura->forecolors[term->colorset],
			&sakura->backcolors[term->colorset], sakura->config.palette,
			PALETTE_SIZE);
		vte_terminal_set_color_cursor(
			VTE_TERMINAL(term->vte), &sakura->curscolors[term->colorset]);


	}

	/* Main window opacity must be set. Otherwise vte widget will remain opaque */
	gtk_widget_set_opacity(sakura->main_window, sakura->backcolors[term->colorset].alpha);
}

gboolean Sakura::on_focus_in(GtkWidget *widget, GdkEvent *event)
{
	if (event->type != GDK_FOCUS_CHANGE)
		return FALSE;

	/* Ignore first focus event */
	if (first_focus) {
		first_focus = false;
		return FALSE;
	}

	if (!focused) {
		focused = true;

		if (!first_focus && config.use_fading) {
			sakura_fade_in();
		}

		set_colors();
		return TRUE;
	}

	return FALSE;
}

gboolean Sakura::on_focus_out(GtkWidget *widget, GdkEvent *event)
{
	if (event->type != GDK_FOCUS_CHANGE)
		return FALSE;

	if (focused) {
		focused = false;

		if (!first_focus && config.use_fading) {
			sakura_fade_out();
		}

		set_colors();
		return TRUE;
	}

	return FALSE;
}

gboolean Sakura::on_key_press(GtkWidget *widget, GdkEventKey *event)
{
	if (event->type != GDK_KEY_PRESS)
		return FALSE;

	uint32_t topage = 0;

	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in
	 * US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Add/delete tab keybinding pressed */
	if ((event->state & config.add_tab_accelerator) == config.add_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ((event->state & sakura->config.del_tab_accelerator) ==
					config.del_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.del_tab_key)) {
		/* Delete current tab */
		close_tab(nullptr);
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
				gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), topage);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)) == 0) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), npages - 1);
			} else {
				gtk_notebook_prev_page(GTK_NOTEBOOK(notebook));
			}
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)) == (npages - 1)) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
			} else {
				gtk_notebook_next_page(GTK_NOTEBOOK(notebook));
			}
			return TRUE;
		}
	}

	/* Move tab keybinding pressed */
	if (((event->state & config.move_tab_accelerator) == config.move_tab_accelerator)) {
		if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			sakura_move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			sakura_move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ((event->state & config.copy_accelerator) == config.copy_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.copy_key)) {
			sakura_copy(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.paste_key)) {
			sakura_paste(NULL, NULL);
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ((event->state & config.scrollbar_accelerator) == config.scrollbar_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.scrollbar_key)) {
			sakura_show_scrollbar(NULL, NULL);
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ((event->state & config.set_tab_name_accelerator) == config.set_tab_name_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.set_tab_name_key)) {
			sakura_set_name_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ((event->state & config.search_accelerator) == config.search_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.search_key)) {
			sakura_search_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ((event->state & config.font_size_accelerator) == config.font_size_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.increase_font_size_key)) {
			sakura_increase_font(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.decrease_font_size_key)) {
			sakura_decrease_font(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode == sakura_tokeycode(config.keymap.fullscreen_key)) {
		toggle_fullscreen(nullptr);
		return TRUE;
	}

	/* Change in colorset */
	if ((event->state & config.set_colorset_accelerator) == config.set_colorset_accelerator) {
		int i;
		for (i = 0; i < NUM_COLORSETS; i++) {
			if (keycode == sakura_tokeycode(config.keymap.set_colorset_keys[i])) {
				sakura_set_colorset(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void Sakura::on_child_exited(GtkWidget *widget)
{
	gint page = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), gtk_widget_get_parent(widget));
	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	auto *term = sakura_get_page_term(this, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	if (option_hold == TRUE) {
		SAY("hold option has been activated");
		return;
	}

	/* Child should be automatically reaped because we don't use G_SPAWN_DO_NOT_REAP_CHILD flag
	 */
	g_spawn_close_pid(term->pid);

	del_tab(page, true);
}

void Sakura::on_eof(GtkWidget *widget)
{
	SAY("Got EOF signal");

	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Workaround for libvte strange behaviour. There is not child-exited signal for
	   the last terminal, so we need to kill it here.  Check with libvte authors about
	   child-exited/eof signals */
	if (gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook)) == 0) {

		auto *term = sakura_get_page_term(this, 0);

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

		del_tab(0, true);
	}
}

void Sakura::on_page_removed(GtkWidget *widget)
{
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 1) {
		/* If the first tab is disabled, window size changes and we need
		 * to recalculate its size */
		sakura_set_size();
	}
}

void Sakura::close_tab(GtkWidget *)
{
	pid_t pgid;
	GtkWidget *dialog;
	gint response;
	Terminal *term;
	gint page, npages;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	term = sakura_get_page_term(this, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!config.less_questions)) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			del_tab(page, true);
		}
	} else {
		del_tab(page, true);
	}
}
/* Delete the notebook tab passed as a parameter */
void Sakura::del_tab(gint page, bool exit_if_needed)
{
	auto *term = sakura_get_page_term(this, page);
	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = sakura_get_page_term(this, 0);
		const char *title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title)
			gtk_window_set_title(GTK_WINDOW(main_window), title);
	}

	term = sakura_get_page_term(this, page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		if (config.first_tab) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
		}
		sakura->keep_fc = true;
	}

	gtk_widget_hide(term->hbox);
	gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page);

	/* Find the next page, if it exists, and grab focus */
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
		term = sakura_get_page_term(this, page);
		gtk_widget_grab_focus(term->vte);
	}

	if (exit_if_needed) {
		if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 0)
			destroy(nullptr);
	}
}

void Sakura::toggle_fullscreen(GtkWidget *)
{
	if (!m_fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(main_window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(main_window));
	}

	m_fullscreen = !m_fullscreen;
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

void Sakura::beep(GtkWidget *widget)
{
	// Remove the urgency hint. This is necessary to signal the window manager
	// that a new urgent event happened when the urgent hint is set after this.
	gtk_window_set_urgency_hint(GTK_WINDOW(main_window), FALSE);

	if (config.urgent_bell) {
		gtk_window_set_urgency_hint(GTK_WINDOW(main_window), TRUE);
	}
}

gboolean Sakura::notebook_scoll(GtkWidget *widget, GdkEventScroll *event)
{
	gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

	switch (event->direction) {
	case GDK_SCROLL_DOWN: {
		if (config.stop_tab_cycling_at_end_tabs == 1) {
			gtk_notebook_set_current_page(
					GTK_NOTEBOOK(notebook), --page >= 0 ? page : 0);
		} else {
			gtk_notebook_set_current_page(
					GTK_NOTEBOOK(notebook), --page >= 0 ? page : npages - 1);
		}
		break;
	}
	case GDK_SCROLL_UP: {
		if (config.stop_tab_cycling_at_end_tabs == 1) {
			gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook),
					++page < npages ? page : npages - 1);
		} else {
			gtk_notebook_set_current_page(
					GTK_NOTEBOOK(notebook), ++page < npages ? page : 0);
		}
		break;
	}
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
	case GDK_SCROLL_SMOOTH:
		break;
	}

	return FALSE;
}

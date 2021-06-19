#include "notebook.h"
#include <libintl.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <gtkmm.h>
#include <X11/Xlib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include "gettext.h"
#include "terminal.h"
#include "sakura.h"
#include "window.h"
#include "sakuraold.h"

#define TAB_TITLE_CSS                                                                              \
	"* {\n"                                                                                    \
	"padding : 0px;\n"                                                                         \
	"}"

SakuraNotebook::SakuraNotebook(const Config *cfg) : m_cfg(cfg)
{
	set_scrollable(cfg->scrollable_tabs);

	/* Adding mask, for handle scroll events */
	add_events(Gdk::SCROLL_MASK);

	signal_scroll_event().connect(sigc::mem_fun(*this, &SakuraNotebook::on_scroll_event));
	signal_page_removed().connect(sigc::mem_fun(*this, &SakuraNotebook::on_page_removed_event));
}

SakuraNotebook::~SakuraNotebook()
{
	/* Delete all existing tabs */
	while (get_n_pages() >= 1) {
		del_tab(-1);
	}
}

bool SakuraNotebook::on_scroll_event(GdkEventScroll *scroll)
{
	int page = get_current_page();
	int npages = get_n_pages();

	switch (scroll->direction) {
	case GDK_SCROLL_DOWN: {
		if (m_cfg->stop_tab_cycling_at_end_tabs == 1) {
			set_current_page(--page >= 0 ? page : 0);
		} else {
			set_current_page(--page >= 0 ? page : npages - 1);
		}
		break;
	}
	case GDK_SCROLL_UP: {
		if (m_cfg->stop_tab_cycling_at_end_tabs == 1) {
			set_current_page(++page < npages ? page : npages - 1);
		} else {
			set_current_page(++page < npages ? page : 0);
		}
		break;
	}
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
	case GDK_SCROLL_SMOOTH:
		break;
	}

	return false;
}

void SakuraNotebook::on_page_removed_event(Gtk::Widget *, guint)
{
	if (get_n_pages() == 1) {
		/* If the first tab is disabled, window size changes and we need
		 * to recalculate its size */
		sakura->set_size();
	}
}

void SakuraNotebook::move_tab(gint direction)
{
	gint page = get_current_page();
	gint n_pages = get_n_pages();
	auto child = get_nth_page(page);

	if (direction == FORWARD) {
		if (page != n_pages - 1)
			reorder_child(*child, page + 1);
	} else {
		if (page != 0)
			reorder_child(*child, page - 1);
	}
}

/* Find the notebook page for the vte terminal passed as a parameter */
gint SakuraNotebook::find_tab(VteTerminal *vte_term)
{
	gint n_pages = get_n_pages();
	gint matched_page = -1;
	gint page = 0;

	do {
		auto term = get_tab_term(page);
		if ((VteTerminal *)term->vte == vte_term) {
			matched_page = page;
		}
		page++;
	} while (page < n_pages);

	return (matched_page);
}

void SakuraNotebook::show_scrollbar()
{
	sakura->keep_fc = 1;

	gint n_pages = get_n_pages();
	auto term = get_tab_term(get_current_page());

	if (!g_key_file_get_boolean(sakura->cfg, cfg_group, "scrollbar", NULL)) {
		sakura->config.show_scrollbar = true;
		sakura_set_config_boolean("scrollbar", TRUE);
	} else {
		sakura->config.show_scrollbar = false;
		sakura_set_config_boolean("scrollbar", FALSE);
	}

	/* Toggle/Untoggle the scrollbar for all tabs */
	for (int i = (n_pages - 1); i >= 0; i--) {
		term = get_tab_term(i);
		if (!sakura->config.show_scrollbar)
			gtk_widget_hide(term->scrollbar);
		else
			gtk_widget_show(term->scrollbar);
	}
	sakura->set_size();
}

static void sakura_beep(GtkWidget *w, void *data)
{
	auto obj = (Sakura *)data;
	obj->beep(w);
}

void SakuraNotebook::add_tab()
{
	auto term = new Terminal();
	auto tab_label_hbox = new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 2);
	tab_label_hbox->set_hexpand(true);
	tab_label_hbox->pack_start(term->label, true, false, 0);

	Gtk::Button *close_button;
	/* If the tab close button is enabled, create and add it to the tab */
	if (sakura->config.show_closebutton) {
		close_button = new Gtk::Button();
		/* Adding scroll-event to button, to propagate it to notebook (fix for scroll event
		 * when pointer is above the button) */
		close_button->add_events(Gdk::SCROLL_MASK);
		close_button->set_name("closebutton");
		close_button->set_relief(Gtk::RELIEF_NONE);

		GtkWidget *image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
		gtk_container_add(GTK_CONTAINER(close_button->gobj()), image);
		tab_label_hbox->pack_start(*close_button, Gtk::PACK_SHRINK);
	}

	if (sakura->config.tabs_on_bottom) {
		set_tab_pos(Gtk::POS_BOTTOM);
	}

	/* Set tab title style */
	sakura->provider->load_from_data(TAB_TITLE_CSS);

	auto context = tab_label_hbox->get_style_context();
	context->add_provider(sakura->provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	tab_label_hbox->show_all();

	gchar *cwd = NULL;
	/* Select the directory to use for the new tab */
	int index = get_current_page();
	if (index >= 0) {
		Terminal *prev_term = get_tab_term(index);
		cwd = prev_term->get_cwd();

		term->colorset = prev_term->colorset;
	}
	if (!cwd)
		cwd = g_get_current_dir();

	/* Keep values when adding tabs */
	sakura->keep_fc = true;

	if ((index = append_page(term->hbox, *tab_label_hbox)) == -1) {
		sakura_error("Cannot create a new tab");
		exit(1);
	}

	set_tab_reorderable(term->hbox);
	// TODO: Set group id to support detached tabs
	// gtk_notebook_set_tab_detachable(notebook->gobj(), term->hbox, TRUE);

	g_object_set_qdata_full(G_OBJECT(get_nth_page(index)->gobj()), term_data_id, term,
			(GDestroyNotify)Terminal::free);

	/* vte signals */
	g_signal_connect(G_OBJECT(term->vte), "bell", G_CALLBACK(sakura_beep), sakura);
	g_signal_connect(G_OBJECT(term->vte), "increase-font-size",
			G_CALLBACK(&Sakura::increase_font), NULL);
	g_signal_connect(G_OBJECT(term->vte), "decrease-font-size",
			G_CALLBACK(&Sakura::decrease_font), NULL);
	g_signal_connect(G_OBJECT(term->vte), "child-exited", G_CALLBACK(sakura_child_exited),
			sakura);
	g_signal_connect(G_OBJECT(term->vte), "eof", G_CALLBACK(sakura_eof), sakura);
	g_signal_connect(G_OBJECT(term->vte), "window-title-changed",
			G_CALLBACK(sakura_title_changed), NULL);
	g_signal_connect_swapped(G_OBJECT(term->vte), "button-press-event",
			G_CALLBACK(sakura_button_press), sakura->menu->gobj());

	/* Notebook signals */
	if (sakura->config.show_closebutton) {
		g_signal_connect(G_OBJECT(close_button->gobj()), "clicked",
				G_CALLBACK(sakura_closebutton_clicked), term->hbox.gobj());
	}

	/* Since vte-2.91 env is properly overwritten */
	char *command_env[2] = {const_cast<char *>("TERM=xterm-256color"), nullptr};
	/* First tab */
	int npages = get_n_pages();
	if (npages == 1) {
		set_show_tabs(sakura->config.first_tab);
		set_show_border(false);
		sakura->set_font();
		sakura->set_colors();
		/* Set size before showing the widgets but after setting the font */
		sakura->set_size();

		sakura->main_window->show_all();
		if (!sakura->config.show_scrollbar) {
			gtk_widget_hide(term->scrollbar);
		}

		sakura->main_window->show();

#ifdef GDK_WINDOWING_X11
		/* Set WINDOWID env variable */
		auto display = Gdk::Display::get_default();

		if (GDK_IS_X11_DISPLAY(display->gobj())) {
			auto gwin = sakura->main_window->get_window();
			if (gwin.get() != NULL) {
				guint winid = gdk_x11_window_get_xid(gwin->gobj());
				gchar *winidstr = g_strdup_printf("%d", winid);
				g_setenv("WINDOWID", winidstr, FALSE);
				g_free(winidstr);
			}
		}
#endif

		int command_argc = 0;
		char **command_argv;
		if (option_execute || option_xterm_execute) {
			GError *gerror = NULL;
			gchar *path;

			if (option_execute) {
				/* -x option */
				if (!g_shell_parse_argv(option_execute, &command_argc,
						    &command_argv, &gerror)) {
					switch (gerror->code) {
					case G_SHELL_ERROR_EMPTY_STRING:
						sakura_error("Empty exec string");
						exit(1);
						break;
					case G_SHELL_ERROR_BAD_QUOTING:
						sakura_error("Cannot parse command line arguments: "
							     "mangled quoting");
						exit(1);
						break;
					case G_SHELL_ERROR_FAILED:
						sakura_error("Error in exec option command line "
							     "arguments");
						exit(1);
					}
					g_error_free(gerror);
				}
			} else {
				/* -e option - last in the command line, takes all extra arguments
				 */
				if (option_xterm_args) {
					gchar *command_joined;
					command_joined = g_strjoinv(" ", option_xterm_args);
					if (!g_shell_parse_argv(command_joined, &command_argc,
							    &command_argv, &gerror)) {
						switch (gerror->code) {
						case G_SHELL_ERROR_EMPTY_STRING:
							sakura_error("Empty exec string");
							exit(1);
							break;
						case G_SHELL_ERROR_BAD_QUOTING:
							sakura_error("Cannot parse command line "
								     "arguments: mangled quoting");
							exit(1);
						case G_SHELL_ERROR_FAILED:
							sakura_error("Error in exec option command "
								     "line arguments");
							exit(1);
						}
					}
					if (gerror != NULL)
						g_error_free(gerror);
					g_free(command_joined);
				}
			}

			/* Check if the command is valid */
			if (command_argc > 0) {
				path = g_find_program_in_path(command_argv[0]);
				if (path) {
					vte_terminal_spawn_async(VTE_TERMINAL(term->vte),
							VTE_PTY_NO_HELPER, NULL, command_argv,
							command_env, G_SPAWN_SEARCH_PATH, NULL,
							NULL, NULL, -1, NULL, sakura_spawn_callback,
							term);
				} else {
					sakura_error("%s command not found", command_argv[0]);
					command_argc = 0;
					// exit(1);
				}
				free(path);
				g_strfreev(command_argv);
				g_strfreev(option_xterm_args);
			}
		} // else { /* No execute option */

		/* Only fork if there is no execute option or if it has failed */
		if ((!option_execute && !option_xterm_args) || (command_argc == 0)) {
			if (option_hold == TRUE) {
				sakura_error("Hold option given without any command");
				option_hold = FALSE;
			}
			vte_terminal_spawn_async(VTE_TERMINAL(term->vte), VTE_PTY_NO_HELPER, cwd,
					sakura->argv, command_env,
					(GSpawnFlags)(G_SPAWN_SEARCH_PATH |
							G_SPAWN_FILE_AND_ARGV_ZERO),
					NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, term);
		}
		/* Not the first tab */
	} else {
		sakura->set_font();
		sakura->set_colors();
		term->hbox.show_all();

		if (!sakura->config.show_scrollbar) {
			gtk_widget_hide(term->scrollbar);
		}

		if (npages == 2) {
			set_show_tabs(true);
			sakura->set_size();
		}
		/* Call set_current page after showing the widget: gtk ignores this
		 * function in the window is not visible *sigh*. Gtk documentation
		 * says this is for "historical" reasons. Me arse */
		set_current_page(index);
		vte_terminal_spawn_async(VTE_TERMINAL(term->vte), VTE_PTY_NO_HELPER, cwd,
				sakura->argv, command_env,
				(GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO),
				NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, term);
	}

	free(cwd);

	/* Init vte terminal */
	vte_terminal_set_scrollback_lines(VTE_TERMINAL(term->vte), sakura->config.scroll_lines);
	vte_terminal_match_add_regex(
			VTE_TERMINAL(term->vte), sakura->http_vteregexp, PCRE2_CASELESS);
	vte_terminal_match_add_regex(
			VTE_TERMINAL(term->vte), sakura->mail_vteregexp, PCRE2_CASELESS);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(term->vte), TRUE);
	vte_terminal_set_backspace_binding(VTE_TERMINAL(term->vte), VTE_ERASE_ASCII_DELETE);
	vte_terminal_set_word_char_exceptions(
			VTE_TERMINAL(term->vte), sakura->config.word_chars.c_str());
	vte_terminal_set_audible_bell(
			VTE_TERMINAL(term->vte), sakura->config.audible_bell ? TRUE : FALSE);
	vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(term->vte),
			sakura->config.blinking_cursor ? VTE_CURSOR_BLINK_ON
						       : VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_bold_is_bright(
			VTE_TERMINAL(term->vte), sakura->config.allow_bold ? TRUE : FALSE);
	vte_terminal_set_cursor_shape(VTE_TERMINAL(term->vte), sakura->config.cursor_type);

	// sakura->set_colors();

	/* FIXME: Possible race here. Find some way to force to process all configure
	 * events before setting keep_fc again to false */
	sakura->keep_fc = false;
}

void SakuraNotebook::close_tab()
{
	gint page = get_current_page();
	gint npages = get_n_pages();
	auto term = get_tab_term(page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	auto pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!sakura->config.less_questions)) {
		auto dialog = gtk_message_dialog_new(sakura->main_window->gobj(), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		auto response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			del_tab(page, true);
		}
	} else {
		del_tab(page, true);
	}
}

/* Delete the notebook tab passed as a parameter */
void SakuraNotebook::del_tab(gint page, bool exit_if_needed)
{
	auto term = get_tab_term(page);
	gint npages = get_n_pages();

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = get_tab_term(0);
		const char *title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title) {
			sakura->main_window->set_title(title);
		}
	}

	term = get_tab_term(page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		set_show_tabs(sakura->config.first_tab);
		sakura->keep_fc = true;
	}

	term->hbox.hide();
	remove_page(page);

	/* Find the next page, if it exists, and grab focus */
	if (get_n_pages() > 0) {
		term = get_current_tab_term();
		gtk_widget_grab_focus(term->vte);
	}

	if (exit_if_needed) {
		if (get_n_pages() == 0)
			sakura->destroy(nullptr);
	}
}

Terminal *SakuraNotebook::get_tab_term(gint page_id)
{
	return (Terminal *)g_object_get_qdata(
			G_OBJECT(get_nth_page(page_id)->gobj()), term_data_id);
}

Terminal *SakuraNotebook::get_current_tab_term()
{
	return get_tab_term(get_current_page());
}
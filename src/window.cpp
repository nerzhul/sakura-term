#include <iostream>
#include <libintl.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <gtkmm.h>
#include <X11/Xlib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "window.h"
#include "sakuraold.h"
#include "notebook.h"
#include "terminal.h"

#define TAB_TITLE_CSS                                                                              \
	"* {\n"                                                                                    \
	"padding : 0px;\n"                                                                         \
	"}"

SakuraWindow::SakuraWindow(Gtk::WindowType type, const Config *cfg) :
		Gtk::Window(type), m_config(cfg), notebook(new SakuraNotebook(cfg))
{
	set_title("sakura++");

	/* Figure out if we have rgba capabilities. */
	auto screen = get_screen();
	auto visual = screen->get_rgba_visual();
	if (visual.get() != nullptr && screen->is_composited()) {
		gtk_widget_set_visual(GTK_WIDGET(gobj()), visual->gobj());
	}

	/* Add datadir path to icon name and set icon */
	std::string icon_path;
	if (option_icon) {
		icon_path.append(option_icon);
	} else {
		icon_path.append(DATADIR).append("/pixmaps/").append(cfg->icon);
	}
	set_icon_from_file(std::string(icon_path));

	add(*notebook);

	signal_focus_in_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_in));
	signal_focus_out_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_out));
	signal_check_resize().connect(sigc::mem_fun(*this, &SakuraWindow::on_resize));
	signal_delete_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_delete));
}

SakuraWindow::~SakuraWindow()
{
}

bool SakuraWindow::on_delete(GdkEventAny *event)
{
	if (!sakura->config.less_questions) {
		gint npages = notebook->get_n_pages();

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to
		 * the shell PGID */
		for (gint i = 0; i < npages; i++) {
			Terminal *term = sakura->get_page_term(i);
			pid_t pgid = tcgetpgrp(vte_pty_get_fd(
					vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

			/* If running processes are found, we ask one time and exit */
			if ((pgid != -1) && (pgid != term->pid)) {
				std::unique_ptr<Gtk::MessageDialog> dialog(new Gtk::MessageDialog(
						*this,
						_("There are running processes.\n\nDo you really "
						  "want to close "
						  "Sakura?"),
						false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO,
						Gtk::DIALOG_MODAL));

				int response = dialog->run();
				if (response == Gtk::RESPONSE_YES) {
					sakura_config_done();
					return false;
				} else {
					return true;
				}
			}
		}
	}

	sakura_config_done();
	return false;
}

bool SakuraWindow::on_focus_in(GdkEventFocus *event)
{
	if (event->type != GDK_FOCUS_CHANGE)
		return false;

	/* Ignore first focus event */
	if (m_first_focus) {
		m_first_focus = false;
		return false;
	}

	if (!m_focused) {
		m_focused = true;

		if (!m_first_focus && m_config->use_fading) {
			sakura_fade_in();
		}

		sakura->set_colors();
		return true;
	}

	return false;
}

bool SakuraWindow::on_focus_out(GdkEventFocus *event)
{
	if (event->type != GDK_FOCUS_CHANGE)
		return false;

	if (m_focused) {
		m_focused = false;

		if (!m_first_focus && m_config->use_fading) {
			sakura_fade_out();
		}

		sakura->set_colors();
		return true;
	}

	return false;
}

void SakuraWindow::on_resize()
{
	if (get_width() != sakura->width || get_height() != sakura->height) {
		resized = true;
	}
}

void SakuraWindow::toggle_fullscreen()
{
	if (!m_fullscreen) {
		fullscreen();
	} else {
		unfullscreen();
	}

	m_fullscreen = !m_fullscreen;
}

static void sakura_beep(GtkWidget *w, void *data)
{
	auto obj = (Sakura *)data;
	obj->beep(w);
}

void SakuraWindow::add_tab()
{
	Gtk::Button *close_button;
	gchar *cwd = NULL;

	auto term = new Terminal();
	auto tab_label_hbox = new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 2);
	tab_label_hbox->set_hexpand(true);
	tab_label_hbox->pack_start(term->label, true, false, 0);

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
		notebook->set_tab_pos(Gtk::POS_BOTTOM);
	}

	/* Set tab title style */
	sakura->provider->load_from_data(TAB_TITLE_CSS);

	auto context = tab_label_hbox->get_style_context();
	context->add_provider(sakura->provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	tab_label_hbox->show_all();

	/* Select the directory to use for the new tab */
	int index = notebook->get_current_page();
	if (index >= 0) {
		Terminal *prev_term = sakura->get_page_term(index);
		cwd = prev_term->get_cwd();

		term->colorset = prev_term->colorset;
	}
	if (!cwd)
		cwd = g_get_current_dir();

	/* Keep values when adding tabs */
	sakura->keep_fc = true;

	if ((index = notebook->append_page(term->hbox, *tab_label_hbox)) == -1) {
		sakura_error("Cannot create a new tab");
		exit(1);
	}

	notebook->set_tab_reorderable(term->hbox);
	// TODO: Set group id to support detached tabs
	// gtk_notebook_set_tab_detachable(notebook->gobj(), term->hbox, TRUE);


	g_object_set_qdata_full(
			G_OBJECT(notebook->get_nth_page(index)->gobj()),
			term_data_id, term, (GDestroyNotify)Terminal::free);

	/* vte signals */
	g_signal_connect(G_OBJECT(term->vte), "bell", G_CALLBACK(sakura_beep), sakura);
	g_signal_connect(G_OBJECT(term->vte), "increase-font-size",
			G_CALLBACK(sakura_increase_font), NULL);
	g_signal_connect(G_OBJECT(term->vte), "decrease-font-size",
			G_CALLBACK(sakura_decrease_font), NULL);
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
	int npages = notebook->get_n_pages();
	if (npages == 1) {
		notebook->set_show_tabs(sakura->config.first_tab);
		notebook->set_show_border(false);
		sakura_set_font();
		sakura->set_colors();
		/* Set size before showing the widgets but after setting the font */
		sakura_set_size();

		gtk_widget_show_all(GTK_WIDGET(notebook->gobj()));
		if (!sakura->config.show_scrollbar) {
			gtk_widget_hide(term->scrollbar);
		}

		show();

#ifdef GDK_WINDOWING_X11
		/* Set WINDOWID env variable */
		auto display = Gdk::Display::get_default();

		if (GDK_IS_X11_DISPLAY(display->gobj())) {
			GdkWindow *gwin = get_window()->gobj();
			if (gwin != NULL) {
				guint winid = gdk_x11_window_get_xid(gwin);
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
		sakura_set_font();
		sakura->set_colors();
		term->hbox.show_all();

		if (!sakura->config.show_scrollbar) {
			gtk_widget_hide(term->scrollbar);
		}

		if (npages == 2) {
			notebook->set_show_tabs(true);
			sakura_set_size();
		}
		/* Call set_current page after showing the widget: gtk ignores this
		 * function in the window is not visible *sigh*. Gtk documentation
		 * says this is for "historical" reasons. Me arse */
		notebook->set_current_page(index);
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

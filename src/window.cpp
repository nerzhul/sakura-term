#include <iostream>
#include <libintl.h>
#include "window.h"
#include "sakuraold.h"
#include "notebook.h"
#include "terminal.h"

SakuraWindow::SakuraWindow(Gtk::WindowType type, const Config *cfg) :
	Gtk::Window(type),
	m_config(cfg),
	notebook(new SakuraNotebook(cfg))
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
	delete notebook;
}

bool SakuraWindow::on_delete(GdkEventAny *event)
{
	if (!sakura->config.less_questions) {
		gint npages = notebook->get_n_pages();

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to
		 * the shell PGID */
		for (gint i = 0; i < npages; i++) {
			Terminal *term = sakura_get_page_term(sakura, i);
			pid_t pgid = tcgetpgrp(vte_pty_get_fd(
				vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

			/* If running processes are found, we ask one time and exit */
			if ((pgid != -1) && (pgid != term->pid)) {
				std::unique_ptr<Gtk::MessageDialog> dialog(new Gtk::MessageDialog(*this,
					_("There are running processes.\n\nDo you really want to close "
					"Sakura?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO,
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

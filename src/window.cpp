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

	gtk_container_add(GTK_CONTAINER(gobj()), GTK_WIDGET(notebook->gobj()));

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
	Terminal *term;
	GtkWidget *dialog;
	gint response;
	gint npages;
	gint i;
	pid_t pgid;

	if (!sakura->config.less_questions) {
		npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura->main_window->notebook->gobj()));

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to
		 * the shell PGID */
		for (i = 0; i < npages; i++) {
			term = sakura_get_page_term(sakura, i);
			pgid = tcgetpgrp(vte_pty_get_fd(
				vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

			/* If running processes are found, we ask one time and exit */
			if ((pgid != -1) && (pgid != term->pid)) {
				dialog = gtk_message_dialog_new(GTK_WINDOW(sakura->main_window->gobj()),
					GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_YES_NO,
					_("There are running processes.\n\nDo you really "
					  "want to close Sakura?"));

				response = gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);

				if (response == GTK_RESPONSE_YES) {
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

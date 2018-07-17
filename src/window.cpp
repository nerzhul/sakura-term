#include <iostream>
#include "window.h"
#include "sakuraold.h"
#include "notebook.h"

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

	signal_focus_in_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_in));
	signal_focus_out_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_out));
	signal_check_resize().connect(sigc::mem_fun(*this, &SakuraWindow::on_resize));
}

SakuraWindow::~SakuraWindow()
{
	delete notebook;
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

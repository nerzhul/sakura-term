#include <iostream>
#include "window.h"
#include "sakuraold.h"

SakuraWindow::SakuraWindow(Gtk::WindowType type, Config * cfg) :
	Gtk::Window(type),
	config(cfg)
{
	signal_focus_in_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_in));
	signal_focus_out_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_focus_out));
	signal_configure_event().connect(sigc::mem_fun(*this, &SakuraWindow::on_resize));
}

bool SakuraWindow::on_focus_in(GdkEventFocus *event)
{
	if (event->type != GDK_FOCUS_CHANGE)
		return false;

	/* Ignore first focus event */
	if (first_focus) {
		first_focus = false;
		return false;
	}

	if (!focused) {
		focused = true;

		if (!first_focus && config->use_fading) {
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

	if (focused) {
		focused = false;

		if (!first_focus && config->use_fading) {
			sakura_fade_out();
		}

		sakura->set_colors();
		return true;
	}

	return false;
}

bool SakuraWindow::on_resize(GdkEventConfigure *event)
{
	if (event->width != sakura->width || event->height != sakura->height) {
		resized = TRUE;
	}

	std::cout << __FUNCTION__ << std::endl;
	return false;
}

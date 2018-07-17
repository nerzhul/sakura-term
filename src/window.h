#pragma once

#include <gtkmm.h>

class Config;

class SakuraWindow : public Gtk::Window
{
public:
	SakuraWindow(Gtk::WindowType type, Config *cfg);
	~SakuraWindow() = default;

	bool on_focus_in(GdkEventFocus *event);
	bool on_focus_out(GdkEventFocus *event);

private:
	const Config *config;
	bool focused = true;                    /* For fading feature */
	bool first_focus = true;                /* First time gtkwindow recieve focus when is created */
};

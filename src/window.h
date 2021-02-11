#pragma once

#include <gtkmm.h>

class Config;
class SakuraNotebook;

class SakuraWindow : public Gtk::Window
{
public:
	SakuraWindow(Gtk::WindowType type, const Config *cfg);
	~SakuraWindow();

	bool on_focus_in(GdkEventFocus *event);
	bool on_focus_out(GdkEventFocus *event);
	bool on_delete(GdkEventAny *event);
	void on_resize();

	void add_tab();

	GtkWindow *as_gtk_c();

	std::unique_ptr<SakuraNotebook> notebook;
	bool resized = false;

private:
	const Config *m_config;
	bool m_focused = true;                    /* For fading feature */
	bool m_first_focus = true;                /* First time gtkwindow recieve focus when is created */
};

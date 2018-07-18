#pragma once

#include <gtkmm.h>
#include "config.h"

class SakuraNotebook : public Gtk::Notebook
{
public:
	SakuraNotebook(const Config *cfg);
	~SakuraNotebook();

	bool on_scroll_event(GdkEventScroll *scroll);
private:

	const Config *m_cfg = nullptr;
};

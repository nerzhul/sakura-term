#pragma once

#include <gtkmm.h>
#include "config.h"

class SakuraNotebook : public Gtk::Notebook
{
public:
	SakuraNotebook(const Config *cfg);
	~SakuraNotebook();
private:

	const Config *m_cfg = nullptr;
};

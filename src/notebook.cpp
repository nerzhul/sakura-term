#include "notebook.h"
#include "sakuraold.h"

SakuraNotebook::SakuraNotebook(const Config *cfg) :
	m_cfg(cfg)
{
	set_scrollable(cfg->scrollable_tabs);
}

SakuraNotebook::~SakuraNotebook()
{
	/* Delete all existing tabs */
	while (get_n_pages() >= 1) {
		sakura->del_tab(-1);
	}
}

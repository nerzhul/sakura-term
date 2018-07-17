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
	while (gtk_notebook_get_n_pages(gobj()) >= 1) {
		sakura->del_tab(-1);
	}
}

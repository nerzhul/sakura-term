#pragma once

#include <libintl.h>

#define _(String) gettext(String)
#define N_(String) (String)
#define GETTEXT_PACKAGE "sakura"

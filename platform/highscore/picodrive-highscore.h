#pragma once

#include <libhighscore.h>

G_BEGIN_DECLS

#define PICODRIVE_TYPE_CORE (picodrive_core_get_type())

G_DECLARE_FINAL_TYPE (PicoDriveCore, picodrive_core, PICODRIVE, CORE, HsCore)

G_MODULE_EXPORT GType hs_get_core_type (void);

G_END_DECLS
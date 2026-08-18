/*
 * Highscore core glue for PicoDrive
 * Based on the libretro code
 * (C) notaz, 2013
 * (C) aliaspider, 2016
 * (C) Daniel De Matteis, 2013
 * (C) irixxxx, 2020-2024
 * (C) Alice Mikhaylenko (2025)
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include "picodrive-highscore.h"

#include <pico/pico_types.h>

#include <pico/pico_int.h>
#include <pico/pico.h>
#include <pico/patch.h>

#include <platform/common/input_pico.h>

#include "math.h"

#define VOUT_MAX_WIDTH 320
#define VOUT_MAX_HEIGHT 240

#define SND_RATE_MAX 53267

#define SIDE_BORDER 14

static PicoDriveCore *core;

struct _PicoDriveCore
{
  HsCore parent_instance;

  HsSoftwareContext *context;
  gint16 *video_buffer;

  gint16 *audio_buffer;

  int start_line;
  int line_count;
  int start_col;
  int col_count;

  guint8 *current_fb;
  short first_line_bgc;

  char *save_path;
  char *rom_path;

  float colorburst_phase;
};

static void picodrive_mega_drive_core_init (HsMegaDriveCoreInterface *iface);
static void picodrive_mega_drive_32x_core_init (HsMegaDrive32XCoreInterface *iface);
static void picodrive_mega_cd_core_init (HsMegaCdCoreInterface *iface);
static void picodrive_mega_cd_32x_core_init (HsMegaCd32XCoreInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (PicoDriveCore, picodrive_core, HS_TYPE_CORE,
                               G_IMPLEMENT_INTERFACE (HS_TYPE_MEGA_DRIVE_CORE, picodrive_mega_drive_core_init)
                               G_IMPLEMENT_INTERFACE (HS_TYPE_MEGA_DRIVE_32X_CORE, picodrive_mega_drive_32x_core_init)
                               G_IMPLEMENT_INTERFACE (HS_TYPE_MEGA_CD_CORE, picodrive_mega_cd_core_init)
                               G_IMPLEMENT_INTERFACE (HS_TYPE_MEGA_CD_32X_CORE, picodrive_mega_cd_32x_core_init))

void
lprintf (const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  hs_core_log_valist (HS_CORE (core), HS_LOG_INFO, fmt, ap);
  va_end(ap);
}

void
emu_video_mode_change (int start_line, int line_count, int start_col, int col_count)
{
  core->start_line = start_line;
  core->line_count = line_count;
  core->start_col = start_col;
  core->col_count = col_count;

  int w = col_count + SIDE_BORDER * 2;
  int h = Pico.m.pal ? 288 : 240;
  int v_border = start_line + (Pico.m.pal ? 24 : 0);

  hs_software_context_set_row_stride (core->context, 348 * 3 * 2);
  hs_software_context_set_area (core->context, &HS_RECTANGLE_INIT (0, 0, w, h));
  hs_software_context_set_overscan (core->context, &HS_BORDER_INIT (SIDE_BORDER, v_border));
}

void
emu_32x_startup (void)
{
  PicoDrawSetOutFormat (PDF_RGB555, TRUE);
  PicoDrawSetOutBuf (core->video_buffer, VOUT_MAX_WIDTH * 2);
}

static inline void
draw_pixel (short pixel, int field, int line, int col)
{
  gint16 r = (pixel >> 11) & 0x1F;
  gint16 g = (pixel >> 5) & 0x3F;
  gint16 b = pixel & 0x1F;

  core->current_fb[((line * 2 + field) * 348 + col) * 3 + 0] = r << 3 | r >> 2;
  core->current_fb[((line * 2 + field) * 348 + col) * 3 + 1] = g << 2 | g >> 4;
  core->current_fb[((line * 2 + field) * 348 + col) * 3 + 2] = b << 3 | b >> 2;
}

static inline void
fill_line (short bgc, int field, int line)
{
  for (int i = 0; i < SIDE_BORDER * 2 + core->col_count; i++)
    draw_pixel (bgc, field, line, i);
}

static int
end_line (guint num)
{
  short bgc = Pico.est.HighPal[Pico.video.reg[7] & 0x3f];
  int col = 0;

  int h_offset = core->start_col;
  int v_offset = Pico.m.pal ? 24 : 0;

  if (num == core->start_line)
    core->first_line_bgc = bgc;

  for (int i = 0; i < SIDE_BORDER; i++)
    draw_pixel (bgc, 0, num + v_offset, col++);

  for (int i = 0; i < core->col_count; i++)
    draw_pixel (core->video_buffer[num * VOUT_MAX_WIDTH + i - h_offset], 0, num + v_offset, col++);

  for (int i = 0; i < SIDE_BORDER; i++)
    draw_pixel (bgc, 0, num + v_offset, col++);

  return 0;
}

static const char *
find_bios (int *region, const char *cd_fname)
{
  HsMegaCdFirmware firmware_id;

  switch (*region) {
    case 1:
    case 2:
      firmware_id = HS_MEGA_CD_FIRMWARE_JAPAN;
      break;
    case 4:
      firmware_id = HS_MEGA_CD_FIRMWARE_NORTH_AMERICA;
      break;
    case 8:
      firmware_id = HS_MEGA_CD_FIRMWARE_EUROPE;
      break;
    default:
      g_assert_not_reached ();
  }

  hs_core_reset_used_firmware (HS_CORE (core));

  const char *path = hs_core_query_firmware_path (HS_CORE (core), firmware_id);
  if (!path)
    return NULL;

  g_autoptr (GFile) file = g_file_new_for_path (path);

  if (!g_file_query_exists (file, NULL))
    return NULL;

  return path;
}

static const char *
find_msu (const char *cd_fname)
{
  // TODO
  return NULL;
}

static void
snd_write (int len)
{
  hs_core_play_samples (HS_CORE (core), PicoIn.sndOut, len / 2);
}

static gboolean
do_load (PicoDriveCore  *self,
         GFile          *file,
         char           *dest,
         gsize           max_size,
         GError        **error)
{
  g_autofree char *data = NULL;
  gsize size;

  if (max_size == 0)
    return TRUE;

  if (!g_file_query_exists (file, NULL))
    return TRUE;

  if (!g_file_load_contents (file, NULL, &data, &size, NULL, error))
    return FALSE;

  if (size > max_size)
    hs_core_log (HS_CORE (self), HS_LOG_WARNING, "Too large size: %lu, expected %lu", size, max_size);

  size = MIN (size, max_size);

  memcpy (dest, data, size);

  return TRUE;
}

static gsize
get_sram_size (gboolean max)
{
  if (max || Pico.m.frame_count == 0 || Pico.sv.changed)
    return Pico.sv.size;

  return 0;
}

static gboolean
load_save (PicoDriveCore *self, GError **error)
{
  g_autoptr (GFile) file = g_file_new_for_path (self->save_path);

  if (PicoIn.AHW & PAHW_MCD) {
    if (!g_file_query_exists (file, NULL) &&
        !g_file_make_directory_with_parents (file, NULL, error)) {
      return FALSE;
    }

    if (Pico.romsize == 0) {
      g_autoptr (GFile) system_file = g_file_get_child (file, "system.brm");
      if (!do_load (self, system_file, (char *) Pico_mcd->bram, 0x2000, error))
        return FALSE;
    } else {
      g_autoptr (GFile) cart_file = g_file_get_child (file, "cart.brm");
      if (!do_load (self, cart_file, (char *) Pico.sv.data, get_sram_size (TRUE), error))
        return FALSE;
    }
  } else {
    if (!do_load (self, file, (char *) Pico.sv.data, get_sram_size (TRUE), error))
      return FALSE;
  }

  return TRUE;
}

static gboolean
load_game (PicoDriveCore *self, GError **error)
{
  enum media_type_e media_type;
  media_type = PicoLoadMedia (self->rom_path, NULL, 0,
                              "", find_bios, find_msu, NULL);

//  disk_current_index = cd_index;

  switch (media_type) {
  case PM_BAD_DETECT:
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_UNSUPPORTED_GAME, "Failed to detect ROM/CD image type");
    return FALSE;
  case PM_BAD_CD:
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_UNSUPPORTED_GAME, "Invalid CD image");
    return FALSE;
  case PM_BAD_CD_NO_BIOS:
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_MISSING_FIRMWARE, "Missing BIOS");
    return FALSE;
  case PM_ERROR:
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load ROM");
    return FALSE;
  default:
    break;
  }

  PicoLoopPrepare ();
  PsndRerate (0);

  PicoReset ();

  if (!load_save (self, error))
    return FALSE;

  for (int player = 0; player < HS_MEGA_DRIVE_MAX_PLAYERS; player++)
    PicoSetInputDevice (player, PICO_INPUT_PAD_6BTN);

  return TRUE;
}

static gboolean
picodrive_core_load_rom (HsCore      *core,
                         const char **rom_paths,
                         int          n_rom_paths,
                         const char  *save_path,
                         GError     **error)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);

  g_set_str (&self->rom_path, rom_paths[0]);
  self->context = hs_core_create_software_context (HS_CORE (self), 348, 576, HS_PIXEL_FORMAT_R8G8B8);

  PicoInit ();

  g_set_str (&self->save_path, save_path);

  return load_game (self, error);
}

static gboolean
picodrive_core_reset (HsCore *core, gboolean hard, GError **error)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);

  if (PicoReset ()) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to reset");
    return FALSE;
  }

  if (hard)
    self->colorburst_phase = 0;

  return TRUE;
}

static const int BUTTON_MAP[] = {
  GBTN_UP, GBTN_DOWN, GBTN_LEFT, GBTN_RIGHT,
  GBTN_A, GBTN_B, GBTN_C,
  GBTN_X, GBTN_Y, GBTN_Z,
  GBTN_START, GBTN_MODE
};

static void
picodrive_core_poll_input (HsCore *core, HsInputState *input_state)
{
  for (int player = 0; player < HS_MEGA_DRIVE_MAX_PLAYERS; player++) {
    PicoIn.pad[player] = 0;

    for (HsMegaDriveButton btn = 0; btn < HS_MEGA_DRIVE_N_BUTTONS; btn++) {
      if (input_state->mega_drive.pad_buttons[player] & 1 << btn)
        PicoIn.pad[player] |= 1 << BUTTON_MAP[btn];
    }
  }
}

static void
picodrive_core_run_frame (HsCore *core)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);
  PicoIn.skipFrame = 0;

  if (PicoPatches)
    PicoPatchApply();

  self->current_fb = hs_software_context_acquire_framebuffer (self->context);
  PicoFrame ();

  int pal_border = Pico.m.pal ? 24 : 0;
  for (int i = 0; i < self->start_line + pal_border; i++)
    fill_line (self->first_line_bgc, 0, i);

  short bgc = Pico.est.HighPal[Pico.video.reg[7] & 0x3f];
  for (int i = self->start_line + self->line_count + pal_border; i < 240 + pal_border * 2; i++)
    fill_line (bgc, 0, i);

  hs_software_context_release_framebuffer (self->context);

  HsPlatform platform = hs_core_get_platform (core);

  if (platform == HS_PLATFORM_MEGA_DRIVE_32X ||
      platform == HS_PLATFORM_MEGA_CD_32X) {
    if (Pico.m.pal)
      hs_software_context_set_colorburst (self->context, self->col_count * 3.0 / 640.0, 0.0, self->colorburst_phase);
    else
      hs_software_context_set_colorburst (self->context, self->col_count * 3.0 / 512.0, 0.0, self->colorburst_phase);

    self->colorburst_phase = 0.0;
//    self->colorburst_phase = fmod (self->colorburst_phase + 0.2, 1.0);
  } else {
    if (Pico.m.pal)
      hs_software_context_set_colorburst (self->context, self->col_count * 3.0 / 640.0, 0.0, 0.0);
    else
      hs_software_context_set_colorburst (self->context, self->col_count * 3.0 / 512.0, 0.0, 0.5);
  }

  // interlaced - Pico.est.rendstatus & PDRAW_INTERLACE
  // odd - Pico.video.status & SR_ODD
  self->current_fb = NULL;
}

static void
picodrive_core_stop (HsCore *core)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);

  PicoExit ();

  g_clear_object (&self->context);
  g_clear_pointer (&self->save_path, g_free);
  g_clear_pointer (&self->rom_path, g_free);
}

static gboolean
picodrive_core_reload_save (HsCore      *core,
                            const char  *save_path,
                            GError     **error)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);

  PicoExit ();
  PicoInit ();

  g_set_str (&self->save_path, save_path);

  return load_game (self, error);
}

static gboolean
picodrive_core_sync_save (HsCore  *core,
                          GError **error)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);
  g_autoptr (GFile) file = g_file_new_for_path (self->save_path);

  if (PicoIn.AHW & PAHW_MCD) {
    if (!g_file_query_exists (file, NULL) &&
        !g_file_make_directory_with_parents (file, NULL, error)) {
      return FALSE;
    }

    if (Pico.romsize == 0) {
      g_autoptr (GFile) system_file = g_file_get_child (file, "system.brm");

      if (!g_file_replace_contents (system_file, (char *) Pico_mcd->bram, 0x2000, NULL, FALSE,
                                    G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL, error)) {
        return FALSE;
      }
    } else {

      gsize size = get_sram_size (FALSE);
      if (size == 0)
        return TRUE;

      g_autoptr (GFile) cart_file = g_file_get_child (file, "cart.brm");
      if (!g_file_replace_contents (cart_file, (char *) Pico.sv.data, size, NULL, FALSE,
                                    G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL, error)) {
        return FALSE;
      }
    }
  } else {
    gsize size = get_sram_size (FALSE);
    if (size == 0)
      return TRUE;

    if (!g_file_replace_contents (file, (char *) Pico.sv.data, size, NULL, FALSE,
                                  G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL, error)) {
      return FALSE;
    }
  }

  return TRUE;
}

static void
picodrive_core_save_state (HsCore          *core,
                           const char      *path,
                           HsStateCallback  callback)
{
  GError *error = NULL;

  if (PicoState (path, TRUE) != 0) {
    g_set_error (&error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to save state");
    callback (core, &error);
    return;
  }

  callback (core, NULL);
}

static void
picodrive_core_load_state (HsCore          *core,
                           const char      *path,
                           HsStateCallback  callback)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);
  GError *error = NULL;

  if (PicoState (path, FALSE) != 0) {
    g_set_error (&error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load state");
    callback (core, &error);
    return;
  }

  self->colorburst_phase = hs_core_get_colorburst_offset (core);

  callback (core, NULL);
}

static double
picodrive_core_get_frame_rate (HsCore *core)
{
  return Pico.m.pal ? 50 : 60;
}

static double
picodrive_core_get_aspect_ratio (HsCore *core)
{
  PicoDriveCore *self = PICODRIVE_CORE (core);

  // Adapted from Genesis Plus GX port
  gboolean is_h40 = self->col_count == 320;
  double dotrate = (Pico.m.pal ? OSC_PAL : OSC_NTSC) / (is_h40 ? 8.0 : 10.0);
  double videosamplerate = Pico.m.pal ? 14750000.0 : 135000000.0 / 11.0;

  int width = self->col_count + SIDE_BORDER * 2;
  int height = Pico.m.pal ? 288 : 240;

  return (videosamplerate / dotrate) * ((double) width / ((double) height * 2.0));
}

static double
picodrive_core_get_sample_rate (HsCore *core)
{
  return PicoIn.sndRate;
}

static HsRegion
picodrive_core_get_region (HsCore *core)
{
  return Pico.m.pal ? HS_REGION_PAL : HS_REGION_NTSC;
}

static void
picodrive_core_finalize (GObject *object)
{
  PicoDriveCore *self = PICODRIVE_CORE (object);

  g_free (self->audio_buffer);
  g_free (self->video_buffer);

  G_OBJECT_CLASS (picodrive_core_parent_class)->finalize (object);

  core = NULL;
}

static void
picodrive_core_class_init (PicoDriveCoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  HsCoreClass *core_class = HS_CORE_CLASS (klass);

  object_class->finalize = picodrive_core_finalize;

  core_class->load_rom = picodrive_core_load_rom;
  core_class->reset = picodrive_core_reset;
  core_class->poll_input = picodrive_core_poll_input;
  core_class->run_frame = picodrive_core_run_frame;
  core_class->stop = picodrive_core_stop;

  core_class->reload_save = picodrive_core_reload_save;
  core_class->sync_save = picodrive_core_sync_save;

  core_class->save_state = picodrive_core_save_state;
  core_class->load_state = picodrive_core_load_state;

  core_class->get_frame_rate = picodrive_core_get_frame_rate;
  core_class->get_aspect_ratio = picodrive_core_get_aspect_ratio;

  core_class->get_sample_rate = picodrive_core_get_sample_rate;

  core_class->get_region = picodrive_core_get_region;
}

static void
picodrive_core_init (PicoDriveCore *self)
{
  g_assert (!core);

  core = self;

  PicoIn.opt = POPT_EN_STEREO | POPT_EN_FM |
               POPT_EN_PSG | POPT_EN_Z80 |
               POPT_EN_MCD_PCM | POPT_EN_MCD_CDDA | POPT_EN_MCD_GFX |
               POPT_EN_32X | POPT_EN_PWM | // POPT_EN_MCD_RAMCART |
               POPT_ACC_SPRITES | POPT_DIS_32C_BORDER |
               POPT_EN_DRC;

  self->audio_buffer = g_new0 (gint16, SND_RATE_MAX * 2 / 50);
  PicoIn.sndOut = self->audio_buffer;
  PicoIn.sndRate = YM2612_NATIVE_RATE ();
  PicoIn.sndFilterAlpha = (60 * 0x10000 / 100);
  PicoIn.writeSound = snd_write;

  PicoIn.autoRgnOrder = 0x184; // US, EU, JP

  self->video_buffer = g_new0 (gint16, VOUT_MAX_WIDTH * VOUT_MAX_HEIGHT);
  PicoDrawSetOutFormat (PDF_RGB555, FALSE);
  PicoDrawSetOutBuf (self->video_buffer, VOUT_MAX_WIDTH * 2);
  PicoDrawSetCallbacks (NULL, end_line);

  PicoInit ();

//   PicoIn.mcdTrayOpen = disk_tray_open;
//   PicoIn.mcdTrayClose = disk_tray_close;
}

static void
picodrive_mega_drive_core_init (HsMegaDriveCoreInterface *iface)
{
}

static void
picodrive_mega_drive_32x_core_init (HsMegaDrive32XCoreInterface *iface)
{
}

static void
picodrive_mega_cd_core_init (HsMegaCdCoreInterface *iface)
{
}

static void
picodrive_mega_cd_32x_core_init (HsMegaCd32XCoreInterface *iface)
{
}

GType
hs_get_core_type (void)
{
  return PICODRIVE_TYPE_CORE;
}

project('picodrive-highscore', 'c',
  version : '2.0.5',
)

commit = run_command('git', 'describe', '--always', '--dirty', '--match', '', check: false).stdout().strip()
version = meson.project_version()

cc = meson.get_compiler('c')

picodrive_source = [
  'picodrive-highscore.c',
  'mmap.c',

  '../common/mp3.c',
  '../common/mp3_dummy.c',
  '../common/mp3_sync.c',
  '../common/ogg.c',
  '../../cpu/cz80/cz80.c',
  '../../cpu/drc/cmn.c',
  '../../cpu/fame/famec.c',
  '../../cpu/sh2/mame/sh2pico.c',
  '../../cpu/sh2/compiler.c',
  '../../cpu/sh2/sh2.c',
  '../../pico/32x/32x.c',
  '../../pico/32x/draw.c',
  '../../pico/32x/memory.c',
  '../../pico/32x/pwm.c',
  '../../pico/32x/sh2soc.c',
  '../../pico/carthw/carthw.c',
  '../../pico/carthw/eeprom_spi.c',
  '../../pico/carthw/svp/memory.c',
  '../../pico/carthw/svp/ssp16.c',
  '../../pico/carthw/svp/svp.c',
  '../../pico/cd/cd_image.c',
  '../../pico/cd/cd_parse.c',
  '../../pico/cd/cdc.c',
  '../../pico/cd/cdd.c',
  '../../pico/cd/gfx.c',
  '../../pico/cd/gfx_dma.c',
  '../../pico/cd/mcd.c',
  '../../pico/cd/megasd.c',
  '../../pico/cd/memory.c',
  '../../pico/cd/misc.c',
  '../../pico/cd/pcm.c',
  '../../pico/cd/sek.c',
  '../../pico/pico/memory.c',
  '../../pico/pico/pico.c',
  '../../pico/pico/xpcm.c',
  '../../pico/sound/mix.c',
  '../../pico/sound/resampler.c',
  '../../pico/sound/sn76496.c',
  '../../pico/sound/sound.c',
  '../../pico/sound/vgm.c',
  '../../pico/sound/ym2413.c',
  '../../pico/sound/ym2612.c',
  '../../pico/cart.c',
  '../../pico/debug.c',
  '../../pico/draw.c',
  '../../pico/draw2.c',
  '../../pico/eeprom.c',
  '../../pico/media.c',
  '../../pico/memory.c',
  '../../pico/misc.c',
  '../../pico/mode4.c',
  '../../pico/patch.c',
  '../../pico/pico.c',
  '../../pico/sek.c',
  '../../pico/sms.c',
  '../../pico/state.c',
  '../../pico/videoport.c',
  '../../pico/z80if.c',
  '../../unzip/unzip.c',
]

picodrive_deps = [
  dependency('highscore-1'),
  dependency('gio-2.0'),
  dependency('libchdr'),
  dependency('vorbisfile'),
  dependency('zlib'),
  cc.find_library('m', required: false),
]

picodrive_c_args = [
  '-fvisibility=hidden',
  '-falign-functions=2',
  '-fno-common',
  '-finline-limit=42',
  '-fno-unroll-loops',
  '-Wno-stringop-overflow',
  '-Wno-unused-result',
  '-ffast-math',
  '-DEMU_F68K',
  '-D_USE_CZ80',
  '-DUSE_LIBCHDR',
  '-DDRC_SH2',
  '-DNDEBUG',
  '-D__LIBRETRO__',
]

picodrive_include_dirs = [
  include_directories('.'),
  include_directories('../..'),
]

core_dir = get_option('core_install_dir')
if core_dir == ''
  libdir = get_option('prefix') / get_option('libdir')
  core_dir = libdir / 'highscore' / 'cores' / 'picodrive'
endif

shared_library('picodrive-highscore',
  picodrive_source,
  dependencies : picodrive_deps,
  c_args : picodrive_c_args,
  include_directories : picodrive_include_dirs,
  name_prefix: '',
  install_dir : core_dir,
  install : true
)

descriptor = configuration_data()
descriptor.set('version', version)
descriptor.set('commit', commit)
configure_file(
  input: 'picodrive.highscore.in',
  output: 'picodrive.highscore',
  configuration: descriptor,
  install_dir: core_dir,
  install: true
)

install_data(
  '../../COPYING',
  install_dir: core_dir,
  rename: 'picodrive.license'
)

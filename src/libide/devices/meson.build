devices_headers = [
  'ide-device-manager.h',
  'ide-device-provider.h',
  'ide-device.h',
]

devices_sources = [
  'ide-device-manager.c',
  'ide-device-provider.c',
  'ide-device.c',
]

devices_enums = [
  'ide-device.h',
]

libide_enum_headers += files(devices_enums)
libide_public_headers += files(devices_headers)
libide_public_sources += files(devices_sources)

install_headers(devices_headers, subdir: join_paths(libide_header_subdir, 'devices'))

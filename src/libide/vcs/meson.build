vcs_headers = [
  'ide-vcs-config.h',
  'ide-vcs-initializer.h',
  'ide-vcs-uri.h',
  'ide-vcs.h',
]

vcs_sources = [
  'ide-vcs-config.c',
  'ide-vcs-initializer.c',
  'ide-vcs-uri.c',
  'ide-vcs.c',
]

vcs_enums = [
  'ide-vcs-config.h',
]

libide_public_headers += files(vcs_headers)
libide_public_sources += files(vcs_sources)
libide_enum_headers += files(vcs_enums)

install_headers(vcs_headers, subdir: join_paths(libide_header_subdir, 'vcs'))

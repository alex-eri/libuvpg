#project('libuvpq', 'c')

libuvpq_deps = [
  dependency('libuv'),
  dependency('libpq')
]

libuvpq_lib = static_library('uvpq',
  ['uvpq.c','util.c'],
  dependencies: libuvpq_deps,
  #install: true,
  #install_dir:get_option('bindir')
)

libuvpq = declare_dependency(
  link_with : libuvpq_lib
  )

libuvpq_so = shared_library('uvpq',
  ['uvpq.c','util.c'],
  dependencies: libuvpq_deps,
  install: true,
  #install_dir:get_option('bindir')
)


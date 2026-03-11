project('nitrosfx', 'c')

native = get_option('native')
install = native and meson.is_cross_build() ? false : true

nitrosfx_exe = executable('nitrosfx',
    sources: [
        'main.c',
        'util.c',
        'sbnk.c',
        'sdat.c',
        'sseq.c',
        'swar.c',
        'swav.c',
        'cJSON.c',
    ],
    native: native,
    install: install
)

meson.override_find_program('nitrosfx', nitrosfx_exe)
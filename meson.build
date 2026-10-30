project(
    'hdmiosd',
    ['c', 'cpp'],
    version: run_command(
        'sh',
        '-c', '''
        v=$(
            command -v zfr >/dev/null 2>&1 && zfr version 2>/dev/null || true
        )
        if [ -z "$v" ] && [ -f VERSION ]; then
            v=$(head -n1 VERSION | tr -d '\r')
            v=${v#v}
        fi
        if [ -z "$v" ]; then
            v="0.0.0" # FIXED TO 0.0.0, DO NOT MODIFY
        fi
        printf '%s' "$v"
        ''',
        check: false,
    ).stdout().strip(),
    license: 'AGPL-3.0-or-later',
    default_options: [
        'c_std=c99',
        'cpp_std=c++17',
        'warning_level=2',
    ],
)

project_author = 'Lenik'
project_email = 'hdmiosd@bodz.net'
project_year = 2026

cpp = meson.get_compiler('cpp')
asciidoctor = find_program('asciidoctor', required: true)
fs = import('fs')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
mandir = prefix / get_option('mandir')
localedir = prefix / get_option('localedir')
pkgdocdir = datadir / 'doc' / meson.project_name()

config_h = configuration_data()
config_h.set_quoted('PROJECT_VERSION', meson.project_version())
config_h.set_quoted('PROJECT_AUTHOR', project_author)
config_h.set_quoted('PROJECT_EMAIL', project_email)
config_h.set('PROJECT_YEAR', project_year)
config_h.set_quoted('LOCALEDIR', localedir)

configure_file(
    output: 'config.h',
    configuration: config_h,
)

bas_cpp_dep = dependency('bas-cpp', required: true)
bas_ui_dep = dependency('bas-ui', required: true)
wx_dep = dependency('wxwidgets', modules: ['base', 'core'], required: true)
nlohmann_json_dep = dependency('nlohmann_json', required: true)

bas_args = [
    '-DLOGGER_NAME=' + meson.project_name(),
    '-DTEXT_DOMAIN=' + meson.project_name(),
    '-DASSETS_NAME=hdmiosd',
]

lib_objects = []
assets_dir = meson.current_source_dir() / 'assets'
assets_src = 'src/assets.s'
if fs.exists(assets_dir) and fs.exists(assets_src)
    # Depend on asset files so icons/SVG/PNG edits rebuild the zip.
    assets_files = run_command(
        'bash',
        '-c',
        'cd "$1" && find . -type f | sort',
        'find-assets',
        assets_dir,
        check: true,
    ).stdout().strip().split('\n')
    assets_inputs = []
    foreach rel : assets_files
        if rel != ''
            assets_inputs += [assets_dir / rel]
        endif
    endforeach
    assets_zip = custom_target(
        'assets_zip',
        output: 'assets.zip',
        input: assets_inputs,
        command: [
            'sh',
            '-c', 'dst=$(readlink -f "@OUTPUT@"); cd "@0@" && zip -qr "$dst" .'.format(assets_dir),
        ],
        build_by_default: true,
        install: false,
    )
    assets_s = configure_file(
        input: 'src/assets.s',
        output: 'assets.s',
        copy: true,
    )
    as_prog = find_program('as', required: true)
    assets_o = custom_target(
        'assets_o',
        output: 'assets.o',
        input: [assets_s, assets_zip],
        command: [as_prog, '-o', '@OUTPUT@', '@INPUT0@'],
        depends: assets_zip,
    )
    lib_objects += [assets_o]
endif

inc = include_directories('src', '.')

nowarn = cpp.get_supported_arguments(['-Wno-deprecated-declarations'])

sources = [
    'src/module.cpp',
    'src/ddc.cpp',
    'src/vcp.cpp',
    'src/display.cpp',
    'src/settings.cpp',
    'src/icons.cpp',
    'src/theme.cpp',
    'src/osd_draw.cpp',
    'src/osd_frame.cpp',
    'src/osd_canvas.cpp',
    'src/osd_theme_ui.cpp',
    'src/osd_placement.cpp',
    'src/hdmiosd.cpp',
]

executable(
    'hdmiosd',
    sources,
    objects: lib_objects,
    include_directories: inc,
    dependencies: [bas_cpp_dep, bas_ui_dep, wx_dep, nlohmann_json_dep],
    cpp_args: bas_args + nowarn,
    install: true,
    install_dir: bindir,
)

install_data(
    'hdmiosd.bash',
    install_dir: datadir / 'bash-completion' / 'completions',
    rename: 'hdmiosd',
)

install_data(
    [
        'LICENSE',
        'README.md',
        'README-zh_CN.md',
    ],
    install_dir: pkgdocdir,
)

man_asciidoctor_args = [
    asciidoctor,
    '-b', 'manpage',
    '-a', 'project-version=' + meson.project_version(),
    '-a', 'project-year=@0@'.format(project_year),
    '-a', 'project-author=' + project_author,
    '-a', 'project-email=' + project_email,
]

custom_target(
    'hdmiosd-man',
    input: 'man/hdmiosd.adoc',
    output: 'hdmiosd.1',
    command: man_asciidoctor_args + ['-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true,
    install: true,
    install_dir: mandir / 'man1',
)

man_locales = ['fr', 'it', 'de', 'es_MX', 'zh_CN', 'ja', 'ko', 'ar']
install_built = find_program('scripts/install-built-file.sh')
foreach loc : man_locales
    man_loc = custom_target(
        'hdmiosd-man-' + loc,
        input: 'man' / loc / 'hdmiosd.adoc',
        output: 'hdmiosd.' + loc + '.1',
        command: man_asciidoctor_args + ['-o', '@OUTPUT@', '@INPUT@'],
        build_by_default: true,
    )
    meson.add_install_script(
        install_built,
        man_loc,
        mandir / loc / 'man1' / 'hdmiosd.1',
    )
endforeach

subdir('po')

run_target(
    'posync',
    command: [find_program('zfr'), 'translate', '--sync'],
)

subdir('tests')

run_target(
    'look',
    command: [
        find_program('zfr'),
        'build',
        '--look',
        '-C', meson.project_source_root(),
        '-b', meson.project_build_root(),
    ],
)

run_target(
    'install-symlinks',
    command: [
        find_program('bash'),
        meson.project_source_root() / 'scripts' / 'install-symlinks.sh',
        get_option('prefix'),
        bindir,
        datadir,
        mandir,
        meson.project_build_root(),
        meson.project_source_root(),
    ],
)

run_target(
    'uninstall-symlinks',
    command: [
        find_program('bash'),
        meson.project_source_root() / 'scripts' / 'uninstall-symlinks.sh',
        bindir,
        datadir,
        mandir,
    ],
)

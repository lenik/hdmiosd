test_exe = executable(
    'vcp_test',
    [
        'vcp_test.cpp',
        '../src/vcp.cpp',
    ],
    include_directories: include_directories('../src'),
    cpp_args: ['-DTEXT_DOMAIN=hdmiosd'],
    dependencies: [bas_cpp_dep],
    install: false,
)

test('vcp', test_exe)

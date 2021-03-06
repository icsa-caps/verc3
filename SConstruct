# -*- mode:python -*-

import os

# ------------- OPTIONS ------------- #

AddOption('--prefix',
    help="Installation prefix. [Default: /usr/local]",
    metavar='DIR',
    default="/usr/local",
)

AddOption('--release',
    help="Release build target configuration (full optimizations).",
    action='store_true',
    default=False,
)

AddOption('--harden',
    help="Enable hardening compiler flags.",
    action='store_true',
    default=False,
)

AddOption('--dbgopt',
    help="Debug build with some optimizations.",
    action='store_true',
    default=False,
)

AddOption('--sanitize',
    help="Debug build with sanitizers.",
    action='store_true',
    default=False,
)

AddOption('--compile_commands',
    help="Emit compilation database.",
    action='store_true',
    default=False,
)

# ------------- COMMON ------------- #

main = Environment(
    ENV = os.environ,
    PREFIX = GetOption('prefix'),
)

main['CXX']          = os.environ.get('CXX', 'g++')
main['CC']           = os.environ.get('CC', 'gcc')
main['DOXYGEN']      = os.environ.get('DOXYGEN', 'doxygen')
main['CLANG_TIDY']   = os.environ.get('CLANG_TIDY', 'clang-tidy')
main['CLANG_FORMAT'] = os.environ.get('CLANG_FORMAT', 'clang-format')
main['CPPLINT']      = os.environ.get('CPPLINT', 'cpplint')

main.Append(
    CPPDEFINES = {'GSL_THROW_ON_CONTRACT_VIOLATION' : 1},
    CPPFLAGS   = [
        '-Wall', #'-Werror',
        '-isystem', Dir('#/third_party/gflags/include'),
        '-isystem', Dir('#/third_party/mc2lib/include'),
        '-isystem', Dir('#/third_party/GSL')
        ],
    CPPPATH    = ['#/include', '#/src', '#/build/src'],
    CFLAGS     = ['-std=c11', '-pthread'],
    CXXFLAGS   = ['-std=c++14', '-pthread'],
    LIBPATH    = [
        '#/third_party/gflags/lib'
        ],
    LIBS       = ['gflags'],
    LINKFLAGS  = ['-pthread'],
)

# Optional dependencies

# TCMalloc gives noticable performance boost for some use-cases.
main.ParseConfig("pkg-config libtcmalloc --cflags --libs 2>/dev/null || true")

# ------------- BUILD VARIANTS ------------- #

main.VariantDir('build/src',  'src',  duplicate=False)
main.VariantDir('build/test', 'test', duplicate=False)

if GetOption('release'):
    main.Append(CPPDEFINES = ['NDEBUG'],
                CPPFLAGS = ['-O2'])

    if GetOption('harden'):
        main.Append(CPPDEFINES = {'_FORTIFY_SOURCE' : 2})
else:
    main.Append(CPPFLAGS = ['-g'])

    if GetOption('dbgopt'):
        main.Append(CPPFLAGS = ['-O'])

    if GetOption('sanitize'):
        sanitizers = ['undefined', 'address']
        san_flags = ['-fsanitize={}'.format(s) for s in sanitizers]
        main.Append(CPPFLAGS  = san_flags, LINKFLAGS = san_flags)

if GetOption('harden'):
    main.Append(CPPFLAGS = [
                    # More warnings
                    '-Wconversion', '-Wsign-conversion', '-Wformat',
                    '-Wformat-security',
                    # Stack protection
                    '-fstack-protector-strong', '--param', 'ssp-buffer-size=4',
                    # Signed integer overflow checks
                    '-ftrapv'
                ],
                # Enable Full RELRO
                LINKFLAGS = ['-Wl,-z,relro,-z,now'])

# ------------- COLLECT SOURCES/TARGETS ------------- #

if GetOption('compile_commands'):
    main.Tool('compile_commands')
    main.CompileCommands('build')
    if BUILD_TARGETS and 'build/compile_commands.json' not in BUILD_TARGETS:
        BUILD_TARGETS.append('build/compile_commands.json')

main.SConscript('build/src/SConscript' , {'env' : main})
main.SConscript('build/test/SConscript', {'env' : main})

# ------------- ALIASES/COMMANDS------------- #

def Phony(env = main, deps = [], **kw):
    if not env: env = DefaultEnvironment()
    for target, action in kw.items():
        env.AlwaysBuild(env.Alias(target, deps, action))

Phony(
    cleanall = "rm -rf build",

    doc = "rm -rf doc/api/html && $DOXYGEN Doxyfile",

    format = "@echo 'Modifying files in-place...'\n"
             "$CLANG_FORMAT -style=file -i "
               "$$(git ls-files | grep -E '\.(hpp|hh|cpp|cc|cxx|h|c)$$')",

    lint = "$CPPLINT --extensions=hpp,hh,cpp,cc,cxx "
             "--filter=-build/c++11,-whitespace,-legal/copyright "
             "$$(git ls-files | grep -E '\.(hpp|hh|cpp|cc|cxx)$$')",

    tidy = "$CLANG_TIDY -header-filter='.*' "
             "-checks='-*,clang-analyzer-*,google*,misc*,-misc-unused-parameters,performance*,modernize*' "
             "-p build/compile_commands.json "
             "$$(git ls-files | grep -E '\.(cpp|cc|cxx|c)$$')",
)

# vim: set ft=python :

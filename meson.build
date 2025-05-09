# SPDX-License-Identifier: Apache-2.0

project(
	'dcdiag',
	['c', 'cpp'],
	default_options : [
		'c_std=gnu17',
		'cpp_std=gnu++17',
	],
	meson_version : '>=0.56',
)

pymod = import('python')

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

bash = find_program('bash')
perl = find_program('perl')
python = pymod.find_installation()
shell = find_program('sh')

if cpp.get_id() != 'gcc' or cc.get_id() != 'gcc'
	warning('We recommend building OpenDCDiag with the GNU Compiler Collection (gcc). Your build may fail or your executable may not behave as expected.')
endif

if cpp.get_linker_id() != 'ld.bfd' or cc.get_linker_id() != 'ld.bfd'
	warning('We recommend linking OpenDCDiag with the binutils linker (ld). Your build may fail or your executable may not behave as expected.')
endif

march_base = '-march=' + get_option('march_base')

debug_nasm_flags = ''
if get_option('buildtype') == 'debug'
	debug_c_flags = [
		'-DDEBUG',
		'-ggdb3',
		'-O0',
		'-U_FORTIFY_SOURCE'
	]
elif get_option('buildtype') == 'debugoptimized'
	debug_c_flags = [
		'-DDEBUG',
		'-ggdb3',
		'-O3',
		'-U_FORTIFY_SOURCE'
	]
else
	debug_c_flags = [
		'-DNDEBUG',
                '-g'
	]
endif

if get_option('static_libstdcpp') == true
add_project_link_arguments([
	'-static-libstdc++',
], language : 'cpp')
endif

nasm_system_flags = [
	'-felf64',
	'-Xgnu',
	'-DLINUX',
	'-D__LINUX__',
]
default_c_flags = []
default_cpp_flags = []

if target_machine.system() == 'windows'
add_project_arguments([
	'-D__STDC_FORMAT_MACROS',
	'-D__USE_MINGW_ANSI_STDIO',
	'-D_POSIX',
	'-D_WIN32_WINNT=0x0A00',
	'-Wa,-mbig-obj',
	'-include',
	meson.current_source_dir() + '/framework/sysdeps/win32/win32_stdlib.h',
], language: [ 'c', 'cpp' ])
add_project_link_arguments([
	'-mconsole',
	'-Wl,--stack,8388608',
	'-static-libgcc',
	'-Wl,-static',
	'-lpthread',
	'-lm',
	'-lssp',
	'-Wl,-Bdynamic',
	'-lshlwapi',
	'-lntdll',
], language : [ 'c', 'cpp' ])
nasm_system_flags = [
	'-fwin64',
	'-Xgnu',
	'-DWIN_ABI',
]
else
if get_option('buildtype') == 'debug'
	debug_nasm_flags = '-gdwarf'
endif
endif

default_c_flags += [
	'-DEXECUTABLE_NAME="' + get_option('executable_name') + '"',
	'-DSANDSTONE',
	'-maes',
	'-mpclmul',
	'-mno-rdrnd',
	'-fno-associative-math',
	'-falign-loops=32',
	'-fno-asynchronous-unwind-tables',
]

default_cpp_flags += [
	'-DEXECUTABLE_NAME="' + get_option('executable_name') + '"',
	'-DSANDSTONE',
	'-maes',
	'-mpclmul',
	'-mno-rdrnd',
	'-fno-associative-math',
	'-falign-loops=32',
	'-fvisibility-inlines-hidden',
]

if get_option('b_sanitize') != 'none'
	default_c_flags += [
		'-g',
		'-fno-omit-frame-pointer',
	]
	default_cpp_flags += [
		'-g',
		'-fno-omit-frame-pointer',
	]
endif

default_c_warn = [
	'-Wall',
	'-Wextra',
	'-Werror=incompatible-pointer-types',
	'-Werror=implicit-function-declaration',
	'-Werror=int-conversion',
	'-Wno-unused-parameter',
	'-Wno-sign-compare',
]

default_cpp_warn = [
	'-Wall',
	'-Wextra',
	'-Wno-unused-parameter',
	'-Wno-sign-compare',
	'-Wno-missing-field-initializers',
	'-Wno-non-virtual-dtor',
]

# Subdirs can add target dependencies if they wish
target_deps = []

boost_dep = dependency('boost', version : '>=1.69', required : false)
# Check for required headers
# (Also accomodates distributions which do not package a pkg-config file for boost)
boost_has_hdrs = \
	cpp.check_header('boost/algorithm/string.hpp') and \
	cpp.check_header('boost/type_traits/is_complex.hpp')
has_boost = boost_dep.found() or boost_has_hdrs

if not has_boost
	assert(false, 'Could not find boost or its headers')
elif not boost_dep.found()
	boost_dep = []
endif

pthread_dep = dependency('threads')

dl_dep = []
gtest_dep = []
if target_machine.system() != 'windows'
	dl_dep = cc.find_library('dl')
	gtest_dep = dependency('gtest_main')
endif

target_deps += [
        boost_dep,
        dl_dep,
        pthread_dep,
]

generated_gitid_h = configure_file(
	input : [
		'framework/scripts/make-gitid.pl',
	],
	output : 'gitid.h',
	encoding : 'ascii',
	command : [
		perl, '@INPUT0@', '@OUTPUT@', 'opendcdiag',
	],
)

# Subdirs add (static) libraries to these arrays for linking
framework_main = []

sandstone_tests = []
sandstone_tests_link = []
sandstone_tests_link_whole = []

unittests_sources = []

subdir('framework')
subdir('tests')

sandstone_tests_link_whole += [ sandstone_tests ]

executable(
	get_option('executable_name'),
	install : true,
	dependencies : target_deps,
	link_with : [
		sysdeps_a,
		sysdeps_unix_a,
		framework_core_a,
		framework_main,
		sandstone_tests_link,
	],
	link_whole : [
		sandstone_tests_link_whole,
	],
	include_directories : [
		framework_incdir,
	],
	cpp_args : [
		default_cpp_warn,
		default_cpp_flags,
	],
)

executable(
	'unittests',
	files(
		'framework/sysdeps/linux/malloc.cpp',
	),
	unittests_sources,
	dependencies: [
		gtest_dep,
		pthread_dep
	],
	include_directories : [
		framework_incdir,
	],
	cpp_args : [
		'-march=haswell',
		debug_c_flags,
		default_cpp_warn,
	],
	build_by_default : false,
	native : true,
)

summary({
		'Logging format': get_option('logging_format'),
		'Min architecture': get_option('march_base'),
		'Using main function': framework_main.name(),
		'Using -march': march_base,
		'Static libstdc++': get_option('static_libstdcpp'),
		'Executable name': get_option('executable_name'),
		'Fallback executable name': get_option('fallback_exec'),
	},
	section: 'Configuration',
	bool_yn: true)

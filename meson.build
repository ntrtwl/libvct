project('libvct', 'c',
    meson_version: '>=1.2.0',
    version: '1.3.1'
)

fs = import('fs')

cc = meson.get_compiler('c')

############################################################
###                    COMPILER FLAGS                    ###
############################################################

if not meson.is_subproject() and buildtype != 'plain'
    c_args = [
        '-O4,p',
        '-proc', 'arm946e',
        '-enum', 'int',
        '-lang', 'c99',
        '-Cpp_exceptions', 'off',
        '-gccext,on',
        '-msgstyle', 'gcc',
        '-ipa', 'file',
        '-interworking',
        '-inline', 'on,noauto',
        '-char', 'signed',
        '-nosyspath',
        '-stdinc'
    ]
else
    c_args = []
endif


add_project_arguments(c_args,
    language: 'c',
    native: false
)

############################################################
###                    INCLUDE PATHS                     ###
############################################################

libvct_includes = include_directories('include')

############################################################
###                     DEPENDENCIES                     ###
############################################################

nitrosdk_dep = dependency('NitroSDK')
nitrodwc_dep = dependency('NitroDWC')
nitrowifi_dep = dependency('NitroWiFi')

############################################################
###                      LIBRARIES                       ###
############################################################

libvct = static_library('vct',
    include_directories: libvct_includes,
    pic: false,
    c_args: c_args,
    sources: files(
        'src/adpcm.c',
        'src/audio.c',
        'src/decoder.c',
        'src/g711.c',
        'src/queue.c',
        'src/stream.c',
        'src/ssp.c',
        'src/udp_buffer.c',
        'src/vad.c',
        'src/fir.c',
        'src/mixer.c',
        'src/vct_main.c',
    ),
    dependencies: [nitrosdk_dep, nitrodwc_dep, nitrowifi_dep]
)

############################################################
###                    FOR CONSUMERS                     ###
############################################################

libvct_dep = declare_dependency(
    include_directories: libvct_includes,
    link_with: libvct
)

meson.override_dependency('libvct', libvct_dep)

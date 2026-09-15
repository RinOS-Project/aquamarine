project('Aquamarine', 'c', version: '1.0.0',
        default_options: ['c_std=c11', 'warning_level=3'])

aq_inc = include_directories('.', '../libunicode', '../rinresource/include',
                             '../../RinOS-SDK/include')
aq_sources = files('aq_blend.c', 'aq_color.c', 'aq_draw.c', 'aq_gradient.c',
                   'aq_path.c', 'aq_render3d.c', 'aq_surface.c', 'aq_text.c',
                   'aq_transform.c', '../libunicode/core.c')
aquamarine = library('Aquamarine', aq_sources,
                     include_directories: aq_inc,
                     install: true, version: '1.0.0')
aquamarine_dep = declare_dependency(link_with: aquamarine,
                                    include_directories: aq_inc)
install_headers('aq_blend.h', 'aq_clip.h', 'aq_color.h', 'aq_cpp.h',
                'aq_draw.h', 'aq_font_8x16.h', 'aq_gradient.h', 'aq_math.h',
                'aq_math3d.h', 'aq_path.h', 'aq_render3d.h', 'aq_surface.h',
                'aq_text.h', 'aq_transform.h', 'aq_types.h', 'aquamarine.h',
                subdir: 'aquamarine')

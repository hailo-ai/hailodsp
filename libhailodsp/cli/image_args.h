#pragma once

#include <hailo/hailodsp.h>

#include <argp.h>

struct image_arguments {
    char *path;
    size_t width;
    size_t height;
    dsp_image_format_t format;
};

dsp_image_properties_t *read_image(dsp_device device, struct image_arguments *args);
dsp_image_properties_t *alloc_image(dsp_device device, struct image_arguments *args);
int write_image_to_file(char *path, dsp_image_properties_t *image);
void release_image(dsp_device device, dsp_image_properties_t *image);

size_t parse_string_arg(const char *arg,
                        const char **arg_options,
                        size_t arg_options_count,
                        const char *arg_name,
                        struct argp_state *state);
dsp_image_format_t parse_format_arg(const char *arg, const char *arg_name, struct argp_state *state);
size_t parse_uint_arg(const char *arg, const char *arg_name, struct argp_state *state);

#define RELEASE_IMAGE(dev, img)      \
    do {                             \
        if (img) {                   \
            release_image(dev, img); \
        }                            \
    } while (0)

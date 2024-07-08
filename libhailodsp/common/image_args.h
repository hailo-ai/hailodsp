#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <hailo/hailodsp.h>

struct image_arguments {
    const char *path;
    size_t width;
    size_t height;
    size_t stride; // 0 means auto
    dsp_image_format_t format;
    dsp_memory_type_t memory_type;
};

dsp_image_properties_t *read_image(dsp_device device, struct image_arguments *args);
dsp_image_properties_t *alloc_image(dsp_device device, struct image_arguments *args);
int write_image_to_file(const char *path, dsp_image_properties_t *image);
void release_image(dsp_device device, dsp_image_properties_t *image);

#define RELEASE_IMAGE(dev, img)      \
    do {                             \
        if (img) {                   \
            release_image(dev, img); \
        }                            \
    } while (0)

#ifdef __cplusplus
}
#endif
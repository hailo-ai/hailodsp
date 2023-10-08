#include "image_args.h"

#include <cleanup.h>
#include <utils.h>

#include <stdio.h>
#include <strings.h>

const char *format_strings[DSP_IMAGE_FORMAT_COUNT] = {
    [DSP_IMAGE_FORMAT_GRAY8] = "gray8",
    [DSP_IMAGE_FORMAT_RGB] = "rgb",
    [DSP_IMAGE_FORMAT_NV12] = "nv12",
    [DSP_IMAGE_FORMAT_A420] = "a420",
};

static dsp_image_properties_t *generic_alloc_image(dsp_device device,
                                                   struct image_arguments *args,
                                                   dsp_data_plane_t *planes,
                                                   size_t planes_count)
{
    size_t i = 0;
    dsp_image_properties_t *image = NULL;

    image = malloc(sizeof(*image));
    if (image == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_exit;
    }

    dsp_data_plane_t *ret_planes = malloc(sizeof(*ret_planes) * planes_count);
    if (ret_planes == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_free_image;
    }

    for (i = 0; i < planes_count; ++i) {
        void *data;
        dsp_status status = dsp_create_buffer(device, planes[i].bytesused, &data);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "Allocation failed\n");
            goto l_free_planes;
        }

        ret_planes[i].userptr = data;
        ret_planes[i].bytesused = planes[i].bytesused;
        ret_planes[i].bytesperline = planes[i].bytesperline;
    }

    image->planes = ret_planes;
    image->planes_count = planes_count;
    image->format = args->format;
    image->width = args->width;
    image->height = args->height;

    goto l_exit;

l_free_planes:
    for (size_t j = 0; j < i; ++j) {
        (void)dsp_release_buffer(device, ret_planes[j].userptr);
    }
    free(ret_planes);

l_free_image:
    free(image);
    image = NULL;

l_exit:
    return image;
}

static int generic_read_image(dsp_image_properties_t *image, struct image_arguments *args)
{
    FILE *file = NULL;
    int ret;

    file = fopen(args->path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed open file %s\n", args->path);
        ret = 1;
        goto exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        size_t read_count = fread(image->planes[i].userptr, image->planes[i].bytesused, 1, file);
        if (read_count != 1) {
            fprintf(stderr, "Failed to read %ld bytes from file\n", image->planes[i].bytesused);
            ret = 1;
            goto exit;
        }
    }

    ret = 0;

exit:
    CLOSE_FILE(file);
    return ret;
}

static dsp_image_properties_t *alloc_gray8_image(dsp_device device, struct image_arguments *args)
{
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = ROUND_UP(args->width, 4),
            .bytesused = ROUND_UP(args->width, 4) * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_rgb_image(dsp_device device, struct image_arguments *args)
{
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = ROUND_UP(args->width * 3, 4),
            .bytesused = ROUND_UP(args->width * 3, 4) * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_nv12_image(dsp_device device, struct image_arguments *args)
{
    dsp_data_plane_t planes[2] = {
        [0] =
            {
                // Y
                .bytesperline = ROUND_UP(args->width, 4),
                .bytesused = ROUND_UP(args->width, 4) * ROUND_UP(args->height, 2),
            },
        [1] =
            {
                // UV
                .bytesperline = ROUND_UP(args->width, 4),
                .bytesused = (ROUND_UP(args->width, 4) * args->height) / 2,
            },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_a420_image(dsp_device device, struct image_arguments *args)
{
    size_t uv_bytesperline = ROUND_UP(ROUND_UP(args->width, 2) / 2, 4);
    dsp_data_plane_t planes[4] = {
        [0] =
            {
                // Y
                .bytesperline = ROUND_UP(args->width, 4),
                .bytesused = ROUND_UP(args->width, 4) * ROUND_UP(args->height, 2),
            },
        [1] =
            {
                // U
                .bytesperline = uv_bytesperline,
                .bytesused = uv_bytesperline * (ROUND_UP(args->height, 2) / 2),
            },
        [2] =
            {
                // V
                .bytesperline = uv_bytesperline,
                .bytesused = uv_bytesperline * (ROUND_UP(args->height, 2) / 2),
            },
        [3] =
            {
                // alpha
                .bytesperline = ROUND_UP(args->width, 4),
                .bytesused = ROUND_UP(args->width, 4) * ROUND_UP(args->height, 2),
            },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

dsp_image_properties_t *alloc_image(dsp_device device, struct image_arguments *args)
{
    switch (args->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
            return alloc_gray8_image(device, args);
            break;
        case DSP_IMAGE_FORMAT_RGB:
            return alloc_rgb_image(device, args);
            break;
        case DSP_IMAGE_FORMAT_NV12:
            return alloc_nv12_image(device, args);
        case DSP_IMAGE_FORMAT_A420:
            return alloc_a420_image(device, args);
        default:
            fprintf(stderr, "Reading %d format is not supported\n", args->format);
            return NULL;
    }
}

dsp_image_properties_t *read_image(dsp_device device, struct image_arguments *args)
{
    dsp_image_properties_t *image = NULL;
    dsp_image_properties_t *ret_image = NULL;

    image = alloc_image(device, args);
    if (image == NULL) {
        goto exit;
    }

    if (generic_read_image(image, args)) {
        goto exit;
    }

    ret_image = image;
    image = NULL;

exit:
    RELEASE_IMAGE(device, image);

    return ret_image;
}

void release_image(dsp_device device, dsp_image_properties_t *image)
{
    for (size_t i = 0; i < image->planes_count; ++i) {
        (void)dsp_release_buffer(device, image->planes[i].userptr);
    }
    free(image->planes);
    free(image);
}

int write_image_to_file(char *path, dsp_image_properties_t *image)
{
    int err = 1;
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open file %s\n", path);
        err = 1;
        goto l_exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        size_t write_size = fwrite(image->planes[i].userptr, image->planes[i].bytesused, 1, file);
        if (write_size != 1) {
            fprintf(stderr, "Writing plane %ld failed\n", i);
            err = 1;
            goto l_exit;
        }
    }

    err = 0;
l_exit:
    CLOSE_FILE(file);

    return err;
}

size_t parse_string_arg(const char *arg,
                        const char **arg_options,
                        size_t arg_options_count,
                        const char *arg_name,
                        struct argp_state *state)
{
    size_t i;
    for (i = 0; i < arg_options_count; ++i) {
        if (0 == strcasecmp(arg, arg_options[i])) {
            break;
        }
    }
    if (i >= arg_options_count) {
        argp_failure(state, argp_err_exit_status, 0, "%s '%s' does not exist\n", arg_name, arg);
        return -1;
    }
    return i;
}

dsp_image_format_t parse_format_arg(const char *arg, const char *arg_name, struct argp_state *state)
{
    return parse_string_arg(arg, format_strings, ARRAY_LENGTH(format_strings), arg_name, state);
}

size_t parse_uint_arg(const char *arg, const char *arg_name, struct argp_state *state)
{
    char *endptr;
    long number = strtol(arg, &endptr, 10);
    if (*endptr != '\0') {
        argp_failure(state, argp_err_exit_status, 0, "Failed convert '%s' to number", arg_name);
    }
    if (number < 0) {
        argp_failure(state, argp_err_exit_status, 0, "Parameter for '%s' should be positive number", arg_name);
    }
    return number;
}
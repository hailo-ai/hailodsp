#include "image_args.h"

#include <cleanup.h>
#include <hailo/hailodsp.h>
#include <utils.h>

#include <argp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum arg_nums {
    SRC_PATH_ARG_NUM,
    DST_PATH_ARG_NUM,
};

enum option_keys {
    WIDTH_KEY = 'w',
    HEIGHT_KEY = 'h',
    SRC_FORMAT_KEY,
    DST_FORMAT_KEY,
};

static struct argp_option options[] = {{"src-format", SRC_FORMAT_KEY, "FORMAT", 0, "One of: rgb / nv12", 0},
                                       {"dst-format", DST_FORMAT_KEY, "FORMAT", 0, "One of: rgb / nv12", 0},
                                       {"width", WIDTH_KEY, "SIZE", 0, "Image width in pixels", 0},
                                       {"height", HEIGHT_KEY, "SIZE", 0, "Image height in pixels", 0},
                                       {0}};

struct arguments {
    struct image_arguments src;
    struct image_arguments dst;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;

    switch (key) {
        case SRC_FORMAT_KEY:
            args->src.format = parse_format_arg(arg, "src-format", state);
            break;

        case DST_FORMAT_KEY:
            args->dst.format = parse_format_arg(arg, "dst-format", state);
            break;

        case WIDTH_KEY:
            args->src.width = args->dst.width = parse_uint_arg(arg, "width", state);
            break;

        case HEIGHT_KEY:
            args->src.height = args->dst.height = parse_uint_arg(arg, "height", state);
            break;

        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case SRC_PATH_ARG_NUM:
                    args->src.path = arg;
                    break;

                case DST_PATH_ARG_NUM:
                    args->dst.path = arg;
                    break;

                default:
                    argp_usage(state);
                    break;
            }
            break;

        case ARGP_KEY_END:
            if (args->src.path == NULL || args->dst.path == NULL) {
                argp_usage(state);
            }

            if (args->src.width == 0 || args->src.height == 0 || args->dst.width == 0 || args->dst.height == 0) {
                argp_error(state, "Width and Height are mandtory");
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static char *args_doc = "input_path output_path";

int main(int argc, char **argv)
{
    int ret;
    dsp_device device = NULL;
    dsp_image_properties_t *src_image = NULL;
    dsp_image_properties_t *dst_image = NULL;

    struct arguments args = {0};

    struct argp argp = {options, parse_opt, args_doc, NULL, NULL, NULL, NULL};
    argp_parse(&argp, argc, argv, 0, 0, &args);

    printf("Opening device\n");
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        fprintf(stderr, "Open device failed %d\n", status);
        ret = 1;
        goto exit;
    }

    printf("Reading source image: %s\n", args.src.path);
    src_image = read_image(device, &args.src);
    if (src_image == NULL) {
        fprintf(stderr, "Failed to read source image\n");
        ret = 1;
        goto exit;
    }
    printf("Image loaded to DDR. Width: %ld, Height: %ld, Format: %s\n", src_image->width, src_image->height,
           format_arg_to_string(src_image->format));

    printf("Allocating destination image: %s\n", args.dst.path);
    dst_image = alloc_image(device, &args.dst);
    if (dst_image == NULL) {
        fprintf(stderr, "Failed to allocate destination image\n");
        ret = 1;
        goto exit;
    }

    printf("Running format conversion from %s to %s\n", format_arg_to_string(args.src.format),
           format_arg_to_string(args.dst.format));
    status = dsp_convert_format(device, src_image, dst_image);
    if (status != DSP_SUCCESS) {
        printf("Command failed with status %d\n", status);
        ret = 1;
        goto exit;
    }
    printf("command finished\n");

    printf("Writing output to file: %s\n", args.dst.path);
    ret = write_image_to_file(args.dst.path, dst_image);
    if (!ret) {
        goto exit;
    }

    ret = 0;

exit:
    RELEASE_IMAGE(device, src_image);
    RELEASE_IMAGE(device, dst_image);
    DSP_RELEASE_DEVICE(device);

    return ret;
}
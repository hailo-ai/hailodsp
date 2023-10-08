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
    START_X_KEY = 1,
    END_X_KEY,
    START_Y_KEY,
    END_Y_KEY,
    WIDTH_KEY = 'w',
    HEIGHT_KEY = 'h',
    FORMAT_KEY = 'f',
    KERNEL_KEY = 'k',
};

static struct argp_option options[] = {{"format", FORMAT_KEY, "FORMAT", 0, "One of: gray8 / nv12", 0},
                                       {"width", WIDTH_KEY, "SIZE", 0, "Image width in pixels", 0},
                                       {"height", HEIGHT_KEY, "SIZE", 0, "Image height in pixels", 0},
                                       {"start-x", START_X_KEY, "POSITION", 0, "Most left pixel to include in blur", 0},
                                       {"end-x", END_X_KEY, "POSITION", 0, "Most right pixel to include in blur", 0},
                                       {"start-y", START_Y_KEY, "POSITION", 0, "Most top pixel to include in blur", 0},
                                       {"end-y", END_Y_KEY, "POSITION", 0, "Most bottom pixel to include in blur", 0},
                                       {"kernel-size", KERNEL_KEY, "POSITION", 0, "Kernel size to use in blur", 0},
                                       {0}};

struct arguments {
    struct image_arguments image;
    char *output_path;
    uint32_t kernel_size;
    size_t start_x;
    size_t end_x;
    size_t start_y;
    size_t end_y;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;

    switch (key) {
        case FORMAT_KEY:
            args->image.format = parse_format_arg(arg, "format", state);
            break;

        case KERNEL_KEY:
            args->kernel_size = parse_uint_arg(arg, "kernel-size", state);
            break;

        case WIDTH_KEY:
            args->image.width = parse_uint_arg(arg, "width", state);
            break;

        case HEIGHT_KEY:
            args->image.height = parse_uint_arg(arg, "height", state);
            break;

        case START_X_KEY:
            args->start_x = parse_uint_arg(arg, "start-x", state);
            break;

        case END_X_KEY:
            args->end_x = parse_uint_arg(arg, "end-x", state);
            break;

        case START_Y_KEY:
            args->start_y = parse_uint_arg(arg, "start-y", state);
            break;

        case END_Y_KEY:
            args->end_y = parse_uint_arg(arg, "end-y", state);
            break;

        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case SRC_PATH_ARG_NUM:
                    args->image.path = arg;
                    break;

                case DST_PATH_ARG_NUM:
                    args->output_path = arg;
                    break;

                default:
                    argp_usage(state);
                    break;
            }
            break;

        case ARGP_KEY_END:
            if (args->image.path == NULL || args->output_path == NULL) {
                argp_usage(state);
            }

            if (args->image.width == 0 || args->image.height == 0) {
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
    dsp_image_properties_t *image = NULL;

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

    printf("Reading source image: %s\n", args.image.path);
    image = read_image(device, &args.image);
    if (image == NULL) {
        fprintf(stderr, "Failed to read source image\n");
        ret = 1;
        goto exit;
    }
    printf("Image loaded to DDR. Width: %ld, Height: %ld, Format: %s\n", image->width, image->height,
           format_arg_to_string(image->format));

    dsp_blur_roi_t roi = {
        .start_x = args.start_x,
        .start_y = args.start_y,
        .end_x = args.end_x,
        .end_y = args.end_y,
    };

    printf("Running blur (%ld,%ld)-(%ld,%ld) (kernel %ux%u)\n", roi.start_x, roi.start_y, roi.end_x, roi.end_y,
           args.kernel_size, args.kernel_size);
    status = dsp_blur(device, image, &roi, 1, args.kernel_size);
    if (status != DSP_SUCCESS) {
        printf("Command failed with status %d\n", status);
        ret = 1;
        goto exit;
    }

    printf("command finished\n");

    printf("Writing output to file: %s\n", args.output_path);
    ret = write_image_to_file(args.output_path, image);
    if (!ret) {
        goto exit;
    }

    ret = 0;

exit:
    RELEASE_IMAGE(device, image);
    DSP_RELEASE_DEVICE(device);

    return ret;
}
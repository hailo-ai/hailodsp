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
    CROP_START_X_KEY = 1,
    CROP_END_X_KEY,
    CROP_START_Y_KEY,
    CROP_END_Y_KEY,
    INTERPOLATION_KEY,
    SRC_WIDTH_KEY,
    SRC_HEIGHT_KEY,
    DST_WIDTH_KEY,
    DST_HEIGHT_KEY,
    FORMAT_KEY = 'f',
};

static const char *interpolations_strings[INTERPOLATION_TYPE_COUNT] = {
    [INTERPOLATION_TYPE_NEAREST_NEIGHBOR] = "nearest",
    [INTERPOLATION_TYPE_BILINEAR] = "bilinear",
    [INTERPOLATION_TYPE_AREA] = "area",
    [INTERPOLATION_TYPE_BICUBIC] = "bicubic"};

static struct argp_option options[] = {
    {"format", FORMAT_KEY, "FORMAT", 0, "One of: gray8 / rgb / nv12", 0},
    {"crop-start-x", CROP_START_X_KEY, "POSITION", 0, "Most left pixel to include in crop", 0},
    {"crop-end-x", CROP_END_X_KEY, "POSITION", 0, "Most right pixel to include in crop", 0},
    {"crop-start-y", CROP_START_Y_KEY, "POSITION", 0, "Most top pixel to include in crop", 0},
    {"crop-end-y", CROP_END_Y_KEY, "POSITION", 0, "Most bottom pixel to include in crop", 0},
    {"src-width", SRC_WIDTH_KEY, "SIZE", 0, "Destination resize width in pixels", 0},
    {"src-height", SRC_HEIGHT_KEY, "SIZE", 0, "Destination resize height in pixels", 0},
    {"dst-width", DST_WIDTH_KEY, "SIZE", 0, "Destination resize width in pixels", 0},
    {"dst-height", DST_HEIGHT_KEY, "SIZE", 0, "Destination resize height in pixels", 0},
    {"interpolation", INTERPOLATION_KEY, "TYPE", 0, "One of: nearest / bilinear / area / bicubic", 0},
    {0}};

struct arguments {
    struct image_arguments src;
    struct image_arguments dst;
    bool test_crop;
    size_t crop_start_x;
    size_t crop_end_x;
    size_t crop_start_y;
    size_t crop_end_y;
    dsp_interpolation_type_t interpolation;
};

static dsp_interpolation_type_t parse_interpolation_arg(const char *arg, struct argp_state *state)
{
    return parse_string_arg(arg, interpolations_strings, ARRAY_LENGTH(interpolations_strings), "Interpolation", state);
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;

    switch (key) {
        case FORMAT_KEY:
            args->src.format = args->dst.format = parse_format_arg(arg, "format", state);
            break;

        case CROP_START_X_KEY:
            args->crop_start_x = parse_uint_arg(arg, "crop-start-x", state);
            args->test_crop = true;
            break;

        case CROP_END_X_KEY:
            args->crop_end_x = parse_uint_arg(arg, "crop-end-x", state);
            args->test_crop = true;
            break;

        case CROP_START_Y_KEY:
            args->crop_start_y = parse_uint_arg(arg, "crop-start-y", state);
            args->test_crop = true;
            break;

        case CROP_END_Y_KEY:
            args->crop_end_y = parse_uint_arg(arg, "crop-end-y", state);
            args->test_crop = true;
            break;

        case SRC_WIDTH_KEY:
            args->src.width = parse_uint_arg(arg, "src-width", state);
            break;

        case SRC_HEIGHT_KEY:
            args->src.height = parse_uint_arg(arg, "src-height", state);
            break;

        case DST_WIDTH_KEY:
            args->dst.width = parse_uint_arg(arg, "dst-width", state);
            break;

        case DST_HEIGHT_KEY:
            args->dst.height = parse_uint_arg(arg, "dst-height", state);
            break;

        case INTERPOLATION_KEY: {
            args->interpolation = parse_interpolation_arg(arg, state);
            break;
        }

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

    // use bilinear as default interpolation
    args.interpolation = INTERPOLATION_TYPE_BILINEAR;

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

    dsp_resize_params_t resize_params = {
        .interpolation = args.interpolation,
        .src = src_image,
        .dst = dst_image,
    };

    if (args.test_crop) {
        printf(
            "Running crop & resize %s to destination width: %ld, destination height: %ld, crop: (%ld,%ld)-(%ld-%ld)\n",
            interpolations_strings[args.interpolation], args.dst.width, args.dst.height, args.crop_start_x,
            args.crop_start_y, args.crop_end_x, args.crop_end_y);

        dsp_crop_api_t crop_params = {
            .start_x = args.crop_start_x,
            .start_y = args.crop_start_y,
            .end_x = args.crop_end_x,
            .end_y = args.crop_end_y,
        };
        status = dsp_crop_and_resize(device, &resize_params, &crop_params);
    } else {
        printf("Running resize %s to destination width: %ld, destination height: %ld\n",
               interpolations_strings[args.interpolation], args.dst.width, args.dst.height);
        status = dsp_resize(device, &resize_params);
    }

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
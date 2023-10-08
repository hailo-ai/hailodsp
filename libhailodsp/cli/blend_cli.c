#include "image_args.h"

#include <cleanup.h>
#include <hailo/hailodsp.h>
#include <utils.h>

#include <argp.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum arg_nums {
    BACKGROUND_PATH_ARG_NUM,
    OVERLAY_PATH_ARG_NUM,
    OUTPUT_PATH_ARG_NUM,
};

enum option_keys {
    BACKGROUND_WIDTH_KEY = 1,
    BACKGROUND_HEIGHT_KEY,
    BACKGROUND_FORMAT_KEY,
    OVERLAY_WIDTH_KEY,
    OVERLAY_HEIGHT_KEY,
    OVERLAY_FORMAT_KEY,
    OFFSET_X_KEY,
    OFFSET_Y_KEY
};

static struct argp_option options[] = {
    {"background-width", BACKGROUND_WIDTH_KEY, "SIZE", 0, "Background image width in pixels", 0},
    {"background-height", BACKGROUND_HEIGHT_KEY, "SIZE", 0, "Background image height in pixels", 0},
    {"background-format", BACKGROUND_FORMAT_KEY, "FORMAT", 0, "Background image format", 0},
    {"overlay-width", OVERLAY_WIDTH_KEY, "SIZE", 0, "Overlay image width in pixels", 0},
    {"overlay-height", OVERLAY_HEIGHT_KEY, "SIZE", 0, "Overlay image height in pixels", 0},
    {"overlay-format", OVERLAY_FORMAT_KEY, "FORMAT", 0, "Overlay image format", 0},
    {"offset-x", OFFSET_X_KEY, "OFFSET", 0, "Offset on X axis in pixels", 0},
    {"offset-y", OFFSET_Y_KEY, "OFFSET", 0, "Offset on Y axis in pixels", 0},
    {0},
};

struct arguments {
    struct image_arguments background;
    struct image_arguments overlay;
    char *output_path;
    size_t x_offset;
    size_t y_offset;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;

    switch (key) {
        case ARGP_KEY_INIT:
            args->background.format = DSP_IMAGE_FORMAT_COUNT;
            args->overlay.format = DSP_IMAGE_FORMAT_COUNT;
            break;
        case BACKGROUND_WIDTH_KEY:
            args->background.width = parse_uint_arg(arg, "background-width", state);
            break;
        case BACKGROUND_HEIGHT_KEY:
            args->background.height = parse_uint_arg(arg, "background-height", state);
            break;
        case BACKGROUND_FORMAT_KEY:
            args->background.format = parse_format_arg(arg, "background-format", state);
            break;
        case OVERLAY_WIDTH_KEY:
            args->overlay.width = parse_uint_arg(arg, "overlay-width", state);
            break;
        case OVERLAY_HEIGHT_KEY:
            args->overlay.height = parse_uint_arg(arg, "overlay-height", state);
            break;
        case OVERLAY_FORMAT_KEY:
            args->overlay.format = parse_format_arg(arg, "overlay-format", state);
            break;
        case OFFSET_X_KEY:
            args->x_offset = parse_uint_arg(arg, "offset-x", state);
            break;
        case OFFSET_Y_KEY:
            args->y_offset = parse_uint_arg(arg, "offset-y", state);
            break;
        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case BACKGROUND_PATH_ARG_NUM:
                    args->background.path = arg;
                    break;
                case OVERLAY_PATH_ARG_NUM:
                    args->overlay.path = arg;
                    break;
                case OUTPUT_PATH_ARG_NUM:
                    args->output_path = arg;
                    break;
                default:
                    argp_usage(state);
                    break;
            }
            break;
        case ARGP_KEY_END:
            if ((args->background.path == NULL) || (args->overlay.path == NULL) || (args->output_path == NULL)) {
                argp_usage(state);
            }

            if ((args->background.width == 0) || (args->background.height == 0) ||
                (args->background.format == DSP_IMAGE_FORMAT_COUNT)) {
                argp_error(state, "All background information is mandatory");
            }

            if ((args->overlay.width == 0) || (args->overlay.height == 0) ||
                (args->overlay.format == DSP_IMAGE_FORMAT_COUNT)) {
                argp_error(state, "All overlay information is mandatory");
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static char *args_doc = "background_path overlay_path output_path";

int main(int argc, char **argv)
{
    int err = 1;
    dsp_device device = NULL;
    dsp_image_properties_t *background_image = NULL;
    dsp_image_properties_t *overlay_image = NULL;

    struct arguments args = {0};

    struct argp argp = {options, parse_opt, args_doc, NULL, NULL, NULL, NULL};
    argp_parse(&argp, argc, argv, 0, 0, &args);

    printf("Opening device\n");
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        fprintf(stderr, "Open device failed %d\n", status);
        err = 1;
        goto l_err;
    }

    printf("Reading background image: %s\n", args.background.path);
    background_image = read_image(device, &args.background);
    if (background_image == NULL) {
        fprintf(stderr, "Failed to read background image\n");
        err = 1;
        goto l_err;
    }
    printf("Image loaded to DDR. Width: %ld, Height: %ld, Format: %s\n", background_image->width,
           background_image->height, format_arg_to_string(background_image->format));

    printf("Reading overlay image: %s\n", args.overlay.path);
    overlay_image = read_image(device, &args.overlay);
    if (overlay_image == NULL) {
        fprintf(stderr, "Failed to read overlay image\n");
        err = 1;
        goto l_err;
    }
    printf("Image loaded to DDR. Width: %ld, Height: %ld, Format: %s\n", overlay_image->width, overlay_image->height,
           format_arg_to_string(overlay_image->format));

    dsp_overlay_properties_t overlay_properties = {
        .overlay = *overlay_image,
        .x_offset = args.x_offset,
        .y_offset = args.y_offset,
    };

    printf("Running blend\n");
    status = dsp_blend(device, background_image, &overlay_properties, 1);

    if (status != DSP_SUCCESS) {
        printf("Command failed with status %d\n", status);
        err = 1;
        goto l_err;
    }

    printf("command finished\n");

    printf("Writing output to file: %s\n", args.output_path);
    err = write_image_to_file(args.output_path, background_image);
    if (!err) {
        goto l_err;
    }

    err = 0;
l_err:
    if (background_image != NULL) {
        release_image(device, background_image);
    }
    if (overlay_image != NULL) {
        release_image(device, overlay_image);
    }
    if (device != NULL) {
        dsp_release_device(device);
    }
    return err;
}
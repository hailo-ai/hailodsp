#include "bgr_yuv.h"

#include <hailo/hailodsp.h>
#include <utils.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Input and Output file width and height.
 * To keep it simple - they will be the same
 * so we can use the same BMP header.
 */
#define IN_FILE_WIDTH 4096
#define IN_FILE_HEIGHT 2730
#define IN_FILE_BYTES_PER_LINE 4096
#define OUT_FILE_WIDTH 4096
#define OUT_FILE_HEIGHT 2730
#define OUT_FILE_BYTES_PER_LINE 4096

/** Crop input settings */
#define CROP_UP_LEFT_X 1000
#define CROP_UP_LEFT_Y 1000
#define CROP_WIDTH 2048
#define CROP_HEIGHT 1364

/**
 * Crop output settings.
 * Here we use one BPLN for all files
 * But it can be different for each output
 */
#define BYTES_PER_LINE_ALL_FILES 4096
typedef struct {
    size_t width;
    size_t height;
} resize_dimenstions_t;

static resize_dimenstions_t CROP_OUT_DIMENSTIONS[DSP_MULTI_RESIZE_OUTPUTS_COUNT] = {
    {
        .width = 4096,
        .height = 2730,
    },
    {
        .width = 2048,
        .height = 1364,
    },
    {
        .width = 1024,
        .height = 682,
    },
    {
        .width = 682,
        .height = 454,
    },
    {
        .width = 512,
        .height = 340,
    },
};

/** BMP file size and header info. */
#define SIZE (4096 * 2731 * 3)
#define HEADER 54

/** Here we can change the color interpolation. */
#define COLOR_INTERPOLATION INTERPOLATION_TYPE_BICUBIC

/** For conviniance all test arrays are static. */
unsigned char input[SIZE + HEADER];
unsigned char output[SIZE + HEADER];

int main()
{
    dsp_device device = NULL;

    FILE *ptr;
    printf("Openning input file\n");
    ptr = fopen("unwarped.bmp", "rb");
    if (!ptr) {
        printf("Input file not found\n");
        return 1;
    }
    printf("Reading input file\n");
    unsigned long res = fread(input, sizeof(input), 1, ptr);
    if (!res) {
        printf("Error while reading input file!\n");
        return 1;
    }
    fclose(ptr);
    int i;

    printf("Opening device\n");
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        fprintf(stderr, "Open device failed %d\n", status);
        return 1;
    }

    void *input_y, *input_uv;
    void *output_y[DSP_MULTI_RESIZE_OUTPUTS_COUNT], *output_uv[DSP_MULTI_RESIZE_OUTPUTS_COUNT];
    dsp_create_buffer(device, SIZE / 3, &input_y);
    memset(input_y, 0, SIZE / 3);
    dsp_create_buffer(device, SIZE / 6, &input_uv);
    memset(input_uv, 0, SIZE / 6);
    for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
        dsp_create_buffer(device, SIZE / 3, &output_y[i]);
        memset(output_y[i], 0, SIZE / 3);
        dsp_create_buffer(device, SIZE / 6, &output_uv[i]);
        memset(output_uv[i], 0, SIZE / 6);
    }

    dsp_image_properties_t src = {
        .width = IN_FILE_WIDTH,
        .height = IN_FILE_HEIGHT,
        .planes =
            (dsp_data_plane_t[]){
                {
                    .userptr = input_y,
                    .bytesperline = IN_FILE_BYTES_PER_LINE,
                    .bytesused = SIZE / 3,
                },
                {
                    .userptr = input_uv,
                    .bytesperline = IN_FILE_BYTES_PER_LINE,
                    .bytesused = SIZE / 6,
                },
            },
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
    };

    dsp_multi_resize_params_t resize_params = {.src = &src, .interpolation = COLOR_INTERPOLATION};

    for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
        dsp_image_properties_t *img = malloc(sizeof(dsp_image_properties_t));
        dsp_data_plane_t *planes = malloc(sizeof(dsp_data_plane_t) * 2);

        planes[0] = (dsp_data_plane_t){
            .userptr = output_y[i],
            .bytesperline = OUT_FILE_BYTES_PER_LINE,
            .bytesused = SIZE / 3,
        };
        planes[1] = (dsp_data_plane_t){
            .userptr = output_uv[i],
            .bytesperline = OUT_FILE_BYTES_PER_LINE,
            .bytesused = SIZE / 6,
        };

        *img = (dsp_image_properties_t){
            .width = CROP_OUT_DIMENSTIONS[i].width,
            .height = CROP_OUT_DIMENSTIONS[i].height,
            .planes = planes,
            .planes_count = 2,
            .format = DSP_IMAGE_FORMAT_NV12,
        };

        resize_params.dst[i] = img;
    }

    dsp_crop_api_t crop_params = {
        .start_x = CROP_UP_LEFT_X,
        .start_y = CROP_UP_LEFT_Y,
        .end_x = CROP_UP_LEFT_X + CROP_WIDTH,
        .end_y = CROP_UP_LEFT_Y + CROP_HEIGHT,
    };

    /** All of the BMP files will have the same size. */
    for (i = 0; i < HEADER; i++)
        output[i] = input[i];

    printf("Converting Input to YUV\n");
    /** BMP to YUV. */
    bgr_to_yuv(input + HEADER, input_y, input_uv, IN_FILE_WIDTH, IN_FILE_HEIGHT);

    printf("Running multi resize on DSP\n");
    dsp_multi_crop_and_resize(device, &resize_params, &crop_params);

    /** YUV to BMP for FIXED. */
    for (int i = 0; i < DSP_MULTI_RESIZE_OUTPUTS_COUNT; ++i) {
        memset(output + HEADER, 0, SIZE);
        printf("Converting output [%d] to BMP\n", i);
        yuv_to_bgr(output_y[i], output_uv[i], output + HEADER, OUT_FILE_WIDTH, OUT_FILE_HEIGHT);

        printf("Writing result to file\n");

        char filename[256];
        snprintf(filename, ARRAY_LENGTH(filename), "unwarped_yuv_crop%d.bmp", i);
        ptr = fopen(filename, "wb");
        res = fwrite(output, sizeof(output), 1, ptr);
        if (!res) {
            printf("Error while writing output file!\n");
            return 1;
        }
        fclose(ptr);
    }

    return 0;
}

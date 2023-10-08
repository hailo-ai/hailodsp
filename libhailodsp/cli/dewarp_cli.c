#include "bgr_yuv.h"

#include <hailo/hailodsp.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MESH_SQ_SIZE (64)

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

/**
 * Dewarp settings - we have a very simple dewarp
 * coordinates generator with option to rotate and
 * mirror the output (this is done only with the coordinates,
 * the process funcion is only one).
 */
#define DEWARP_SPHERE_RADIUS 1370.0
#define DEWARP_SPHERE_CENTER_X 2021.0
#define DEWARP_SPHERE_CENTER_Y 1305.0
#define DEWARP_SPHERE_ZOOM 0.0
#define DEWARP_SPHERE_ROTATION_RAD 0.0
#define DEWARP_SPHERE_MIRROR_X 0
#define DEWARP_SPHERE_MIRROR_Y 0

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
#define CROP_OUT_WIDTH_0 4096
#define CROP_OUT_HEIGHT_0 2730
#define CROP_OUT_WIDTH_1 2048
#define CROP_OUT_HEIGHT_1 1364
#define CROP_OUT_WIDTH_2 1024
#define CROP_OUT_HEIGHT_2 682
#define CROP_OUT_WIDTH_3 682
#define CROP_OUT_HEIGHT_3 454
#define CROP_OUT_WIDTH_4 512
#define CROP_OUT_HEIGHT_4 340

/** BMP file size and header info. */
#define SIZE (4096 * 2731 * 3)
#define HEADER 54

/** Here we can change the color interpolation. */
#define COLOR_INTERPOLATION INTERPOLATION_TYPE_BICUBIC

/**
 * A very basic Dewarp initialization (only for the test).
 * Here we can see that all Dewarp rotation and mirror can be
 * done only with the MESH coordinates, without changing the
 * process functions. That is why we don't need extra code
 * for any of rotation or mirror.
 * Actually the rotation angle can be not only 0, 90, 180, 270,
 * but any number in radians.
 */
dsp_dewarp_mesh_t *new_dewarp_for_test(dsp_device device,
                                       int mesh_sq_size,
                                       int out_width,
                                       int out_height,
                                       float radius,
                                       float center_x,
                                       float center_y,
                                       float center_zoom,
                                       float rot_angle,
                                       int mirror_x,
                                       int mirror_y)
{
    dsp_dewarp_mesh_t *dewarp = malloc(sizeof(dsp_dewarp_mesh_t));
    dewarp->mesh_width = (out_width / mesh_sq_size) + 2;
    dewarp->mesh_height = (out_height / mesh_sq_size) + 2;
    dewarp->mesh_sq_size = mesh_sq_size;
    dsp_create_buffer(device, dewarp->mesh_width * dewarp->mesh_width * 2 * sizeof(int), (void **)&dewarp->mesh_table);
    int x, y;
    float upper_left_x = center_x - ((((float)dewarp->mesh_width - 1.0) / 2.0) * (float)mesh_sq_size);
    float upper_left_y = center_y - ((((float)dewarp->mesh_height - 1.0) / 2.0) * (float)mesh_sq_size);
    for (y = 0; y < dewarp->mesh_height; y++) {
        for (x = 0; x < dewarp->mesh_width; x++) {
            float output_x = upper_left_x + (float)x * (float)mesh_sq_size;
            float output_y = upper_left_y + (float)y * (float)mesh_sq_size;
            float ratio = radius / sqrt((center_x - output_x) * (center_x - output_x) +
                                        (center_y - output_y) * (center_y - output_y) +
                                        (center_zoom + radius) * (center_zoom + radius));
            float x_rot = (output_x - center_x) * ratio;
            float y_rot = (output_y - center_y) * ratio;
            if (mirror_x) {
                dewarp->mesh_table[(y * dewarp->mesh_width * 2) + (2 * x) + 0] =
                    (int)(65536.0 * (center_x - (x_rot * cos(rot_angle) - y_rot * sin(rot_angle))));
            } else {
                dewarp->mesh_table[(y * dewarp->mesh_width * 2) + (2 * x) + 0] =
                    (int)(65536.0 * (center_x + (x_rot * cos(rot_angle) - y_rot * sin(rot_angle))));
            }
            if (mirror_y) {
                dewarp->mesh_table[(y * dewarp->mesh_width * 2) + (2 * x) + 1] =
                    (int)(65536.0 * (center_y - (y_rot * cos(rot_angle) + x_rot * sin(rot_angle))));
            } else {
                dewarp->mesh_table[(y * dewarp->mesh_width * 2) + (2 * x) + 1] =
                    (int)(65536.0 * (center_y + (y_rot * cos(rot_angle) + x_rot * sin(rot_angle))));
            }
        }
    }
    return dewarp;
}

/** Dewarp del (only for the test). */
void del_dewarp(dsp_dewarp_mesh_t *dewarp)
{
    free(dewarp);
}

/** For conviniance all test arrays are static. */
unsigned char input[SIZE + HEADER];
unsigned char output[SIZE + HEADER];

void *data_mem0;
void *data_mem1;

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
    int res = fread(input, sizeof(input), 1, ptr);
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

    printf("Creating mesh\n");
    /** Dewarp init. */
    dsp_dewarp_mesh_t *dewarp =
        new_dewarp_for_test(device, MAX_MESH_SQ_SIZE, OUT_FILE_WIDTH, OUT_FILE_HEIGHT, DEWARP_SPHERE_RADIUS,
                            DEWARP_SPHERE_CENTER_X, DEWARP_SPHERE_CENTER_Y, DEWARP_SPHERE_ZOOM,
                            DEWARP_SPHERE_ROTATION_RAD, DEWARP_SPHERE_MIRROR_X, DEWARP_SPHERE_MIRROR_Y);

    void *input_y, *input_uv;
    void *output_y, *output_uv;
    dsp_create_buffer(device, SIZE / 3, &input_y);
    dsp_create_buffer(device, SIZE / 6, &input_uv);
    dsp_create_buffer(device, SIZE / 3, &output_y);
    dsp_create_buffer(device, SIZE / 6, &output_uv);

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

    dsp_image_properties_t dst = {
        .width = OUT_FILE_WIDTH,
        .height = OUT_FILE_HEIGHT,
        .planes =
            (dsp_data_plane_t[]){
                {
                    .userptr = output_y,
                    .bytesperline = OUT_FILE_BYTES_PER_LINE,
                    .bytesused = SIZE / 3,
                },
                {
                    .userptr = output_uv,
                    .bytesperline = OUT_FILE_BYTES_PER_LINE,
                    .bytesused = SIZE / 6,
                },
            },
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
    };

    /** All of the BMP files will have the same size. */
    for (i = 0; i < HEADER; i++)
        output[i] = input[i];

    printf("Converting to YUV\n");
    /** BMP to YUV. */
    bgr_to_yuv(input + HEADER, input_y, input_uv, IN_FILE_WIDTH, IN_FILE_HEIGHT);

    // /** A creation of Dewarp "BufT" inputs and outputs for FIXED. */
    // BufT input_yuv = {input_y, input_uv, IN_FILE_WIDTH, IN_FILE_HEIGHT, BYTES_PER_LINE_ALL_FILES};
    // BufT output_yuv = {output_y, output_uv, OUT_FILE_WIDTH, OUT_FILE_HEIGHT, BYTES_PER_LINE_ALL_FILES};

    // /** Dewarp process for FIXED. */
    printf("Running dewarp on DSP\n");
    dsp_dewarp(device, &src, &dst, dewarp, COLOR_INTERPOLATION);

    /** YUV to BMP for FIXED. */
    printf("Converting to BMP\n");
    yuv_to_bgr(output_y, output_uv, output + HEADER, OUT_FILE_WIDTH, OUT_FILE_HEIGHT);

    /** Output dewarped FIXED file is saved. */
    printf("Writing result to file\n");
    ptr = fopen("unwarped_yuv.bmp", "wb");
    res = fwrite(output, sizeof(output), 1, ptr);
    if (!res) {
        printf("Error while writing output file!\n");
        return 1;
    }
    fclose(ptr);

    /** Dewarp free. */
    del_dewarp(dewarp);

    return 0;
}

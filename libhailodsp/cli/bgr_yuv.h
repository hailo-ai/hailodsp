#pragma once

void bgr_to_yuv(unsigned char *input, unsigned char *output_y, unsigned char *output_uv, int width, int height);

void yuv_to_bgr(unsigned char *input_y, unsigned char *input_uv, unsigned char *output, int width, int height);

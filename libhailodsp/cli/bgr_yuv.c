/** A dummy BMP RGB to YUV420fsp. */
void bgr_to_yuv(unsigned char *input, unsigned char *output_y, unsigned char *output_uv, int width, int height)
{
    int x, y;
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            float r00 = (float)input[((2 * y + 0) * width + (2 * x + 0)) * 3 + 2];
            float g00 = (float)input[((2 * y + 0) * width + (2 * x + 0)) * 3 + 1];
            float b00 = (float)input[((2 * y + 0) * width + (2 * x + 0)) * 3 + 0];
            float r01 = (float)input[((2 * y + 0) * width + (2 * x + 1)) * 3 + 2];
            float g01 = (float)input[((2 * y + 0) * width + (2 * x + 1)) * 3 + 1];
            float b01 = (float)input[((2 * y + 0) * width + (2 * x + 1)) * 3 + 0];
            float r10 = (float)input[((2 * y + 1) * width + (2 * x + 0)) * 3 + 2];
            float g10 = (float)input[((2 * y + 1) * width + (2 * x + 0)) * 3 + 1];
            float b10 = (float)input[((2 * y + 1) * width + (2 * x + 0)) * 3 + 0];
            float r11 = (float)input[((2 * y + 1) * width + (2 * x + 1)) * 3 + 2];
            float g11 = (float)input[((2 * y + 1) * width + (2 * x + 1)) * 3 + 1];
            float b11 = (float)input[((2 * y + 1) * width + (2 * x + 1)) * 3 + 0];
            float y00 = 0.114f * b00 + 0.587f * g00 + 0.299f * r00;
            float y01 = 0.114f * b01 + 0.587f * g01 + 0.299f * r01;
            float y10 = 0.114f * b10 + 0.587f * g10 + 0.299f * r10;
            float y11 = 0.114f * b11 + 0.587f * g11 + 0.299f * r11;
            float u00 = 0.492f * (b00 - y00);
            float u01 = 0.492f * (b01 - y01);
            float u10 = 0.492f * (b10 - y10);
            float u11 = 0.492f * (b11 - y11);
            float v00 = 0.877f * (r00 - y00);
            float v01 = 0.877f * (r01 - y01);
            float v10 = 0.877f * (r10 - y10);
            float v11 = 0.877f * (r11 - y11);
            float u_avg = (u00 + u01 + u10 + u11) * 0.25f + 128.0f;
            float v_avg = (v00 + v01 + v10 + v11) * 0.25f + 128.0f;
            if (y00 > 255.0f)
                y00 = 255.0f;
            if (y01 > 255.0f)
                y01 = 255.0f;
            if (y10 > 255.0f)
                y10 = 255.0f;
            if (y11 > 255.0f)
                y11 = 255.0f;
            if (y00 < 0.0f)
                y00 = 0.0f;
            if (y01 < 0.0f)
                y01 = 0.0f;
            if (y10 < 0.0f)
                y10 = 0.0f;
            if (y11 < 0.0f)
                y11 = 0.0f;
            if (u_avg > 255.0f)
                u_avg = 255.0f;
            if (u_avg < 0.0f)
                u_avg = 0.0f;
            if (v_avg > 255.0f)
                v_avg = 255.0f;
            if (v_avg < 0.0f)
                v_avg = 0.0f;
            int half_height = (height / 2);
            output_y[(2 * (half_height - y - 1) + 1) * width + (2 * x + 0)] = (unsigned char)y00;
            output_y[(2 * (half_height - y - 1) + 1) * width + (2 * x + 1)] = (unsigned char)y01;
            output_y[(2 * (half_height - y - 1) + 0) * width + (2 * x + 0)] = (unsigned char)y10;
            output_y[(2 * (half_height - y - 1) + 0) * width + (2 * x + 1)] = (unsigned char)y11;
            output_uv[(half_height - y - 1) * width + (2 * x) + 0] = (unsigned char)u_avg;
            output_uv[(half_height - y - 1) * width + (2 * x) + 1] = (unsigned char)v_avg;
        }
    }
}

/** A dummy YUV420fsp to BMP RGB. */
void yuv_to_bgr(unsigned char *input_y, unsigned char *input_uv, unsigned char *output, int width, int height)
{
    int x, y;
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            int half_height = (height / 2);
            float y00 = (float)input_y[(2 * (half_height - y - 1) + 1) * width + (2 * x + 0)];
            float y01 = (float)input_y[(2 * (half_height - y - 1) + 1) * width + (2 * x + 1)];
            float y10 = (float)input_y[(2 * (half_height - y - 1) + 0) * width + (2 * x + 0)];
            float y11 = (float)input_y[(2 * (half_height - y - 1) + 0) * width + (2 * x + 1)];
            float u00 = (float)input_uv[(half_height - y - 1) * width + (2 * x) + 0] - 128.0f;
            float u01 = u00;
            float u10 = u00;
            float u11 = u00;
            float v00 = (float)input_uv[(half_height - y - 1) * width + (2 * x) + 1] - 128.0f;
            float v01 = v00;
            float v10 = v00;
            float v11 = v00;
            float r00 = y00 + 1.140f * v00;
            float r01 = y01 + 1.140f * v01;
            float r10 = y10 + 1.140f * v10;
            float r11 = y11 + 1.140f * v11;
            float g00 = y00 - 0.395f * u00 - 0.581f * v00;
            float g01 = y01 - 0.395f * u01 - 0.581f * v01;
            float g10 = y10 - 0.395f * u10 - 0.581f * v10;
            float g11 = y11 - 0.395f * u11 - 0.581f * v11;
            float b00 = y00 + 2.032f * u00;
            float b01 = y01 + 2.032f * u01;
            float b10 = y10 + 2.032f * u10;
            float b11 = y11 + 2.032f * u11;

            if (r00 > 255.0f)
                r00 = 255.0f;
            if (r01 > 255.0f)
                r01 = 255.0f;
            if (r10 > 255.0f)
                r10 = 255.0f;
            if (r11 > 255.0f)
                r11 = 255.0f;
            if (g00 > 255.0f)
                g00 = 255.0f;
            if (g01 > 255.0f)
                g01 = 255.0f;
            if (g10 > 255.0f)
                g10 = 255.0f;
            if (g11 > 255.0f)
                g11 = 255.0f;
            if (b00 > 255.0f)
                b00 = 255.0f;
            if (b01 > 255.0f)
                b01 = 255.0f;
            if (b10 > 255.0f)
                b10 = 255.0f;
            if (b11 > 255.0f)
                b11 = 255.0f;
            if (r00 < 0.0f)
                r00 = 0.0f;
            if (r01 < 0.0f)
                r01 = 0.0f;
            if (r10 < 0.0f)
                r10 = 0.0f;
            if (r11 < 0.0f)
                r11 = 0.0f;
            if (g00 < 0.0f)
                g00 = 0.0f;
            if (g01 < 0.0f)
                g01 = 0.0f;
            if (g10 < 0.0f)
                g10 = 0.0f;
            if (g11 < 0.0f)
                g11 = 0.0f;
            if (b00 < 0.0f)
                b00 = 0.0f;
            if (b01 < 0.0f)
                b01 = 0.0f;
            if (b10 < 0.0f)
                b10 = 0.0f;
            if (b11 < 0.0f)
                b11 = 0.0f;

            output[((2 * y + 0) * width + (2 * x + 0)) * 3 + 2] = (unsigned char)r00;
            output[((2 * y + 0) * width + (2 * x + 0)) * 3 + 1] = (unsigned char)g00;
            output[((2 * y + 0) * width + (2 * x + 0)) * 3 + 0] = (unsigned char)b00;
            output[((2 * y + 0) * width + (2 * x + 1)) * 3 + 2] = (unsigned char)r01;
            output[((2 * y + 0) * width + (2 * x + 1)) * 3 + 1] = (unsigned char)g01;
            output[((2 * y + 0) * width + (2 * x + 1)) * 3 + 0] = (unsigned char)b01;
            output[((2 * y + 1) * width + (2 * x + 0)) * 3 + 2] = (unsigned char)r10;
            output[((2 * y + 1) * width + (2 * x + 0)) * 3 + 1] = (unsigned char)g10;
            output[((2 * y + 1) * width + (2 * x + 0)) * 3 + 0] = (unsigned char)b10;
            output[((2 * y + 1) * width + (2 * x + 1)) * 3 + 2] = (unsigned char)r11;
            output[((2 * y + 1) * width + (2 * x + 1)) * 3 + 1] = (unsigned char)g11;
            output[((2 * y + 1) * width + (2 * x + 1)) * 3 + 0] = (unsigned char)b11;
        }
    }
}
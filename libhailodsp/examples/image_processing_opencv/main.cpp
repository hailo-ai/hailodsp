/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 *
 * Example: Image Processing with OpenCV Mat and Hailo DSP API
 *
 * This example demonstrates how to use the Hailo DSP API to perform image processing
 * operations (format conversion and rotation) using OpenCV Mat objects for buffer management.
 * The code is intended as a reference for prototyping and development, showing how to integrate
 * Hailo DSP acceleration into applications using user-space buffers (OpenCV Mat).
 *
 * Workflow:
 *   1. Parse command-line arguments for input/output image paths.
 *   2. Read an input image using OpenCV (user-space buffer).
 *   3. Allocate OpenCV Mat objects for all intermediate and output images.
 *   4. Create a DSP device handle.
 *   5. Convert the input image from BGR to NV12 format using the DSP.
 *   6. Rotate the NV12 image by 45 degrees using the DSP.
 *   7. Convert the rotated NV12 image back to BGR using the DSP.
 *   8. Save the final output image as a JPEG file using OpenCV.
 *   9. Release all resources.
 *
 * Usage:
 *   ./image_processing_opencv -i <input_image> -o <output_image_base>
 */

#include <CLI/CLI.hpp>
#include <hailo/hailodsp.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>

/**
 * @brief Command-line arguments structure.
 */
struct CLIArgs {
    std::string input_image_path;  ///< Path to input image file
    std::string output_image_path; ///< Base path for output image file (without extension)
};

/**
 * @brief Structure holding all OpenCV Mat buffers used in the pipeline.
 *
 *        User-space buffers with automatic cache management - simpler to use
 *        but may have performance overhead compared to DMA-BUF.
 *
 *        For memory type comparison and trade-offs, see:
 *        libhailodsp/doc/sphinx/source/libhailodsp/image_properties.rst
 */
struct Buffers {
    cv::Mat input_buffer;              ///< Input image buffer (BGR, user-space)
    cv::Mat after_nv12_convert_plane0; ///< Y plane after BGR->NV12 conversion (user-space)
    cv::Mat after_nv12_convert_plane1; ///< UV plane after BGR->NV12 conversion (user-space)
    cv::Mat after_rotate_plane0;       ///< Y plane after rotation (user-space)
    cv::Mat after_rotate_plane1;       ///< UV plane after rotation (user-space)
    cv::Mat rgb_converted_image;       ///< Final BGR image after NV12->BGR conversion (user-space)
};

/**
 * @brief Read an input image from disk using OpenCV.
 * @param input_image_path Path to the input image file
 * @return cv::Mat containing the image data, or empty on failure
 */
static cv::Mat read_input_image(const std::string &input_image_path)
{
    cv::Mat input_image = cv::imread(input_image_path, cv::IMREAD_COLOR);
    if (input_image.empty()) {
        std::cerr << "Failed to read image: " << input_image_path << std::endl;
        return cv::Mat();
    }
    if (input_image.type() != CV_8UC3) {
        std::cerr << "Input image must be 8-bit 3-channel (BGR) format." << std::endl;
        return cv::Mat();
    }
    return input_image;
}

/**
 * @brief Allocate all OpenCV Mat buffers required for the pipeline.
 *        These are user-space buffers managed by OpenCV, not DMA-BUF.
 * @param width Image width
 * @param height Image height
 * @param buffers Output structure to fill
 * @return true on success, false on failure
 */
static bool allocate_opencv_buffers(size_t width, size_t height, Buffers &buffers)
{
    // Allocate Y plane for NV12 (single channel)
    buffers.after_nv12_convert_plane0 = cv::Mat(height, width, CV_8UC1);
    if (buffers.after_nv12_convert_plane0.empty()) {
        std::cerr << "Failed to allocate output buffer for plane 0 (Y)." << std::endl;
        return false;
    }
    // Allocate UV plane for NV12 (two channels, half resolution)
    buffers.after_nv12_convert_plane1 = cv::Mat(height / 2, width / 2, CV_8UC2);
    if (buffers.after_nv12_convert_plane1.empty()) {
        std::cerr << "Failed to allocate output buffer for plane 1 (UV)." << std::endl;
        return false;
    }
    // Allocate Y plane for rotated NV12
    buffers.after_rotate_plane0 = cv::Mat(height, width, CV_8UC1);
    if (buffers.after_rotate_plane0.empty()) {
        std::cerr << "Failed to allocate output buffer for plane 0 after rotation (Y)." << std::endl;
        return false;
    }
    // Allocate UV plane for rotated NV12
    buffers.after_rotate_plane1 = cv::Mat(height / 2, width / 2, CV_8UC2);
    if (buffers.after_rotate_plane1.empty()) {
        std::cerr << "Failed to allocate output buffer for plane 1 after rotation (UV)." << std::endl;
        return false;
    }
    // Allocate final BGR image
    buffers.rgb_converted_image = cv::Mat(height, width, CV_8UC3);
    if (buffers.rgb_converted_image.empty()) {
        std::cerr << "Failed to allocate output buffer for BGR image." << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Convert BGR image to NV12 format using the DSP.
 *
 *        Uses OpenCV Mat objects with user-space memory pointers.
 *        DSP driver handles cache synchronization automatically.
 *
 * @param device DSP device handle
 * @param buffers Buffers structure containing OpenCV Mat objects
 * @return true on success, false on failure
 */
static bool convert_format_to_nv12_with_dsp(dsp_device device, const Buffers &buffers)
{
    /*
     * Configure DSP for BGR to NV12 conversion using user-space buffers.
     * Key difference from DMA-BUF: uses .userptr instead of .fd
     */
    dsp_data_plane_t input_planes[1] = {{
        .userptr = buffers.input_buffer.data,
        .bytesperline = buffers.input_buffer.cols * buffers.input_buffer.elemSize(),
        .bytesused = buffers.input_buffer.total() * buffers.input_buffer.elemSize(),
    }};
    dsp_image_properties_t input_image_props = {
        .width = static_cast<size_t>(buffers.input_buffer.cols),
        .height = static_cast<size_t>(buffers.input_buffer.rows),
        .planes = input_planes,
        .planes_count = 1,
        .format = DSP_IMAGE_FORMAT_BGR,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };

    // NV12 output: separate Y and UV planes
    dsp_data_plane_t output_planes[2] = {
        {
            .userptr = buffers.after_nv12_convert_plane0.data, // Y plane
            .bytesperline = buffers.after_nv12_convert_plane0.cols * buffers.after_nv12_convert_plane0.elemSize(),
            .bytesused = buffers.after_nv12_convert_plane0.total() * buffers.after_nv12_convert_plane0.elemSize(),
        },
        {
            .userptr = buffers.after_nv12_convert_plane1.data, // UV plane
            .bytesperline = buffers.after_nv12_convert_plane1.cols * buffers.after_nv12_convert_plane1.elemSize(),
            .bytesused = buffers.after_nv12_convert_plane1.total() * buffers.after_nv12_convert_plane1.elemSize(),
        },
    };
    dsp_image_properties_t output_image_props = {
        .width = static_cast<size_t>(buffers.after_nv12_convert_plane0.cols),
        .height = static_cast<size_t>(buffers.after_nv12_convert_plane0.rows),
        .planes = output_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };

    // Execute format conversion
    dsp_status status = dsp_convert_format(device, &input_image_props, &output_image_props);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to convert image format to NV12: status=" << status << std::endl;
        dsp_release_device(device);
        return false;
    }

    std::cout << "Image format converted from BGR to NV12 successfully." << std::endl;
    return true;
}

/**
 * @brief Rotate an NV12 image by 45 degrees using the DSP.
 *        Uses user-space buffers (OpenCV Mat) for both input and output.
 * @param device DSP device handle
 * @param buffers Buffers structure
 * @return true on success, false on failure
 */
static bool rotate_with_dsp(dsp_device device, const Buffers &buffers)
{
    dsp_data_plane_t input_planes[2] = {
        {
            .userptr = buffers.after_nv12_convert_plane0.data,
            .bytesperline = buffers.after_nv12_convert_plane0.cols * buffers.after_nv12_convert_plane0.elemSize(),
            .bytesused = buffers.after_nv12_convert_plane0.total() * buffers.after_nv12_convert_plane0.elemSize(),
        },
        {
            .userptr = buffers.after_nv12_convert_plane1.data,
            .bytesperline = buffers.after_nv12_convert_plane1.cols * buffers.after_nv12_convert_plane1.elemSize(),
            .bytesused = buffers.after_nv12_convert_plane1.total() * buffers.after_nv12_convert_plane1.elemSize(),
        },
    };
    dsp_image_properties_t input_image_props = {
        .width = static_cast<size_t>(buffers.after_nv12_convert_plane0.cols),
        .height = static_cast<size_t>(buffers.after_nv12_convert_plane0.rows),
        .planes = input_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };
    dsp_data_plane_t output_planes[2] = {
        {
            .userptr = buffers.after_rotate_plane0.data,
            .bytesperline = buffers.after_rotate_plane0.cols * buffers.after_rotate_plane0.elemSize(),
            .bytesused = buffers.after_rotate_plane0.total() * buffers.after_rotate_plane0.elemSize(),
        },
        {
            .userptr = buffers.after_rotate_plane1.data,
            .bytesperline = buffers.after_rotate_plane1.cols * buffers.after_rotate_plane1.elemSize(),
            .bytesused = buffers.after_rotate_plane1.total() * buffers.after_rotate_plane1.elemSize(),
        },
    };
    dsp_image_properties_t output_image_props = {
        .width = static_cast<size_t>(buffers.after_rotate_plane0.cols),
        .height = static_cast<size_t>(buffers.after_rotate_plane0.rows),
        .planes = output_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };

    // Configure rotation parameters
    dsp_affine_rotation_params_t rotate_params = {
        .src = &input_image_props,
        .dst = &output_image_props,
        .interpolation = INTERPOLATION_TYPE_BILINEAR, // Smooth interpolation for quality
        .theta = 45.0f,                               // Rotate 45 degrees clockwise
    };

    dsp_status status = dsp_rotate(device, &rotate_params);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to rotate image: status=" << status << std::endl;
        return false;
    }

    std::cout << "Image rotated by 45 degrees successfully." << std::endl;

    return true;
}

/**
 * @brief Convert rotated NV12 image back to BGR using the DSP.
 *        Uses user-space buffers (OpenCV Mat) for both input and output.
 * @param device DSP device handle
 * @param buffers Buffers structure
 * @return true on success, false on failure
 */
static bool convert_format_to_bgr_with_dsp(dsp_device device, Buffers &buffers)
{
    dsp_data_plane_t input_planes[2] = {
        {
            .userptr = buffers.after_rotate_plane0.data,
            .bytesperline = buffers.after_rotate_plane0.cols * buffers.after_rotate_plane0.elemSize(),
            .bytesused = buffers.after_rotate_plane0.total() * buffers.after_rotate_plane0.elemSize(),
        },
        {
            .userptr = buffers.after_rotate_plane1.data,
            .bytesperline = buffers.after_rotate_plane1.cols * buffers.after_rotate_plane1.elemSize(),
            .bytesused = buffers.after_rotate_plane1.total() * buffers.after_rotate_plane1.elemSize(),
        },
    };
    dsp_image_properties_t input_image_props = {
        .width = static_cast<size_t>(buffers.after_rotate_plane0.cols),
        .height = static_cast<size_t>(buffers.after_rotate_plane0.rows),
        .planes = input_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };
    dsp_data_plane_t output_planes[1] = {{
        .userptr = buffers.rgb_converted_image.data,
        .bytesperline = static_cast<size_t>(buffers.rgb_converted_image.cols * 3), // 3 bytes per pixel for BGR
        .bytesused = buffers.rgb_converted_image.total() * buffers.rgb_converted_image.elemSize(),
    }};
    dsp_image_properties_t output_image_props = {
        .width = static_cast<size_t>(buffers.rgb_converted_image.cols),
        .height = static_cast<size_t>(buffers.rgb_converted_image.rows),
        .planes = output_planes,
        .planes_count = 1,
        .format = DSP_IMAGE_FORMAT_BGR,
        .memory = DSP_MEMORY_TYPE_USERPTR,
    };

    dsp_status status = dsp_convert_format(device, &input_image_props, &output_image_props);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to convert image format to BGR: status=" << status << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Save the final BGR image to disk as a JPEG file using OpenCV.
 * @param output_image_path Base path for output image (without extension)
 * @param buffers Buffers structure
 * @return true on success, false on failure
 */
static bool save_bgr_image(const std::string &output_image_path, Buffers &buffers)
{
    std::string output_file = output_image_path + ".jpg";
    if (!cv::imwrite(output_file, buffers.rgb_converted_image)) {
        std::cerr << "Failed to save output image: " << output_file << std::endl;
        return false;
    }
    std::cout << "Image processing completed successfully. Output: " << output_file << std::endl;
    return true;
}

/**
 * @brief Main entry point for the image processing example.
 */
int main(int argc, char *argv[])
{
    CLIArgs cli_args;
    CLI::App app{"Image processing with OpenCV Mat example"};
    app.add_option("-i,--input", cli_args.input_image_path, "Input image path")->required();
    app.add_option("-o,--output", cli_args.output_image_path, "Output image path (without extension)")->required();
    CLI11_PARSE(app, argc, argv);

    // Load input image into OpenCV Mat
    Buffers buffers;
    buffers.input_buffer = read_input_image(cli_args.input_image_path);
    if (buffers.input_buffer.empty())
        return 1;

    // Allocate OpenCV Mat objects for processing pipeline
    if (!allocate_opencv_buffers(buffers.input_buffer.cols, buffers.input_buffer.rows, buffers))
        return 1;

    // Initialize DSP device
    dsp_device device;
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to create DSP device." << std::endl;
        return 1;
    }

    // DSP processing pipeline: BGR->NV12->Rotate->NV12->BGR
    if (!convert_format_to_nv12_with_dsp(device, buffers))
        return 1;

    if (!rotate_with_dsp(device, buffers))
        return 1;

    if (!convert_format_to_bgr_with_dsp(device, buffers))
        return 1;

    // Save output using OpenCV
    if (!save_bgr_image(cli_args.output_image_path, buffers))
        return 1;

    dsp_release_device(device);
    return 0;
}
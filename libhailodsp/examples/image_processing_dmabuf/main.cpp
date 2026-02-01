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
 * Example: Image Processing with DMA-BUF and Hailo DSP API
 *
 * This example demonstrates how to use the Hailo DSP API to perform image processing
 * operations (format conversion and rotation) using DMA-BUF buffers for efficient zero-copy
 * data transfer. The code is intended as a reference for integrating Hailo DSP acceleration into production
 * applications.
 *
 * Key Features:
 *   - Zero-copy data access with buffers allocated in contiguous memory (DMA-BUF)
 *   - Manual cache synchronization for maximum performance
 *   - No automatic CPU synchronization overhead between DSP operations
 *   - Optimal for real-time processing applications
 *
 * Workflow:
 *   1. Parse command-line arguments for input/output image paths.
 *   2. Read an input image using OpenCV (for demonstration; in production, input would be a DMA-BUF buffer).
 *   3. Allocate DMA-BUF buffers for all intermediate and output images.
 *   4. Copy the input image data into a DMA-BUF buffer.
 *   5. Create a DSP device handle.
 *   6. Convert the input image from BGR to NV12 format using the DSP.
 *   7. Rotate the NV12 image by 45 degrees using the DSP.
 *   8. Convert the rotated NV12 image back to BGR using the DSP.
 *   9. Save the final output image as a JPEG file.
 *  10. Release all resources.
 *
 * Usage:
 *   ./image_processing_dmabuf -i <input_image> -o <output_image_base>
 */

#include "dma_heap_buffer.hpp"

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
 * @brief Structure holding all DMA-BUF buffers used in the pipeline.
 *
 *        DMA-BUF buffers provide zero-copy data access for optimal performance.
 *        Each buffer requires explicit cache synchronization using start_read/end_read
 *        and start_write/end_write methods.
 *
 *        For detailed memory type comparison, see the documentation
 */
struct Buffers {
    DmaHeapBuffer input_buffer;              ///< Input image buffer (BGR)
    DmaHeapBuffer after_nv12_convert_plane0; ///< Y plane after BGR->NV12 conversion
    DmaHeapBuffer after_nv12_convert_plane1; ///< UV plane after BGR->NV12 conversion
    DmaHeapBuffer after_rotate_plane0;       ///< Y plane after rotation
    DmaHeapBuffer after_rotate_plane1;       ///< UV plane after rotation
    DmaHeapBuffer rgb_converted_image;       ///< Final RGB image after NV12->BGR conversion
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
 * @brief Allocate all DMA-BUF buffers required for the pipeline.
 *        DMA-BUF buffers are used for zero-copy data transfer and optimal performance.
 *        Each buffer is allocated with the required size for its image format and role.
 * @param width Image width
 * @param height Image height
 * @param buffers Output structure to fill
 * @return true on success, false on failure
 */
static bool allocate_dmabuf_buffers(size_t width, size_t height, Buffers &buffers)
{
    size_t rgb_buffer_size = width * height * 3;    // 3 bytes per pixel for RGB
    size_t nv12_plane0_size = width * height;       // Y plane
    size_t nv12_plane1_size = (width * height) / 2; // UV plane (half size for NV12)

    buffers.input_buffer = DmaHeapBuffer(rgb_buffer_size);
    if (!buffers.input_buffer.valid()) {
        std::cerr << "Failed to allocate input DMA-BUF buffer (size=" << rgb_buffer_size << ")." << std::endl;
        return false;
    }
    buffers.after_nv12_convert_plane0 = DmaHeapBuffer(nv12_plane0_size);
    if (!buffers.after_nv12_convert_plane0.valid()) {
        std::cerr << "Failed to allocate output DMA-BUF buffer for plane 0 (Y)." << std::endl;
        return false;
    }
    buffers.after_nv12_convert_plane1 = DmaHeapBuffer(nv12_plane1_size);
    if (!buffers.after_nv12_convert_plane1.valid()) {
        std::cerr << "Failed to allocate output DMA-BUF buffer for plane 1 (UV)." << std::endl;
        return false;
    }
    buffers.after_rotate_plane0 = DmaHeapBuffer(nv12_plane0_size);
    if (!buffers.after_rotate_plane0.valid()) {
        std::cerr << "Failed to allocate output DMA-BUF buffer for plane 0 after rotation (Y)." << std::endl;
        return false;
    }
    buffers.after_rotate_plane1 = DmaHeapBuffer(nv12_plane1_size);
    if (!buffers.after_rotate_plane1.valid()) {
        std::cerr << "Failed to allocate output DMA-BUF buffer for plane 1 after rotation (UV)." << std::endl;
        return false;
    }
    buffers.rgb_converted_image = DmaHeapBuffer(rgb_buffer_size);
    if (!buffers.rgb_converted_image.valid()) {
        std::cerr << "Failed to allocate output DMA-BUF buffer for RGB image." << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Copy input image data to a DMA-BUF buffer.
 *        This step is only needed for demonstration purposes when the input is not already a DMA-BUF buffer.
 *        In production, input would typically be provided as a DMA-BUF buffer directly.
 * @param input_image Input image (cv::Mat)
 * @param input_buffer DMA-BUF buffer to copy into
 * @return true on success, false on failure
 */
static bool copy_image_to_dmabuf(const cv::Mat &input_image, DmaHeapBuffer &input_buffer)
{
    void *input_addr = input_buffer.map();
    if (!input_addr) {
        std::cerr << "Failed to map input DMA-BUF buffer." << std::endl;
        return false;
    }
    input_buffer.start_write();
    memcpy(input_addr, input_image.data, input_image.total() * input_image.elemSize());
    input_buffer.end_write();
    return true;
}

/**
 * @brief Convert BGR image to NV12 format using the DSP.
 *
 *        Converts single-plane BGR (3 bytes/pixel) to dual-plane NV12 format
 *        (Y plane + UV plane) using DSP hardware acceleration.
 *
 * @param device DSP device handle
 * @param width Image width
 * @param height Image height
 * @param buffers Buffers structure containing DMA-BUF file descriptors
 * @return true on success, false on failure
 */
static bool convert_format_to_nv12_with_dsp(dsp_device device, size_t width, size_t height, const Buffers &buffers)
{
    /*
     * Configure input/output buffer properties for DSP format conversion.
     * DMA-BUF uses .fd (file descriptor), .bytesperline (stride), .bytesused (buffer size).
     */
    dsp_data_plane_t input_planes[1] = {{
        .fd = buffers.input_buffer.fd(),
        .bytesperline = width * 3, // BGR: 3 bytes per pixel
        .bytesused = buffers.input_buffer.size(),
    }};
    dsp_image_properties_t input_image_props = {
        .width = width,
        .height = height,
        .planes = input_planes,
        .planes_count = 1,
        .format = DSP_IMAGE_FORMAT_BGR,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };

    // NV12 format: separate Y plane (luminance) and UV plane (chrominance)
    dsp_data_plane_t output_planes[2] = {
        {
            .fd = buffers.after_nv12_convert_plane0.fd(), // Y plane
            .bytesperline = width,
            .bytesused = buffers.after_nv12_convert_plane0.size(),
        },
        {
            .fd = buffers.after_nv12_convert_plane1.fd(), // UV plane
            .bytesperline = width,
            .bytesused = buffers.after_nv12_convert_plane1.size(),
        },
    };
    dsp_image_properties_t output_image_props = {
        .width = width,
        .height = height,
        .planes = output_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };

    // Execute DSP format conversion
    dsp_status status = dsp_convert_format(device, &input_image_props, &output_image_props);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to convert image format: status=" << status << std::endl;
        dsp_release_device(device);
        return false;
    }

    std::cout << "Image format converted from BGR to NV12 successfully." << std::endl;
    return true;
}

/**
 * @brief Rotate an NV12 image by 45 degrees using the DSP.
 *
 *        Applies affine transformation with bilinear interpolation for smooth results.
 *
 * @param device DSP device handle
 * @param width Image width
 * @param height Image height
 * @param buffers Buffers structure containing DMA-BUF file descriptors
 * @return true on success, false on failure
 */
static bool rotate_with_dsp(dsp_device device, size_t width, size_t height, const Buffers &buffers)
{
    // Configure input NV12 planes (Y + UV) for rotation
    dsp_data_plane_t input_planes[2] = {
        {
            .fd = buffers.after_nv12_convert_plane0.fd(), // Y plane
            .bytesperline = width,
            .bytesused = buffers.after_nv12_convert_plane0.size(),
        },
        {
            .fd = buffers.after_nv12_convert_plane1.fd(), // UV plane
            .bytesperline = width,
            .bytesused = buffers.after_nv12_convert_plane1.size(),
        },
    };
    dsp_image_properties_t input_image_props = {
        .width = width,
        .height = height,
        .planes = input_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };

    // Configure output NV12 planes for rotated result
    dsp_data_plane_t output_planes[2] = {
        {
            .fd = buffers.after_rotate_plane0.fd(), // Rotated Y plane
            .bytesperline = width,
            .bytesused = buffers.after_rotate_plane0.size(),
        },
        {
            .fd = buffers.after_rotate_plane1.fd(), // Rotated UV plane
            .bytesperline = width,
            .bytesused = buffers.after_rotate_plane1.size(),
        },
    };
    dsp_image_properties_t output_image_props = {
        .width = width,
        .height = height,
        .planes = output_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };

    dsp_affine_rotation_params_t rotate_params = {
        .src = &input_image_props,
        .dst = &output_image_props,
        .interpolation = INTERPOLATION_TYPE_BILINEAR,
        .theta = 45.0f, // 45 degrees clockwise
    };

    // Execute DSP rotation
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
 *        Uses DMA-BUF buffers for both input and output for zero-copy performance.
 *        Manual cache synchronization is required before and after DSP operations.
 * @param device DSP device handle
 * @param width Image width
 * @param height Image height
 * @param buffers Buffers structure
 * @return true on success, false on failure
 */
static bool convert_format_to_bgr_with_dsp(dsp_device device, size_t width, size_t height, Buffers &buffers)
{
    /*
     * Prepare DSP parameters for format conversion
     * If using MediaLibrary buffers, convenient methods are provided to convert a MediaLibrary buffer
     * to a dsp_image_properties_t structure.
     */
    dsp_data_plane_t input_planes[2] = {
        {
            .fd = buffers.after_rotate_plane0.fd(),
            .bytesperline = width,
            .bytesused = buffers.after_rotate_plane0.size(),
        },
        {
            .fd = buffers.after_rotate_plane1.fd(),
            .bytesperline = width,
            .bytesused = buffers.after_rotate_plane1.size(),
        },
    };
    dsp_image_properties_t input_image_props = {
        .width = width,
        .height = height,
        .planes = input_planes,
        .planes_count = 2,
        .format = DSP_IMAGE_FORMAT_NV12,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };
    dsp_data_plane_t output_planes[1] = {{
        .fd = buffers.rgb_converted_image.fd(),
        .bytesperline = width * 3, // 3 bytes per pixel for RGB
        .bytesused = buffers.rgb_converted_image.size(),
    }};
    dsp_image_properties_t output_image_props = {
        .width = width,
        .height = height,
        .planes = output_planes,
        .planes_count = 1,
        .format = DSP_IMAGE_FORMAT_BGR,
        .memory = DSP_MEMORY_TYPE_DMABUF,
    };

    /*
     * Call the DSP API to convert the image format
     */
    dsp_status status = dsp_convert_format(device, &input_image_props, &output_image_props);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to convert image format to BGR: status=" << status << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Save the final BGR image from DMA-BUF to disk as a JPEG file.
 *        Manual cache synchronization is performed before reading the buffer.
 *        In production, output could be passed directly to downstream components as a DMA-BUF buffer.
 * @param output_image_path Base path for output image (without extension)
 * @param width Image width
 * @param height Image height
 * @param buffers Buffers structure
 * @return true on success, false on failure
 */
static bool save_bgr_image(const std::string &output_image_path, size_t width, size_t height, Buffers &buffers)
{
    void *output_addr_plane0 = buffers.rgb_converted_image.map();
    if (!output_addr_plane0) {
        std::cerr << "Failed to map output DMA-BUF buffer for BGR image." << std::endl;
        return false;
    }
    /* Synchronize the CPU cache before accessing the buffer data */
    buffers.rgb_converted_image.start_read();

    cv::Mat output_image(height, width, CV_8UC3, output_addr_plane0);
    std::string output_file = output_image_path + ".jpg";
    if (!cv::imwrite(output_file, output_image)) {
        std::cerr << "Failed to save output image: " << output_file << std::endl;
        return false;
    }

    /* End CPU cache synchronization */
    buffers.rgb_converted_image.end_read();
    std::cout << "Image processing completed successfully. Output: " << output_file << std::endl;
    return true;
}

/**
 * @brief Main entry point for the DMA-BUF image processing example.
 *
 *        Demonstrates production-ready DSP processing with zero-copy DMA-BUF buffers.
 *        Processes: Input Image -> BGR->NV12 -> Rotate 45° -> NV12->BGR -> Output Image
 *
 *        Note: File I/O used for demo only. Production code would use existing DMA-BUF
 *        buffers from video pipeline components.
 */
int main(int argc, char *argv[])
{
    CLIArgs cli_args;
    CLI::App app{"Image processing with OpenCV Mat example"};
    app.add_option("-i,--input", cli_args.input_image_path, "Input image path")->required();
    app.add_option("-o,--output", cli_args.output_image_path, "Output image path (without extension)")->required();
    CLI11_PARSE(app, argc, argv);

    // Load input image (demo only - production uses existing DMA-BUF)
    cv::Mat input_image = read_input_image(cli_args.input_image_path);
    if (input_image.empty())
        return 1;

    // Allocate DMA-BUF buffers for zero-copy processing
    Buffers buffers;
    if (!allocate_dmabuf_buffers(input_image.cols, input_image.rows, buffers))
        return 1;

    // Copy input data to DMA-BUF (demo only)
    if (!copy_image_to_dmabuf(input_image, buffers.input_buffer))
        return 1;

    // Initialize DSP device
    dsp_device device;
    dsp_status status = dsp_create_device(&device);
    if (status != DSP_SUCCESS) {
        std::cerr << "Failed to create DSP device." << std::endl;
        return 1;
    }

    // DSP processing pipeline: BGR->NV12->Rotate->NV12->BGR
    if (!convert_format_to_nv12_with_dsp(device, input_image.cols, input_image.rows, buffers))
        return 1;

    if (!rotate_with_dsp(device, input_image.cols, input_image.rows, buffers))
        return 1;

    if (!convert_format_to_bgr_with_dsp(device, input_image.cols, input_image.rows, buffers))
        return 1;

    // Save output (demo only)
    if (!save_bgr_image(cli_args.output_image_path, input_image.cols, input_image.rows, buffers))
        return 1;

    dsp_release_device(device);
    return 0;
}
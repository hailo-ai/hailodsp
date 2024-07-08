#include "image_args.h"

#include <cleanup.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <utils.h>

#include <asm/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static dsp_image_properties_t *generic_alloc_image_userptr(dsp_device device,
                                                           struct image_arguments *args,
                                                           dsp_data_plane_t *planes,
                                                           size_t planes_count)
{
    size_t i = 0;
    dsp_image_properties_t *image = NULL;

    image = malloc(sizeof(*image));
    if (image == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_exit;
    }

    dsp_data_plane_t *ret_planes = malloc(sizeof(*ret_planes) * planes_count);
    if (ret_planes == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_free_image;
    }

    for (i = 0; i < planes_count; ++i) {
        void *data;
        dsp_status status = dsp_create_buffer(device, planes[i].bytesused, &data);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "DSP allocation failed, bytes=%ld\n", planes[i].bytesused);
            goto l_free_planes;
        }

        ret_planes[i].userptr = data;
        ret_planes[i].bytesused = planes[i].bytesused;
        ret_planes[i].bytesperline = planes[i].bytesperline;
    }

    image->planes = ret_planes;
    image->planes_count = planes_count;
    image->format = args->format;
    image->width = args->width;
    image->height = args->height;
    image->memory = DSP_MEMORY_TYPE_USERPTR;

    goto l_exit;

l_free_planes:
    for (size_t j = 0; j < i; ++j) {
        (void)dsp_release_buffer(device, ret_planes[j].userptr);
    }
    free(ret_planes);

l_free_image:
    free(image);
    image = NULL;

l_exit:
    return image;
}

static dsp_image_properties_t *generic_alloc_image_dmabuf(struct image_arguments *args,
                                                          dsp_data_plane_t *planes,
                                                          size_t planes_count)
{
    size_t i = 0;
    int fd = -1;
    dsp_image_properties_t *image = NULL;
    dsp_data_plane_t *ret_planes = NULL;

    image = malloc(sizeof(*image));
    if (image == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_err;
    }

    fd = open("/dev/dma_heap/linux,cma", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open dma heap\n");
        goto l_err;
    }

    ret_planes = calloc(planes_count, sizeof(*ret_planes));
    if (ret_planes == NULL) {
        fprintf(stderr, "Allocation failed\n");
        goto l_err;
    }

    // set all fds to -1 so we can close them all in case of error
    for (i = 0; i < planes_count; ++i) {
        ret_planes[i].fd = -1;
    }

    for (i = 0; i < planes_count; ++i) {
        struct dma_heap_allocation_data alloc_data = {
            .len = planes[i].bytesused,
            .fd_flags = O_RDWR | O_CLOEXEC,
        };

        int ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
        if (ret < 0) {
            fprintf(stderr, "Failed to allocate dma heap buffer\n");
            goto l_err;
        }

        ret_planes[i].fd = alloc_data.fd;
        ret_planes[i].bytesused = planes[i].bytesused;
        ret_planes[i].bytesperline = planes[i].bytesperline;
    }

    image->planes = ret_planes;
    image->planes_count = planes_count;
    image->format = args->format;
    image->width = args->width;
    image->height = args->height;
    image->memory = DSP_MEMORY_TYPE_DMABUF;

    goto l_exit;

l_err:
    if (ret_planes != NULL) {
        for (size_t j = 0; j < i; ++j) {
            if (ret_planes[j].fd != -1) {
                (void)close(ret_planes[j].fd);
            }
        }
        free(ret_planes);
    }
    if (image != NULL) {
        free(image);
        image = NULL;
    }

l_exit:
    if (fd != -1) {
        (void)close(fd);
    }
    return image;
}

static dsp_image_properties_t *generic_alloc_image(dsp_device device,
                                                   struct image_arguments *args,
                                                   dsp_data_plane_t *planes,
                                                   size_t planes_count)
{
    if (args->memory_type == DSP_MEMORY_TYPE_DMABUF) {
        return generic_alloc_image_dmabuf(args, planes, planes_count);
    } else {
        return generic_alloc_image_userptr(device, args, planes, planes_count);
    }
}

static int generic_read_image_userptr(dsp_image_properties_t *image, struct image_arguments *args)
{
    FILE *file = NULL;
    int ret;

    file = fopen(args->path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed open file %s\n", args->path);
        ret = 1;
        goto l_exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        dsp_status status;

        status = dsp_buffer_sync_start(image->planes[i].userptr, DSP_BUFFER_SYNC_WRITE);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "dsp_buffer_sync_start(plane #%zu) failed with %d", i, status);
            ret = 1;
            goto l_exit;
        }

        size_t read_count = fread(image->planes[i].userptr, image->planes[i].bytesused, 1, file);
        if (read_count != 1) {
            fprintf(stderr, "Failed to read %ld bytes from file\n", image->planes[i].bytesused);
            ret = 1;
            goto l_exit;
        }

        status = dsp_buffer_sync_end(image->planes[i].userptr, DSP_BUFFER_SYNC_WRITE);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "dsp_buffer_sync_end(plane #%zu) failed with %d", i, status);
            ret = 1;
            goto l_exit;
        }
    }

    ret = 0;

l_exit:
    CLOSE_FILE(file);
    return ret;
}

static int generic_read_image_dmabuf(dsp_image_properties_t *image, struct image_arguments *args)
{
    void *addr = NULL;
    size_t mapped_size = 0;
    FILE *file = NULL;
    int ret;

    file = fopen(args->path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed open file %s\n", args->path);
        ret = 1;
        goto l_exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        mapped_size = image->planes[i].bytesused;
        addr = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, image->planes[i].fd, 0);
        if (addr == MAP_FAILED) {
            fprintf(stderr, "Failed to mmap dma buf\n");
            ret = 1;
            goto l_exit;
        }

        struct dma_buf_sync sync_start = {
            .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
        };

        ret = ioctl(image->planes[i].fd, DMA_BUF_IOCTL_SYNC, &sync_start);
        if (ret < 0) {
            fprintf(stderr, "Failed to sync dma buf\n");
            ret = 1;
            goto l_exit;
        }

        size_t read_count = fread(addr, mapped_size, 1, file);
        if (read_count != 1) {
            fprintf(stderr, "Failed to read %ld bytes from file\n", mapped_size);
            ret = 1;
            goto l_exit;
        }

        struct dma_buf_sync sync_end = {
            .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
        };
        ret = ioctl(image->planes[i].fd, DMA_BUF_IOCTL_SYNC, &sync_end);
        if (ret < 0) {
            fprintf(stderr, "Failed to sync dma buf\n");
            ret = 1;
            goto l_exit;
        }

        ret = munmap(addr, mapped_size);
        if (ret < 0) {
            fprintf(stderr, "Failed to munmap dma buf\n");
            ret = 1;
            goto l_exit;
        }
        addr = NULL;
    }

    ret = 0;

l_exit:
    CLOSE_FILE(file);
    if (addr != NULL) {
        (void)munmap(addr, mapped_size);
    }
    return ret;
}

int generic_read_image(dsp_image_properties_t *image, struct image_arguments *args)
{
    if (args->memory_type == DSP_MEMORY_TYPE_DMABUF) {
        return generic_read_image_dmabuf(image, args);
    } else {
        return generic_read_image_userptr(image, args);
    }
}

static dsp_image_properties_t *alloc_gray8_image(dsp_device device, struct image_arguments *args)
{
    size_t bytesperline = args->stride == 0 ? ROUND_UP(args->width, 4) : args->stride;
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = bytesperline,
            .bytesused = bytesperline * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_gray16_image(dsp_device device, struct image_arguments *args)
{
    size_t bytesperline = args->stride == 0 ? ROUND_UP(args->width * 2, 4) : args->stride;
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = bytesperline,
            .bytesused = bytesperline * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_rgb_image(dsp_device device, struct image_arguments *args)
{
    size_t bytesperline = args->stride == 0 ? ROUND_UP(args->width * 3, 4) : args->stride;
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = bytesperline,
            .bytesused = bytesperline * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_argb_image(dsp_device device, struct image_arguments *args)
{
    size_t bytesperline = args->stride == 0 ? args->width * 4 : args->stride;
    dsp_data_plane_t planes[1] = {
        {
            .bytesperline = bytesperline,
            .bytesused = bytesperline * args->height,
        },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_nv12_image(dsp_device device, struct image_arguments *args)
{
    size_t bytesperline = args->stride == 0 ? ROUND_UP(args->width, 4) : args->stride;
    dsp_data_plane_t planes[2] = {
        [0] =
            {
                // Y
                .bytesperline = bytesperline,
                .bytesused = bytesperline * ROUND_UP(args->height, 2),
            },
        [1] =
            {
                // UV
                .bytesperline = bytesperline,
                .bytesused = (bytesperline * args->height) / 2,
            },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

static dsp_image_properties_t *alloc_a420_image(dsp_device device, struct image_arguments *args)
{
    size_t y_bytesperline = args->stride == 0 ? ROUND_UP(args->width, 4) : args->stride;
    size_t uv_bytesperline = args->stride == 0 ? ROUND_UP(ROUND_UP(args->width, 2) / 2, 4) : args->stride / 2;
    dsp_data_plane_t planes[4] = {
        [0] =
            {
                // Y
                .bytesperline = y_bytesperline,
                .bytesused = y_bytesperline * ROUND_UP(args->height, 2),
            },
        [1] =
            {
                // U
                .bytesperline = uv_bytesperline,
                .bytesused = uv_bytesperline * (ROUND_UP(args->height, 2) / 2),
            },
        [2] =
            {
                // V
                .bytesperline = uv_bytesperline,
                .bytesused = uv_bytesperline * (ROUND_UP(args->height, 2) / 2),
            },
        [3] =
            {
                // alpha
                .bytesperline = y_bytesperline,
                .bytesused = y_bytesperline * ROUND_UP(args->height, 2),
            },
    };

    return generic_alloc_image(device, args, planes, ARRAY_LENGTH(planes));
}

dsp_image_properties_t *alloc_image(dsp_device device, struct image_arguments *args)
{
    switch (args->format) {
        case DSP_IMAGE_FORMAT_GRAY8:
            return alloc_gray8_image(device, args);
        case DSP_IMAGE_FORMAT_GRAY16:
            return alloc_gray16_image(device, args);
        case DSP_IMAGE_FORMAT_RGB:
            return alloc_rgb_image(device, args);
        case DSP_IMAGE_FORMAT_ARGB:
            return alloc_argb_image(device, args);
        case DSP_IMAGE_FORMAT_NV12:
            return alloc_nv12_image(device, args);
        case DSP_IMAGE_FORMAT_A420:
            return alloc_a420_image(device, args);
        default:
            fprintf(stderr, "Reading %d format is not supported\n", args->format);
            return NULL;
    }
}

dsp_image_properties_t *read_image(dsp_device device, struct image_arguments *args)
{
    dsp_image_properties_t *image = NULL;
    dsp_image_properties_t *ret_image = NULL;

    image = alloc_image(device, args);
    if (image == NULL) {
        goto l_exit;
    }

    if (generic_read_image(image, args)) {
        goto l_exit;
    }

    ret_image = image;
    image = NULL;

l_exit:
    RELEASE_IMAGE(device, image);

    return ret_image;
}

void release_image(dsp_device device, dsp_image_properties_t *image)
{
    for (size_t i = 0; i < image->planes_count; ++i) {
        if (image->memory == DSP_MEMORY_TYPE_USERPTR) {
            (void)dsp_release_buffer(device, image->planes[i].userptr);
        } else if (image->planes[i].fd != -1) {
            (void)close(image->planes[i].fd);
        }
    }

    free(image->planes);
    free(image);
}

static int write_image_to_file_userptr(const char *path, dsp_image_properties_t *image)
{
    int err = 1;
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open file %s\n", path);
        err = 1;
        goto l_exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        dsp_status status;

        status = dsp_buffer_sync_start(image->planes[i].userptr, DSP_BUFFER_SYNC_READ);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "dsp_buffer_sync_start(plane #%zu) failed with %d", i, status);
            err = 1;
            goto l_exit;
        }

        size_t write_size = fwrite(image->planes[i].userptr, image->planes[i].bytesused, 1, file);
        if (write_size != 1) {
            fprintf(stderr, "Writing plane %ld failed\n", i);
            err = 1;
            goto l_exit;
        }

        status = dsp_buffer_sync_end(image->planes[i].userptr, DSP_BUFFER_SYNC_READ);
        if (status != DSP_SUCCESS) {
            fprintf(stderr, "dsp_buffer_sync_end(plane #%zu) failed with %d", i, status);
            err = 1;
            goto l_exit;
        }
    }

    err = 0;
l_exit:
    CLOSE_FILE(file);

    return err;
}

static int write_image_to_file_dmabuf(const char *path, dsp_image_properties_t *image)
{
    void *addr = NULL;
    size_t mapped_size = 0;
    int err = 1;
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open file %s\n", path);
        err = 1;
        goto l_exit;
    }

    for (size_t i = 0; i < image->planes_count; ++i) {
        mapped_size = image->planes[i].bytesused;
        void *addr = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, image->planes[i].fd, 0);
        if (addr == MAP_FAILED) {
            fprintf(stderr, "Failed to mmap plane %ld\n", i);
            err = 1;
            goto l_exit;
        }

        struct dma_buf_sync sync_start = {
            .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ,
        };

        int ret = ioctl(image->planes[i].fd, DMA_BUF_IOCTL_SYNC, &sync_start);
        if (ret < 0) {
            fprintf(stderr, "Failed to sync dma buf\n");
            err = 1;
            goto l_exit;
        }

        size_t write_size = fwrite(addr, mapped_size, 1, file);
        if (write_size != 1) {
            fprintf(stderr, "Writing plane %ld failed\n", i);
            err = 1;
            goto l_exit;
        }

        struct dma_buf_sync sync_end = {
            .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ,
        };

        ret = ioctl(image->planes[i].fd, DMA_BUF_IOCTL_SYNC, &sync_end);
        if (ret < 0) {
            fprintf(stderr, "Failed to sync dma buf\n");
            err = 1;
            goto l_exit;
        }

        ret = munmap(addr, mapped_size);
        if (ret < 0) {
            fprintf(stderr, "Failed to munmap dma buf\n");
            err = 1;
            goto l_exit;
        }
        addr = NULL;
    }

    err = 0;
l_exit:
    CLOSE_FILE(file);
    if (addr != NULL) {
        (void)munmap(addr, mapped_size);
    }
    return err;
}

int write_image_to_file(const char *path, dsp_image_properties_t *image)
{
    if (image->memory == DSP_MEMORY_TYPE_DMABUF) {
        return write_image_to_file_dmabuf(path, image);
    } else {
        return write_image_to_file_userptr(path, image);
    }
}
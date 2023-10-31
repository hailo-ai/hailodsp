/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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

#pragma once

#include "hailo/hailodsp.h"
#include "user_dsp_interface.h"

#ifdef __cplusplus
extern "C" {
#endif
dsp_status dsp_crop_and_resize_perf(dsp_device device,
                                    const dsp_resize_params_t *resize_params,
                                    const dsp_crop_api_t *crop_params,
                                    perf_info_t *perf_info);

dsp_status dsp_resize_perf(dsp_device device, const dsp_resize_params_t *resize_params, perf_info_t *perf_info);

dsp_status dsp_multi_crop_and_resize_perf(dsp_device device,
                                          const dsp_multi_resize_params_t *resize_params,
                                          const dsp_crop_api_t *crop_params,
                                          perf_info_t *perf_info);

#ifdef __cplusplus
}
#endif
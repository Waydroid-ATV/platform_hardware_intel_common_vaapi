/*
 * Copyright ? 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWAR
 *
 * Authors:
 *    Pengfei Qu <Pengfei.qu@intel.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <va/va.h>

#include "intel_batchbuffer.h"
#include "intel_driver.h"

#include "i965_defines.h"
#include "i965_structs.h"
#include "i965_drv_video.h"
#include "i965_encoder.h"
#include "i965_encoder_utils.h"
#include "intel_media.h"

#include "i965_gpe_utils.h"
#include "i965_encoder_common.h"
#include "i965_avc_encoder_common.h"
#include "gen9_avc_encoder_kernels.h"
#include "gen9_avc_encoder.h"
#include "gen9_avc_const_def.h"

#define MAX_URB_SIZE                    4096 /* In register */
#define NUM_KERNELS_PER_GPE_CONTEXT     1
#define MBENC_KERNEL_BASE GEN9_AVC_KERNEL_MBENC_QUALITY_I

#define OUT_BUFFER_2DW(batch, bo, is_target, delta)  do {               \
        if (bo) {                                                       \
            OUT_BCS_RELOC(batch,                                        \
                            bo,                                         \
                            I915_GEM_DOMAIN_INSTRUCTION,                \
                            is_target ? I915_GEM_DOMAIN_INSTRUCTION : 0,     \
                            delta);                                     \
            OUT_BCS_BATCH(batch, 0);                                    \
        } else {                                                        \
            OUT_BCS_BATCH(batch, 0);                                    \
            OUT_BCS_BATCH(batch, 0);                                    \
        }                                                               \
    } while (0)

#define OUT_BUFFER_3DW(batch, bo, is_target, delta, attr)  do { \
        OUT_BUFFER_2DW(batch, bo, is_target, delta);            \
        OUT_BCS_BATCH(batch, attr);                             \
    } while (0)


static const uint32_t qm_flat[16] = {
    0x10101010, 0x10101010, 0x10101010, 0x10101010,
    0x10101010, 0x10101010, 0x10101010, 0x10101010,
    0x10101010, 0x10101010, 0x10101010, 0x10101010,
    0x10101010, 0x10101010, 0x10101010, 0x10101010
};

static const uint32_t fqm_flat[32] = {
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000,
    0x10001000, 0x10001000, 0x10001000, 0x10001000
};

static unsigned int slice_type_kernel[3] = {1,2,0};

const gen9_avc_brc_init_reset_curbe_data gen9_avc_brc_init_reset_curbe_init_data =
{
    // unsigned int 0
    {
            0
    },

    // unsigned int 1
    {
            0
    },

    // unsigned int 2
    {
            0
    },

    // unsigned int 3
    {
            0
    },

    // unsigned int 4
    {
            0
    },

    // unsigned int 5
    {
            0
    },

    // unsigned int 6
    {
            0
    },

    // unsigned int 7
    {
            0
    },

    // unsigned int 8
    {
            0,
            0
    },

    // unsigned int 9
    {
            0,
            0
    },

    // unsigned int 10
    {
            0,
            0
    },

    // unsigned int 11
    {
            0,
            1
    },

    // unsigned int 12
    {
            51,
            0
    },

    // unsigned int 13
    {
            40,
            60,
            80,
            120
    },

    // unsigned int 14
    {
            35,
            60,
            80,
            120
    },

    // unsigned int 15
    {
            40,
            60,
            90,
            115
    },

    // unsigned int 16
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 17
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 18
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 19
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 20
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 21
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 22
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 23
    {
            0
    }
};

const gen9_avc_frame_brc_update_curbe_data gen9_avc_frame_brc_update_curbe_init_data =
{
    // unsigned int 0
    {
            0
    },

    // unsigned int 1
    {
            0
    },

    // unsigned int 2
    {
            0
    },

    // unsigned int 3
    {
            10,
            50
    },

    // unsigned int 4
    {
            100,
            150
    },

    // unsigned int 5
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 6
    {
            0,
            0,
            0,
            0,
            0,
            0
    },

    // unsigned int 7
    {
            0
    },

    // unsigned int 8
    {
            1,
            1,
            3,
            2
    },

    // unsigned int 9
    {
            1,
            40,
            5,
            5
    },

    // unsigned int 10
    {
            3,
            1,
            7,
            18
    },

    // unsigned int 11
    {
            25,
            37,
            40,
            75
    },

    // unsigned int 12
    {
            97,
            103,
            125,
            160
    },

    // unsigned int 13
    {
            -3,
            -2,
            -1,
            0
    },

    // unsigned int 14
    {
            1,
            2,
            3,
            0xff
    },

    // unsigned int 15
    {
            0,
            0,
            0,
            0
    },

    // unsigned int 16
    {
            0
    },

    // unsigned int 17
    {
            0
    },

    // unsigned int 18
    {
            0
    },

    // unsigned int 19
    {
            0
    },

    // unsigned int 20
    {
            0
    },

    // unsigned int 21
    {
            0
    },

    // unsigned int 22
    {
            0
    },

    // unsigned int 23
    {
            0
    },

};
static void
gen9_free_surfaces_avc(void **data)
{
    struct gen9_surface_avc *avc_surface;

    if (!data || !*data)
        return;

    avc_surface = *data;

    if (avc_surface->scaled_4x_surface_obj) {
        i965_DestroySurfaces(avc_surface->ctx, &avc_surface->scaled_4x_surface_id, 1);
        avc_surface->scaled_4x_surface_id = VA_INVALID_SURFACE;
        avc_surface->scaled_4x_surface_obj = NULL;
    }

    if (avc_surface->scaled_16x_surface_obj) {
        i965_DestroySurfaces(avc_surface->ctx, &avc_surface->scaled_16x_surface_id, 1);
        avc_surface->scaled_16x_surface_id = VA_INVALID_SURFACE;
        avc_surface->scaled_16x_surface_obj = NULL;
    }

    if (avc_surface->scaled_32x_surface_obj) {
        i965_DestroySurfaces(avc_surface->ctx, &avc_surface->scaled_32x_surface_id, 1);
        avc_surface->scaled_32x_surface_id = VA_INVALID_SURFACE;
        avc_surface->scaled_32x_surface_obj = NULL;
    }

    i965_free_gpe_resource(&avc_surface->res_mb_code_surface);
    i965_free_gpe_resource(&avc_surface->res_mv_data_surface);
    i965_free_gpe_resource(&avc_surface->res_ref_pic_select_surface);

    dri_bo_unreference(avc_surface->dmv_top);
    avc_surface->dmv_top = NULL;
    dri_bo_unreference(avc_surface->dmv_bottom);
    avc_surface->dmv_bottom = NULL;

    free(avc_surface);

    *data = NULL;

    return;
}

static VAStatus
gen9_avc_init_check_surfaces(VADriverContextP ctx,
                             struct object_surface *obj_surface,
                             struct intel_encoder_context *encoder_context,
                             struct avc_surface_param *surface_param)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;

    struct gen9_surface_avc *avc_surface;
    int downscaled_width_4x, downscaled_height_4x;
    int downscaled_width_16x, downscaled_height_16x;
    int downscaled_width_32x, downscaled_height_32x;
    int size = 0;
    unsigned int frame_width_in_mbs = ALIGN(surface_param->frame_width,16) / 16;
    unsigned int frame_height_in_mbs = ALIGN(surface_param->frame_height,16) / 16;
    unsigned int frame_mb_nums = frame_width_in_mbs * frame_height_in_mbs;
    int allocate_flag = 1;
    int width,height;

    if (!obj_surface || !obj_surface->bo)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    if (obj_surface->private_data &&
        obj_surface->free_private_data != gen9_free_surfaces_avc) {
        obj_surface->free_private_data(&obj_surface->private_data);
        obj_surface->private_data = NULL;
    }

    if (obj_surface->private_data) {
        return VA_STATUS_SUCCESS;
    }

    avc_surface = calloc(1, sizeof(struct gen9_surface_avc));

    if (!avc_surface)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    avc_surface->ctx = ctx;
    obj_surface->private_data = avc_surface;
    obj_surface->free_private_data = gen9_free_surfaces_avc;

    downscaled_width_4x = generic_state->frame_width_4x;
    downscaled_height_4x = generic_state->frame_height_4x;

    i965_CreateSurfaces(ctx,
                        downscaled_width_4x,
                        downscaled_height_4x,
                        VA_RT_FORMAT_YUV420,
                        1,
                        &avc_surface->scaled_4x_surface_id);

    avc_surface->scaled_4x_surface_obj = SURFACE(avc_surface->scaled_4x_surface_id);

    if (!avc_surface->scaled_4x_surface_obj) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    i965_check_alloc_surface_bo(ctx, avc_surface->scaled_4x_surface_obj, 1,
                                VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);

    downscaled_width_16x = generic_state->frame_width_16x;
    downscaled_height_16x = generic_state->frame_height_16x;
    i965_CreateSurfaces(ctx,
                        downscaled_width_16x,
                        downscaled_height_16x,
                        VA_RT_FORMAT_YUV420,
                        1,
                        &avc_surface->scaled_16x_surface_id);
    avc_surface->scaled_16x_surface_obj = SURFACE(avc_surface->scaled_16x_surface_id);

    if (!avc_surface->scaled_16x_surface_obj) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    i965_check_alloc_surface_bo(ctx, avc_surface->scaled_16x_surface_obj, 1,
                                VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);

    downscaled_width_32x = generic_state->frame_width_32x;
    downscaled_height_32x = generic_state->frame_height_32x;
    i965_CreateSurfaces(ctx,
                        downscaled_width_32x,
                        downscaled_height_32x,
                        VA_RT_FORMAT_YUV420,
                        1,
                        &avc_surface->scaled_32x_surface_id);
    avc_surface->scaled_32x_surface_obj = SURFACE(avc_surface->scaled_32x_surface_id);

    if (!avc_surface->scaled_32x_surface_obj) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    i965_check_alloc_surface_bo(ctx, avc_surface->scaled_32x_surface_obj, 1,
                                VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);

    /*mb code and mv data for each frame*/
    size = frame_mb_nums * 16 * 4;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
        &avc_surface->res_mb_code_surface,
        ALIGN(size,0x1000),
        "mb code buffer");
    if (!allocate_flag)
        goto failed_allocation;

    size = frame_mb_nums * 32 * 4;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
        &avc_surface->res_mv_data_surface,
        ALIGN(size,0x1000),
        "mv data buffer");
    if (!allocate_flag)
        goto failed_allocation;

    /* ref pic list*/
    if(avc_state->ref_pic_select_list_supported)
    {
        width = ALIGN(frame_width_in_mbs * 8,64);
        height= frame_height_in_mbs ;
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_surface->res_ref_pic_select_surface,
                                     width, height,
                                     width,
                                     "Ref pic select list buffer");
        if (!allocate_flag)
            goto failed_allocation;
    }

    /*direct mv*/
    avc_surface->dmv_top =
        dri_bo_alloc(i965->intel.bufmgr,
        "direct mv top Buffer",
        68 * frame_mb_nums,
        64);
    avc_surface->dmv_bottom =
        dri_bo_alloc(i965->intel.bufmgr,
        "direct mv bottom Buffer",
        68 * frame_mb_nums,
        64);
    assert(avc_surface->dmv_top);
    assert(avc_surface->dmv_bottom);

    return VA_STATUS_SUCCESS;

failed_allocation:
    return VA_STATUS_ERROR_ALLOCATION_FAILED;
}

static VAStatus
gen9_avc_allocate_resources(VADriverContextP ctx,
                            struct encode_state *encode_state,
                            struct intel_encoder_context *encoder_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    unsigned int size  = 0;
    unsigned int width  = 0;
    unsigned int height  = 0;
    unsigned char * data  = NULL;
    int allocate_flag = 1;
    int i = 0;

    /*all the surface/buffer are allocated here*/

    /*second level batch buffer for image state write when cqp etc*/
    i965_free_gpe_resource(&avc_ctx->res_image_state_batch_buffer_2nd_level);
    size = INTEL_AVC_IMAGE_STATE_CMD_SIZE ;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                             &avc_ctx->res_image_state_batch_buffer_2nd_level,
                             ALIGN(size,0x1000),
                             "second levle batch (image state write) buffer");
    if (!allocate_flag)
        goto failed_allocation;

    i965_free_gpe_resource(&avc_ctx->res_slice_batch_buffer_2nd_level);
    /* include (dw) 2* (ref_id + weight_state + pak_insert_obj) + slice state(11) + slice/pps/sps headers, no mb code size
       2*(10 + 98 + X) + 11*/
    size = 4096 + (320 * 4 + 80 + 16) * encode_state->num_slice_params_ext;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                             &avc_ctx->res_slice_batch_buffer_2nd_level,
                             ALIGN(size,0x1000),
                             "second levle batch (slice) buffer");
    if (!allocate_flag)
        goto failed_allocation;

    /* scaling related surface   */
    if(avc_state->mb_status_supported)
    {
        i965_free_gpe_resource(&avc_ctx->res_mb_status_buffer);
        size = (generic_state->frame_width_in_mbs * generic_state->frame_height_in_mbs * 16 * 4 + 1023)&~0x3ff;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_mb_status_buffer,
                                 ALIGN(size,0x1000),
                                 "MB statistics output buffer");
        if (!allocate_flag)
            goto failed_allocation;
        i965_zero_gpe_resource(&avc_ctx->res_mb_status_buffer);
    }

    if(avc_state->flatness_check_supported)
    {
        width = generic_state->frame_width_in_mbs * 4;
        height= generic_state->frame_height_in_mbs * 4;
        i965_free_gpe_resource(&avc_ctx->res_flatness_check_surface);
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_ctx->res_flatness_check_surface,
                                     width, height,
                                     ALIGN(width,64),
                                     "Flatness check buffer");
        if (!allocate_flag)
            goto failed_allocation;
    }
    /* me related surface */
    width = generic_state->downscaled_width_4x_in_mb * 8;
    height= generic_state->downscaled_height_4x_in_mb * 4 * 10;
    i965_free_gpe_resource(&avc_ctx->s4x_memv_distortion_buffer);
    allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                 &avc_ctx->s4x_memv_distortion_buffer,
                                 width, height,
                                 ALIGN(width,64),
                                 "4x MEMV distortion buffer");
    if (!allocate_flag)
        goto failed_allocation;
    i965_zero_gpe_resource(&avc_ctx->s4x_memv_distortion_buffer);

    width = (generic_state->downscaled_width_4x_in_mb + 7)/8 * 64;
    height= (generic_state->downscaled_height_4x_in_mb + 1)/2 * 8;
    i965_free_gpe_resource(&avc_ctx->s4x_memv_min_distortion_brc_buffer);
    allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                 &avc_ctx->s4x_memv_min_distortion_brc_buffer,
                                 width, height,
                                 width,
                                 "4x MEMV min distortion brc buffer");
    if (!allocate_flag)
        goto failed_allocation;
    i965_zero_gpe_resource(&avc_ctx->s4x_memv_min_distortion_brc_buffer);


    width = ALIGN(generic_state->downscaled_width_4x_in_mb * 32,64);
    height= generic_state->downscaled_height_4x_in_mb * 4 * 2 * 10;
    i965_free_gpe_resource(&avc_ctx->s4x_memv_data_buffer);
    allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                 &avc_ctx->s4x_memv_data_buffer,
                                 width, height,
                                 width,
                                 "4x MEMV data buffer");
    if (!allocate_flag)
        goto failed_allocation;
    i965_zero_gpe_resource(&avc_ctx->s4x_memv_data_buffer);


    width = ALIGN(generic_state->downscaled_width_16x_in_mb * 32,64);
    height= generic_state->downscaled_height_16x_in_mb * 4 * 2 * 10 ;
    i965_free_gpe_resource(&avc_ctx->s16x_memv_data_buffer);
    allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                 &avc_ctx->s16x_memv_data_buffer,
                                 width, height,
                                 width,
                                 "16x MEMV data buffer");
    if (!allocate_flag)
        goto failed_allocation;
    i965_zero_gpe_resource(&avc_ctx->s16x_memv_data_buffer);


    width = ALIGN(generic_state->downscaled_width_32x_in_mb * 32,64);
    height= generic_state->downscaled_height_32x_in_mb * 4 * 2 * 10 ;
    i965_free_gpe_resource(&avc_ctx->s32x_memv_data_buffer);
    allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                 &avc_ctx->s32x_memv_data_buffer,
                                 width, height,
                                 width,
                                 "32x MEMV data buffer");
    if (!allocate_flag)
        goto failed_allocation;
    i965_zero_gpe_resource(&avc_ctx->s32x_memv_data_buffer);


    if(!generic_state->brc_allocated)
    {
        /*brc related surface */
        i965_free_gpe_resource(&avc_ctx->res_brc_history_buffer);
        size = 864;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_brc_history_buffer,
                                 ALIGN(size,0x1000),
                                 "brc history buffer");
        if (!allocate_flag)
            goto failed_allocation;

        i965_free_gpe_resource(&avc_ctx->res_brc_pre_pak_statistics_output_buffer);
        size = 64;//44
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_brc_pre_pak_statistics_output_buffer,
                                 ALIGN(size,0x1000),
                                 "brc pak statistic buffer");
        if (!allocate_flag)
            goto failed_allocation;

        i965_free_gpe_resource(&avc_ctx->res_brc_image_state_read_buffer);
        size = INTEL_AVC_IMAGE_STATE_CMD_SIZE * 7;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_brc_image_state_read_buffer,
                                 ALIGN(size,0x1000),
                                 "brc image state read buffer");
        if (!allocate_flag)
            goto failed_allocation;

        i965_free_gpe_resource(&avc_ctx->res_brc_image_state_write_buffer);
        size = INTEL_AVC_IMAGE_STATE_CMD_SIZE * 7;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_brc_image_state_write_buffer,
                                 ALIGN(size,0x1000),
                                 "brc image state write buffer");
        if (!allocate_flag)
            goto failed_allocation;

        width = ALIGN(64,64);
        height= 44;
        i965_free_gpe_resource(&avc_ctx->res_brc_const_data_buffer);
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_ctx->res_brc_const_data_buffer,
                                     width, height,
                                     width,
                                     "brc const data buffer");
        if (!allocate_flag)
            goto failed_allocation;

        if(generic_state->brc_distortion_buffer_supported)
        {
            width = ALIGN(generic_state->downscaled_width_4x_in_mb * 8,64);
            height= ALIGN(generic_state->downscaled_height_4x_in_mb * 4,8);
            width = (generic_state->downscaled_width_4x_in_mb + 7)/8 * 64;
            height= (generic_state->downscaled_height_4x_in_mb + 1)/2 * 8;
            i965_free_gpe_resource(&avc_ctx->res_brc_dist_data_surface);
            allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                         &avc_ctx->res_brc_dist_data_surface,
                                         width, height,
                                         width,
                                         "brc dist data buffer");
            if (!allocate_flag)
                goto failed_allocation;
            i965_zero_gpe_resource(&avc_ctx->res_brc_dist_data_surface);
        }

        if(generic_state->brc_roi_enable)
        {
            width = ALIGN(generic_state->downscaled_width_4x_in_mb * 16,64);
            height= ALIGN(generic_state->downscaled_height_4x_in_mb * 4,8);
            i965_free_gpe_resource(&avc_ctx->res_mbbrc_roi_surface);
            allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                         &avc_ctx->res_mbbrc_roi_surface,
                                         width, height,
                                         width,
                                         "mbbrc roi buffer");
            if (!allocate_flag)
                goto failed_allocation;
            i965_zero_gpe_resource(&avc_ctx->res_mbbrc_roi_surface);
        }

        /*mb qp in mb brc*/
        width = ALIGN(generic_state->downscaled_width_4x_in_mb * 4,64);
        height= ALIGN(generic_state->downscaled_height_4x_in_mb * 4,8);
        i965_free_gpe_resource(&avc_ctx->res_mbbrc_mb_qp_data_surface);
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_ctx->res_mbbrc_mb_qp_data_surface,
                                     width, height,
                                     width,
                                     "mbbrc mb qp buffer");
        if (!allocate_flag)
            goto failed_allocation;

        i965_free_gpe_resource(&avc_ctx->res_mbbrc_const_data_buffer);
        size = 16 * 52 * 4;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_mbbrc_const_data_buffer,
                                 ALIGN(size,0x1000),
                                 "mbbrc const data buffer");
        if (!allocate_flag)
            goto failed_allocation;

        generic_state->brc_allocated = 1;
    }

    /*mb qp external*/
    if(avc_state->mb_qp_data_enable)
    {
        width = ALIGN(generic_state->downscaled_width_4x_in_mb * 4,64);
        height= ALIGN(generic_state->downscaled_height_4x_in_mb * 4,8);
        i965_free_gpe_resource(&avc_ctx->res_mb_qp_data_surface);
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_ctx->res_mb_qp_data_surface,
                                     width, height,
                                     width,
                                     "external mb qp buffer");
        if (!allocate_flag)
            goto failed_allocation;
    }


    /* maybe it is not needed by now. it is used in crypt mode*/
    i965_free_gpe_resource(&avc_ctx->res_brc_mbenc_curbe_write_buffer);
    size = ALIGN(sizeof(gen9_avc_mbenc_curbe_data), 64) + ALIGN(sizeof(struct gen8_interface_descriptor_data), 64) ;//* NUM_GEN9_AVC_KERNEL_MBENC;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                             &avc_ctx->res_brc_mbenc_curbe_write_buffer,
                             size,
                             "mbenc curbe data buffer");
    if (!allocate_flag)
        goto failed_allocation;

    /*     mbenc related surface. it share most of surface with other kernels     */
    if(avc_state->arbitrary_num_mbs_in_slice)
    {
        width = (generic_state->frame_width_in_mbs + 1) * 64;
        height= generic_state->frame_height_in_mbs ;
        i965_free_gpe_resource(&avc_ctx->res_mbenc_slice_map_surface);
        allocate_flag = i965_gpe_allocate_2d_resource(i965->intel.bufmgr,
                                     &avc_ctx->res_mbenc_slice_map_surface,
                                     width, height,
                                     width,
                                     "slice map buffer");
        if (!allocate_flag)
            goto failed_allocation;

        /*generate slice map,default one slice per frame.*/
    }

    /* sfd related surface  */
    if(avc_state->sfd_enable)
    {
        i965_free_gpe_resource(&avc_ctx->res_sfd_output_buffer);
        size = 128;
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_sfd_output_buffer,
                                 size,
                                 "sfd output buffer");
        if (!allocate_flag)
            goto failed_allocation;

        i965_free_gpe_resource(&avc_ctx->res_sfd_cost_table_p_frame_buffer);
        size = ALIGN(52,64);
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_sfd_cost_table_p_frame_buffer,
                                 size,
                                 "sfd P frame cost table buffer");
        if (!allocate_flag)
            goto failed_allocation;
        data = i965_map_gpe_resource(&(avc_ctx->res_sfd_cost_table_p_frame_buffer));
        assert(data);
        memcpy(data,gen9_avc_sfd_cost_table_p_frame,sizeof(unsigned char) *52);
        i965_unmap_gpe_resource(&(avc_ctx->res_sfd_cost_table_p_frame_buffer));

        i965_free_gpe_resource(&avc_ctx->res_sfd_cost_table_b_frame_buffer);
        size = ALIGN(52,64);
        allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_sfd_cost_table_b_frame_buffer,
                                 size,
                                 "sfd B frame cost table buffer");
        if (!allocate_flag)
            goto failed_allocation;
        data = i965_map_gpe_resource(&(avc_ctx->res_sfd_cost_table_b_frame_buffer));
        assert(data);
        memcpy(data,gen9_avc_sfd_cost_table_b_frame,sizeof(unsigned char) *52);
        i965_unmap_gpe_resource(&(avc_ctx->res_sfd_cost_table_b_frame_buffer));
    }

    /* wp related surfaces */
    if(avc_state->weighted_prediction_supported)
    {
        for(i = 0; i < 2 ; i++)
        {
            if (avc_ctx->wp_output_pic_select_surface_obj[i]) {
                continue;
            }

            width = generic_state->frame_width_in_pixel;
            height= generic_state->frame_height_in_pixel ;
            i965_CreateSurfaces(ctx,
                                width,
                                height,
                                VA_RT_FORMAT_YUV420,
                                1,
                                &avc_ctx->wp_output_pic_select_surface_id[i]);
            avc_ctx->wp_output_pic_select_surface_obj[i] = SURFACE(avc_ctx->wp_output_pic_select_surface_id[i]);

            if (!avc_ctx->wp_output_pic_select_surface_obj[i]) {
                goto failed_allocation;
            }

            i965_check_alloc_surface_bo(ctx, avc_ctx->wp_output_pic_select_surface_obj[i], 1,
                                VA_FOURCC('N', 'V', '1', '2'), SUBSAMPLE_YUV420);
        }
        i965_free_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[0]);
        i965_object_surface_to_2d_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[0], avc_ctx->wp_output_pic_select_surface_obj[0]);
        i965_free_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[1]);
        i965_object_surface_to_2d_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[1], avc_ctx->wp_output_pic_select_surface_obj[1]);
    }

    /* other   */

    i965_free_gpe_resource(&avc_ctx->res_mad_data_buffer);
    size = 4 * 1;
    allocate_flag = i965_allocate_gpe_resource(i965->intel.bufmgr,
                                 &avc_ctx->res_mad_data_buffer,
                                 ALIGN(size,0x1000),
                                 "MAD data buffer");
    if (!allocate_flag)
        goto failed_allocation;

    return VA_STATUS_SUCCESS;

failed_allocation:
    return VA_STATUS_ERROR_ALLOCATION_FAILED;
}

static void
gen9_avc_free_resources(struct encoder_vme_mfc_context * vme_context)
{
    if(!vme_context)
        return;

    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    VADriverContextP ctx = avc_ctx->ctx;
    int i = 0;

    /* free all the surface/buffer here*/
    i965_free_gpe_resource(&avc_ctx->res_image_state_batch_buffer_2nd_level);
    i965_free_gpe_resource(&avc_ctx->res_slice_batch_buffer_2nd_level);
    i965_free_gpe_resource(&avc_ctx->res_mb_status_buffer);
    i965_free_gpe_resource(&avc_ctx->res_flatness_check_surface);
    i965_free_gpe_resource(&avc_ctx->s4x_memv_distortion_buffer);
    i965_free_gpe_resource(&avc_ctx->s4x_memv_min_distortion_brc_buffer);
    i965_free_gpe_resource(&avc_ctx->s4x_memv_data_buffer);
    i965_free_gpe_resource(&avc_ctx->s16x_memv_data_buffer);
    i965_free_gpe_resource(&avc_ctx->s32x_memv_data_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_history_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_pre_pak_statistics_output_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_image_state_read_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_image_state_write_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_const_data_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_dist_data_surface);
    i965_free_gpe_resource(&avc_ctx->res_mbbrc_roi_surface);
    i965_free_gpe_resource(&avc_ctx->res_mbbrc_mb_qp_data_surface);
    i965_free_gpe_resource(&avc_ctx->res_mb_qp_data_surface);
    i965_free_gpe_resource(&avc_ctx->res_mbbrc_const_data_buffer);
    i965_free_gpe_resource(&avc_ctx->res_brc_mbenc_curbe_write_buffer);
    i965_free_gpe_resource(&avc_ctx->res_mbenc_slice_map_surface);
    i965_free_gpe_resource(&avc_ctx->res_sfd_output_buffer);
    i965_free_gpe_resource(&avc_ctx->res_sfd_cost_table_p_frame_buffer);
    i965_free_gpe_resource(&avc_ctx->res_sfd_cost_table_b_frame_buffer);
    i965_free_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[0]);
    i965_free_gpe_resource(&avc_ctx->res_wp_output_pic_select_surface_list[1]);
    i965_free_gpe_resource(&avc_ctx->res_mad_data_buffer);

    for(i = 0;i < 2 ; i++)
    {
        if (avc_ctx->wp_output_pic_select_surface_obj[i]) {
            i965_DestroySurfaces(ctx, &avc_ctx->wp_output_pic_select_surface_id[i], 1);
            avc_ctx->wp_output_pic_select_surface_id[i] = VA_INVALID_SURFACE;
            avc_ctx->wp_output_pic_select_surface_obj[i] = NULL;
        }
    }

}

static void
gen9_avc_run_kernel_media_object(VADriverContextP ctx,
                             struct intel_encoder_context *encoder_context,
                             struct i965_gpe_context *gpe_context,
                             int media_function,
                             struct gpe_media_object_parameter *param)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;

    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct encoder_status_buffer_internal *status_buffer;
    struct gpe_mi_store_data_imm_parameter mi_store_data_imm;

    if (!batch)
        return;

    intel_batchbuffer_start_atomic(batch, 0x1000);

    status_buffer = &(avc_ctx->status_buffer);
    memset(&mi_store_data_imm, 0, sizeof(mi_store_data_imm));
    mi_store_data_imm.bo = status_buffer->bo;
    mi_store_data_imm.offset = status_buffer->media_index_offset;
    mi_store_data_imm.dw0 = media_function;
    gen8_gpe_mi_store_data_imm(ctx, batch, &mi_store_data_imm);

    intel_batchbuffer_emit_mi_flush(batch);
    gen9_gpe_pipeline_setup(ctx, gpe_context, batch);
    gen8_gpe_media_object(ctx, gpe_context, batch, param);
    gen8_gpe_media_state_flush(ctx, gpe_context, batch);

    gen9_gpe_pipeline_end(ctx, gpe_context, batch);

    intel_batchbuffer_end_atomic(batch);

    intel_batchbuffer_flush(batch);
}

static void
gen9_avc_run_kernel_media_object_walker(VADriverContextP ctx,
                                    struct intel_encoder_context *encoder_context,
                                    struct i965_gpe_context *gpe_context,
                                    int media_function,
                                    struct gpe_media_object_walker_parameter *param)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;

    struct intel_batchbuffer *batch = encoder_context->base.batch;
    struct encoder_status_buffer_internal *status_buffer;
    struct gpe_mi_store_data_imm_parameter mi_store_data_imm;

    if (!batch)
        return;

    intel_batchbuffer_start_atomic(batch, 0x1000);

    intel_batchbuffer_emit_mi_flush(batch);

    status_buffer = &(avc_ctx->status_buffer);
    memset(&mi_store_data_imm, 0, sizeof(mi_store_data_imm));
    mi_store_data_imm.bo = status_buffer->bo;
    mi_store_data_imm.offset = status_buffer->media_index_offset;
    mi_store_data_imm.dw0 = media_function;
    gen8_gpe_mi_store_data_imm(ctx, batch, &mi_store_data_imm);

    gen9_gpe_pipeline_setup(ctx, gpe_context, batch);
    gen8_gpe_media_object_walker(ctx, gpe_context, batch, param);
    gen8_gpe_media_state_flush(ctx, gpe_context, batch);

    gen9_gpe_pipeline_end(ctx, gpe_context, batch);

    intel_batchbuffer_end_atomic(batch);

    intel_batchbuffer_flush(batch);
}

static void
gen9_init_gpe_context_avc(VADriverContextP ctx,
                          struct i965_gpe_context *gpe_context,
                          struct encoder_kernel_parameter *kernel_param)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);

    gpe_context->curbe.length = kernel_param->curbe_size; // in bytes

    gpe_context->sampler.entry_size = 0;
    gpe_context->sampler.max_entries = 0;

    if (kernel_param->sampler_size) {
        gpe_context->sampler.entry_size = ALIGN(kernel_param->sampler_size, 64);
        gpe_context->sampler.max_entries = 1;
    }

    gpe_context->idrt.entry_size = ALIGN(sizeof(struct gen8_interface_descriptor_data), 64); // 8 dws, 1 register
    gpe_context->idrt.max_entries = NUM_KERNELS_PER_GPE_CONTEXT;

    gpe_context->surface_state_binding_table.max_entries = MAX_AVC_ENCODER_SURFACES;
    gpe_context->surface_state_binding_table.binding_table_offset = 0;
    gpe_context->surface_state_binding_table.surface_state_offset = ALIGN(MAX_AVC_ENCODER_SURFACES * 4, 64);
    gpe_context->surface_state_binding_table.length = ALIGN(MAX_AVC_ENCODER_SURFACES * 4, 64) + ALIGN(MAX_AVC_ENCODER_SURFACES * SURFACE_STATE_PADDED_SIZE_GEN9, 64);

    if (i965->intel.eu_total > 0)
        gpe_context->vfe_state.max_num_threads = 6 * i965->intel.eu_total;
    else
        gpe_context->vfe_state.max_num_threads = 112; // 16 EU * 7 threads

    gpe_context->vfe_state.curbe_allocation_size = MAX(1, ALIGN(gpe_context->curbe.length, 32) >> 5); // in registers
    gpe_context->vfe_state.urb_entry_size = MAX(1, ALIGN(kernel_param->inline_data_size, 32) >> 5); // in registers
    gpe_context->vfe_state.num_urb_entries = (MAX_URB_SIZE -
                                              gpe_context->vfe_state.curbe_allocation_size -
                                              ((gpe_context->idrt.entry_size >> 5) *
                                               gpe_context->idrt.max_entries)) / gpe_context->vfe_state.urb_entry_size;
    gpe_context->vfe_state.num_urb_entries = CLAMP(1, 127, gpe_context->vfe_state.num_urb_entries);
    gpe_context->vfe_state.gpgpu_mode = 0;
}

static void
gen9_init_vfe_scoreboard_avc(struct i965_gpe_context *gpe_context,
                             struct encoder_scoreboard_parameter *scoreboard_param)
{
    gpe_context->vfe_desc5.scoreboard0.mask = scoreboard_param->mask;
    gpe_context->vfe_desc5.scoreboard0.type = scoreboard_param->type;
    gpe_context->vfe_desc5.scoreboard0.enable = scoreboard_param->enable;

    if (scoreboard_param->walkpat_flag) {
        gpe_context->vfe_desc5.scoreboard0.mask = 0x0F;
        gpe_context->vfe_desc5.scoreboard0.type = 1;

        gpe_context->vfe_desc6.scoreboard1.delta_x0 = 0x0;
        gpe_context->vfe_desc6.scoreboard1.delta_y0 = 0xF;

        gpe_context->vfe_desc6.scoreboard1.delta_x1 = 0x0;
        gpe_context->vfe_desc6.scoreboard1.delta_y1 = 0xE;

        gpe_context->vfe_desc6.scoreboard1.delta_x2 = 0xF;
        gpe_context->vfe_desc6.scoreboard1.delta_y2 = 0x3;

        gpe_context->vfe_desc6.scoreboard1.delta_x3 = 0xF;
        gpe_context->vfe_desc6.scoreboard1.delta_y3 = 0x1;
    } else {
        // Scoreboard 0
        gpe_context->vfe_desc6.scoreboard1.delta_x0 = 0xF;
        gpe_context->vfe_desc6.scoreboard1.delta_y0 = 0x0;

        // Scoreboard 1
        gpe_context->vfe_desc6.scoreboard1.delta_x1 = 0x0;
        gpe_context->vfe_desc6.scoreboard1.delta_y1 = 0xF;

        // Scoreboard 2
        gpe_context->vfe_desc6.scoreboard1.delta_x2 = 0x1;
        gpe_context->vfe_desc6.scoreboard1.delta_y2 = 0xF;

        // Scoreboard 3
        gpe_context->vfe_desc6.scoreboard1.delta_x3 = 0xF;
        gpe_context->vfe_desc6.scoreboard1.delta_y3 = 0xF;

        // Scoreboard 4
        gpe_context->vfe_desc7.scoreboard2.delta_x4 = 0xF;
        gpe_context->vfe_desc7.scoreboard2.delta_y4 = 0x1;

        // Scoreboard 5
        gpe_context->vfe_desc7.scoreboard2.delta_x5 = 0x0;
        gpe_context->vfe_desc7.scoreboard2.delta_y5 = 0xE;

        // Scoreboard 6
        gpe_context->vfe_desc7.scoreboard2.delta_x6 = 0x1;
        gpe_context->vfe_desc7.scoreboard2.delta_y6 = 0xE;

        // Scoreboard 7
        gpe_context->vfe_desc7.scoreboard2.delta_x6 = 0xF;
        gpe_context->vfe_desc7.scoreboard2.delta_y6 = 0xE;
    }
}
/*
VME pipeline related function
*/

/*
scaling kernel related function
*/
static void
gen9_avc_set_curbe_scaling4x(VADriverContextP ctx,
                           struct encode_state *encode_state,
                           struct i965_gpe_context *gpe_context,
                           struct intel_encoder_context *encoder_context,
                           void *param)
{
    gen9_avc_scaling4x_curbe_data *curbe_cmd;
    struct scaling_param *surface_param = (struct scaling_param *)param;

    curbe_cmd = i965_gpe_context_map_curbe(gpe_context);

    if (!curbe_cmd)
        return;

    memset(curbe_cmd, 0, sizeof(gen9_avc_scaling4x_curbe_data));

    curbe_cmd->dw0.input_picture_width  = surface_param->input_frame_width;
    curbe_cmd->dw0.input_picture_height = surface_param->input_frame_height;

    curbe_cmd->dw1.input_y_bti = GEN9_AVC_SCALING_FRAME_SRC_Y_INDEX;
    curbe_cmd->dw2.output_y_bti = GEN9_AVC_SCALING_FRAME_DST_Y_INDEX;


    curbe_cmd->dw5.flatness_threshold = 128;
    curbe_cmd->dw6.enable_mb_flatness_check = surface_param->enable_mb_flatness_check;
    curbe_cmd->dw7.enable_mb_variance_output = surface_param->enable_mb_variance_output;
    curbe_cmd->dw8.enable_mb_pixel_average_output = surface_param->enable_mb_pixel_average_output;

    if (curbe_cmd->dw6.enable_mb_flatness_check ||
        curbe_cmd->dw7.enable_mb_variance_output ||
        curbe_cmd->dw8.enable_mb_pixel_average_output)
    {
        curbe_cmd->dw10.mbv_proc_stat_bti = GEN9_AVC_SCALING_FRAME_MBVPROCSTATS_DST_INDEX;
    }

    i965_gpe_context_unmap_curbe(gpe_context);
    return;
}

static void
gen9_avc_set_curbe_scaling2x(VADriverContextP ctx,
                           struct encode_state *encode_state,
                           struct i965_gpe_context *gpe_context,
                           struct intel_encoder_context *encoder_context,
                           void *param)
{
    gen9_avc_scaling2x_curbe_data *curbe_cmd;
    struct scaling_param *surface_param = (struct scaling_param *)param;

    curbe_cmd = i965_gpe_context_map_curbe(gpe_context);

    if (!curbe_cmd)
        return;

    memset(curbe_cmd, 0, sizeof(gen9_avc_scaling2x_curbe_data));

    curbe_cmd->dw0.input_picture_width  = surface_param->input_frame_width;
    curbe_cmd->dw0.input_picture_height = surface_param->input_frame_height;

    curbe_cmd->dw8.input_y_bti = GEN9_AVC_SCALING_FRAME_SRC_Y_INDEX;
    curbe_cmd->dw9.output_y_bti = GEN9_AVC_SCALING_FRAME_DST_Y_INDEX;

    i965_gpe_context_unmap_curbe(gpe_context);
    return;
}

static void
gen9_avc_send_surface_scaling(VADriverContextP ctx,
                              struct encode_state *encode_state,
                              struct i965_gpe_context *gpe_context,
                              struct intel_encoder_context *encoder_context,
                              void *param)
{
    struct scaling_param *surface_param = (struct scaling_param *)param;
    unsigned int surface_format;
    unsigned int res_size;

    if (surface_param->scaling_out_use_32unorm_surf_fmt)
        surface_format = I965_SURFACEFORMAT_R32_UNORM;
    else if (surface_param->scaling_out_use_16unorm_surf_fmt)
        surface_format = I965_SURFACEFORMAT_R16_UNORM;
    else
        surface_format = I965_SURFACEFORMAT_R8_UNORM;

    gen9_add_2d_gpe_surface(ctx, gpe_context,
                            surface_param->input_surface,
                            0, 1, surface_format,
                            GEN9_AVC_SCALING_FRAME_SRC_Y_INDEX);

    gen9_add_2d_gpe_surface(ctx, gpe_context,
                            surface_param->output_surface,
                            0, 1, surface_format,
                            GEN9_AVC_SCALING_FRAME_DST_Y_INDEX);

    /*add buffer mv_proc_stat, here need change*/
    if (surface_param->mbv_proc_stat_enabled)
    {
        res_size = 16 * (surface_param->input_frame_width/16) * (surface_param->input_frame_height/16) * sizeof(unsigned int);

        gen9_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    surface_param->pres_mbv_proc_stat_buffer,
                                    0,
                                    res_size/4,
                                    0,
                                    GEN9_AVC_SCALING_FRAME_MBVPROCSTATS_DST_INDEX);
    }else if(surface_param->enable_mb_flatness_check)
    {
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       surface_param->pres_flatness_check_surface,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_SCALING_FRAME_MBVPROCSTATS_DST_INDEX);
    }

    return;
}

static VAStatus
gen9_avc_kernel_scaling(VADriverContextP ctx,
                        struct encode_state *encode_state,
                        struct intel_encoder_context *encoder_context,
                        int hme_type)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;

    struct i965_gpe_context *gpe_context;
    struct scaling_param surface_param;
    struct object_surface *obj_surface;
    struct gen9_surface_avc *avc_priv_surface;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct gpe_encoder_kernel_walker_parameter kernel_walker_param;
    unsigned int downscaled_width_in_mb, downscaled_height_in_mb;
    int media_function = 0;
    int kernel_idx = 0;

    obj_surface = encode_state->reconstructed_object;
    avc_priv_surface = obj_surface->private_data;

    memset(&surface_param,0,sizeof(struct scaling_param));
    switch(hme_type)
    {
    case INTEL_ENC_HME_4x :
        {
            media_function = INTEL_MEDIA_STATE_4X_SCALING;
            kernel_idx = GEN9_AVC_KERNEL_SCALING_4X_IDX;
            downscaled_width_in_mb = generic_state->downscaled_width_4x_in_mb;
            downscaled_height_in_mb = generic_state->downscaled_height_4x_in_mb;

            surface_param.input_surface = encode_state->input_yuv_object ;
            surface_param.input_frame_width = generic_state->frame_width_in_pixel ;
            surface_param.input_frame_height = generic_state->frame_height_in_pixel ;

            surface_param.output_surface = avc_priv_surface->scaled_4x_surface_obj ;
            surface_param.output_frame_width = generic_state->frame_width_4x ;
            surface_param.output_frame_height = generic_state->frame_height_4x ;

            surface_param.enable_mb_flatness_check = avc_state->flatness_check_enable;
            surface_param.enable_mb_variance_output = avc_state->mb_status_enable;
            surface_param.enable_mb_pixel_average_output = avc_state->mb_status_enable;

            surface_param.blk8x8_stat_enabled = 0 ;
            surface_param.use_4x_scaling  = 1 ;
            surface_param.use_16x_scaling = 0 ;
            surface_param.use_32x_scaling = 0 ;
            break;
        }
    case INTEL_ENC_HME_16x :
        {
            media_function = INTEL_MEDIA_STATE_16X_SCALING;
            kernel_idx = GEN9_AVC_KERNEL_SCALING_4X_IDX;
            downscaled_width_in_mb = generic_state->downscaled_width_16x_in_mb;
            downscaled_height_in_mb = generic_state->downscaled_height_16x_in_mb;

            surface_param.input_surface = avc_priv_surface->scaled_4x_surface_obj ;
            surface_param.input_frame_width = generic_state->frame_width_4x ;
            surface_param.input_frame_height = generic_state->frame_height_4x ;

            surface_param.output_surface = avc_priv_surface->scaled_16x_surface_obj ;
            surface_param.output_frame_width = generic_state->frame_width_16x ;
            surface_param.output_frame_height = generic_state->frame_height_16x ;

            surface_param.enable_mb_flatness_check = 0 ;
            surface_param.enable_mb_variance_output = 0 ;
            surface_param.enable_mb_pixel_average_output = 0 ;

            surface_param.blk8x8_stat_enabled = 0 ;
            surface_param.use_4x_scaling  = 0 ;
            surface_param.use_16x_scaling = 1 ;
            surface_param.use_32x_scaling = 0 ;

            break;
        }
    case INTEL_ENC_HME_32x :
        {
            media_function = INTEL_MEDIA_STATE_32X_SCALING;
            kernel_idx = GEN9_AVC_KERNEL_SCALING_2X_IDX;
            downscaled_width_in_mb = generic_state->downscaled_width_32x_in_mb;
            downscaled_height_in_mb = generic_state->downscaled_height_32x_in_mb;

            surface_param.input_surface = avc_priv_surface->scaled_16x_surface_obj ;
            surface_param.input_frame_width = generic_state->frame_width_16x ;
            surface_param.input_frame_height = generic_state->frame_height_16x ;

            surface_param.output_surface = avc_priv_surface->scaled_32x_surface_obj ;
            surface_param.output_frame_width = generic_state->frame_width_32x ;
            surface_param.output_frame_height = generic_state->frame_height_32x ;

            surface_param.enable_mb_flatness_check = 0 ;
            surface_param.enable_mb_variance_output = 0 ;
            surface_param.enable_mb_pixel_average_output = 0 ;

            surface_param.blk8x8_stat_enabled = 0 ;
            surface_param.use_4x_scaling  = 0 ;
            surface_param.use_16x_scaling = 0 ;
            surface_param.use_32x_scaling = 1 ;
            break;
        }
    default :
        assert(0);

    }

    gpe_context = &(avc_ctx->context_scaling.gpe_contexts[kernel_idx]);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    if(surface_param.use_32x_scaling)
    {
        generic_ctx->pfn_set_curbe_scaling2x(ctx,encode_state,gpe_context,encoder_context,&surface_param);
    }else
    {
        generic_ctx->pfn_set_curbe_scaling4x(ctx,encode_state,gpe_context,encoder_context,&surface_param);
    }

    if(surface_param.use_32x_scaling)
    {
        surface_param.scaling_out_use_16unorm_surf_fmt = 1 ;
        surface_param.scaling_out_use_32unorm_surf_fmt = 0 ;
    }else
    {
        surface_param.scaling_out_use_16unorm_surf_fmt = 0 ;
        surface_param.scaling_out_use_32unorm_surf_fmt = 1 ;
    }

    if(surface_param.use_4x_scaling)
    {
        if(avc_state->mb_status_supported)
        {
            surface_param.enable_mb_flatness_check = 0;
            surface_param.mbv_proc_stat_enabled = (surface_param.use_4x_scaling)?(avc_state->mb_status_enable || avc_state->flatness_check_enable):0 ;
            surface_param.pres_mbv_proc_stat_buffer = &(avc_ctx->res_mb_status_buffer);

        }else
        {
            surface_param.enable_mb_flatness_check = (surface_param.use_4x_scaling)?avc_state->flatness_check_enable:0;
            surface_param.mbv_proc_stat_enabled = 0 ;
            surface_param.pres_flatness_check_surface = &(avc_ctx->res_flatness_check_surface);
        }
    }

    generic_ctx->pfn_send_scaling_surface(ctx,encode_state,gpe_context,encoder_context,&surface_param);


    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    if(surface_param.use_32x_scaling)
    {
        kernel_walker_param.resolution_x = downscaled_width_in_mb ;
        kernel_walker_param.resolution_y = downscaled_height_in_mb ;
    }else
    {
        /* the scaling is based on 8x8 blk level */
        kernel_walker_param.resolution_x = downscaled_width_in_mb * 2;
        kernel_walker_param.resolution_y = downscaled_height_in_mb * 2;
    }
    kernel_walker_param.no_dependency = 1;

    i965_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);

    gen9_avc_run_kernel_media_object_walker(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}
/*
frame/mb brc related function
*/
static void
gen9_avc_init_mfx_avc_img_state(VADriverContextP ctx,
                                struct encode_state *encode_state,
                                struct intel_encoder_context *encoder_context,
                                struct gen9_mfx_avc_img_state *pstate)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    VAEncSequenceParameterBufferH264 *seq_param = avc_state->seq_param;
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;

    memset(pstate, 0, sizeof(*pstate));

    pstate->dw0.dword_length = (sizeof(struct gen9_mfx_avc_img_state)) / 4 -2;
    pstate->dw0.sub_opcode_b = 0;
    pstate->dw0.sub_opcode_a = 0;
    pstate->dw0.command_opcode = 1;
    pstate->dw0.pipeline = 2;
    pstate->dw0.command_type = 3;

    pstate->dw1.frame_size_in_mbs = generic_state->frame_width_in_mbs * generic_state->frame_height_in_mbs ;

    pstate->dw2.frame_width_in_mbs_minus1 = generic_state->frame_width_in_mbs - 1;
    pstate->dw2.frame_height_in_mbs_minus1 = generic_state->frame_height_in_mbs - 1;

    pstate->dw3.image_structure = 0;//frame is zero
    pstate->dw3.weighted_bipred_idc = pic_param->pic_fields.bits.weighted_bipred_idc;
    pstate->dw3.weighted_pred_flag = pic_param->pic_fields.bits.weighted_pred_flag;
    pstate->dw3.brc_domain_rate_control_enable = 0;//1,set for vdenc;
    pstate->dw3.chroma_qp_offset = pic_param->chroma_qp_index_offset;
    pstate->dw3.second_chroma_qp_offset = pic_param->second_chroma_qp_index_offset;

    pstate->dw4.field_picture_flag = 0;
    pstate->dw4.mbaff_mode_active = seq_param->seq_fields.bits.mb_adaptive_frame_field_flag;
    pstate->dw4.frame_mb_only_flag = seq_param->seq_fields.bits.frame_mbs_only_flag;
    pstate->dw4.transform_8x8_idct_mode_flag = pic_param->pic_fields.bits.transform_8x8_mode_flag;
    pstate->dw4.direct_8x8_interface_flag = seq_param->seq_fields.bits.direct_8x8_inference_flag;
    pstate->dw4.constrained_intra_prediction_flag = pic_param->pic_fields.bits.constrained_intra_pred_flag;
    pstate->dw4.entropy_coding_flag = pic_param->pic_fields.bits.entropy_coding_mode_flag;
    pstate->dw4.mb_mv_format_flag = 1;
    pstate->dw4.chroma_format_idc = seq_param->seq_fields.bits.chroma_format_idc;
    pstate->dw4.mv_unpacked_flag = 1;
    pstate->dw4.insert_test_flag = 0;
    pstate->dw4.load_slice_pointer_flag = 0;
    pstate->dw4.macroblock_stat_enable = 0;        /* disable in the first pass */
    pstate->dw4.minimum_frame_size = 0;
    pstate->dw5.intra_mb_max_bit_flag = 1;
    pstate->dw5.inter_mb_max_bit_flag = 1;
    pstate->dw5.frame_size_over_flag = 1;
    pstate->dw5.frame_size_under_flag = 1;
    pstate->dw5.intra_mb_ipcm_flag = 1;
    pstate->dw5.mb_rate_ctrl_flag = 0;             /* Always 0 in VDEnc mode */
    pstate->dw5.non_first_pass_flag = 0;
    pstate->dw5.aq_enable = pstate->dw5.aq_rounding = 0;
    pstate->dw5.aq_chroma_disable = 1;
    if(pstate->dw4.entropy_coding_flag && (avc_state->tq_enable))
    {
        pstate->dw5.aq_enable = avc_state->tq_enable;
        pstate->dw5.aq_rounding = avc_state->tq_rounding;
    }else
    {
        pstate->dw5.aq_rounding = 0;
    }

    pstate->dw6.intra_mb_max_size = 2700;
    pstate->dw6.inter_mb_max_size = 4095;

    pstate->dw8.slice_delta_qp_max0 = 0;
    pstate->dw8.slice_delta_qp_max1 = 0;
    pstate->dw8.slice_delta_qp_max2 = 0;
    pstate->dw8.slice_delta_qp_max3 = 0;

    pstate->dw9.slice_delta_qp_min0 = 0;
    pstate->dw9.slice_delta_qp_min1 = 0;
    pstate->dw9.slice_delta_qp_min2 = 0;
    pstate->dw9.slice_delta_qp_min3 = 0;

    pstate->dw10.frame_bitrate_min = 0;
    pstate->dw10.frame_bitrate_min_unit = 1;
    pstate->dw10.frame_bitrate_min_unit_mode = 1;
    pstate->dw10.frame_bitrate_max = (1 << 14) - 1;
    pstate->dw10.frame_bitrate_max_unit = 1;
    pstate->dw10.frame_bitrate_max_unit_mode = 1;

    pstate->dw11.frame_bitrate_min_delta = 0;
    pstate->dw11.frame_bitrate_max_delta = 0;

    pstate->dw12.vad_error_logic = 1;
    /* TODO: set paramters DW19/DW20 for slices */
}

void gen9_avc_set_image_state(VADriverContextP ctx,
                              struct encode_state *encode_state,
                              struct intel_encoder_context *encoder_context,
                              struct i965_gpe_resource *gpe_resource)
{
    struct encoder_vme_mfc_context * pak_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )pak_context->generic_enc_state;
    char *pdata;
    int i;
    unsigned int * data;
    struct gen9_mfx_avc_img_state cmd;

    pdata = i965_map_gpe_resource(gpe_resource);

    gen9_avc_init_mfx_avc_img_state(ctx,encode_state,encoder_context,&cmd);
    for(i = 0; i < generic_state->num_pak_passes;i++)
    {

        if(i == 0)
        {
            cmd.dw4.macroblock_stat_enable = 0;
            cmd.dw5.non_first_pass_flag = 0;
        }else
        {
            cmd.dw4.macroblock_stat_enable = 1;
            cmd.dw5.non_first_pass_flag = 1;
            cmd.dw5.intra_mb_ipcm_flag = 1;

        }
         cmd.dw5.mb_rate_ctrl_flag = 0;
         memcpy(pdata,&cmd,sizeof(struct gen9_mfx_avc_img_state));
         data = (unsigned int *)(pdata + sizeof(struct gen9_mfx_avc_img_state));
        *data = MI_BATCH_BUFFER_END;

         pdata += INTEL_AVC_IMAGE_STATE_CMD_SIZE;
    }
    i965_unmap_gpe_resource(gpe_resource);
    return;
}

void gen9_avc_set_image_state_non_brc(VADriverContextP ctx,
                                      struct encode_state *encode_state,
                                      struct intel_encoder_context *encoder_context,
                                      struct i965_gpe_resource *gpe_resource)
{
    struct encoder_vme_mfc_context * pak_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )pak_context->generic_enc_state;
    char *pdata;

    unsigned int * data;
    struct gen9_mfx_avc_img_state cmd;

    pdata = i965_map_gpe_resource(gpe_resource);

    gen9_avc_init_mfx_avc_img_state(ctx,encode_state,encoder_context,&cmd);

    if(generic_state->curr_pak_pass == 0)
    {
        cmd.dw4.macroblock_stat_enable = 0;
        cmd.dw5.non_first_pass_flag = 0;

    }
    else
    {
        cmd.dw4.macroblock_stat_enable = 1;
        cmd.dw5.non_first_pass_flag = 0;
        cmd.dw5.intra_mb_ipcm_flag = 1;
    }

    cmd.dw5.mb_rate_ctrl_flag = 0;
    memcpy(pdata,&cmd,sizeof(struct gen9_mfx_avc_img_state));
    data = (unsigned int *)(pdata + sizeof(struct gen9_mfx_avc_img_state));
    *data = MI_BATCH_BUFFER_END;

    i965_unmap_gpe_resource(gpe_resource);
    return;
}

static void
gen9_avc_init_brc_const_data(VADriverContextP ctx,
                             struct encode_state *encode_state,
                             struct intel_encoder_context *encoder_context)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    struct i965_gpe_resource *gpe_resource = NULL;
    unsigned char * data =NULL;
    unsigned char * data_tmp = NULL;
    unsigned int size = 0;
    unsigned int table_idx = 0;
    unsigned int block_based_skip_enable = avc_state->block_based_skip_enable;
    int i = 0;

    struct object_surface *obj_surface;
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    VASurfaceID surface_id;
    unsigned int transform_8x8_mode_flag = pic_param->pic_fields.bits.transform_8x8_mode_flag;

    gpe_resource = &(avc_ctx->res_brc_const_data_buffer);
    assert(gpe_resource);

    i965_zero_gpe_resource(gpe_resource);

    data = i965_map_gpe_resource(gpe_resource);
    assert(data);

    table_idx = slice_type_kernel[generic_state->frame_type];

    /* Fill surface with QP Adjustment table, Distortion threshold table, MaxFrame threshold table, Distortion QP Adjustment Table*/
    size = sizeof(gen9_avc_qp_adjustment_dist_threshold_max_frame_threshold_dist_qp_adjustment_ipb);
    memcpy(data,gen9_avc_qp_adjustment_dist_threshold_max_frame_threshold_dist_qp_adjustment_ipb,size*sizeof(unsigned char));

    data += size;

    /* skip threshold table*/
    size = 128;
    switch(generic_state->frame_type)
    {
    case SLICE_TYPE_P:
        memcpy(data,gen9_avc_skip_value_p[block_based_skip_enable][transform_8x8_mode_flag],size * sizeof(unsigned char));
        break;
    case SLICE_TYPE_B:
        memcpy(data,gen9_avc_skip_value_b[block_based_skip_enable][transform_8x8_mode_flag],size * sizeof(unsigned char));
        break;
    default:
        /*SLICE_TYPE_I,no change */
        break;
    }

    if((generic_state->frame_type != SLICE_TYPE_I) && avc_state->non_ftq_skip_threshold_lut_input_enable)
    {
        for(i = 0; i< 52 ; i++)
        {
            *(data + 1 + (i * 2)) = (unsigned char)i965_avc_calc_skip_value(block_based_skip_enable,transform_8x8_mode_flag,avc_state->non_ftq_skip_threshold_lut[i]);
        }
    }
    data += size;

    /*fill the qp for ref list*/
    size = 32 + 32 +32 +160;
    memset(data,0xff,32);
    memset(data+32+32,0xff,32);
    switch(generic_state->frame_type)
    {
    case SLICE_TYPE_P:
        {
            for(i = 0 ; i <  slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
            {
               surface_id = slice_param->RefPicList0[i].picture_id;
               obj_surface = SURFACE(surface_id);
               if (!obj_surface)
                   break;
               *(data + i) = avc_state->list_ref_idx[0][i];//?
            }
        }
        break;
    case SLICE_TYPE_B:
        {
            data = data + 32 + 32;
            for(i = 0 ; i <  slice_param->num_ref_idx_l1_active_minus1 + 1; i++)
            {
               surface_id = slice_param->RefPicList1[i].picture_id;
               obj_surface = SURFACE(surface_id);
               if (!obj_surface)
                   break;
               *(data + i) = avc_state->list_ref_idx[1][i];//?
            }

            data = data - 32 - 32;

            for(i = 0 ; i <  slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
            {
               surface_id = slice_param->RefPicList0[i].picture_id;
               obj_surface = SURFACE(surface_id);
               if (!obj_surface)
                   break;
               *(data + i) = avc_state->list_ref_idx[0][i];//?
            }
        }
        break;
    default:
        /*SLICE_TYPE_I,no change */
        break;
    }
    data += size;

    /*mv cost and mode cost*/
    size = 1664;
    memcpy(data,(unsigned char *)&gen9_avc_mode_mv_cost_table[table_idx][0][0],size * sizeof(unsigned char));

    if(avc_state->old_mode_cost_enable)
    {   data_tmp = data;
        for(i = 0; i < 52 ; i++)
        {
            *(data_tmp +3) = (unsigned int)gen9_avc_old_intra_mode_cost[i];
            data_tmp += 16;
        }
    }

    if(avc_state->ftq_skip_threshold_lut_input_enable)
    {
        for(i = 0; i < 52 ; i++)
        {
            *(data + (i * 32) + 24) =
            *(data + (i * 32) + 25) =
            *(data + (i * 32) + 27) =
            *(data + (i * 32) + 28) =
            *(data + (i * 32) + 29) =
            *(data + (i * 32) + 30) =
            *(data + (i * 32) + 31) = avc_state->ftq_skip_threshold_lut[i];
        }

    }
    data += size;

    /*ref cost*/
    size = 128;
    memcpy(data,(unsigned char *)&gen9_avc_ref_cost[table_idx][0],size * sizeof(unsigned char));
    data += size;

    /*scaling factor*/
    size = 64;
    if(avc_state->adaptive_intra_scaling_enable)
    {
        memcpy(data,(unsigned char *)&gen9_avc_adaptive_intra_scaling_factor,size * sizeof(unsigned char));
    }else
    {
        memcpy(data,(unsigned char *)&gen9_avc_intra_scaling_factor,size * sizeof(unsigned char));
    }
    i965_unmap_gpe_resource(gpe_resource);
}

static void
gen9_avc_init_brc_const_data_old(VADriverContextP ctx,
                                 struct encode_state *encode_state,
                                 struct intel_encoder_context *encoder_context)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    struct i965_gpe_resource *gpe_resource = NULL;
    unsigned int * data =NULL;
    unsigned int * data_tmp = NULL;
    unsigned int size = 0;
    unsigned int table_idx = 0;
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;
    unsigned int block_based_skip_enable = avc_state->block_based_skip_enable;
    unsigned int transform_8x8_mode_flag = pic_param->pic_fields.bits.transform_8x8_mode_flag;
    int i = 0;

    gpe_resource = &(avc_ctx->res_brc_const_data_buffer);
    assert(gpe_resource);

    i965_zero_gpe_resource(gpe_resource);

    data = i965_map_gpe_resource(gpe_resource);
    assert(data);

    table_idx = slice_type_kernel[generic_state->frame_type];

    /* Fill surface with QP Adjustment table, Distortion threshold table, MaxFrame threshold table, Distortion QP Adjustment Table*/
    size = sizeof(gen75_avc_qp_adjustment_dist_threshold_max_frame_threshold_dist_qp_adjustment_ipb);
    memcpy(data,gen75_avc_qp_adjustment_dist_threshold_max_frame_threshold_dist_qp_adjustment_ipb,size*sizeof(unsigned char));

    data += size;

    /* skip threshold table*/
    size = 128;
    switch(generic_state->frame_type)
    {
    case SLICE_TYPE_P:
        memcpy(data,gen9_avc_skip_value_p[block_based_skip_enable][transform_8x8_mode_flag],size * sizeof(unsigned char));
        break;
    case SLICE_TYPE_B:
        memcpy(data,gen9_avc_skip_value_b[block_based_skip_enable][transform_8x8_mode_flag],size * sizeof(unsigned char));
        break;
    default:
        /*SLICE_TYPE_I,no change */
        break;
    }

    if((generic_state->frame_type != SLICE_TYPE_I) && avc_state->non_ftq_skip_threshold_lut_input_enable)
    {
        for(i = 0; i< 52 ; i++)
        {
            *(data + 1 + (i * 2)) = (unsigned char)i965_avc_calc_skip_value(block_based_skip_enable,transform_8x8_mode_flag,avc_state->non_ftq_skip_threshold_lut[i]);
        }
    }
    data += size;

    /*fill the qp for ref list*/
    size = 128;
    data += size;
    size = 128;
    data += size;

    /*mv cost and mode cost*/
    size = 1664;
    memcpy(data,(unsigned char *)&gen75_avc_mode_mv_cost_table[table_idx][0][0],size * sizeof(unsigned char));

    if(avc_state->old_mode_cost_enable)
    {   data_tmp = data;
        for(i = 0; i < 52 ; i++)
        {
            *(data_tmp +3) = (unsigned int)gen9_avc_old_intra_mode_cost[i];
            data_tmp += 16;
        }
    }

    if(avc_state->ftq_skip_threshold_lut_input_enable)
    {
        for(i = 0; i < 52 ; i++)
        {
            *(data + (i * 32) + 24) =
            *(data + (i * 32) + 25) =
            *(data + (i * 32) + 27) =
            *(data + (i * 32) + 28) =
            *(data + (i * 32) + 29) =
            *(data + (i * 32) + 30) =
            *(data + (i * 32) + 31) = avc_state->ftq_skip_threshold_lut[i];
        }

    }
    data += size;

    /*ref cost*/
    size = 128;
    memcpy(data,(unsigned char *)&gen9_avc_ref_cost[table_idx][0],size * sizeof(unsigned char));

    i965_unmap_gpe_resource(gpe_resource);
}
static void
gen9_avc_set_curbe_brc_init_reset(VADriverContextP ctx,
                                  struct encode_state *encode_state,
                                  struct i965_gpe_context *gpe_context,
                                  struct intel_encoder_context *encoder_context,
                                  void * param)
{
    gen9_avc_brc_init_reset_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    double input_bits_per_frame = 0;
    double bps_ratio = 0;
    VAEncSequenceParameterBufferH264 * seq_param = avc_state->seq_param;
    struct avc_param common_param;

    cmd = i965_gpe_context_map_curbe(gpe_context);

    memcpy(cmd,&gen9_avc_brc_init_reset_curbe_init_data,sizeof(gen9_avc_brc_init_reset_curbe_data));

    memset(&common_param,0,sizeof(common_param));
    common_param.frame_width_in_pixel = generic_state->frame_width_in_pixel;
    common_param.frame_height_in_pixel = generic_state->frame_height_in_pixel;
    common_param.frame_width_in_mbs = generic_state->frame_width_in_mbs;
    common_param.frame_height_in_mbs = generic_state->frame_height_in_mbs;
    common_param.frames_per_100s = generic_state->frames_per_100s;
    common_param.vbv_buffer_size_in_bit = generic_state->vbv_buffer_size_in_bit;
    common_param.target_bit_rate = generic_state->target_bit_rate;

    cmd->dw0.profile_level_max_frame = i965_avc_get_profile_level_max_frame(&common_param,seq_param->level_idc);
    cmd->dw1.init_buf_full_in_bits = generic_state->init_vbv_buffer_fullness_in_bit;
    cmd->dw2.buf_size_in_bits = generic_state->vbv_buffer_size_in_bit;
    cmd->dw3.average_bit_rate = generic_state->target_bit_rate * 1000;
    cmd->dw4.max_bit_rate = generic_state->max_bit_rate * 1000;
    cmd->dw8.gop_p = (generic_state->gop_ref_distance)?((generic_state->gop_size -1)/generic_state->gop_ref_distance):0;
    cmd->dw9.gop_b = (generic_state->gop_size - 1 - cmd->dw8.gop_p);
    cmd->dw9.frame_width_in_bytes = generic_state->frame_width_in_pixel;
    cmd->dw10.frame_height_in_bytes = generic_state->frame_height_in_pixel;
    cmd->dw12.no_slices = avc_state->slice_num;

    //VUI
    if(seq_param->vui_parameters_present_flag && generic_state->internal_rate_mode != INTEL_BRC_AVBR )
    {
        cmd->dw4.max_bit_rate = cmd->dw4.max_bit_rate;
        if(generic_state->internal_rate_mode == INTEL_BRC_CBR)
        {
            cmd->dw3.average_bit_rate = cmd->dw4.max_bit_rate;

        }

    }
    cmd->dw6.frame_rate_m = generic_state->frames_per_100s;
    cmd->dw7.frame_rate_d = 100;
    cmd->dw8.brc_flag = 0;
    cmd->dw8.brc_flag |= (generic_state->mb_brc_enabled)? 0 : 0x8000;


    if(generic_state->internal_rate_mode == INTEL_BRC_CBR)
    { //CBR
        cmd->dw4.max_bit_rate = cmd->dw3.average_bit_rate;
        cmd->dw8.brc_flag = cmd->dw8.brc_flag |INTEL_ENCODE_BRCINIT_ISCBR;

    }else if(generic_state->internal_rate_mode == INTEL_BRC_VBR)
    {//VBR
        if(cmd->dw4.max_bit_rate < cmd->dw3.average_bit_rate)
        {
            cmd->dw4.max_bit_rate = cmd->dw3.average_bit_rate << 1;
        }
        cmd->dw8.brc_flag = cmd->dw8.brc_flag |INTEL_ENCODE_BRCINIT_ISVBR;

    }else if(generic_state->internal_rate_mode == INTEL_BRC_AVBR)
    { //AVBR
        cmd->dw4.max_bit_rate =cmd->dw3.average_bit_rate;
        cmd->dw8.brc_flag = cmd->dw8.brc_flag |INTEL_ENCODE_BRCINIT_ISAVBR;

    }
    //igonre icq/vcm/qvbr

    cmd->dw10.avbr_accuracy = generic_state->avbr_curracy;
    cmd->dw11.avbr_convergence = generic_state->avbr_convergence;

    //frame bits
    input_bits_per_frame = (double)(cmd->dw4.max_bit_rate) * (double)(cmd->dw7.frame_rate_d)/(double)(cmd->dw6.frame_rate_m);;

    if(cmd->dw2.buf_size_in_bits == 0)
    {
       cmd->dw2.buf_size_in_bits = (unsigned int)(input_bits_per_frame * 4);
    }

    if(cmd->dw1.init_buf_full_in_bits == 0)
    {
       cmd->dw1.init_buf_full_in_bits = cmd->dw2.buf_size_in_bits * 7/8;
    }
    if(cmd->dw1.init_buf_full_in_bits < (unsigned int)(input_bits_per_frame * 2))
    {
       cmd->dw1.init_buf_full_in_bits = (unsigned int)(input_bits_per_frame * 2);
    }
    if(cmd->dw1.init_buf_full_in_bits > cmd->dw2.buf_size_in_bits)
    {
       cmd->dw1.init_buf_full_in_bits = cmd->dw2.buf_size_in_bits;
    }

    //AVBR
    if(generic_state->internal_rate_mode == INTEL_BRC_AVBR)
    {
       cmd->dw2.buf_size_in_bits = 2 * generic_state->target_bit_rate * 1000;
       cmd->dw1.init_buf_full_in_bits = (unsigned int)(3 * cmd->dw2.buf_size_in_bits/4);

    }

    bps_ratio = input_bits_per_frame / (cmd->dw2.buf_size_in_bits/30.0);
    bps_ratio = (bps_ratio < 0.1)? 0.1:(bps_ratio > 3.5)?3.5:bps_ratio;


    cmd->dw16.deviation_threshold_0_pand_b = (unsigned int)(-50 * pow(0.90,bps_ratio));
    cmd->dw16.deviation_threshold_1_pand_b = (unsigned int)(-50 * pow(0.66,bps_ratio));
    cmd->dw16.deviation_threshold_2_pand_b = (unsigned int)(-50 * pow(0.46,bps_ratio));
    cmd->dw16.deviation_threshold_3_pand_b = (unsigned int)(-50 * pow(0.3, bps_ratio));
    cmd->dw17.deviation_threshold_4_pand_b = (unsigned int)(50 *  pow(0.3, bps_ratio));
    cmd->dw17.deviation_threshold_5_pand_b = (unsigned int)(50 * pow(0.46, bps_ratio));
    cmd->dw17.deviation_threshold_6_pand_b = (unsigned int)(50 * pow(0.7,  bps_ratio));
    cmd->dw17.deviation_threshold_7_pand_b = (unsigned int)(50 * pow(0.9,  bps_ratio));
    cmd->dw18.deviation_threshold_0_vbr = (unsigned int)(-50 * pow(0.9, bps_ratio));
    cmd->dw18.deviation_threshold_1_vbr = (unsigned int)(-50 * pow(0.7, bps_ratio));
    cmd->dw18.deviation_threshold_2_vbr = (unsigned int)(-50 * pow(0.5, bps_ratio));
    cmd->dw18.deviation_threshold_3_vbr = (unsigned int)(-50 * pow(0.3, bps_ratio));
    cmd->dw19.deviation_threshold_4_vbr = (unsigned int)(100 * pow(0.4, bps_ratio));
    cmd->dw19.deviation_threshold_5_vbr = (unsigned int)(100 * pow(0.5, bps_ratio));
    cmd->dw19.deviation_threshold_6_vbr = (unsigned int)(100 * pow(0.75,bps_ratio));
    cmd->dw19.deviation_threshold_7_vbr = (unsigned int)(100 * pow(0.9, bps_ratio));
    cmd->dw20.deviation_threshold_0_i = (unsigned int)(-50 * pow(0.8, bps_ratio));
    cmd->dw20.deviation_threshold_1_i = (unsigned int)(-50 * pow(0.6, bps_ratio));
    cmd->dw20.deviation_threshold_2_i = (unsigned int)(-50 * pow(0.34,bps_ratio));
    cmd->dw20.deviation_threshold_3_i = (unsigned int)(-50 * pow(0.2, bps_ratio));
    cmd->dw21.deviation_threshold_4_i = (unsigned int)(50 * pow(0.2,  bps_ratio));
    cmd->dw21.deviation_threshold_5_i = (unsigned int)(50 * pow(0.4,  bps_ratio));
    cmd->dw21.deviation_threshold_6_i = (unsigned int)(50 * pow(0.66, bps_ratio));
    cmd->dw21.deviation_threshold_7_i = (unsigned int)(50 * pow(0.9,  bps_ratio));

    cmd->dw22.sliding_window_size = generic_state->window_size;

    i965_gpe_context_unmap_curbe(gpe_context);

    return;
}

static void
gen9_avc_send_surface_brc_init_reset(VADriverContextP ctx,
                                     struct encode_state *encode_state,
                                     struct i965_gpe_context *gpe_context,
                                     struct intel_encoder_context *encoder_context,
                                     void * param_mbenc)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;

    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_history_buffer,
                                0,
                                avc_ctx->res_brc_history_buffer.size,
                                0,
                                GEN9_AVC_BRC_INIT_RESET_HISTORY_INDEX);

    gen9_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &avc_ctx->res_brc_dist_data_surface,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   GEN9_AVC_BRC_INIT_RESET_DISTORTION_INDEX);

    return;
}

static VAStatus
gen9_avc_kernel_brc_init_reset(VADriverContextP ctx,
                               struct encode_state *encode_state,
                               struct intel_encoder_context *encoder_context)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_parameter media_object_param;
    struct gpe_media_object_inline_data media_object_inline_data;
    int media_function = 0;
    int kernel_idx = GEN9_AVC_KERNEL_BRC_INIT;

    media_function = INTEL_MEDIA_STATE_BRC_INIT_RESET;

    if(generic_state->brc_inited)
        kernel_idx = GEN9_AVC_KERNEL_BRC_RESET;

    gpe_context = &(avc_ctx->context_brc.gpe_contexts[kernel_idx]);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    generic_ctx->pfn_set_curbe_brc_init_reset(ctx,encode_state,gpe_context,encoder_context,NULL);

    generic_ctx->pfn_send_brc_init_reset_surface(ctx,encode_state,gpe_context,encoder_context,NULL);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    memset(&media_object_inline_data, 0, sizeof(media_object_inline_data));
    media_object_param.pinline_data = &media_object_inline_data;
    media_object_param.inline_size = sizeof(media_object_inline_data);

    gen9_avc_run_kernel_media_object(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_avc_set_curbe_brc_frame_update(VADriverContextP ctx,
                                    struct encode_state *encode_state,
                                    struct i965_gpe_context *gpe_context,
                                    struct intel_encoder_context *encoder_context,
                                    void * param)
{
    gen9_avc_frame_brc_update_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    struct object_surface *obj_surface;
    struct gen9_surface_avc *avc_priv_surface;
    struct avc_param common_param;
    VAEncSequenceParameterBufferH264 * seq_param = avc_state->seq_param;

    obj_surface = encode_state->reconstructed_object;

    if (!obj_surface || !obj_surface->private_data)
        return;
    avc_priv_surface = obj_surface->private_data;

    cmd = i965_gpe_context_map_curbe(gpe_context);

    memcpy(cmd,&gen9_avc_frame_brc_update_curbe_init_data,sizeof(gen9_avc_frame_brc_update_curbe_data));

    cmd->dw5.target_size_flag = 0 ;
    if(generic_state->brc_init_current_target_buf_full_in_bits > (double)generic_state->brc_init_reset_buf_size_in_bits)
    {
        /*overflow*/
        generic_state->brc_init_current_target_buf_full_in_bits -= (double)generic_state->brc_init_reset_buf_size_in_bits;
        cmd->dw5.target_size_flag = 1 ;
    }

    if(generic_state->skip_frame_enbale)
    {
        cmd->dw6.num_skip_frames = generic_state->num_skip_frames ;
        cmd->dw7.size_skip_frames = generic_state->size_skip_frames;

        generic_state->brc_init_current_target_buf_full_in_bits += generic_state->brc_init_reset_input_bits_per_frame * generic_state->num_skip_frames;

    }
    cmd->dw0.target_size = (unsigned int)generic_state->brc_init_current_target_buf_full_in_bits ;
    cmd->dw1.frame_number = generic_state->seq_frame_number ;
    cmd->dw2.size_of_pic_headers = generic_state->herder_bytes_inserted << 3 ;
    cmd->dw5.cur_frame_type = generic_state->frame_type ;
    cmd->dw5.brc_flag = 0 ;
    cmd->dw5.brc_flag |= (avc_priv_surface->is_as_ref)?INTEL_ENCODE_BRCUPDATE_IS_REFERENCE:0 ;

    if(avc_state->multi_pre_enable)
    {
        cmd->dw5.brc_flag  |= INTEL_ENCODE_BRCUPDATE_IS_ACTUALQP ;
        cmd->dw14.qp_index_of_cur_pic = avc_priv_surface->frame_idx ; //do not know this. use -1
    }

    cmd->dw5.max_num_paks = generic_state->num_pak_passes ;
    if(avc_state->min_max_qp_enable)
    {
        switch(generic_state->frame_type)
        {
        case SLICE_TYPE_I:
            cmd->dw6.minimum_qp = avc_state->min_qp_i ;
            cmd->dw6.maximum_qp = avc_state->max_qp_i ;
            break;
        case SLICE_TYPE_P:
            cmd->dw6.minimum_qp = avc_state->min_qp_p ;
            cmd->dw6.maximum_qp = avc_state->max_qp_p ;
            break;
        case SLICE_TYPE_B:
            cmd->dw6.minimum_qp = avc_state->min_qp_b ;
            cmd->dw6.maximum_qp = avc_state->max_qp_b ;
            break;
        }
    }else
    {
        cmd->dw6.minimum_qp = 0 ;
        cmd->dw6.maximum_qp = 0 ;
    }
    cmd->dw6.enable_force_skip = avc_state->enable_force_skip ;
    cmd->dw6.enable_sliding_window = 0 ;

    generic_state->brc_init_current_target_buf_full_in_bits += generic_state->brc_init_reset_input_bits_per_frame;

    if(generic_state->internal_rate_mode == INTEL_BRC_AVBR)
    {
        cmd->dw3.start_gadj_frame0 = (unsigned int)((10 *   generic_state->avbr_convergence) / (double)150);
        cmd->dw3.start_gadj_frame1 = (unsigned int)((50 *   generic_state->avbr_convergence) / (double)150);
        cmd->dw4.start_gadj_frame2 = (unsigned int)((100 *  generic_state->avbr_convergence) / (double)150);
        cmd->dw4.start_gadj_frame3 = (unsigned int)((150 *  generic_state->avbr_convergence) / (double)150);
        cmd->dw11.g_rate_ratio_threshold_0 = (unsigned int)((100 - (generic_state->avbr_curracy / (double)30)*(100 - 40)));
        cmd->dw11.g_rate_ratio_threshold_1 = (unsigned int)((100 - (generic_state->avbr_curracy / (double)30)*(100 - 75)));
        cmd->dw12.g_rate_ratio_threshold_2 = (unsigned int)((100 - (generic_state->avbr_curracy / (double)30)*(100 - 97)));
        cmd->dw12.g_rate_ratio_threshold_3 = (unsigned int)((100 + (generic_state->avbr_curracy / (double)30)*(103 - 100)));
        cmd->dw12.g_rate_ratio_threshold_4 = (unsigned int)((100 + (generic_state->avbr_curracy / (double)30)*(125 - 100)));
        cmd->dw12.g_rate_ratio_threshold_5 = (unsigned int)((100 + (generic_state->avbr_curracy / (double)30)*(160 - 100)));

    }
    cmd->dw15.enable_roi = generic_state->brc_roi_enable ;

    memset(&common_param,0,sizeof(common_param));
    common_param.frame_width_in_pixel = generic_state->frame_width_in_pixel;
    common_param.frame_height_in_pixel = generic_state->frame_height_in_pixel;
    common_param.frame_width_in_mbs = generic_state->frame_width_in_mbs;
    common_param.frame_height_in_mbs = generic_state->frame_height_in_mbs;
    common_param.frames_per_100s = generic_state->frames_per_100s;
    common_param.vbv_buffer_size_in_bit = generic_state->vbv_buffer_size_in_bit;
    common_param.target_bit_rate = generic_state->target_bit_rate;

    cmd->dw19.user_max_frame = i965_avc_get_profile_level_max_frame(&common_param,seq_param->level_idc);
    i965_gpe_context_unmap_curbe(gpe_context);

    return;
}

static void
gen9_avc_send_surface_brc_frame_update(VADriverContextP ctx,
                                       struct encode_state *encode_state,
                                       struct i965_gpe_context *gpe_context,
                                       struct intel_encoder_context *encoder_context,
                                       void * param_brc)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct brc_param * param = (struct brc_param *)param_brc ;
    struct i965_gpe_context * gpe_context_mbenc = param->gpe_context_mbenc;


    /* brc history buffer*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_history_buffer,
                                0,
                                avc_ctx->res_brc_history_buffer.size,
                                0,
                                GEN9_AVC_FRAME_BRC_UPDATE_HISTORY_INDEX);

    /* previous pak buffer*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_pre_pak_statistics_output_buffer,
                                0,
                                avc_ctx->res_brc_pre_pak_statistics_output_buffer.size,
                                0,
                                GEN9_AVC_FRAME_BRC_UPDATE_PAK_STATISTICS_OUTPUT_INDEX);

    /* image state command buffer read only*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_image_state_read_buffer,
                                0,
                                avc_ctx->res_brc_image_state_read_buffer.size,
                                0,
                                GEN9_AVC_FRAME_BRC_UPDATE_IMAGE_STATE_READ_INDEX);

    /* image state command buffer write only*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_image_state_write_buffer,
                                0,
                                avc_ctx->res_brc_image_state_write_buffer.size,
                                0,
                                GEN9_AVC_FRAME_BRC_UPDATE_IMAGE_STATE_WRITE_INDEX);

    /*  Mbenc curbe input buffer */
    gen9_add_dri_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    gpe_context_mbenc->dynamic_state.bo,
                                    0,
                                    ALIGN(gpe_context_mbenc->curbe.length, 64),
                                    gpe_context_mbenc->curbe.offset,
                                    GEN9_AVC_FRAME_BRC_UPDATE_MBENC_CURBE_READ_INDEX);
    /* Mbenc curbe output buffer */
    gen9_add_dri_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    gpe_context_mbenc->dynamic_state.bo,
                                    0,
                                    ALIGN(gpe_context_mbenc->curbe.length, 64),
                                    gpe_context_mbenc->curbe.offset,
                                    GEN9_AVC_FRAME_BRC_UPDATE_MBENC_CURBE_WRITE_INDEX);

    /* AVC_ME Distortion 2D surface buffer,input/output. is it res_brc_dist_data_surface*/
    gen9_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &avc_ctx->res_brc_dist_data_surface,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   GEN9_AVC_FRAME_BRC_UPDATE_DISTORTION_INDEX);

    /* BRC const data 2D surface buffer */
    gen9_add_buffer_2d_gpe_surface(ctx,
                                   gpe_context,
                                   &avc_ctx->res_brc_const_data_buffer,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   GEN9_AVC_FRAME_BRC_UPDATE_CONSTANT_DATA_INDEX);

    /* MB statistical data surface*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_mb_status_buffer,
                                0,
                                avc_ctx->res_mb_status_buffer.size,
                                0,
                                GEN9_AVC_FRAME_BRC_UPDATE_MB_STATUS_INDEX);

    return;
}

static VAStatus
gen9_avc_kernel_brc_frame_update(VADriverContextP ctx,
                                 struct encode_state *encode_state,
                                 struct intel_encoder_context *encoder_context)

{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_parameter media_object_param;
    struct gpe_media_object_inline_data media_object_inline_data;
    int media_function = 0;
    int kernel_idx = 0;
    unsigned int mb_const_data_buffer_in_use,mb_qp_buffer_in_use;
    unsigned int brc_enabled = 0;
    unsigned int roi_enable = (generic_state->num_roi > 0)?1:0;
    unsigned int dirty_roi_enable = ((generic_state->dirty_num_roi > 0) && (generic_state->frame_type == SLICE_TYPE_P) && (0));

    /* the following set the mbenc curbe*/
    struct mbenc_param curbe_mbenc_param ;
    struct brc_param curbe_brc_param ;

    mb_const_data_buffer_in_use =
        generic_state->mb_brc_enabled ||
        roi_enable ||
        dirty_roi_enable ||
        avc_state->mb_qp_data_enable ||
        avc_state->rolling_intra_refresh_enable;
    mb_qp_buffer_in_use =
        generic_state->mb_brc_enabled ||
        generic_state->brc_roi_enable ||
        avc_state->mb_qp_data_enable;

    switch(generic_state->kernel_mode)
    {
    case INTEL_ENC_KERNEL_NORMAL :
        {
            kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_NORMAL_I;
            break;
        }
    case INTEL_ENC_KERNEL_PERFORMANCE :
        {
            kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_PERFORMANCE_I;
            break;
        }
    case INTEL_ENC_KERNEL_QUALITY :
        {
            kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_QUALITY_I;
            break;
        }
    default:
        assert(0);

    }

    if(generic_state->frame_type == SLICE_TYPE_P)
    {
        kernel_idx += 1;
    }
    else if(generic_state->frame_type == SLICE_TYPE_B)
    {
        kernel_idx += 2;
    }

    gpe_context = &(avc_ctx->context_mbenc.gpe_contexts[kernel_idx]);
    gen8_gpe_context_init(ctx, gpe_context);

    memset(&curbe_mbenc_param,0,sizeof(struct mbenc_param));

    curbe_mbenc_param.mb_const_data_buffer_in_use = mb_const_data_buffer_in_use;
    curbe_mbenc_param.mb_qp_buffer_in_use = mb_qp_buffer_in_use;
    curbe_mbenc_param.mbenc_i_frame_dist_in_use = 0;
    curbe_mbenc_param.brc_enabled = brc_enabled;
    curbe_mbenc_param.roi_enabled = roi_enable;

    /* set curbe mbenc*/
    generic_ctx->pfn_set_curbe_mbenc(ctx,encode_state,gpe_context,encoder_context,&curbe_mbenc_param);
    avc_state->mbenc_curbe_set_in_brc_update = 1;

    /*begin brc frame update*/
    memset(&curbe_brc_param,0,sizeof(struct brc_param));
    curbe_brc_param.gpe_context_mbenc = gpe_context;
    media_function = INTEL_MEDIA_STATE_BRC_UPDATE;
    kernel_idx = GEN9_AVC_KERNEL_BRC_FRAME_UPDATE;
    gpe_context = &(avc_ctx->context_brc.gpe_contexts[kernel_idx]);
    curbe_brc_param.gpe_context_brc_frame_update = gpe_context;

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);
    /*brc copy ignored*/

    /* set curbe frame update*/
    generic_ctx->pfn_set_curbe_brc_frame_update(ctx,encode_state,gpe_context,encoder_context,&curbe_brc_param);

    /* load brc constant data, is it same as mbenc mb brc constant data? no.*/
    if(avc_state->multi_pre_enable)
    {
        gen9_avc_init_brc_const_data(ctx,encode_state,encoder_context);
    }else
    {
        gen9_avc_init_brc_const_data_old(ctx,encode_state,encoder_context);
    }
    /* image state construct*/
    gen9_avc_set_image_state(ctx,encode_state,encoder_context,&(avc_ctx->res_brc_image_state_read_buffer));
    /* set surface frame mbenc*/
    generic_ctx->pfn_send_brc_frame_update_surface(ctx,encode_state,gpe_context,encoder_context,&curbe_brc_param);


    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    memset(&media_object_inline_data, 0, sizeof(media_object_inline_data));
    media_object_param.pinline_data = &media_object_inline_data;
    media_object_param.inline_size = sizeof(media_object_inline_data);

    gen9_avc_run_kernel_media_object(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_param);

    return VA_STATUS_SUCCESS;
}

static void
gen9_avc_set_curbe_brc_mb_update(VADriverContextP ctx,
                                 struct encode_state *encode_state,
                                 struct i965_gpe_context *gpe_context,
                                 struct intel_encoder_context *encoder_context,
                                 void * param)
{
    gen9_avc_mb_brc_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;

    cmd = i965_gpe_context_map_curbe(gpe_context);
    memset(cmd,0,sizeof(gen9_avc_mb_brc_curbe_data));

    cmd->dw0.cur_frame_type = generic_state->frame_type;
    if(generic_state->brc_roi_enable)
    {
        cmd->dw0.enable_roi = 1;
    }else
    {
        cmd->dw0.enable_roi = 0;
    }

    i965_gpe_context_unmap_curbe(gpe_context);

    return;
}

static void
gen9_avc_send_surface_brc_mb_update(VADriverContextP ctx,
                                    struct encode_state *encode_state,
                                    struct i965_gpe_context *gpe_context,
                                    struct intel_encoder_context *encoder_context,
                                    void * param_mbenc)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;

    /* brc history buffer*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_brc_history_buffer,
                                0,
                                avc_ctx->res_brc_history_buffer.size,
                                0,
                                GEN9_AVC_MB_BRC_UPDATE_HISTORY_INDEX);

    /* MB qp data buffer is it same as res_mbbrc_mb_qp_data_surface*/
    if(generic_state->mb_brc_enabled)
    {
        gen9_add_buffer_2d_gpe_surface(ctx,
                                       gpe_context,
                                       &avc_ctx->res_mbbrc_mb_qp_data_surface,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MB_BRC_UPDATE_MB_QP_INDEX);

    }

    /* BRC roi feature*/
    if(generic_state->brc_roi_enable)
    {
        gen9_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    &avc_ctx->res_mbbrc_roi_surface,
                                    0,
                                    avc_ctx->res_mbbrc_roi_surface.size,
                                    0,
                                    GEN9_AVC_MB_BRC_UPDATE_ROI_INDEX);

    }

    /* MB statistical data surface*/
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                &avc_ctx->res_mb_status_buffer,
                                0,
                                avc_ctx->res_mb_status_buffer.size,
                                0,
                                GEN9_AVC_MB_BRC_UPDATE_MB_STATUS_INDEX);

    return;
}

static VAStatus
gen9_avc_kernel_brc_mb_update(VADriverContextP ctx,
                              struct encode_state *encode_state,
                              struct intel_encoder_context *encoder_context)

{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct gpe_encoder_kernel_walker_parameter kernel_walker_param;
    int media_function = 0;
    int kernel_idx = 0;

    media_function = INTEL_MEDIA_STATE_MB_BRC_UPDATE;
    kernel_idx = GEN9_AVC_KERNEL_BRC_MB_UPDATE;
    gpe_context = &(avc_ctx->context_brc.gpe_contexts[kernel_idx]);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    /* set curbe brc mb update*/
    generic_ctx->pfn_set_curbe_brc_mb_update(ctx,encode_state,gpe_context,encoder_context,NULL);


    /* set surface brc mb update*/
    generic_ctx->pfn_send_brc_mb_update_surface(ctx,encode_state,gpe_context,encoder_context,NULL);


    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    /* the scaling is based on 8x8 blk level */
    kernel_walker_param.resolution_x = (generic_state->frame_width_in_mbs + 1)/2;
    kernel_walker_param.resolution_y = (generic_state->frame_height_in_mbs + 1)/2 ;
    kernel_walker_param.no_dependency = 1;

    i965_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);

    gen9_avc_run_kernel_media_object_walker(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

/*
mbenc kernel related function,it include intra dist kernel
*/
static int
gen9_avc_get_biweight(int dist_scale_factor_ref_id0_list0, unsigned short weighted_bipredidc)
{
    int biweight = 32;      // default value

    /* based on kernel HLD*/
    if (weighted_bipredidc != INTEL_AVC_WP_MODE_IMPLICIT)
    {
        biweight = 32;
    }
    else
    {
        biweight = (dist_scale_factor_ref_id0_list0 + 2) >> 2;

        if (biweight != 16 && biweight != 21 &&
            biweight != 32 && biweight != 43 && biweight != 48)
        {
            biweight = 32;        // If # of B-pics between two refs is more than 3. VME does not support it.
        }
    }

    return biweight;
}

static void
gen9_avc_get_dist_scale_factor(VADriverContextP ctx,
                               struct encode_state *encode_state,
                               struct intel_encoder_context *encoder_context)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;

    int max_num_references;
    VAPictureH264 *curr_pic;
    VAPictureH264 *ref_pic_l0;
    VAPictureH264 *ref_pic_l1;
    int i = 0;
    int tb = 0;
    int td = 0;
    int tx = 0;
    int tmp = 0;
    int poc0 = 0;
    int poc1 = 0;

    max_num_references = pic_param->num_ref_idx_l0_active_minus1 + 1;

    memset(avc_state->dist_scale_factor_list0,0,32*sizeof(unsigned int));
    curr_pic = &pic_param->CurrPic;
    for(i = 0; i < max_num_references; i++)
    {
        ref_pic_l0 = &(slice_param->RefPicList0[i]);

        if((ref_pic_l0->flags & VA_PICTURE_H264_INVALID) ||
           (ref_pic_l0->picture_id == VA_INVALID_SURFACE) )
            break;
        ref_pic_l1 = &(slice_param->RefPicList1[0]);
        if((ref_pic_l0->flags & VA_PICTURE_H264_INVALID) ||
           (ref_pic_l0->picture_id == VA_INVALID_SURFACE) )
            break;

        poc0 = (curr_pic->TopFieldOrderCnt - ref_pic_l0->TopFieldOrderCnt);
        poc1 = (ref_pic_l1->TopFieldOrderCnt - ref_pic_l0->TopFieldOrderCnt);
        CLIP(poc0,-128,127);
        CLIP(poc1,-128,127);
        tb = poc0;
        td = poc1;

        if(td == 0)
        {
            td = 1;
        }
        tmp = (td/2 > 0)?(td/2):(-(td/2));
        tx = (16384 + tmp)/td ;
        tmp = (tb*tx+32)>>6;
        CLIP(tmp,-1024,1023);
        avc_state->dist_scale_factor_list0[i] = tmp;
    }
    return;
}

static unsigned int
gen9_avc_get_qp_from_ref_list(VADriverContextP ctx,
                              VAEncSliceParameterBufferH264 *slice_param,
                              int list,
                              int ref_frame_idx)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct object_surface *obj_surface;
    struct gen9_surface_avc *avc_priv_surface;
    VASurfaceID surface_id;

    assert(slice_param);
    assert(list < 2);

    if(list == 0)
    {
        if(ref_frame_idx < slice_param->num_ref_idx_l0_active_minus1 + 1)
            surface_id = slice_param->RefPicList0[ref_frame_idx].picture_id;
        else
            return 0;
    }else
    {
        if(ref_frame_idx < slice_param->num_ref_idx_l1_active_minus1 + 1)
            surface_id = slice_param->RefPicList1[ref_frame_idx].picture_id;
        else
            return 0;
    }
    obj_surface = SURFACE(surface_id);
    if(obj_surface && obj_surface->private_data)
    {
        avc_priv_surface = obj_surface->private_data;
        return avc_priv_surface->qp_value;
    }else
    {
        return 0;
    }
}

static void
gen9_avc_load_mb_brc_const_data(VADriverContextP ctx,
                        struct encode_state *encode_state,
                        struct intel_encoder_context *encoder_context)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;

    struct i965_gpe_resource *gpe_resource = NULL;
    unsigned int * data =NULL;
    unsigned int * data_tmp = NULL;
    unsigned int size = 16 * 52;
    unsigned int table_idx = 0;
    unsigned int block_based_skip_enable = avc_state->block_based_skip_enable;
    unsigned int transform_8x8_mode_flag = pic_param->pic_fields.bits.transform_8x8_mode_flag;
    int i = 0;

    gpe_resource = &(avc_ctx->res_mbbrc_const_data_buffer);
    assert(gpe_resource);
    data = i965_map_gpe_resource(gpe_resource);
    assert(data);

    table_idx = slice_type_kernel[generic_state->frame_type];

    memcpy(data,gen9_avc_mb_brc_const_data[table_idx][0],size*sizeof(unsigned int));

    data_tmp = data;

    switch(generic_state->frame_type)
    {
    case SLICE_TYPE_I:
        for(i = 0; i < 52 ; i++)
        {
            if(avc_state->old_mode_cost_enable)
                *data = (unsigned int)gen9_avc_old_intra_mode_cost[i];
            data += 16;
        }
        break;
    case SLICE_TYPE_P:
    case SLICE_TYPE_B:
        for(i = 0; i < 52 ; i++)
        {
            if(generic_state->frame_type == SLICE_TYPE_P)
            {
                if(avc_state->skip_bias_adjustment_enable)
                    *(data + 3) = (unsigned int)gen9_avc_mv_cost_p_skip_adjustment[i];
            }
            if(avc_state->non_ftq_skip_threshold_lut_input_enable)
            {
                *(data + 9) = (unsigned int)i965_avc_calc_skip_value(block_based_skip_enable,transform_8x8_mode_flag,avc_state->non_ftq_skip_threshold_lut[i]);
            }else if(generic_state->frame_type == SLICE_TYPE_P)
            {
                *(data + 9) = (unsigned int)gen9_avc_skip_value_p[block_based_skip_enable][transform_8x8_mode_flag][i];
            }else
            {
                *(data + 9) = (unsigned int)gen9_avc_skip_value_b[block_based_skip_enable][transform_8x8_mode_flag][i];
            }

            if(avc_state->adaptive_intra_scaling_enable)
            {
                *(data + 10) = (unsigned int)gen9_avc_adaptive_intra_scaling_factor[i];
            }else
            {
                *(data + 10) = (unsigned int)gen9_avc_intra_scaling_factor[i];

            }
            data += 16;

        }
        break;
    default:
        assert(0);
    }

    data = data_tmp;
    for(i = 0; i < 52 ; i++)
    {
        if(avc_state->ftq_skip_threshold_lut_input_enable)
        {
            *(data + 6) =  (avc_state->ftq_skip_threshold_lut[i] |
                (avc_state->ftq_skip_threshold_lut[i] <<16) |
                (avc_state->ftq_skip_threshold_lut[i] <<24) );
            *(data + 7) =  (avc_state->ftq_skip_threshold_lut[i] |
                (avc_state->ftq_skip_threshold_lut[i] <<8) |
                (avc_state->ftq_skip_threshold_lut[i] <<16) |
                (avc_state->ftq_skip_threshold_lut[i] <<24) );
        }

        if(avc_state->kernel_trellis_enable)
        {
            *(data + 11) = (unsigned int)avc_state->lamda_value_lut[i][0];
            *(data + 12) = (unsigned int)avc_state->lamda_value_lut[i][1];

        }
        data += 16;

    }
    i965_unmap_gpe_resource(gpe_resource);
}

static void
gen9_avc_set_curbe_mbenc(VADriverContextP ctx,
                         struct encode_state *encode_state,
                         struct i965_gpe_context *gpe_context,
                         struct intel_encoder_context *encoder_context,
                         void * param)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    gen9_avc_mbenc_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    VAEncPictureParameterBufferH264  *pic_param = avc_state->pic_param;
    VASurfaceID surface_id;
    struct object_surface *obj_surface;

    struct mbenc_param * curbe_param = (struct mbenc_param *)param ;
    unsigned char qp = 0;
    unsigned char me_method = 0;
    unsigned int mbenc_i_frame_dist_in_use = curbe_param->mbenc_i_frame_dist_in_use;
    unsigned int table_idx = 0;

    unsigned int preset = generic_state->preset;
    me_method = (generic_state->frame_type == SLICE_TYPE_B)? gen9_avc_b_me_method[preset]:gen9_avc_p_me_method[preset];
    qp = pic_param->pic_init_qp + slice_param->slice_qp_delta;

    cmd = (gen9_avc_mbenc_curbe_data *)i965_gpe_context_map_curbe(gpe_context);
    memset(cmd,0,sizeof(gen9_avc_mbenc_curbe_data));

    if(mbenc_i_frame_dist_in_use)
    {
        memcpy(cmd,gen9_avc_mbenc_curbe_i_frame_dist_init_data,sizeof(gen9_avc_mbenc_curbe_data));

    }else
    {
        switch(generic_state->frame_type)
        {
        case SLICE_TYPE_I:
            memcpy(cmd,gen9_avc_mbenc_curbe_normal_i_frame_init_data,sizeof(gen9_avc_mbenc_curbe_data));
            break;
        case SLICE_TYPE_P:
            memcpy(cmd,gen9_avc_mbenc_curbe_normal_p_frame_init_data,sizeof(gen9_avc_mbenc_curbe_data));
            break;
        case SLICE_TYPE_B:
            memcpy(cmd,gen9_avc_mbenc_curbe_normal_b_frame_init_data,sizeof(gen9_avc_mbenc_curbe_data));
            break;
        default:
            assert(0);
        }

    }
    cmd->dw0.adaptive_enable = gen9_avc_enable_adaptive_search[preset];
    cmd->dw37.adaptive_enable = gen9_avc_enable_adaptive_search[preset];
    cmd->dw0.t8x8_flag_for_inter_enable = pic_param->pic_fields.bits.transform_8x8_mode_flag;
    cmd->dw37.t8x8_flag_for_inter_enable = pic_param->pic_fields.bits.transform_8x8_mode_flag;

    cmd->dw2.max_len_sp = gen9_avc_max_len_sp[preset];
    cmd->dw38.max_len_sp = 0;

    cmd->dw3.src_access = 0;
    cmd->dw3.ref_access = 0;

    if(avc_state->ftq_enable && (generic_state->frame_type != SLICE_TYPE_I))
    {
        if(avc_state->ftq_override)
        {
            cmd->dw3.ftq_enable = avc_state->ftq_enable;

        }else
        {
            if(generic_state->frame_type == SLICE_TYPE_P)
            {
                cmd->dw3.ftq_enable = gen9_avc_max_ftq_based_skip[preset] & 0x01;

            }else
            {
                cmd->dw3.ftq_enable = (gen9_avc_max_ftq_based_skip[preset] >> 1) & 0x01;
            }
        }
    }else
    {
        cmd->dw3.ftq_enable = 0;
    }

    if(avc_state->disable_sub_mb_partion)
        cmd->dw3.sub_mb_part_mask = 0x7;

    if(mbenc_i_frame_dist_in_use)
    {
        cmd->dw2.pitch_width = generic_state->downscaled_width_4x_in_mb;
        cmd->dw4.picture_height_minus1 = generic_state->downscaled_height_4x_in_mb - 1;
        cmd->dw5.slice_mb_height = (avc_state->slice_height + 4 - 1)/4;
        cmd->dw6.batch_buffer_end = 0;
        cmd->dw31.intra_compute_type = 1;

    }else
    {
        cmd->dw2.pitch_width = generic_state->frame_width_in_mbs;
        cmd->dw4.picture_height_minus1 = generic_state->frame_height_in_mbs - 1;
        cmd->dw5.slice_mb_height = (avc_state->arbitrary_num_mbs_in_slice)?generic_state->frame_height_in_mbs:avc_state->slice_height;

        {
            memcpy(&(cmd->dw8),gen9_avc_mode_mv_cost_table[slice_type_kernel[generic_state->frame_type]][qp],8*sizeof(unsigned int));
            if((generic_state->frame_type == SLICE_TYPE_I) && avc_state->old_mode_cost_enable)
            {
                //cmd->dw8 = gen9_avc_old_intra_mode_cost[qp];
            }else if(avc_state->skip_bias_adjustment_enable)
            {
                /* Load different MvCost for P picture when SkipBiasAdjustment is enabled
                // No need to check for P picture as the flag is only enabled for P picture */
                cmd->dw11.value = gen9_avc_mv_cost_p_skip_adjustment[qp];

            }
        }

        table_idx = (generic_state->frame_type == SLICE_TYPE_B)?1:0;
        memcpy(&(cmd->dw16),table_enc_search_path[table_idx][me_method],16*sizeof(unsigned int));
    }
    cmd->dw4.enable_fbr_bypass = avc_state->fbr_bypass_enable;
    cmd->dw4.enable_intra_cost_scaling_for_static_frame = avc_state->sfd_enable && generic_state->hme_enabled;
    cmd->dw4.field_parity_flag = 0;//bottom field
    cmd->dw4.enable_cur_fld_idr = 0;//field realted
    cmd->dw4.contrained_intra_pred_flag = pic_param->pic_fields.bits.constrained_intra_pred_flag;
    cmd->dw4.hme_enable = generic_state->hme_enabled;
    cmd->dw4.picture_type = slice_type_kernel[generic_state->frame_type];
    cmd->dw4.use_actual_ref_qp_value = generic_state->hme_enabled && (gen9_avc_mr_disable_qp_check[preset] == 0);


    cmd->dw7.intra_part_mask = pic_param->pic_fields.bits.transform_8x8_mode_flag?0:0x02;
    cmd->dw7.src_field_polarity = 0;//field related

    /*ftq_skip_threshold_lut set,dw14 /15*/

    /*r5 disable NonFTQSkipThresholdLUT*/
    if(generic_state->frame_type == SLICE_TYPE_P)
    {
        cmd->dw32.skip_val = gen9_avc_skip_value_p[avc_state->block_based_skip_enable][pic_param->pic_fields.bits.transform_8x8_mode_flag][qp];

    }else if(generic_state->frame_type == SLICE_TYPE_B)
    {
        cmd->dw32.skip_val = gen9_avc_skip_value_b[avc_state->block_based_skip_enable][pic_param->pic_fields.bits.transform_8x8_mode_flag][qp];

    }

    cmd->dw13.qp_prime_y = qp;
    cmd->dw13.qp_prime_cb = qp;
    cmd->dw13.qp_prime_cr = qp;
    cmd->dw13.target_size_in_word = 0xff;//hardcode for brc disable


    if((generic_state->frame_type != SLICE_TYPE_I)&& avc_state->multi_pre_enable)
    {
        switch(gen9_avc_multi_pred[preset])
        {
        case 0:
            cmd->dw32.mult_pred_l0_disable = 128;
            cmd->dw32.mult_pred_l1_disable = 128;
            break;
        case 1:
            cmd->dw32.mult_pred_l0_disable = (generic_state->frame_type == SLICE_TYPE_P)?1:128;
            cmd->dw32.mult_pred_l1_disable = 128;
            break;
        case 2:
            cmd->dw32.mult_pred_l0_disable = (generic_state->frame_type == SLICE_TYPE_B)?1:128;
            cmd->dw32.mult_pred_l1_disable = (generic_state->frame_type == SLICE_TYPE_B)?1:128;
            break;
        case 3:
            cmd->dw32.mult_pred_l0_disable = 1;
            cmd->dw32.mult_pred_l1_disable = (generic_state->frame_type == SLICE_TYPE_B)?1:128;
            break;

        }

    }else
    {
        cmd->dw32.mult_pred_l0_disable = 128;
        cmd->dw32.mult_pred_l1_disable = 128;
    }

    /*field setting for dw33 34, ignored*/

    if(avc_state->adaptive_transform_decision_enable)
    {
        if(generic_state->frame_type != SLICE_TYPE_I)
        {
            cmd->dw34.enable_adaptive_tx_decision = 1;
        }

        cmd->dw58.mb_texture_threshold = 1024;
        cmd->dw58.tx_decision_threshold = 128;
    }


    if(generic_state->frame_type == SLICE_TYPE_B)
    {
        cmd->dw34.list1_ref_id0_frm_field_parity = 0; //frame only
        cmd->dw34.list1_ref_id0_frm_field_parity = 0;
        cmd->dw34.b_direct_mode = slice_param->direct_spatial_mv_pred_flag;
    }
    cmd->dw34.b_original_bff = 0; //frame only
    cmd->dw34.enable_mb_flatness_check_optimization = avc_state->flatness_check_enable;
    cmd->dw34.roi_enable_flag = curbe_param->roi_enabled;
    cmd->dw34.mad_enable_falg = avc_state->mad_enable;
    cmd->dw34.mb_brc_enable = avc_state->mb_qp_data_enable || generic_state->mb_brc_enabled;
    cmd->dw34.arbitray_num_mbs_per_slice = avc_state->arbitrary_num_mbs_in_slice;
    cmd->dw34.force_non_skip_check = avc_state->mb_disable_skip_map_enable;

    if(cmd->dw34.force_non_skip_check)
    {
       cmd->dw34.disable_enc_skip_check = avc_state->skip_check_disable;
    }

    cmd->dw36.check_all_fractional_enable = avc_state->caf_enable;
    cmd->dw38.ref_threshold = 400;
    cmd->dw39.hme_ref_windows_comb_threshold = (generic_state->frame_type == SLICE_TYPE_B)?gen9_avc_hme_b_combine_len[preset]:gen9_avc_hme_combine_len[preset];

    /* Default:2 used for MBBRC (MB QP Surface width and height are 4x downscaled picture in MB unit * 4  bytes)
       0 used for MBQP data surface (MB QP Surface width and height are same as the input picture size in MB unit * 1bytes)
       starting GEN9, BRC use split kernel, MB QP surface is same size as input picture */
    cmd->dw47.mb_qp_read_factor = (avc_state->mb_qp_data_enable || generic_state->mb_brc_enabled)?0:2;

    if(mbenc_i_frame_dist_in_use)
    {
        cmd->dw13.qp_prime_y = 0;
        cmd->dw13.qp_prime_cb = 0;
        cmd->dw13.qp_prime_cr = 0;
        cmd->dw33.intra_16x16_nondc_penalty = 0;
        cmd->dw33.intra_8x8_nondc_penalty = 0;
        cmd->dw33.intra_4x4_nondc_penalty = 0;

    }
    if(cmd->dw4.use_actual_ref_qp_value)
    {
        cmd->dw44.actual_qp_value_for_ref_id0_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,0);
        cmd->dw44.actual_qp_value_for_ref_id1_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,1);
        cmd->dw44.actual_qp_value_for_ref_id2_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,2);
        cmd->dw44.actual_qp_value_for_ref_id3_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,3);
        cmd->dw45.actual_qp_value_for_ref_id4_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,4);
        cmd->dw45.actual_qp_value_for_ref_id5_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,5);
        cmd->dw45.actual_qp_value_for_ref_id6_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,6);
        cmd->dw45.actual_qp_value_for_ref_id7_list0 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,0,7);
        cmd->dw46.actual_qp_value_for_ref_id0_list1 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,1,0);
        cmd->dw46.actual_qp_value_for_ref_id1_list1 =  gen9_avc_get_qp_from_ref_list(ctx,slice_param,1,1);
    }

    table_idx = slice_type_kernel[generic_state->frame_type];
    cmd->dw46.ref_cost = gen9_avc_ref_cost[table_idx][qp];

    if(generic_state->frame_type == SLICE_TYPE_I)
    {
        cmd->dw0.skip_mode_enable = 0;
        cmd->dw37.skip_mode_enable = 0;
        cmd->dw36.hme_combine_overlap = 0;
        cmd->dw47.intra_cost_sf = 16;
        cmd->dw34.enable_direct_bias_adjustment = 0;
        cmd->dw34.enable_global_motion_bias_adjustment = 0;

    }else if(generic_state->frame_type == SLICE_TYPE_P)
    {
        cmd->dw1.max_num_mvs = i965_avc_get_max_mv_per_2mb(avc_state->seq_param->level_idc)/2;
        cmd->dw3.bme_disable_fbr = 1;
        cmd->dw5.ref_width = gen9_avc_search_x[preset];
        cmd->dw5.ref_height = gen9_avc_search_y[preset];
        cmd->dw7.non_skip_zmv_added = 1;
        cmd->dw7.non_skip_mode_added = 1;
        cmd->dw7.skip_center_mask = 1;
        cmd->dw47.intra_cost_sf = (avc_state->adaptive_intra_scaling_enable)?gen9_avc_adaptive_intra_scaling_factor[qp]:gen9_avc_intra_scaling_factor[qp];
        cmd->dw47.max_vmv_r = i965_avc_get_max_mv_len(avc_state->seq_param->level_idc) * 4;//frame onlys
        cmd->dw36.hme_combine_overlap = 1;
        cmd->dw36.num_ref_idx_l0_minus_one = (avc_state->multi_pre_enable)?slice_param->num_ref_idx_l0_active_minus1:0;
        cmd->dw39.ref_width = gen9_avc_search_x[preset];
        cmd->dw39.ref_height = gen9_avc_search_y[preset];
        cmd->dw34.enable_direct_bias_adjustment = 0;
        cmd->dw34.enable_global_motion_bias_adjustment = avc_state->global_motion_bias_adjustment_enable;
        if(avc_state->global_motion_bias_adjustment_enable)
            cmd->dw59.hme_mv_cost_scaling_factor = avc_state->hme_mv_cost_scaling_factor;

    }else
    {
        cmd->dw1.max_num_mvs = i965_avc_get_max_mv_per_2mb(avc_state->seq_param->level_idc)/2;
        cmd->dw1.bi_weight = avc_state->bi_weight;
        cmd->dw3.search_ctrl = 7;
        cmd->dw3.skip_type = 1;
        cmd->dw5.ref_width = gen9_avc_b_search_x[preset];
        cmd->dw5.ref_height = gen9_avc_b_search_y[preset];
        cmd->dw7.skip_center_mask = 0xff;
        cmd->dw47.intra_cost_sf = (avc_state->adaptive_intra_scaling_enable)?gen9_avc_adaptive_intra_scaling_factor[qp]:gen9_avc_intra_scaling_factor[qp];
        cmd->dw47.max_vmv_r = i965_avc_get_max_mv_len(avc_state->seq_param->level_idc) * 4;//frame only
        cmd->dw36.hme_combine_overlap = 1;
        surface_id = slice_param->RefPicList1[0].picture_id;
        obj_surface = SURFACE(surface_id);
        if (!obj_surface)
        {
            WARN_ONCE("Invalid backward reference frame\n");
            return;
        }
        cmd->dw36.is_fwd_frame_short_term_ref = !!( slice_param->RefPicList1[0].flags & VA_PICTURE_H264_SHORT_TERM_REFERENCE);

        cmd->dw36.num_ref_idx_l0_minus_one = (avc_state->multi_pre_enable)?slice_param->num_ref_idx_l0_active_minus1:0;
        cmd->dw36.num_ref_idx_l1_minus_one = (avc_state->multi_pre_enable)?slice_param->num_ref_idx_l1_active_minus1:0;
        cmd->dw39.ref_width = gen9_avc_b_search_x[preset];
        cmd->dw39.ref_height = gen9_avc_b_search_y[preset];
        cmd->dw40.dist_scale_factor_ref_id0_list0 = avc_state->dist_scale_factor_list0[0];
        cmd->dw40.dist_scale_factor_ref_id1_list0 = avc_state->dist_scale_factor_list0[1];
        cmd->dw41.dist_scale_factor_ref_id2_list0 = avc_state->dist_scale_factor_list0[2];
        cmd->dw41.dist_scale_factor_ref_id3_list0 = avc_state->dist_scale_factor_list0[3];
        cmd->dw42.dist_scale_factor_ref_id4_list0 = avc_state->dist_scale_factor_list0[4];
        cmd->dw42.dist_scale_factor_ref_id5_list0 = avc_state->dist_scale_factor_list0[5];
        cmd->dw43.dist_scale_factor_ref_id6_list0 = avc_state->dist_scale_factor_list0[6];
        cmd->dw43.dist_scale_factor_ref_id7_list0 = avc_state->dist_scale_factor_list0[7];

        cmd->dw34.enable_direct_bias_adjustment = avc_state->direct_bias_adjustment_enable;
        if(cmd->dw34.enable_direct_bias_adjustment)
        {
            cmd->dw7.non_skip_zmv_added = 1;
            cmd->dw7.non_skip_mode_added = 1;
        }

        cmd->dw34.enable_global_motion_bias_adjustment = avc_state->global_motion_bias_adjustment_enable;
        if(avc_state->global_motion_bias_adjustment_enable)
            cmd->dw59.hme_mv_cost_scaling_factor = avc_state->hme_mv_cost_scaling_factor;

    }

    avc_state->block_based_skip_enable = cmd->dw3.block_based_skip_enable;

    if(avc_state->rolling_intra_refresh_enable)
    {
        /*by now disable it*/
        cmd->dw34.widi_intra_refresh_en = avc_state->rolling_intra_refresh_enable;

    }else
    {
        cmd->dw34.widi_intra_refresh_en = 0;
    }

    cmd->dw34.enable_per_mb_static_check = avc_state->sfd_enable && generic_state->hme_enabled;
    cmd->dw34.enable_adaptive_search_window_size = avc_state->adaptive_search_window_enable;

    /*roi set disable by now. 49-56*/
    if(curbe_param->roi_enabled)
    {
        cmd->dw49.roi_1_x_left   = generic_state->roi[0].left;
        cmd->dw49.roi_1_y_top    = generic_state->roi[0].top;
        cmd->dw50.roi_1_x_right  = generic_state->roi[0].right;
        cmd->dw50.roi_1_y_bottom = generic_state->roi[0].bottom;

        cmd->dw51.roi_2_x_left   = generic_state->roi[1].left;
        cmd->dw51.roi_2_y_top    = generic_state->roi[1].top;
        cmd->dw52.roi_2_x_right  = generic_state->roi[1].right;
        cmd->dw52.roi_2_y_bottom = generic_state->roi[1].bottom;

        cmd->dw53.roi_3_x_left   = generic_state->roi[2].left;
        cmd->dw53.roi_3_y_top    = generic_state->roi[2].top;
        cmd->dw54.roi_3_x_right  = generic_state->roi[2].right;
        cmd->dw54.roi_3_y_bottom = generic_state->roi[2].bottom;

        cmd->dw55.roi_4_x_left   = generic_state->roi[3].left;
        cmd->dw55.roi_4_y_top    = generic_state->roi[3].top;
        cmd->dw56.roi_4_x_right  = generic_state->roi[3].right;
        cmd->dw56.roi_4_y_bottom = generic_state->roi[3].bottom;

        if(!generic_state->brc_enabled)
        {
            char tmp = 0;
            tmp = generic_state->roi[0].value;
            CLIP(tmp,-qp,52-qp);
            cmd->dw57.roi_1_dqp_prime_y = tmp;
            tmp = generic_state->roi[1].value;
            CLIP(tmp,-qp,52-qp);
            cmd->dw57.roi_2_dqp_prime_y = tmp;
            tmp = generic_state->roi[2].value;
            CLIP(tmp,-qp,52-qp);
            cmd->dw57.roi_3_dqp_prime_y = tmp;
            tmp = generic_state->roi[3].value;
            CLIP(tmp,-qp,52-qp);
            cmd->dw57.roi_4_dqp_prime_y = tmp;
        }else
        {
            cmd->dw34.roi_enable_flag = 0;
        }
    }

    cmd->dw64.mb_data_surf_index = GEN9_AVC_MBENC_MFC_AVC_PAK_OBJ_INDEX;
    cmd->dw65.mv_data_surf_index = GEN9_AVC_MBENC_IND_MV_DATA_INDEX;
    cmd->dw66.i_dist_surf_index = GEN9_AVC_MBENC_BRC_DISTORTION_INDEX;
    cmd->dw67.src_y_surf_index = GEN9_AVC_MBENC_CURR_Y_INDEX;
    cmd->dw68.mb_specific_data_surf_index = GEN9_AVC_MBENC_MB_SPECIFIC_DATA_INDEX;
    cmd->dw69.aux_vme_out_surf_index = GEN9_AVC_MBENC_AUX_VME_OUT_INDEX;
    cmd->dw70.curr_ref_pic_sel_surf_index = GEN9_AVC_MBENC_REFPICSELECT_L0_INDEX;
    cmd->dw71.hme_mv_pred_fwd_bwd_surf_index = GEN9_AVC_MBENC_MV_DATA_FROM_ME_INDEX;
    cmd->dw72.hme_dist_surf_index = GEN9_AVC_MBENC_4XME_DISTORTION_INDEX;
    cmd->dw73.slice_map_surf_index = GEN9_AVC_MBENC_SLICEMAP_DATA_INDEX;
    cmd->dw74.fwd_frm_mb_data_surf_index = GEN9_AVC_MBENC_FWD_MB_DATA_INDEX;
    cmd->dw75.fwd_frm_mv_surf_index = GEN9_AVC_MBENC_FWD_MV_DATA_INDEX;
    cmd->dw76.mb_qp_buffer = GEN9_AVC_MBENC_MBQP_INDEX;
    cmd->dw77.mb_brc_lut = GEN9_AVC_MBENC_MBBRC_CONST_DATA_INDEX;
    cmd->dw78.vme_inter_prediction_surf_index = GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_0_INDEX;
    cmd->dw79.vme_inter_prediction_mr_surf_index = GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_1_INDEX;
    cmd->dw80.mb_stats_surf_index = GEN9_AVC_MBENC_MB_STATS_INDEX;
    cmd->dw81.mad_surf_index = GEN9_AVC_MBENC_MAD_DATA_INDEX;
    cmd->dw82.force_non_skip_mb_map_surface = GEN9_AVC_MBENC_FORCE_NONSKIP_MB_MAP_INDEX;
    cmd->dw83.widi_wa_surf_index = GEN9_AVC_MBENC_WIDI_WA_INDEX;
    cmd->dw84.brc_curbe_surf_index = GEN9_AVC_MBENC_BRC_CURBE_DATA_INDEX;
    cmd->dw85.static_detection_cost_table_index = GEN9_AVC_MBENC_SFD_COST_TABLE_INDEX;

    i965_gpe_context_unmap_curbe(gpe_context);

    return;
}

static void
gen9_avc_send_surface_mbenc(VADriverContextP ctx,
                            struct encode_state *encode_state,
                            struct i965_gpe_context *gpe_context,
                            struct intel_encoder_context *encoder_context,
                            void * param_mbenc)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    struct object_surface *obj_surface;
    struct gen9_surface_avc *avc_priv_surface;
    struct i965_gpe_resource *gpe_resource;
    struct mbenc_param * param = (struct mbenc_param *)param_mbenc ;
     VASurfaceID surface_id;
    unsigned int mbenc_i_frame_dist_in_use = param->mbenc_i_frame_dist_in_use;
    unsigned int size = 0;
    unsigned int w_mb = generic_state->frame_width_in_mbs;
    unsigned int h_mb = generic_state->frame_height_in_mbs;
    int i = 0;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];

    obj_surface = encode_state->reconstructed_object;

    if (!obj_surface || !obj_surface->private_data)
        return;
    avc_priv_surface = obj_surface->private_data;

    /*pak obj command buffer output*/
    size = w_mb * h_mb * 16 * 4;
    gpe_resource = &avc_priv_surface->res_mb_code_surface;
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                gpe_resource,
                                0,
                                size / 4,
                                0,
                                GEN9_AVC_MBENC_MFC_AVC_PAK_OBJ_INDEX);

    /*mv data buffer output*/
    size = w_mb * h_mb * 32 * 4;
    gpe_resource = &avc_priv_surface->res_mv_data_surface;
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                gpe_resource,
                                0,
                                size / 4,
                                0,
                                GEN9_AVC_MBENC_IND_MV_DATA_INDEX);

    /*input current  YUV surface, current input Y/UV object*/
    if(mbenc_i_frame_dist_in_use)
    {
        obj_surface = encode_state->reconstructed_object;
        if (!obj_surface || !obj_surface->private_data)
            return;
        avc_priv_surface = obj_surface->private_data;
        obj_surface = avc_priv_surface->scaled_4x_surface_obj;
    }else
    {
        obj_surface = encode_state->input_yuv_object;
    }
    gen9_add_2d_gpe_surface(ctx,
                            gpe_context,
                            obj_surface,
                            0,
                            1,
                            I965_SURFACEFORMAT_R8_UNORM,
                            GEN9_AVC_MBENC_CURR_Y_INDEX);

    gen9_add_2d_gpe_surface(ctx,
                            gpe_context,
                            obj_surface,
                            1,
                            1,
                            I965_SURFACEFORMAT_R16_UINT,
                            GEN9_AVC_MBENC_CURR_UV_INDEX);

    if(generic_state->hme_enabled)
    {
        /*memv input 4x*/
        gpe_resource = &(avc_ctx->s4x_memv_data_buffer);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_MV_DATA_FROM_ME_INDEX);
        /* memv distortion input*/
        gpe_resource = &(avc_ctx->s4x_memv_distortion_buffer);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_4XME_DISTORTION_INDEX);
    }

    /*mbbrc const data_buffer*/
    if(param->mb_const_data_buffer_in_use)
    {
        size = 16 * 52 * sizeof(unsigned int);
        gpe_resource = &avc_ctx->res_mbbrc_const_data_buffer;
        gen9_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    gpe_resource,
                                    0,
                                    size / 4,
                                    0,
                                    GEN9_AVC_MBENC_MBBRC_CONST_DATA_INDEX);

    }

    /*mb qp data_buffer*/
    if(param->mb_qp_buffer_in_use)
    {
        if(avc_state->mb_qp_data_enable)
            gpe_resource = &(avc_ctx->res_mb_qp_data_surface);
        else
            gpe_resource = &(avc_ctx->res_mbbrc_mb_qp_data_surface);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_MBQP_INDEX);
    }

    /*input current  YUV surface, current input Y/UV object*/
    if(mbenc_i_frame_dist_in_use)
    {
        obj_surface = encode_state->reconstructed_object;
        if (!obj_surface || !obj_surface->private_data)
            return;
        avc_priv_surface = obj_surface->private_data;
        obj_surface = avc_priv_surface->scaled_4x_surface_obj;
    }else
    {
        obj_surface = encode_state->input_yuv_object;
    }
    gen9_add_adv_gpe_surface(ctx, gpe_context,
                             obj_surface,
                             GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_0_INDEX);
    /*input ref YUV surface*/
    for(i = 0; i < slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
    {
        surface_id = slice_param->RefPicList0[i].picture_id;
        obj_surface = SURFACE(surface_id);
        if (!obj_surface || !obj_surface->private_data)
            break;

        gen9_add_adv_gpe_surface(ctx, gpe_context,
                                 obj_surface,
                                 GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_0_INDEX+i*2 + 1);
    }
    /*input current  YUV surface, current input Y/UV object*/
    if(mbenc_i_frame_dist_in_use)
    {
        obj_surface = encode_state->reconstructed_object;
        if (!obj_surface || !obj_surface->private_data)
            return;
        avc_priv_surface = obj_surface->private_data;
        obj_surface = avc_priv_surface->scaled_4x_surface_obj;
    }else
    {
        obj_surface = encode_state->input_yuv_object;
    }
    gen9_add_adv_gpe_surface(ctx, gpe_context,
                             obj_surface,
                             GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_1_INDEX);

    for(i = 0; i < slice_param->num_ref_idx_l1_active_minus1 + 1; i++)
    {
        if(i > 0) break;// only  one ref supported here for B frame
        surface_id = slice_param->RefPicList1[i].picture_id;
        obj_surface = SURFACE(surface_id);
        if (!obj_surface || !obj_surface->private_data)
            break;

        gen9_add_adv_gpe_surface(ctx, gpe_context,
                                 obj_surface,
                                 GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_1_INDEX+i*2 + 1);
        gen9_add_adv_gpe_surface(ctx, gpe_context,
                                 obj_surface,
                                 GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_0_INDEX+i*2 + 2);
        if(i == 0)
        {
            avc_priv_surface = obj_surface->private_data;
            /*pak obj command buffer output(mb code)*/
            size = w_mb * h_mb * 16 * 4;
            gpe_resource = &avc_priv_surface->res_mb_code_surface;
            gen9_add_buffer_gpe_surface(ctx,
                                        gpe_context,
                                        gpe_resource,
                                        0,
                                        size / 4,
                                        0,
                                        GEN9_AVC_MBENC_FWD_MB_DATA_INDEX);

            /*mv data buffer output*/
            size = w_mb * h_mb * 32 * 4;
            gpe_resource = &avc_priv_surface->res_mv_data_surface;
            gen9_add_buffer_gpe_surface(ctx,
                                        gpe_context,
                                        gpe_resource,
                                        0,
                                        size / 4,
                                        0,
                                        GEN9_AVC_MBENC_FWD_MV_DATA_INDEX);

        }

        if( i < INTEL_AVC_MAX_BWD_REF_NUM)
        {
            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     obj_surface,
                                     GEN9_AVC_MBENC_VME_INTER_PRED_CURR_PIC_IDX_1_INDEX+i*2 + 1 + INTEL_AVC_MAX_BWD_REF_NUM);
        }

    }

    /* BRC distortion data buffer for I frame*/
    if(mbenc_i_frame_dist_in_use)
    {
        gpe_resource = &(avc_ctx->res_brc_dist_data_surface);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_BRC_DISTORTION_INDEX);
    }

    /* as ref frame ,update later RefPicSelect of Current Picture*/
    obj_surface = encode_state->reconstructed_object;
    avc_priv_surface = obj_surface->private_data;
    if(avc_state->ref_pic_select_list_supported && avc_priv_surface->is_as_ref)
    {
        gpe_resource = &(avc_priv_surface->res_ref_pic_select_surface);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_REFPICSELECT_L0_INDEX);

    }

    if(param->mb_vproc_stats_enable)
    {
        /*mb status buffer input*/
        size = w_mb * h_mb * 16 * 4;
        gpe_resource = &(avc_ctx->res_mb_status_buffer);
        gen9_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    gpe_resource,
                                    0,
                                    size / 4,
                                    0,
                                    GEN9_AVC_MBENC_MB_STATS_INDEX);

    }else if(avc_state->flatness_check_enable)
    {

        gpe_resource = &(avc_ctx->res_flatness_check_surface);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_MB_STATS_INDEX);
    }

    if(param->mad_enable)
    {
        /*mad buffer input*/
        size = 4;
        gpe_resource = &(avc_ctx->res_mad_data_buffer);
        gen9_add_buffer_gpe_surface(ctx,
                                    gpe_context,
                                    gpe_resource,
                                    0,
                                    size / 4,
                                    0,
                                    GEN9_AVC_MBENC_MAD_DATA_INDEX);
        i965_zero_gpe_resource(gpe_resource);
    }

    /*brc updated mbenc curbe data buffer,it is ignored*/

    /*artitratry num mbs in slice*/
    if(avc_state->arbitrary_num_mbs_in_slice)
    {
        /*slice surface input*/
        gpe_resource = &(avc_ctx->res_mbenc_slice_map_surface);
        gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                       gpe_resource,
                                       1,
                                       I965_SURFACEFORMAT_R8_UNORM,
                                       GEN9_AVC_MBENC_SLICEMAP_DATA_INDEX);
    }

    /* BRC distortion data buffer for I frame */
    if(!mbenc_i_frame_dist_in_use)
    {
        if(avc_state->mb_disable_skip_map_enable)
        {
            gpe_resource = &(avc_ctx->res_mb_disable_skip_map_surface);
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_MBENC_FORCE_NONSKIP_MB_MAP_INDEX);
        }

        if(avc_state->sfd_enable && generic_state->hme_enabled)
        {
            if(generic_state->frame_type == SLICE_TYPE_P)
            {
                gpe_resource = &(avc_ctx->res_sfd_cost_table_p_frame_buffer);

            }else if(generic_state->frame_type == SLICE_TYPE_B)
            {
                gpe_resource = &(avc_ctx->res_sfd_cost_table_b_frame_buffer);
            }

            if(generic_state->frame_type != SLICE_TYPE_I)
            {
                gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                               gpe_resource,
                                               1,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               GEN9_AVC_MBENC_SFD_COST_TABLE_INDEX);
            }
        }
    }

    return;
}

static VAStatus
gen9_avc_kernel_mbenc(VADriverContextP ctx,
                      struct encode_state *encode_state,
                      struct intel_encoder_context *encoder_context,
                      bool i_frame_dist_in_use)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct gpe_encoder_kernel_walker_parameter kernel_walker_param;
    unsigned int downscaled_width_in_mb, downscaled_height_in_mb;
    int media_function = 0;
    int kernel_idx = 0;
    unsigned int mb_const_data_buffer_in_use = 0;
    unsigned int mb_qp_buffer_in_use = 0;
    unsigned int brc_enabled = 0;
    unsigned int roi_enable = (generic_state->num_roi > 0)?1:0;
    unsigned int dirty_roi_enable = ((generic_state->dirty_num_roi > 0) && (generic_state->frame_type == SLICE_TYPE_P) && (0));
    struct mbenc_param param ;

    int mbenc_i_frame_dist_in_use = i_frame_dist_in_use;
    int mad_enable = 0;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];

    mb_const_data_buffer_in_use =
        generic_state->mb_brc_enabled ||
        roi_enable ||
        dirty_roi_enable ||
        avc_state->mb_qp_data_enable ||
        avc_state->rolling_intra_refresh_enable;
    mb_qp_buffer_in_use =
        generic_state->mb_brc_enabled ||
        generic_state->brc_roi_enable ||
        avc_state->mb_qp_data_enable;

    if(mbenc_i_frame_dist_in_use)
    {
        media_function = INTEL_MEDIA_STATE_ENC_I_FRAME_DIST;
        kernel_idx = GEN9_AVC_KERNEL_BRC_I_FRAME_DIST;
        downscaled_width_in_mb = generic_state->downscaled_width_4x_in_mb;
        downscaled_height_in_mb = generic_state->downscaled_height_4x_in_mb;
        mad_enable = 0;
        brc_enabled = 0;

        gpe_context = &(avc_ctx->context_brc.gpe_contexts[kernel_idx]);
    }else
    {
        switch(generic_state->kernel_mode)
        {
        case INTEL_ENC_KERNEL_NORMAL :
            {
                media_function = INTEL_MEDIA_STATE_ENC_NORMAL;
                kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_NORMAL_I;
                break;
            }
        case INTEL_ENC_KERNEL_PERFORMANCE :
            {
                media_function = INTEL_MEDIA_STATE_ENC_PERFORMANCE;
                kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_PERFORMANCE_I;
                break;
            }
        case INTEL_ENC_KERNEL_QUALITY :
            {
                media_function = INTEL_MEDIA_STATE_ENC_QUALITY;
                kernel_idx = MBENC_KERNEL_BASE + GEN9_AVC_KERNEL_MBENC_QUALITY_I;
                break;
            }
        default:
            assert(0);

        }

        if(generic_state->frame_type == SLICE_TYPE_P)
        {
           kernel_idx += 1;
        }
        else if(generic_state->frame_type == SLICE_TYPE_B)
        {
           kernel_idx += 2;
        }

        downscaled_width_in_mb = generic_state->frame_width_in_mbs;
        downscaled_height_in_mb = generic_state->frame_height_in_mbs;
        mad_enable = avc_state->mad_enable;
        brc_enabled = generic_state->brc_enabled;

        gpe_context = &(avc_ctx->context_mbenc.gpe_contexts[kernel_idx]);
    }

    memset(&param,0,sizeof(struct mbenc_param));

    param.mb_const_data_buffer_in_use = mb_const_data_buffer_in_use;
    param.mb_qp_buffer_in_use = mb_qp_buffer_in_use;
    param.mbenc_i_frame_dist_in_use = mbenc_i_frame_dist_in_use;
    param.mad_enable = mad_enable;
    param.brc_enabled = brc_enabled;
    param.roi_enabled = roi_enable;

    if(avc_state->mb_status_supported)
    {
        param.mb_vproc_stats_enable =  avc_state->flatness_check_enable || avc_state->adaptive_transform_decision_enable;
    }

    if(!avc_state->mbenc_curbe_set_in_brc_update)
    {
        gen8_gpe_context_init(ctx, gpe_context);
    }

    gen9_gpe_reset_binding_table(ctx, gpe_context);

    if(!avc_state->mbenc_curbe_set_in_brc_update)
    {
        /*set curbe here*/
        generic_ctx->pfn_set_curbe_mbenc(ctx,encode_state,gpe_context,encoder_context,&param);
    }

    /* MB brc const data buffer set up*/
    if(mb_const_data_buffer_in_use)
    {
        gen9_avc_load_mb_brc_const_data(ctx,encode_state,encoder_context);
    }

    /*clear the mad buffer*/
    if(mad_enable)
    {
        i965_zero_gpe_resource(&(avc_ctx->res_mad_data_buffer));
    }
    /*send surface*/
    generic_ctx->pfn_send_mbenc_surface(ctx,encode_state,gpe_context,encoder_context,&param);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    /*walker setting*/
    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));

    kernel_walker_param.use_scoreboard = 1;
    kernel_walker_param.resolution_x = downscaled_width_in_mb ;
    kernel_walker_param.resolution_y = downscaled_height_in_mb ;
    if(mbenc_i_frame_dist_in_use)
    {
        kernel_walker_param.no_dependency = 1;
    }else
    {
        switch(generic_state->frame_type)
        {
        case SLICE_TYPE_I:
            kernel_walker_param.walker_degree = WALKER_45_DEGREE;
            break;
        case SLICE_TYPE_P:
            kernel_walker_param.walker_degree = WALKER_26_DEGREE;
            break;
        case SLICE_TYPE_B:
            kernel_walker_param.walker_degree = WALKER_26_DEGREE;
            if(!slice_param->direct_spatial_mv_pred_flag)
            {
                kernel_walker_param.walker_degree = WALKER_45_DEGREE;
            }
            break;
        default:
            assert(0);
        }
        kernel_walker_param.no_dependency = 0;
    }

    i965_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);

    gen9_avc_run_kernel_media_object_walker(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_walker_param);
    return VA_STATUS_SUCCESS;
}

/*
me kernle related function
*/
static void
gen9_avc_set_curbe_me(VADriverContextP ctx,
                      struct encode_state *encode_state,
                      struct i965_gpe_context *gpe_context,
                      struct intel_encoder_context *encoder_context,
                      void * param)
{
    gen9_avc_me_curbe_data *curbe_cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];

    struct me_param * curbe_param = (struct me_param *)param ;
    unsigned char  use_mv_from_prev_step = 0;
    unsigned char write_distortions = 0;
    unsigned char qp_prime_y = 0;
    unsigned char me_method = gen9_avc_p_me_method[generic_state->preset];
    unsigned char seach_table_idx = 0;
    unsigned char mv_shift_factor = 0, prev_mv_read_pos_factor = 0;
    unsigned int downscaled_width_in_mb, downscaled_height_in_mb;
    unsigned int scale_factor = 0;

    qp_prime_y = avc_state->pic_param->pic_init_qp + slice_param->slice_qp_delta;
    switch(curbe_param->hme_type)
    {
    case INTEL_ENC_HME_4x :
        {
            use_mv_from_prev_step = (generic_state->b16xme_enabled)? 1:0;
            write_distortions = 1;
            mv_shift_factor = 2;
            scale_factor = 4;
            prev_mv_read_pos_factor = 0;
            break;
        }
    case INTEL_ENC_HME_16x :
        {
            use_mv_from_prev_step = (generic_state->b32xme_enabled)? 1:0;
            write_distortions = 0;
            mv_shift_factor = 2;
            scale_factor = 16;
            prev_mv_read_pos_factor = 1;
            break;
        }
    case INTEL_ENC_HME_32x :
        {
            use_mv_from_prev_step = 0;
            write_distortions = 0;
            mv_shift_factor = 1;
            scale_factor = 32;
            prev_mv_read_pos_factor = 0;
            break;
        }
    default:
        assert(0);

    }
    curbe_cmd = i965_gpe_context_map_curbe(gpe_context);

    if (!curbe_cmd)
        return;

    downscaled_width_in_mb = ALIGN(generic_state->frame_width_in_pixel/scale_factor,16)/16;
    downscaled_height_in_mb = ALIGN(generic_state->frame_height_in_pixel/scale_factor,16)/16;

    memcpy(curbe_cmd,gen9_avc_me_curbe_init_data,sizeof(gen9_avc_me_curbe_data));

    curbe_cmd->dw3.sub_pel_mode = 3;
    if(avc_state->field_scaling_output_interleaved)
    {
        /*frame set to zero,field specified*/
        curbe_cmd->dw3.src_access = 0;
        curbe_cmd->dw3.ref_access = 0;
        curbe_cmd->dw7.src_field_polarity = 0;
    }
    curbe_cmd->dw4.picture_height_minus1 = downscaled_height_in_mb - 1;
    curbe_cmd->dw4.picture_width = downscaled_width_in_mb;
    curbe_cmd->dw5.qp_prime_y = qp_prime_y;

    curbe_cmd->dw6.use_mv_from_prev_step = use_mv_from_prev_step;
    curbe_cmd->dw6.write_distortions = write_distortions;
    curbe_cmd->dw6.super_combine_dist = gen9_avc_super_combine_dist[generic_state->preset];
    curbe_cmd->dw6.max_vmvr = i965_avc_get_max_mv_len(avc_state->seq_param->level_idc) * 4;//frame only

    if(generic_state->frame_type == SLICE_TYPE_B)
    {
        curbe_cmd->dw1.bi_weight = 32;
        curbe_cmd->dw13.num_ref_idx_l1_minus1 = slice_param->num_ref_idx_l1_active_minus1;
        me_method = gen9_avc_b_me_method[generic_state->preset];
        seach_table_idx = 1;
    }

    if(generic_state->frame_type == SLICE_TYPE_P ||
       generic_state->frame_type == SLICE_TYPE_B )
       curbe_cmd->dw13.num_ref_idx_l0_minus1 = slice_param->num_ref_idx_l0_active_minus1;

    curbe_cmd->dw13.ref_streamin_cost = 5;
    curbe_cmd->dw13.roi_enable = 0;

    curbe_cmd->dw15.prev_mv_read_pos_factor = prev_mv_read_pos_factor;
    curbe_cmd->dw15.mv_shift_factor = mv_shift_factor;

    memcpy(&curbe_cmd->dw16,table_enc_search_path[seach_table_idx][me_method],14*sizeof(int));

    curbe_cmd->dw32._4x_memv_output_data_surf_index = GEN9_AVC_ME_MV_DATA_SURFACE_INDEX;
    curbe_cmd->dw33._16x_32x_memv_input_data_surf_index = (curbe_param->hme_type == INTEL_ENC_HME_32x)? GEN9_AVC_32XME_MV_DATA_SURFACE_INDEX:GEN9_AVC_16XME_MV_DATA_SURFACE_INDEX ;
    curbe_cmd->dw34._4x_me_output_dist_surf_index = GEN9_AVC_ME_DISTORTION_SURFACE_INDEX;
    curbe_cmd->dw35._4x_me_output_brc_dist_surf_index = GEN9_AVC_ME_BRC_DISTORTION_INDEX;
    curbe_cmd->dw36.vme_fwd_inter_pred_surf_index = GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX;
    curbe_cmd->dw37.vme_bdw_inter_pred_surf_index = GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX;
    curbe_cmd->dw38.reserved = GEN9_AVC_ME_VDENC_STREAMIN_INDEX;

    i965_gpe_context_unmap_curbe(gpe_context);
    return;
}

static void
gen9_avc_send_surface_me(VADriverContextP ctx,
                         struct encode_state *encode_state,
                         struct i965_gpe_context *gpe_context,
                         struct intel_encoder_context *encoder_context,
                         void * param)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);

    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;

    struct object_surface *obj_surface, *input_surface;
    struct gen9_surface_avc *avc_priv_surface;
    struct i965_gpe_resource *gpe_resource;
    struct me_param * curbe_param = (struct me_param *)param ;

    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    VASurfaceID surface_id;
    int i = 0;

    /* all scaled input surface stored in reconstructed_object*/
    obj_surface = encode_state->reconstructed_object;
    if (!obj_surface || !obj_surface->private_data)
        return;
    avc_priv_surface = obj_surface->private_data;


    switch(curbe_param->hme_type)
    {
    case INTEL_ENC_HME_4x :
        {
            /*memv output 4x*/
            gpe_resource = &avc_ctx->s4x_memv_data_buffer;
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_ME_MV_DATA_SURFACE_INDEX);

            /*memv input 16x*/
            if(generic_state->b16xme_enabled)
            {
                gpe_resource = &avc_ctx->s16x_memv_data_buffer;
                gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                               gpe_resource,
                                               1,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               GEN9_AVC_16XME_MV_DATA_SURFACE_INDEX);
            }
            /* brc distortion  output*/
            gpe_resource = &avc_ctx->res_brc_dist_data_surface;
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_ME_BRC_DISTORTION_INDEX);
           /* memv distortion output*/
            gpe_resource = &avc_ctx->s4x_memv_distortion_buffer;
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_ME_DISTORTION_SURFACE_INDEX);
            /*input current down scaled YUV surface*/
            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_4x_surface_obj;
            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX);
            /*input ref scaled YUV surface*/
            for(i = 0; i < slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList0[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_4x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX+i*2 + 1);
            }

            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_4x_surface_obj;

            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX);

            for(i = 0; i < slice_param->num_ref_idx_l1_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList1[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_4x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX+i*2 + 1);
            }
            break;

        }
    case INTEL_ENC_HME_16x :
        {
            gpe_resource = &avc_ctx->s16x_memv_data_buffer;
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_ME_MV_DATA_SURFACE_INDEX);

            if(generic_state->b32xme_enabled)
            {
                gpe_resource = &avc_ctx->s32x_memv_data_buffer;
                gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                               gpe_resource,
                                               1,
                                               I965_SURFACEFORMAT_R8_UNORM,
                                               GEN9_AVC_32XME_MV_DATA_SURFACE_INDEX);
            }

            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_16x_surface_obj;
            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX);

            for(i = 0; i < slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList0[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_16x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX+i*2 + 1);
            }

            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_16x_surface_obj;

            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX);

            for(i = 0; i < slice_param->num_ref_idx_l1_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList1[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_16x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX+i*2 + 1);
            }
            break;
        }
    case INTEL_ENC_HME_32x :
        {
            gpe_resource = &avc_ctx->s32x_memv_data_buffer;
            gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                           gpe_resource,
                                           1,
                                           I965_SURFACEFORMAT_R8_UNORM,
                                           GEN9_AVC_ME_MV_DATA_SURFACE_INDEX);

            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_32x_surface_obj;
            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX);

            for(i = 0; i < slice_param->num_ref_idx_l0_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList0[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_32x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_FWD_REF_INDEX+i*2 + 1);
            }

            obj_surface = encode_state->reconstructed_object;
            avc_priv_surface = obj_surface->private_data;
            input_surface = avc_priv_surface->scaled_32x_surface_obj;

            gen9_add_adv_gpe_surface(ctx, gpe_context,
                                     input_surface,
                                     GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX);

            for(i = 0; i < slice_param->num_ref_idx_l1_active_minus1 + 1; i++)
            {
                surface_id = slice_param->RefPicList1[i].picture_id;
                obj_surface = SURFACE(surface_id);
                if (!obj_surface || !obj_surface->private_data)
                    break;
                avc_priv_surface = obj_surface->private_data;

                input_surface = avc_priv_surface->scaled_32x_surface_obj;

                gen9_add_adv_gpe_surface(ctx, gpe_context,
                                         input_surface,
                                         GEN9_AVC_ME_CURR_FOR_BWD_REF_INDEX+i*2 + 1);
            }
            break;
        }
    default:
        assert(0);

    }
}

static VAStatus
gen9_avc_kernel_me(VADriverContextP ctx,
                   struct encode_state *encode_state,
                   struct intel_encoder_context *encoder_context,
                   int hme_type)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct gpe_encoder_kernel_walker_parameter kernel_walker_param;
    unsigned int downscaled_width_in_mb, downscaled_height_in_mb;
    int media_function = 0;
    int kernel_idx = 0;
    struct me_param param ;
    unsigned int scale_factor = 0;

    switch(hme_type)
    {
    case INTEL_ENC_HME_4x :
        {
            media_function = INTEL_MEDIA_STATE_4X_ME;
            scale_factor = 4;
            break;
        }
    case INTEL_ENC_HME_16x :
        {
            media_function = INTEL_MEDIA_STATE_16X_ME;
            scale_factor = 16;
            break;
        }
    case INTEL_ENC_HME_32x :
        {
            media_function = INTEL_MEDIA_STATE_32X_ME;
            scale_factor = 32;
            break;
        }
    default:
        assert(0);

    }

    downscaled_width_in_mb = ALIGN(generic_state->frame_width_in_pixel/scale_factor,16)/16;
    downscaled_height_in_mb = ALIGN(generic_state->frame_height_in_pixel/scale_factor,16)/16;

    /* I frame should not come here.*/
    kernel_idx = (generic_state->frame_type == SLICE_TYPE_P)? GEN9_AVC_KERNEL_ME_P_IDX : GEN9_AVC_KERNEL_ME_B_IDX;
    gpe_context = &(avc_ctx->context_me.gpe_contexts[kernel_idx]);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    /*set curbe*/
    memset(&param,0,sizeof(param));
    param.hme_type = hme_type;
    generic_ctx->pfn_set_curbe_me(ctx,encode_state,gpe_context,encoder_context,&param);

    /*send surface*/
    generic_ctx->pfn_send_me_surface(ctx,encode_state,gpe_context,encoder_context,&param);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    /* the scaling is based on 8x8 blk level */
    kernel_walker_param.resolution_x = downscaled_width_in_mb ;
    kernel_walker_param.resolution_y = downscaled_height_in_mb ;
    kernel_walker_param.no_dependency = 1;

    i965_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);

    gen9_avc_run_kernel_media_object_walker(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}

/*
wp related function
*/
static void
gen9_avc_set_curbe_wp(VADriverContextP ctx,
                     struct encode_state *encode_state,
                     struct i965_gpe_context *gpe_context,
                     struct intel_encoder_context *encoder_context,
                     void * param)
{
    gen9_avc_wp_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    struct wp_param * curbe_param = (struct wp_param *)param;

    cmd = i965_gpe_context_map_curbe(gpe_context);

    if (!cmd)
        return;
    memset(cmd,0,sizeof(gen9_avc_wp_curbe_data));
    if(curbe_param->ref_list_idx)
    {
        cmd->dw0.default_weight = slice_param->luma_weight_l1[0];
        cmd->dw0.default_offset = slice_param->luma_offset_l1[0];
    }else
    {
        cmd->dw0.default_weight = slice_param->luma_weight_l0[0];
        cmd->dw0.default_offset = slice_param->luma_offset_l0[0];
    }

    cmd->dw49.input_surface = GEN9_AVC_WP_INPUT_REF_SURFACE_INDEX;
    cmd->dw50.output_surface = GEN9_AVC_WP_OUTPUT_SCALED_SURFACE_INDEX;

    i965_gpe_context_unmap_curbe(gpe_context);

}

static void
gen9_avc_send_surface_wp(VADriverContextP ctx,
                         struct encode_state *encode_state,
                         struct i965_gpe_context *gpe_context,
                         struct intel_encoder_context *encoder_context,
                         void * param)
{
    struct i965_driver_data *i965 = i965_driver_data(ctx);
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    struct wp_param * curbe_param = (struct wp_param *)param;
    struct object_surface *obj_surface;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];
    VASurfaceID surface_id;

    if(curbe_param->ref_list_idx)
    {
        surface_id = slice_param->RefPicList1[0].picture_id;
        obj_surface = SURFACE(surface_id);
        if (!obj_surface || !obj_surface->private_data)
            avc_state->weighted_ref_l1_enable = 0;
        else
            avc_state->weighted_ref_l1_enable = 1;
    }else
    {
        surface_id = slice_param->RefPicList0[0].picture_id;
        obj_surface = SURFACE(surface_id);
        if (!obj_surface || !obj_surface->private_data)
            avc_state->weighted_ref_l0_enable = 0;
        else
            avc_state->weighted_ref_l0_enable = 1;
    }
    if(!obj_surface)
        obj_surface = encode_state->reference_objects[0];


    gen9_add_adv_gpe_surface(ctx, gpe_context,
                             obj_surface,
                             GEN9_AVC_WP_INPUT_REF_SURFACE_INDEX);

    obj_surface = avc_ctx->wp_output_pic_select_surface_obj[curbe_param->ref_list_idx];
    gen9_add_adv_gpe_surface(ctx, gpe_context,
                             obj_surface,
                             GEN9_AVC_WP_OUTPUT_SCALED_SURFACE_INDEX);
}


static VAStatus
gen9_avc_kernel_wp(VADriverContextP ctx,
                   struct encode_state *encode_state,
                   struct intel_encoder_context *encoder_context,
                   unsigned int list1_in_use)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_walker_parameter media_object_walker_param;
    struct gpe_encoder_kernel_walker_parameter kernel_walker_param;
    int media_function = INTEL_MEDIA_STATE_ENC_WP;
    struct wp_param param;

    gpe_context = &(avc_ctx->context_wp.gpe_contexts);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    memset(&param,0,sizeof(param));
    param.ref_list_idx = (list1_in_use == 1)? 1: 0;
    /*set curbe*/
    generic_ctx->pfn_set_curbe_wp(ctx,encode_state,gpe_context,encoder_context,&param);

    /*send surface*/
    generic_ctx->pfn_send_wp_surface(ctx,encode_state,gpe_context,encoder_context,&param);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&kernel_walker_param, 0, sizeof(kernel_walker_param));
    /* the scaling is based on 8x8 blk level */
    kernel_walker_param.resolution_x = generic_state->frame_width_in_mbs;
    kernel_walker_param.resolution_y = generic_state->frame_height_in_mbs;
    kernel_walker_param.no_dependency = 1;

    i965_init_media_object_walker_parameter(&kernel_walker_param, &media_object_walker_param);

    gen9_avc_run_kernel_media_object_walker(ctx, encoder_context,
                                        gpe_context,
                                        media_function,
                                        &media_object_walker_param);

    return VA_STATUS_SUCCESS;
}


/*
sfd related function
*/
static void
gen9_avc_set_curbe_sfd(VADriverContextP ctx,
                     struct encode_state *encode_state,
                     struct i965_gpe_context *gpe_context,
                     struct intel_encoder_context *encoder_context,
                     void * param)
{
    gen9_avc_sfd_curbe_data *cmd;
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct generic_enc_codec_state * generic_state = (struct generic_enc_codec_state * )vme_context->generic_enc_state;
    struct avc_enc_state * avc_state = (struct avc_enc_state * )vme_context->private_enc_state;
    VAEncSliceParameterBufferH264 * slice_param = avc_state->slice_param[0];

    cmd = i965_gpe_context_map_curbe(gpe_context);

    if (!cmd)
        return;
    memset(cmd,0,sizeof(gen9_avc_sfd_curbe_data));

    cmd->dw0.enable_intra_cost_scaling_for_static_frame = 1 ;
    cmd->dw0.enable_adaptive_mv_stream_in = 0 ; //vdenc
    cmd->dw0.stream_in_type = 7 ;             //vdenc
    cmd->dw0.slice_type = slice_type_kernel[generic_state->frame_type]  ;
    cmd->dw0.brc_mode_enable = generic_state->brc_enabled ;
    cmd->dw0.vdenc_mode_disable = 1 ;

    cmd->dw1.hme_stream_in_ref_cost = 5 ;
    cmd->dw1.num_of_refs = slice_param->num_ref_idx_l0_active_minus1 ;//vdenc
    cmd->dw1.qp_value = avc_state->pic_param->pic_init_qp + slice_param->slice_qp_delta ;

    cmd->dw2.frame_width_in_mbs = generic_state->frame_width_in_mbs ;
    cmd->dw2.frame_height_in_mbs = generic_state->frame_height_in_mbs ;

    cmd->dw3.large_mv_threshold = 128 ;
    cmd->dw4.total_large_mv_threshold = (generic_state->frame_width_in_mbs * generic_state->frame_height_in_mbs)/100 ;
    cmd->dw5.zmv_threshold = 4 ;
    cmd->dw6.total_zmv_threshold = (generic_state->frame_width_in_mbs * generic_state->frame_height_in_mbs * avc_state->zero_mv_threshold)/100 ; // zero_mv_threshold = 60;
    cmd->dw7.min_dist_threshold = 10 ;

    if(generic_state->frame_type == SLICE_TYPE_P)
    {
        memcpy(cmd->cost_table,gen9_avc_sfd_cost_table_p_frame,52* sizeof(unsigned char));

    }else if(generic_state->frame_type == SLICE_TYPE_B)
    {
        memcpy(cmd->cost_table,gen9_avc_sfd_cost_table_b_frame,52* sizeof(unsigned char));
    }

    cmd->dw21.actual_width_in_mb = cmd->dw2.frame_width_in_mbs ;
    cmd->dw21.actual_height_in_mb = cmd->dw2.frame_height_in_mbs ;
    cmd->dw24.vdenc_input_image_state_index = GEN9_AVC_SFD_VDENC_INPUT_IMAGE_STATE_INDEX ;
    cmd->dw26.mv_data_surface_index = GEN9_AVC_SFD_MV_DATA_SURFACE_INDEX ;
    cmd->dw27.inter_distortion_surface_index = GEN9_AVC_SFD_INTER_DISTORTION_SURFACE_INDEX ;
    cmd->dw28.output_data_surface_index = GEN9_AVC_SFD_OUTPUT_DATA_SURFACE_INDEX ;
    cmd->dw29.vdenc_output_image_state_index = GEN9_AVC_SFD_VDENC_OUTPUT_IMAGE_STATE_INDEX ;

    i965_gpe_context_unmap_curbe(gpe_context);

}

static void
gen9_avc_send_surface_sfd(VADriverContextP ctx,
                          struct encode_state *encode_state,
                          struct i965_gpe_context *gpe_context,
                          struct intel_encoder_context *encoder_context,
                          void * param)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct i965_gpe_resource *gpe_resource;
    int size = 0;

    /*HME mv data surface memv output 4x*/
    gpe_resource = &avc_ctx->s4x_memv_data_buffer;
    gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                   gpe_resource,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   GEN9_AVC_SFD_MV_DATA_SURFACE_INDEX);

    /* memv distortion */
    gpe_resource = &avc_ctx->s4x_memv_distortion_buffer;
    gen9_add_buffer_2d_gpe_surface(ctx, gpe_context,
                                   gpe_resource,
                                   1,
                                   I965_SURFACEFORMAT_R8_UNORM,
                                   GEN9_AVC_SFD_INTER_DISTORTION_SURFACE_INDEX);
    /*buffer output*/
    size = 32 * 4 *4;
    gpe_resource = &avc_ctx->res_sfd_output_buffer;
    gen9_add_buffer_gpe_surface(ctx,
                                gpe_context,
                                gpe_resource,
                                0,
                                size / 4,
                                0,
                                GEN9_AVC_SFD_OUTPUT_DATA_SURFACE_INDEX);

}

static VAStatus
gen9_avc_kernel_sfd(VADriverContextP ctx,
                    struct encode_state *encode_state,
                    struct intel_encoder_context *encoder_context)
{
    struct encoder_vme_mfc_context * vme_context = (struct encoder_vme_mfc_context *)encoder_context->vme_context;
    struct gen9_avc_encoder_context * avc_ctx = (struct gen9_avc_encoder_context * )vme_context->private_enc_ctx;
    struct generic_encoder_context * generic_ctx = (struct generic_encoder_context * )vme_context->generic_enc_ctx;

    struct i965_gpe_context *gpe_context;
    struct gpe_media_object_parameter media_object_param;
    struct gpe_media_object_inline_data media_object_inline_data;
    int media_function = INTEL_MEDIA_STATE_STATIC_FRAME_DETECTION;
    gpe_context = &(avc_ctx->context_sfd.gpe_contexts);

    gen8_gpe_context_init(ctx, gpe_context);
    gen9_gpe_reset_binding_table(ctx, gpe_context);

    /*set curbe*/
    generic_ctx->pfn_set_curbe_sfd(ctx,encode_state,gpe_context,encoder_context,NULL);

    /*send surface*/
    generic_ctx->pfn_send_sfd_surface(ctx,encode_state,gpe_context,encoder_context,NULL);

    gen8_gpe_setup_interface_data(ctx, gpe_context);

    memset(&media_object_param, 0, sizeof(media_object_param));
    memset(&media_object_inline_data, 0, sizeof(media_object_inline_data));
    media_object_param.pinline_data = &media_object_inline_data;
    media_object_param.inline_size = sizeof(media_object_inline_data);

    gen9_avc_run_kernel_media_object(ctx, encoder_context,
                                     gpe_context,
                                     media_function,
                                     &media_object_param);

    return VA_STATUS_SUCCESS;
}
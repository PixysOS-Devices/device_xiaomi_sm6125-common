/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "res_manager.h"

#define __CLASS__ "ResManager"

namespace sde {

void ResManager::RotationConfig(const LayerTransform &transform, LayerRect *src_rect,
                              HWRotateInfo *left_rotate, HWRotateInfo *right_rotate,
                              uint32_t *rotate_count) {
  float src_width = src_rect->right - src_rect->left;
  float src_height = src_rect->bottom - src_rect->top;
  LayerRect dst_rect;
  // Rotate output is a temp buffer, always output to the top left corner for saving memory
  dst_rect.top = 0.0f;
  dst_rect.left = 0.0f;
  // downscale when doing rotation
  dst_rect.right = src_height / left_rotate->downscale_ratio_x;
  dst_rect.bottom = src_width / left_rotate->downscale_ratio_y;

  left_rotate->src_roi = *src_rect;
  left_rotate->pipe_id = kPipeIdNeedsAssignment;
  left_rotate->dst_roi = dst_rect;
  // Always use one rotator for now
  right_rotate->Reset();

  *src_rect = dst_rect;
  (*rotate_count)++;
}

DisplayError ResManager::SrcSplitConfig(DisplayResourceContext *display_resource_ctx,
                                        const LayerTransform &transform, const LayerRect &src_rect,
                                        const LayerRect &dst_rect, HWLayerConfig *layer_config) {
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;
  layer_config->is_right_pipe = false;

  if ((src_rect.right - src_rect.left) >= kMaxSourcePipeWidth ||
      (dst_rect.right - dst_rect.left) >= kMaxInterfaceWidth || hw_res_info_.always_src_split) {
    SplitRect(transform.flip_horizontal, src_rect, dst_rect, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi);
    left_pipe->pipe_id = kPipeIdNeedsAssignment;
    right_pipe->pipe_id = kPipeIdNeedsAssignment;
    layer_config->is_right_pipe = true;
  } else {
    left_pipe->src_roi = src_rect;
    left_pipe->dst_roi = dst_rect;
    left_pipe->pipe_id = kPipeIdNeedsAssignment;
    right_pipe->Reset();
  }
  return kErrorNone;
}

DisplayError ResManager::DisplaySplitConfig(DisplayResourceContext *display_resource_ctx,
                                            const LayerTransform &transform,
                                            const LayerRect &src_rect, const LayerRect &dst_rect,
                                            HWLayerConfig *layer_config) {
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;
  // for display split case
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;
  LayerRect scissor, dst_left, crop_left, crop_right, dst_right;
  layer_config->is_right_pipe = false;
  scissor.right = FLOAT(display_attributes.split_left);
  scissor.bottom = FLOAT(display_attributes.y_pixels);

  crop_left = src_rect;
  dst_left = dst_rect;
  crop_right = crop_left;
  dst_right = dst_left;
  CalculateCropRects(scissor, transform, &crop_left, &dst_left);

  scissor.left = FLOAT(display_attributes.split_left);
  scissor.top = 0.0f;
  scissor.right = FLOAT(display_attributes.x_pixels);
  scissor.bottom = FLOAT(display_attributes.y_pixels);
  CalculateCropRects(scissor, transform, &crop_right, &dst_right);
  if ((crop_left.right - crop_left.left) >= kMaxSourcePipeWidth) {
    if (crop_right.right != crop_right.left)
      return kErrorNotSupported;
    // 2 pipes both are on the left
    SplitRect(transform.flip_horizontal, crop_left, dst_left, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi);
    left_pipe->pipe_id = kPipeIdNeedsAssignment;
    right_pipe->pipe_id = kPipeIdNeedsAssignment;
    layer_config->is_right_pipe = true;
  } else if ((crop_right.right - crop_right.left) >= kMaxSourcePipeWidth) {
    if (crop_left.right != crop_left.left)
      return kErrorNotSupported;
    // 2 pipes both are on the right
    SplitRect(transform.flip_horizontal, crop_right, dst_right, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi);
    left_pipe->pipe_id = kPipeIdNeedsAssignment;
    right_pipe->pipe_id = kPipeIdNeedsAssignment;
    layer_config->is_right_pipe = true;
  } else if (UINT32(dst_left.right) > UINT32(dst_left.left)) {
    // assign left pipe
    left_pipe->src_roi = crop_left;
    left_pipe->dst_roi = dst_left;
    left_pipe->pipe_id = kPipeIdNeedsAssignment;
  } else {
    // Set default value, left_pipe is not needed.
    left_pipe->Reset();
  }

  // assign right pipe if needed
  if (UINT32(dst_right.right) > UINT32(dst_right.left)) {
    if (left_pipe->pipe_id) {
      right_pipe->src_roi = crop_right;
      right_pipe->dst_roi = dst_right;
      right_pipe->pipe_id = kPipeIdNeedsAssignment;
      layer_config->is_right_pipe = true;
    } else {
      // If left pipe is not used, use left pipe first.
      left_pipe->src_roi = crop_right;
      left_pipe->dst_roi = dst_right;
      left_pipe->pipe_id = kPipeIdNeedsAssignment;
      right_pipe->Reset();
    }
  } else {
    // need not right pipe
    right_pipe->Reset();
  }

  return kErrorNone;
}

DisplayError ResManager::Config(DisplayResourceContext *display_resource_ctx, HWLayers *hw_layers,
                                uint32_t *rotate_count) {
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;
  HWLayersInfo &layer_info = hw_layers->info;
  DisplayError error = kErrorNone;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
    float rot_scale_x = 1.0f, rot_scale_y = 1.0f;
    if (!IsValidDimension(layer.src_rect, layer.dst_rect)) {
      DLOGE_IF(kTagResources, "Input is invalid");
      LogRectVerbose("input layer src_rect", layer.src_rect);
      LogRectVerbose("input layer dst_rect", layer.dst_rect);
      return kErrorNotSupported;
    }

    LayerRect scissor, src_rect, dst_rect;
    src_rect = layer.src_rect;
    dst_rect = layer.dst_rect;
    scissor.right = FLOAT(display_attributes.x_pixels);
    scissor.bottom = FLOAT(display_attributes.y_pixels);
    CalculateCropRects(scissor, layer.transform, &src_rect, &dst_rect);

    if (ValidateScaling(layer, src_rect, dst_rect, &rot_scale_x, &rot_scale_y))
      return kErrorNotSupported;

    HWRotateInfo *left_rotate, *right_rotate;
    // config rotator first
    left_rotate = &hw_layers->config[i].left_rotate;
    right_rotate = &hw_layers->config[i].right_rotate;

    LayerTransform transform = layer.transform;
    if (IsRotationNeeded(transform.rotation) ||
        UINT32(rot_scale_x) != 1 || UINT32(rot_scale_y) != 1) {
      left_rotate->downscale_ratio_x = rot_scale_x;
      right_rotate->downscale_ratio_x = rot_scale_x;
      left_rotate->downscale_ratio_y = rot_scale_y;
      right_rotate->downscale_ratio_y = rot_scale_y;

      RotationConfig(layer.transform, &src_rect, left_rotate, right_rotate, rotate_count);
      // rotator will take care of flipping, reset tranform
      transform = LayerTransform();
    } else {
      left_rotate->Reset();
      right_rotate->Reset();
    }

    if (hw_res_info_.is_src_split) {
      error = SrcSplitConfig(display_resource_ctx, transform, src_rect,
                             dst_rect, &hw_layers->config[i]);
    } else {
      error = DisplaySplitConfig(display_resource_ctx, transform, src_rect,
                                 dst_rect, &hw_layers->config[i]);
    }

    if (error != kErrorNone)
      break;

    DLOGV_IF(kTagResources, "layer = %d, left pipe_id = %x",
             i, hw_layers->config[i].left_pipe.pipe_id);
    LogRectVerbose("input layer src_rect", layer.src_rect);
    LogRectVerbose("input layer dst_rect", layer.dst_rect);
    LogRectVerbose("cropped src_rect", src_rect);
    LogRectVerbose("cropped dst_rect", dst_rect);
    LogRectVerbose("left pipe src", hw_layers->config[i].left_pipe.src_roi);
    LogRectVerbose("left pipe dst", hw_layers->config[i].left_pipe.dst_roi);
    if (hw_layers->config[i].right_pipe.pipe_id) {
      LogRectVerbose("right pipe src", hw_layers->config[i].right_pipe.src_roi);
      LogRectVerbose("right pipe dst", hw_layers->config[i].right_pipe.dst_roi);
    }
  }

  return error;
}

DisplayError ResManager::ValidateScaling(const Layer &layer, const LayerRect &crop,
                                         const LayerRect &dst, float *rot_scale_x,
                                         float *rot_scale_y) {
  bool rotated90 = IsRotationNeeded(layer.transform.rotation);
  float crop_width = rotated90 ? crop.bottom - crop.top : crop.right - crop.left;
  float crop_height = rotated90 ? crop.right - crop.left : crop.bottom - crop.top;
  float dst_width = dst.right - dst.left;
  float dst_height = dst.bottom - dst.top;

  if ((dst_width < 1.0f) || (dst_height < 1.0f)) {
    DLOGV_IF(kTagResources, "Destination region is too small w = %d, h = %d",
    dst_width, dst_height);
    return kErrorNotSupported;
  }

  if ((crop_width < 1.0f) || (crop_height < 1.0f)) {
    DLOGV_IF(kTagResources, "source region is too small w = %d, h = %d", crop_width, crop_height);
    return kErrorNotSupported;
  }

  float scale_x = crop_width / dst_width;
  float scale_y = crop_height / dst_height;

  if ((UINT32(scale_x) > 1) || (UINT32(scale_y) > 1)) {
    const uint32_t max_scale_down = hw_res_info_.max_scale_down;
    uint32_t max_downscale_with_rotator;

    if (hw_res_info_.has_rotator_downscale)
      max_downscale_with_rotator = max_scale_down * kMaxRotateDownScaleRatio;
    else
      max_downscale_with_rotator = max_scale_down;

    if (((!hw_res_info_.has_decimation) || (IsMacroTileFormat(layer.input_buffer))) &&
        (scale_x > max_scale_down || scale_y > max_scale_down)) {
      DLOGV_IF(kTagResources,
               "Scaling down is over the limit is_tile = %d, scale_x = %d, scale_y = %d",
               IsMacroTileFormat(layer.input_buffer), scale_x, scale_y);
      return kErrorNotSupported;
    } else if (scale_x > max_downscale_with_rotator || scale_y > max_downscale_with_rotator) {
      DLOGV_IF(kTagResources, "Scaling down is over the limit scale_x = %d, scale_y = %d",
               scale_x, scale_y);
      return kErrorNotSupported;
    }
  }

  const uint32_t max_scale_up = hw_res_info_.max_scale_up;
  if (UINT32(scale_x) < 1 && scale_x > 0.0f) {
    if ((1.0f / scale_x) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_x = %d", 1.0f / scale_x);
      return kErrorNotSupported;
    }
  }

  if (UINT32(scale_y) < 1 && scale_y > 0.0f) {
    if ((1.0f / scale_y) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_y = %d", 1.0f / scale_y);
      return kErrorNotSupported;
    }
  }

  // Calculate rotator downscale ratio
  float rot_scale = 1.0f;
  while (scale_x > hw_res_info_.max_scale_down) {
    scale_x /= 2;
    rot_scale *= 2;
  }
  *rot_scale_x = rot_scale;

  rot_scale = 1.0f;
  while (scale_y > hw_res_info_.max_scale_down) {
    scale_y /= 2;
    rot_scale *= 2;
  }
  *rot_scale_y = rot_scale;
  DLOGV_IF(kTagResources, "rotator scaling hor = %.0f, ver = %.0f", *rot_scale_x, *rot_scale_y);

  return kErrorNone;
}

void ResManager::CalculateCut(const LayerTransform &transform, float *left_cut_ratio,
                              float *top_cut_ratio, float *right_cut_ratio,
                              float *bottom_cut_ratio) {
  if (transform.flip_horizontal) {
    Swap(*left_cut_ratio, *right_cut_ratio);
  }

  if (transform.flip_vertical) {
    Swap(*top_cut_ratio, *bottom_cut_ratio);
  }

  if (IsRotationNeeded(transform.rotation)) {
    // Anti clock swapping
    float tmp_cut_ratio = *left_cut_ratio;
    *left_cut_ratio = *top_cut_ratio;
    *top_cut_ratio = *right_cut_ratio;
    *right_cut_ratio = *bottom_cut_ratio;
    *bottom_cut_ratio = tmp_cut_ratio;
  }
}

void ResManager::CalculateCropRects(const LayerRect &scissor, const LayerTransform &transform,
                                    LayerRect *crop, LayerRect *dst) {
  float &crop_left = crop->left;
  float &crop_top = crop->top;
  float &crop_right = crop->right;
  float &crop_bottom = crop->bottom;
  float crop_width = crop->right - crop->left;
  float crop_height = crop->bottom - crop->top;

  float &dst_left = dst->left;
  float &dst_top = dst->top;
  float &dst_right = dst->right;
  float &dst_bottom = dst->bottom;
  float dst_width = dst->right - dst->left;
  float dst_height = dst->bottom - dst->top;

  const float &sci_left = scissor.left;
  const float &sci_top = scissor.top;
  const float &sci_right = scissor.right;
  const float &sci_bottom = scissor.bottom;

  float left_cut_ratio = 0.0, right_cut_ratio = 0.0, top_cut_ratio = 0.0, bottom_cut_ratio = 0.0;
  bool need_cut = false;

  if (dst_left < sci_left) {
    left_cut_ratio = (sci_left - dst_left) / dst_width;
    dst_left = sci_left;
    need_cut = true;
  }

  if (dst_right > sci_right) {
    right_cut_ratio = (dst_right - sci_right) / dst_width;
    dst_right = sci_right;
    need_cut = true;
  }

  if (dst_top < sci_top) {
    top_cut_ratio = (sci_top - dst_top) / (dst_height);
    dst_top = sci_top;
    need_cut = true;
  }

  if (dst_bottom > sci_bottom) {
    bottom_cut_ratio = (dst_bottom - sci_bottom) / (dst_height);
    dst_bottom = sci_bottom;
    need_cut = true;
  }

  if (!need_cut)
    return;

  CalculateCut(transform, &left_cut_ratio, &top_cut_ratio, &right_cut_ratio, &bottom_cut_ratio);
  crop_left += crop_width * left_cut_ratio;
  crop_top += crop_height * top_cut_ratio;
  crop_right -= crop_width * right_cut_ratio;
  crop_bottom -= crop_height * bottom_cut_ratio;
}

bool ResManager::IsValidDimension(const LayerRect &src, const LayerRect &dst) {
  // Make sure source in integral
  if (src.left - roundf(src.left)     ||
      src.top - roundf(src.top)       ||
      src.right - roundf(src.right)   ||
      src.bottom - roundf(src.bottom)) {
    DLOGE_IF(kTagResources, "Input ROI is not integral");
    return false;
  }

  if (src.left > src.right || src.top > src.bottom || dst.left > dst.right ||
      dst.top > dst.bottom) {
    return false;
  } else {
    return true;
  }
}

void ResManager::SetDecimationFactor(HWPipeInfo *pipe) {
  float max_down_scale = FLOAT(hw_res_info_.max_scale_down);
  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;
  float down_scale = src_h / dst_h;
  pipe->decimation = 1;

  if (!hw_res_info_.has_decimation || (down_scale <= max_down_scale))
    return;

  // Decimation is the remaining downscale factor after doing max SDE downscale.
  // In SDE, decimation is supported in powers of 2.
  // For ex: If a pipe needs downscale of 8 but max_down_scale is 4
  // So decimation = powf(2.0, ceilf(log2f(8) - log2f(4))) = powf(2.0, 1.0) = 2
  float decimation_factor = ceilf(log2f(down_scale) - log2f(max_down_scale));
  pipe->decimation = UINT8(powf(2.0f, decimation_factor));
}

void ResManager::SplitRect(bool flip_horizontal, const LayerRect &src_rect,
                           const LayerRect &dst_rect, LayerRect *src_left, LayerRect *dst_left,
                           LayerRect *src_right, LayerRect *dst_right) {
  // Split rectangle horizontally and evenly into two.
  float src_width = src_rect.right - src_rect.left;
  float dst_width = dst_rect.right - dst_rect.left;
  if (flip_horizontal) {
    src_left->top = src_rect.top;
    src_left->left = src_rect.left;
    src_left->right = src_rect.left + (src_width / 2);
    src_left->bottom = src_rect.bottom;

    dst_left->top = dst_rect.top;
    dst_left->left = dst_rect.left + (dst_width / 2);
    dst_left->right = dst_rect.right;
    dst_left->bottom = dst_rect.bottom;

    src_right->top = src_rect.top;
    src_right->left = src_left->right;
    src_right->right = src_rect.right;
    src_right->bottom = src_rect.bottom;

    dst_right->top = dst_rect.top;
    dst_right->left = dst_rect.left;
    dst_right->right = dst_left->left;
    dst_right->bottom = dst_rect.bottom;
  } else {
    src_left->top = src_rect.top;
    src_left->left = src_rect.left;
    src_left->right = src_rect.left + (src_width / 2);
    src_left->bottom = src_rect.bottom;

    dst_left->top = dst_rect.top;
    dst_left->left = dst_rect.left;
    dst_left->right = dst_rect.left + (dst_width / 2);
    dst_left->bottom = dst_rect.bottom;

    src_right->top = src_rect.top;
    src_right->left = src_left->right;
    src_right->right = src_rect.right;
    src_right->bottom = src_rect.bottom;

    dst_right->top = dst_rect.top;
    dst_right->left = dst_left->right;
    dst_right->right = dst_rect.right;
    dst_right->bottom = dst_rect.bottom;
  }
}

void ResManager::LogRectVerbose(const char *prefix, const LayerRect &roi) {
  DLOGV_IF(kTagResources,"%s: left = %.0f, top = %.0f, right = %.0f, bottom = %.0f",
           prefix, roi.left, roi.top, roi.right, roi.bottom);
}

}  // namespace sde

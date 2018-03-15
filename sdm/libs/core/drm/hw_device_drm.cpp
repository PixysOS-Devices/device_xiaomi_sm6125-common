/*
* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define __STDC_FORMAT_MACROS

#include <ctype.h>
#include <drm/drm_fourcc.h>
#include <drm_lib_loader.h>
#include <drm_master.h>
#include <drm_res_mgr.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <utils/sys.h>
#include <drm/sde_drm.h>
#include <private/color_params.h>
#include <utils/rect.h>

#include <sstream>
#include <ctime>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <limits>

#include "hw_device_drm.h"
#include "hw_info_interface.h"

#define __CLASS__ "HWDeviceDRM"

#ifndef DRM_FORMAT_MOD_QCOM_COMPRESSED
#define DRM_FORMAT_MOD_QCOM_COMPRESSED fourcc_mod_code(QCOM, 1)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_DX
#define DRM_FORMAT_MOD_QCOM_DX fourcc_mod_code(QCOM, 0x2)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_TIGHT
#define DRM_FORMAT_MOD_QCOM_TIGHT fourcc_mod_code(QCOM, 0x4)
#endif

using std::string;
using std::to_string;
using std::fstream;
using std::unordered_map;
using std::stringstream;
using std::ifstream;
using std::ofstream;
using drm_utils::DRMMaster;
using drm_utils::DRMResMgr;
using drm_utils::DRMLibLoader;
using drm_utils::DRMBuffer;
using sde_drm::GetDRMManager;
using sde_drm::DestroyDRMManager;
using sde_drm::DRMDisplayType;
using sde_drm::DRMDisplayToken;
using sde_drm::DRMConnectorInfo;
using sde_drm::DRMPPFeatureInfo;
using sde_drm::DRMRect;
using sde_drm::DRMRotation;
using sde_drm::DRMBlendType;
using sde_drm::DRMSrcConfig;
using sde_drm::DRMOps;
using sde_drm::DRMTopology;
using sde_drm::DRMPowerMode;
using sde_drm::DRMSecureMode;
using sde_drm::DRMSecurityLevel;
using sde_drm::DRMCscType;
using sde_drm::DRMMultiRectMode;

namespace sdm {

static PPBlock GetPPBlock(const HWToneMapLut &lut_type) {
  PPBlock pp_block = kPPBlockMax;
  switch (lut_type) {
    case kDma1dIgc:
    case kDma1dGc:
      pp_block = kDGM;
      break;
    case kVig1dIgc:
    case kVig3dGamut:
      pp_block = kVIG;
      break;
    default:
      DLOGE("Unknown PP Block");
      break;
  }
  return pp_block;
}

static void GetDRMFormat(LayerBufferFormat format, uint32_t *drm_format,
                         uint64_t *drm_format_modifier) {
  switch (format) {
    case kFormatRGBA8888:
      *drm_format = DRM_FORMAT_ABGR8888;
      break;
    case kFormatRGBA8888Ubwc:
      *drm_format = DRM_FORMAT_ABGR8888;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatRGBA5551:
      *drm_format = DRM_FORMAT_ABGR1555;
      break;
    case kFormatRGBA4444:
      *drm_format = DRM_FORMAT_ABGR4444;
      break;
    case kFormatBGRA8888:
      *drm_format = DRM_FORMAT_ARGB8888;
      break;
    case kFormatRGBX8888:
      *drm_format = DRM_FORMAT_XBGR8888;
      break;
    case kFormatRGBX8888Ubwc:
      *drm_format = DRM_FORMAT_XBGR8888;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatBGRX8888:
      *drm_format = DRM_FORMAT_XRGB8888;
      break;
    case kFormatRGB888:
      *drm_format = DRM_FORMAT_BGR888;
      break;
    case kFormatRGB565:
      *drm_format = DRM_FORMAT_BGR565;
      break;
    case kFormatBGR565:
      *drm_format = DRM_FORMAT_RGB565;
      break;
    case kFormatBGR565Ubwc:
      *drm_format = DRM_FORMAT_BGR565;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatRGBA1010102:
      *drm_format = DRM_FORMAT_ABGR2101010;
      break;
    case kFormatRGBA1010102Ubwc:
      *drm_format = DRM_FORMAT_ABGR2101010;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatARGB2101010:
      *drm_format = DRM_FORMAT_BGRA1010102;
      break;
    case kFormatRGBX1010102:
      *drm_format = DRM_FORMAT_XBGR2101010;
      break;
    case kFormatRGBX1010102Ubwc:
      *drm_format = DRM_FORMAT_XBGR2101010;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatXRGB2101010:
      *drm_format = DRM_FORMAT_BGRX1010102;
      break;
    case kFormatBGRA1010102:
      *drm_format = DRM_FORMAT_ARGB2101010;
      break;
    case kFormatABGR2101010:
      *drm_format = DRM_FORMAT_RGBA1010102;
      break;
    case kFormatBGRX1010102:
      *drm_format = DRM_FORMAT_XRGB2101010;
      break;
    case kFormatXBGR2101010:
      *drm_format = DRM_FORMAT_RGBX1010102;
      break;
    case kFormatYCbCr420SemiPlanar:
      *drm_format = DRM_FORMAT_NV12;
      break;
    case kFormatYCbCr420SemiPlanarVenus:
      *drm_format = DRM_FORMAT_NV12;
      break;
    case kFormatYCbCr420SPVenusUbwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatYCbCr420SPVenusTile:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_TILE;
      break;
    case kFormatYCrCb420SemiPlanar:
      *drm_format = DRM_FORMAT_NV21;
      break;
    case kFormatYCrCb420SemiPlanarVenus:
      *drm_format = DRM_FORMAT_NV21;
      break;
    case kFormatYCbCr420P010:
    case kFormatYCbCr420P010Venus:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_DX;
      break;
    case kFormatYCbCr420P010Ubwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
        DRM_FORMAT_MOD_QCOM_DX;
      break;
    case kFormatYCbCr420P010Tile:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_TILE |
        DRM_FORMAT_MOD_QCOM_DX;
      break;
    case kFormatYCbCr420TP10Ubwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
        DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT;
      break;
    case kFormatYCbCr420TP10Tile:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_TILE |
        DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT;
      break;
    case kFormatYCbCr422H2V1SemiPlanar:
      *drm_format = DRM_FORMAT_NV16;
      break;
    case kFormatYCrCb422H2V1SemiPlanar:
      *drm_format = DRM_FORMAT_NV61;
      break;
    case kFormatYCrCb420PlanarStride16:
      *drm_format = DRM_FORMAT_YVU420;
      break;
    default:
      DLOGW("Unsupported format %s", GetFormatString(format));
  }
}

HWDeviceDRM::Registry::Registry(BufferAllocator *buffer_allocator) :
  buffer_allocator_(buffer_allocator) {
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if (!master) {
    DLOGE("Failed to acquire DRM Master instance");
    return;
  }

  // If RMFB is ref-counted, we should immediately make a call to clean up fb_id after commit.
  // Driver will release fb_id after its usage. Otherwise speculatively free up fb_id after 3
  // cycles assuming driver is done with it.
  rmfb_delay_ = master->IsRmFbRefCounted() ? 1 : 3;
  hashmap_ = new std::unordered_map<int, uint32_t>[rmfb_delay_];
}

HWDeviceDRM::Registry::~Registry() {
  delete [] hashmap_;
}

void HWDeviceDRM::Registry::Register(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = &layer.input_buffer;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[0];

    if (hw_rotator_session->mode == kRotatorOffline && hw_rotate_info->valid) {
      input_buffer = &hw_rotator_session->output_buffer;
    }

    MapBufferToFbId(input_buffer);

    if (hw_rotator_session->mode == kRotatorInline && hw_rotate_info->valid &&
        hw_rotator_session->output_buffer.planes[0].fd >= 0) {
      MapBufferToFbId(&hw_rotator_session->output_buffer);
    }
  }
}

void HWDeviceDRM::Registry::MapBufferToFbId(LayerBuffer* buffer) {
  int fd = buffer->planes[0].fd;
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if (!master) {
    DLOGE("Failed to acquire DRM Master instance");
    return;
  }

  if (fd >= 0 && hashmap_[current_index_].find(fd) == hashmap_[current_index_].end()) {
    AllocatedBufferInfo buf_info{};
    DRMBuffer layout{};
    buf_info.fd = layout.fd = fd;
    buf_info.aligned_width = layout.width = buffer->width;
    buf_info.aligned_height = layout.height = buffer->height;
    buf_info.format = buffer->format;
    GetDRMFormat(buf_info.format, &layout.drm_format, &layout.drm_format_modifier);
    buffer_allocator_->GetBufferLayout(buf_info, layout.stride, layout.offset,
        &layout.num_planes);
    uint32_t fb_id = 0;
    int ret = master->CreateFbId(layout, &fb_id);
    if (ret < 0) {
      DLOGE("CreateFbId failed. width %d, height %d, format: %s, stride %u, error %d",
          layout.width, layout.height, GetFormatString(buf_info.format), layout.stride[0],
          errno);
    } else {
      hashmap_[current_index_][fd] = fb_id;
    }
  }
  return;
}

void HWDeviceDRM::Registry::Next() {
  current_index_ = (current_index_ + 1) % rmfb_delay_;
}

void HWDeviceDRM::Registry::Unregister() {
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if (!master) {
    DLOGE("Failed to acquire DRM Master instance");
    return;
  }

  auto &curr_map = hashmap_[current_index_];
  for (auto &pair : curr_map) {
    uint32_t fb_id = pair.second;
    int ret = master->RemoveFbId(fb_id);
    if (ret < 0) {
      DLOGE("Removing fb_id %d failed with error %d", fb_id, errno);
    }
  }

  curr_map.clear();
}

void HWDeviceDRM::Registry::Clear() {
  for (int i = 0; i < rmfb_delay_; i++) {
    Unregister();
    Next();
  }
  current_index_ = 0;
}

uint32_t HWDeviceDRM::Registry::GetFbId(int fd) {
  auto it = hashmap_[current_index_].find(fd);
  return (it == hashmap_[current_index_].end()) ? 0 : it->second;
}

HWDeviceDRM::HWDeviceDRM(BufferSyncHandler *buffer_sync_handler, BufferAllocator *buffer_allocator,
                         HWInfoInterface *hw_info_intf)
    : hw_info_intf_(hw_info_intf), buffer_sync_handler_(buffer_sync_handler),
      registry_(buffer_allocator) {
  hw_info_intf_ = hw_info_intf;
}

DisplayError HWDeviceDRM::Init() {
  int ret = 0;
  DRMMaster *drm_master = {};
  DRMMaster::GetInstance(&drm_master);
  drm_master->GetHandle(&dev_fd_);
  DRMLibLoader::GetInstance()->FuncGetDRMManager()(dev_fd_, &drm_mgr_intf_);

  if (drm_mgr_intf_->RegisterDisplay(disp_type_, &token_)) {
    DLOGE("RegisterDisplay failed for %s", device_name_);
    return kErrorResources;
  }

  if (token_.conn_id > INT32_MAX) {
    DLOGE("Connector id %u beyond supported range", token_.conn_id);
    drm_mgr_intf_->UnregisterDisplay(token_);
    return kErrorNotSupported;
  }

  ret = drm_mgr_intf_->CreateAtomicReq(token_, &drm_atomic_intf_);
  if (ret) {
    DLOGE("Failed creating atomic request for connector id %u. Error: %d.", token_.conn_id, ret);
    drm_mgr_intf_->UnregisterDisplay(token_);
    return kErrorResources;
  }

  ret = drm_mgr_intf_->GetConnectorInfo(token_.conn_id, &connector_info_);
  if (ret) {
    DLOGE("Failed getting info for connector id %u. Error: %d.", token_.conn_id, ret);
    drm_mgr_intf_->DestroyAtomicReq(drm_atomic_intf_);
    drm_atomic_intf_ = {};
    drm_mgr_intf_->UnregisterDisplay(token_);
    return kErrorHardware;
  }

  if (connector_info_.modes.empty()) {
    DLOGE("Critical error: Zero modes on connector id %u.", token_.conn_id);
    drm_mgr_intf_->DestroyAtomicReq(drm_atomic_intf_);
    drm_atomic_intf_ = {};
    drm_mgr_intf_->UnregisterDisplay(token_);
    return kErrorHardware;
  }

  hw_info_intf_->GetHWResourceInfo(&hw_resource_);

  InitializeConfigs();
  PopulateHWPanelInfo();
  UpdateMixerAttributes();

  // TODO(user): In future, remove has_qseed3 member, add version and pass version to constructor
  if (hw_resource_.has_qseed3) {
    hw_scale_ = new HWScaleDRM(HWScaleDRM::Version::V2);
  }

  std::unique_ptr<HWColorManagerDrm> hw_color_mgr(new HWColorManagerDrm());
  hw_color_mgr_ = std::move(hw_color_mgr);

  return kErrorNone;
}

DisplayError HWDeviceDRM::Deinit() {
  DisplayError err = kErrorNone;
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_CRTC, token_.conn_id, 0);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id, DRMPowerMode::OFF);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, nullptr);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 0);
  int ret = NullCommit(true /* synchronous */, false /* retain_planes */);
  if (ret) {
    DLOGE("Commit failed with error: %d", ret);
    err = kErrorHardware;
  }

  delete hw_scale_;
  registry_.Clear();
  display_attributes_ = {};
  drm_mgr_intf_->DestroyAtomicReq(drm_atomic_intf_);
  drm_atomic_intf_ = {};
  drm_mgr_intf_->UnregisterDisplay(token_);
  return err;
}

void HWDeviceDRM::InitializeConfigs() {
  current_mode_index_ = 0;
  // Update current mode with preferred mode
  for (uint32_t mode_index = 0; mode_index < connector_info_.modes.size(); mode_index++) {
      if (connector_info_.modes[mode_index].mode.type & DRM_MODE_TYPE_PREFERRED) {
        current_mode_index_ = mode_index;
        break;
      }
  }

  display_attributes_.resize(connector_info_.modes.size());

  uint32_t width = connector_info_.modes[current_mode_index_].mode.hdisplay;
  uint32_t height = connector_info_.modes[current_mode_index_].mode.vdisplay;
  for (uint32_t i = 0; i < connector_info_.modes.size(); i++) {
    auto &mode = connector_info_.modes[i].mode;
    if (mode.hdisplay != width || mode.vdisplay != height) {
      resolution_switch_enabled_ = true;
    }
    PopulateDisplayAttributes(i);
  }
}

DisplayError HWDeviceDRM::PopulateDisplayAttributes(uint32_t index) {
  drmModeModeInfo mode = {};
  uint32_t mm_width = 0;
  uint32_t mm_height = 0;
  DRMTopology topology = DRMTopology::SINGLE_LM;

  if (default_mode_) {
    DRMResMgr *res_mgr = nullptr;
    int ret = DRMResMgr::GetInstance(&res_mgr);
    if (ret < 0) {
      DLOGE("Failed to acquire DRMResMgr instance");
      return kErrorResources;
    }

    res_mgr->GetMode(&mode);
    res_mgr->GetDisplayDimInMM(&mm_width, &mm_height);
  } else {
    mode = connector_info_.modes[index].mode;
    mm_width = connector_info_.mmWidth;
    mm_height = connector_info_.mmHeight;
    topology = connector_info_.modes[index].topology;
  }

  display_attributes_[index].x_pixels = mode.hdisplay;
  display_attributes_[index].y_pixels = mode.vdisplay;
  display_attributes_[index].fps = mode.vrefresh;
  display_attributes_[index].vsync_period_ns =
    UINT32(1000000000L / display_attributes_[index].fps);

  /*
              Active                 Front           Sync           Back
              Region                 Porch                          Porch
     <-----------------------><----------------><-------------><-------------->
     <----- [hv]display ----->
     <------------- [hv]sync_start ------------>
     <--------------------- [hv]sync_end --------------------->
     <-------------------------------- [hv]total ----------------------------->
   */

  display_attributes_[index].v_front_porch = mode.vsync_start - mode.vdisplay;
  display_attributes_[index].v_pulse_width = mode.vsync_end - mode.vsync_start;
  display_attributes_[index].v_back_porch = mode.vtotal - mode.vsync_end;
  display_attributes_[index].v_total = mode.vtotal;
  display_attributes_[index].h_total = mode.htotal;
  display_attributes_[index].is_device_split =
      (topology == DRMTopology::DUAL_LM || topology == DRMTopology::DUAL_LM_MERGE ||
       topology == DRMTopology::DUAL_LM_MERGE_DSC || topology == DRMTopology::DUAL_LM_DSC ||
       topology == DRMTopology::DUAL_LM_DSCMERGE);
  display_attributes_[index].clock_khz = mode.clock;

  // If driver doesn't return panel width/height information, default to 320 dpi
  if (INT(mm_width) <= 0 || INT(mm_height) <= 0) {
    mm_width  = UINT32(((FLOAT(mode.hdisplay) * 25.4f) / 320.0f) + 0.5f);
    mm_height = UINT32(((FLOAT(mode.vdisplay) * 25.4f) / 320.0f) + 0.5f);
    DLOGW("Driver doesn't report panel physical width and height - defaulting to 320dpi");
  }

  display_attributes_[index].x_dpi = (FLOAT(mode.hdisplay) * 25.4f) / FLOAT(mm_width);
  display_attributes_[index].y_dpi = (FLOAT(mode.vdisplay) * 25.4f) / FLOAT(mm_height);
  SetTopology(topology, &display_attributes_[index].topology);

  DLOGI("Display attributes[%d]: WxH: %dx%d, DPI: %fx%f, FPS: %d, LM_SPLIT: %d, V_BACK_PORCH: %d," \
        " V_FRONT_PORCH: %d, V_PULSE_WIDTH: %d, V_TOTAL: %d, H_TOTAL: %d, CLK: %dKHZ, TOPOLOGY: %d",
        index, display_attributes_[index].x_pixels, display_attributes_[index].y_pixels,
        display_attributes_[index].x_dpi, display_attributes_[index].y_dpi,
        display_attributes_[index].fps, display_attributes_[index].is_device_split,
        display_attributes_[index].v_back_porch, display_attributes_[index].v_front_porch,
        display_attributes_[index].v_pulse_width, display_attributes_[index].v_total,
        display_attributes_[index].h_total, display_attributes_[index].clock_khz,
        display_attributes_[index].topology);

  return kErrorNone;
}

void HWDeviceDRM::PopulateHWPanelInfo() {
  hw_panel_info_ = {};

  snprintf(hw_panel_info_.panel_name, sizeof(hw_panel_info_.panel_name), "%s",
           connector_info_.panel_name.c_str());

  uint32_t index = current_mode_index_;
  hw_panel_info_.split_info.left_split = display_attributes_[index].x_pixels;
  if (display_attributes_[index].is_device_split) {
    hw_panel_info_.split_info.left_split = hw_panel_info_.split_info.right_split =
        display_attributes_[index].x_pixels / 2;
  }

  hw_panel_info_.partial_update = connector_info_.modes[index].num_roi;
  hw_panel_info_.left_roi_count = UINT32(connector_info_.modes[index].num_roi);
  hw_panel_info_.right_roi_count = UINT32(connector_info_.modes[index].num_roi);
  hw_panel_info_.left_align = connector_info_.modes[index].xstart;
  hw_panel_info_.top_align = connector_info_.modes[index].ystart;
  hw_panel_info_.width_align = connector_info_.modes[index].walign;
  hw_panel_info_.height_align = connector_info_.modes[index].halign;
  hw_panel_info_.min_roi_width = connector_info_.modes[index].wmin;
  hw_panel_info_.min_roi_height = connector_info_.modes[index].hmin;
  hw_panel_info_.needs_roi_merge = connector_info_.modes[index].roi_merge;
  hw_panel_info_.dynamic_fps = connector_info_.dynamic_fps;
  drmModeModeInfo current_mode = connector_info_.modes[current_mode_index_].mode;
  if (hw_panel_info_.dynamic_fps) {
    uint32_t min_fps = current_mode.vrefresh;
    uint32_t max_fps = current_mode.vrefresh;
    for (uint32_t mode_index = 0; mode_index < connector_info_.modes.size(); mode_index++) {
      if ((current_mode.vdisplay == connector_info_.modes[mode_index].mode.vdisplay) &&
          (current_mode.hdisplay == connector_info_.modes[mode_index].mode.hdisplay)) {
        if (min_fps > connector_info_.modes[mode_index].mode.vrefresh)  {
          min_fps = connector_info_.modes[mode_index].mode.vrefresh;
        }
        if (max_fps < connector_info_.modes[mode_index].mode.vrefresh)  {
          max_fps = connector_info_.modes[mode_index].mode.vrefresh;
        }
      }
    }
    hw_panel_info_.min_fps = min_fps;
    hw_panel_info_.max_fps = max_fps;
  } else {
    hw_panel_info_.min_fps = current_mode.vrefresh;
    hw_panel_info_.max_fps = current_mode.vrefresh;
  }

  hw_panel_info_.is_primary_panel = connector_info_.is_primary;
  hw_panel_info_.is_pluggable = 0;
  hw_panel_info_.hdr_enabled = connector_info_.panel_hdr_prop.hdr_enabled;
  hw_panel_info_.peak_luminance = connector_info_.panel_hdr_prop.peak_brightness;
  hw_panel_info_.blackness_level = connector_info_.panel_hdr_prop.blackness_level;
  hw_panel_info_.primaries.white_point[0] = connector_info_.panel_hdr_prop.display_primaries[0];
  hw_panel_info_.primaries.white_point[1] = connector_info_.panel_hdr_prop.display_primaries[1];
  hw_panel_info_.primaries.red[0] = connector_info_.panel_hdr_prop.display_primaries[2];
  hw_panel_info_.primaries.red[1] = connector_info_.panel_hdr_prop.display_primaries[3];
  hw_panel_info_.primaries.green[0] = connector_info_.panel_hdr_prop.display_primaries[4];
  hw_panel_info_.primaries.green[1] = connector_info_.panel_hdr_prop.display_primaries[5];
  hw_panel_info_.primaries.blue[0] = connector_info_.panel_hdr_prop.display_primaries[6];
  hw_panel_info_.primaries.blue[1] = connector_info_.panel_hdr_prop.display_primaries[7];
  hw_panel_info_.transfer_time_us = connector_info_.transfer_time_us;

  // no supprt for 90 rotation only flips or 180 supported
  hw_panel_info_.panel_orientation.rotation = 0;
  hw_panel_info_.panel_orientation.flip_horizontal =
    (connector_info_.panel_orientation == DRMRotation::FLIP_H) ||
    (connector_info_.panel_orientation == DRMRotation::ROT_180);
  hw_panel_info_.panel_orientation.flip_vertical =
    (connector_info_.panel_orientation == DRMRotation::FLIP_V) ||
    (connector_info_.panel_orientation == DRMRotation::ROT_180);

  GetHWDisplayPortAndMode();
  GetHWPanelMaxBrightness();

  DLOGI("%s, Panel Interface = %s, Panel Mode = %s, Is Primary = %d", device_name_,
        interface_str_.c_str(), hw_panel_info_.mode == kModeVideo ? "Video" : "Command",
        hw_panel_info_.is_primary_panel);
  DLOGI("Partial Update = %d, Dynamic FPS = %d, HDR Panel = %d", hw_panel_info_.partial_update,
        hw_panel_info_.dynamic_fps, hw_panel_info_.hdr_enabled);
  DLOGI("Align: left = %d, width = %d, top = %d, height = %d", hw_panel_info_.left_align,
        hw_panel_info_.width_align, hw_panel_info_.top_align, hw_panel_info_.height_align);
  DLOGI("ROI: min_width = %d, min_height = %d, need_merge = %d", hw_panel_info_.min_roi_width,
        hw_panel_info_.min_roi_height, hw_panel_info_.needs_roi_merge);
  DLOGI("FPS: min = %d, max = %d", hw_panel_info_.min_fps, hw_panel_info_.max_fps);
  DLOGI("Left Split = %d, Right Split = %d", hw_panel_info_.split_info.left_split,
        hw_panel_info_.split_info.right_split);
  DLOGI("Panel Transfer time = %d us", hw_panel_info_.transfer_time_us);
}

void HWDeviceDRM::GetHWDisplayPortAndMode() {
  hw_panel_info_.port = kPortDefault;
  hw_panel_info_.mode =
      (connector_info_.panel_mode == sde_drm::DRMPanelMode::VIDEO) ? kModeVideo : kModeCommand;

  if (default_mode_) {
    return;
  }

  switch (connector_info_.type) {
    case DRM_MODE_CONNECTOR_DSI:
      hw_panel_info_.port = kPortDSI;
      interface_str_ = "DSI";
      break;
    case DRM_MODE_CONNECTOR_LVDS:
      hw_panel_info_.port = kPortLVDS;
      interface_str_ = "LVDS";
      break;
    case DRM_MODE_CONNECTOR_eDP:
      hw_panel_info_.port = kPortEDP;
      interface_str_ = "EDP";
      break;
    case DRM_MODE_CONNECTOR_TV:
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
      hw_panel_info_.port = kPortDTV;
      interface_str_ = "HDMI";
      break;
    case DRM_MODE_CONNECTOR_VIRTUAL:
      hw_panel_info_.port = kPortWriteBack;
      interface_str_ = "Virtual";
      break;
    case DRM_MODE_CONNECTOR_DisplayPort:
      // TODO(user): Add when available
      interface_str_ = "DisplayPort";
      break;
  }

  return;
}

void HWDeviceDRM::GetHWPanelMaxBrightness() {
  char brightness[kMaxStringLength] = {0};
  string kMaxBrightnessNode = "/sys/class/backlight/panel0-backlight/max_brightness";

  hw_panel_info_.panel_max_brightness = 255;
  int fd = Sys::open_(kMaxBrightnessNode.c_str(), O_RDONLY);
  if (fd < 0) {
    DLOGW("Failed to open max brightness node = %s, error = %s", kMaxBrightnessNode.c_str(),
          strerror(errno));
    return;
  }

  if (Sys::pread_(fd, brightness, sizeof(brightness), 0) > 0) {
    hw_panel_info_.panel_max_brightness = atoi(brightness);
    DLOGI("Max brightness level = %d", hw_panel_info_.panel_max_brightness);
  } else {
    DLOGW("Failed to read max brightness level. error = %s", strerror(errno));
  }

  Sys::close_(fd);
}

DisplayError HWDeviceDRM::GetActiveConfig(uint32_t *active_config) {
  if (IsResolutionSwitchEnabled()) {
    *active_config = current_mode_index_;
  } else {
    *active_config = 0;
  }
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetNumDisplayAttributes(uint32_t *count) {
  if (IsResolutionSwitchEnabled()) {
    *count = UINT32(display_attributes_.size());
    if (*count <= 0) {
       return kErrorHardware;
    }
  } else {
    *count = 1;
  }
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetDisplayAttributes(uint32_t index,
                                               HWDisplayAttributes *display_attributes) {
  if (index >= display_attributes_.size()) {
    return kErrorParameters;
  }
  if (IsResolutionSwitchEnabled()) {
    *display_attributes = display_attributes_[index];
  } else {
    *display_attributes = display_attributes_[current_mode_index_];
  }
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetHWPanelInfo(HWPanelInfo *panel_info) {
  *panel_info = hw_panel_info_;
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetDisplayAttributes(uint32_t index) {
  if (!IsResolutionSwitchEnabled()) {
    return kErrorNotSupported;
  }

  if (index >= display_attributes_.size()) {
    DLOGE("Invalid mode index %d mode size %d", index, UINT32(display_attributes_.size()));
    return kErrorParameters;
  }

  current_mode_index_ = index;
  PopulateHWPanelInfo();
  UpdateMixerAttributes();

  DLOGI("Display attributes[%d]: WxH: %dx%d, DPI: %fx%f, FPS: %d, LM_SPLIT: %d, V_BACK_PORCH: %d," \
        " V_FRONT_PORCH: %d, V_PULSE_WIDTH: %d, V_TOTAL: %d, H_TOTAL: %d, CLK: %dKHZ, TOPOLOGY: %d",
        index, display_attributes_[index].x_pixels, display_attributes_[index].y_pixels,
        display_attributes_[index].x_dpi, display_attributes_[index].y_dpi,
        display_attributes_[index].fps, display_attributes_[index].is_device_split,
        display_attributes_[index].v_back_porch, display_attributes_[index].v_front_porch,
        display_attributes_[index].v_pulse_width, display_attributes_[index].v_total,
        display_attributes_[index].h_total, display_attributes_[index].clock_khz,
        display_attributes_[index].topology);

  return kErrorNone;
}

DisplayError HWDeviceDRM::SetDisplayAttributes(const HWDisplayAttributes &display_attributes) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetConfigIndex(char *mode, uint32_t *index) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::PowerOn(int *release_fence) {
  DTRACE_SCOPED();
  if (!drm_atomic_intf_) {
    DLOGE("DRM Atomic Interface is null!");
    return kErrorUndefined;
  }

  if (first_cycle_) {
    return kErrorNone;
  }

  int64_t release_fence_t = -1;
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id, DRMPowerMode::ON);
  drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence_t);

  int ret = NullCommit(true /* synchronous */, true /* retain_planes */);
  if (ret) {
    DLOGE("Failed with error: %d", ret);
    return kErrorHardware;
  }

  *release_fence = static_cast<int>(release_fence_t);
  DLOGD_IF(kTagDriverConfig, "RELEASE fence created: fd:%d", *release_fence);
  return kErrorNone;
}

DisplayError HWDeviceDRM::PowerOff() {
  DTRACE_SCOPED();
  if (!drm_atomic_intf_) {
    DLOGE("DRM Atomic Interface is null!");
    return kErrorUndefined;
  }

  SetFullROI();
  drmModeModeInfo current_mode = connector_info_.modes[current_mode_index_].mode;
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, &current_mode);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id, DRMPowerMode::OFF);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 0);
  int ret = NullCommit(true /* synchronous */, false /* retain_planes */);
  if (ret) {
    DLOGE("Failed with error: %d", ret);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::Doze(int *release_fence) {
  DTRACE_SCOPED();

  int64_t release_fence_t = -1;
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id, DRMPowerMode::DOZE);
  drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence_t);
  int ret = NullCommit(true /* synchronous */, true /* retain_planes */);
  if (ret) {
    DLOGE("Failed with error: %d", ret);
    return kErrorHardware;
  }

  *release_fence = static_cast<int>(release_fence_t);
  DLOGD_IF(kTagDriverConfig, "RELEASE fence created: fd:%d", *release_fence);
  return kErrorNone;
}

DisplayError HWDeviceDRM::DozeSuspend(int *release_fence) {
  DTRACE_SCOPED();

  int64_t release_fence_t = -1;
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id,
                            DRMPowerMode::DOZE_SUSPEND);
  drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence_t);
  int ret = NullCommit(true /* synchronous */, true /* retain_planes */);
  if (ret) {
    DLOGE("Failed with error: %d", ret);
    return kErrorHardware;
  }

  *release_fence = static_cast<int>(release_fence_t);
  DLOGD_IF(kTagDriverConfig, "RELEASE fence created: fd:%d", *release_fence);
  return kErrorNone;
}

DisplayError HWDeviceDRM::Standby() {
  return kErrorNone;
}

void HWDeviceDRM::SetupAtomic(HWLayers *hw_layers, bool validate) {
  if (default_mode_) {
    return;
  }

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());
  HWQosData &qos_data = hw_layers->qos_data;
  DRMSecurityLevel crtc_security_level = DRMSecurityLevel::SECURE_NON_SECURE;
  uint32_t index = current_mode_index_;
  drmModeModeInfo current_mode = connector_info_.modes[index].mode;

  solid_fills_.clear();

  // TODO(user): Once destination scalar is enabled we can always send ROIs if driver allows
  if (hw_panel_info_.partial_update) {
    const int kNumMaxROIs = 4;
    DRMRect crtc_rects[kNumMaxROIs] = {{0, 0, mixer_attributes_.width, mixer_attributes_.height}};
    DRMRect conn_rects[kNumMaxROIs] = {{0, 0, display_attributes_[index].x_pixels,
                                        display_attributes_[index].y_pixels}};

    for (uint32_t i = 0; i < hw_layer_info.left_frame_roi.size(); i++) {
      auto &roi = hw_layer_info.left_frame_roi.at(i);
      // TODO(user): In multi PU, stitch ROIs vertically adjacent and upate plane destination
      crtc_rects[i].left = UINT32(roi.left);
      crtc_rects[i].right = UINT32(roi.right);
      crtc_rects[i].top = UINT32(roi.top);
      crtc_rects[i].bottom = UINT32(roi.bottom);
      // TODO(user): In Dest scaler + PU, populate from HWDestScaleInfo->panel_roi
      // TODO(user): panel_roi need to be made as a vector in HWLayersInfo and
      // needs to be removed from  HWDestScaleInfo.
      conn_rects[i].left = UINT32(roi.left);
      conn_rects[i].right = UINT32(roi.right);
      conn_rects[i].top = UINT32(roi.top);
      conn_rects[i].bottom = UINT32(roi.bottom);
    }

    uint32_t num_rects = std::max(1u, static_cast<uint32_t>(hw_layer_info.left_frame_roi.size()));
    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ROI, token_.crtc_id,
                              num_rects, crtc_rects);
    drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_ROI, token_.conn_id,
                              num_rects, conn_rects);
  }

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = &layer.input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (hw_layers->config[i].use_solidfill_stage) {
      hw_layers->config[i].hw_solidfill_stage.solid_fill_info = layer.solid_fill_info;
      AddSolidfillStage(hw_layers->config[i].hw_solidfill_stage, layer.plane_alpha);
      continue;
    }

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotator_session->mode == kRotatorOffline && hw_rotate_info->valid) {
        input_buffer = &hw_rotator_session->output_buffer;
      }

      uint32_t fb_id = registry_.GetFbId(input_buffer->planes[0].fd);
      if (pipe_info->valid && fb_id) {
        uint32_t pipe_id = pipe_info->pipe_id;
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ALPHA, pipe_id, layer.plane_alpha);

        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ZORDER, pipe_id, pipe_info->z_order);
        DRMBlendType blending = {};
        SetBlending(layer.blending, &blending);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_BLEND_TYPE, pipe_id, blending);
        DRMRect src = {};
        SetRect(pipe_info->src_roi, &src);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SRC_RECT, pipe_id, src);
        DRMRect rot_dst = {0, 0, 0, 0};
        if (hw_rotator_session->mode == kRotatorInline && hw_rotate_info->valid) {
          SetRect(hw_rotate_info->dst_roi, &rot_dst);
          drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ROTATION_DST_RECT, pipe_id, rot_dst);
          if (hw_rotator_session->output_buffer.planes[0].fd >= 0) {
            uint32_t rot_fb_id = registry_.GetFbId(hw_rotator_session->output_buffer.planes[0].fd);
            if (rot_fb_id) {
              drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ROT_FB_ID, pipe_id, rot_fb_id);
            }
          }
        }
        DRMRect dst = {};
        SetRect(pipe_info->dst_roi, &dst);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_DST_RECT, pipe_id, dst);
        DRMRect excl = {};
        SetRect(pipe_info->excl_rect, &excl);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_EXCL_RECT, pipe_id, excl);
        uint32_t rot_bit_mask = 0;
        SetRotation(layer.transform, hw_rotator_session->mode, &rot_bit_mask);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ROTATION, pipe_id, rot_bit_mask);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_H_DECIMATION, pipe_id,
                                  pipe_info->horizontal_decimation);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_V_DECIMATION, pipe_id,
                                  pipe_info->vertical_decimation);

        DRMSecureMode fb_secure_mode;
        DRMSecurityLevel security_level;
        SetSecureConfig(layer.input_buffer, &fb_secure_mode, &security_level);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_FB_SECURE_MODE, pipe_id, fb_secure_mode);
        if (security_level > crtc_security_level) {
          crtc_security_level = security_level;
        }

        uint32_t config = 0;
        SetSrcConfig(layer.input_buffer, hw_rotator_session->mode, &config);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SRC_CONFIG, pipe_id, config);;
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_FB_ID, pipe_id, fb_id);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_CRTC, pipe_id, token_.crtc_id);
        if (!validate && input_buffer->acquire_fence_fd >= 0) {
          drm_atomic_intf_->Perform(DRMOps::PLANE_SET_INPUT_FENCE, pipe_id,
                                    input_buffer->acquire_fence_fd);
        }
        if (hw_scale_) {
          SDEScaler scaler_output = {};
          hw_scale_->SetScaler(pipe_info->scale_data, &scaler_output);
          // TODO(user): Remove qseed3 and add version check, then send appropriate scaler object
          if (hw_resource_.has_qseed3) {
            drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SCALER_CONFIG, pipe_id,
                                      reinterpret_cast<uint64_t>(&scaler_output.scaler_v2));
          }
        }

        DRMCscType csc_type = DRMCscType::kCscTypeMax;
        SelectCscType(layer.input_buffer, &csc_type);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_CSC_CONFIG, pipe_id, &csc_type);

        DRMMultiRectMode multirect_mode;
        SetMultiRectMode(pipe_info->flags, &multirect_mode);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_MULTIRECT_MODE, pipe_id, multirect_mode);

        SetSsppTonemapFeatures(pipe_info);
      }
    }
  }

  SetSolidfillStages();
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_CORE_CLK, token_.crtc_id, qos_data.clock_hz);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_CORE_AB, token_.crtc_id, qos_data.core_ab_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_CORE_IB, token_.crtc_id, qos_data.core_ib_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_LLCC_AB, token_.crtc_id, qos_data.llcc_ab_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_LLCC_IB, token_.crtc_id, qos_data.llcc_ib_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_DRAM_AB, token_.crtc_id, qos_data.dram_ab_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_DRAM_IB, token_.crtc_id, qos_data.dram_ib_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ROT_PREFILL_BW, token_.crtc_id,
                            qos_data.rot_prefill_bw_bps);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ROT_CLK, token_.crtc_id, qos_data.rot_clock_hz);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_SECURITY_LEVEL, token_.crtc_id, crtc_security_level);
  drm_atomic_intf_->Perform(DRMOps::DPPS_COMMIT_FEATURE, 0 /* argument is not used */);

  if (!validate) {
    drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence_);
    drm_atomic_intf_->Perform(DRMOps::CONNECTOR_GET_RETIRE_FENCE, token_.conn_id, &retire_fence_);
  }

  DLOGI_IF(kTagDriverConfig, "%s::%s System Clock=%d Hz, Core: AB=%llu Bps, IB=%llu Bps, " \
           "LLCC: AB=%llu Bps, IB=%llu Bps, DRAM AB=%llu Bps, IB=%llu Bps, "\
           "Rot: Bw=%llu Bps, Clock=%d Hz", validate ? "Validate" : "Commit", device_name_,
           qos_data.clock_hz, qos_data.core_ab_bps, qos_data.core_ib_bps, qos_data.llcc_ab_bps,
           qos_data.llcc_ib_bps, qos_data.dram_ab_bps, qos_data.dram_ib_bps,
           qos_data.rot_prefill_bw_bps, qos_data.rot_clock_hz);

  // Set refresh rate
  if (vrefresh_) {
    for (uint32_t mode_index = 0; mode_index < connector_info_.modes.size(); mode_index++) {
      if ((current_mode.vdisplay == connector_info_.modes[mode_index].mode.vdisplay) &&
          (current_mode.hdisplay == connector_info_.modes[mode_index].mode.hdisplay) &&
          (vrefresh_ == connector_info_.modes[mode_index].mode.vrefresh)) {
        current_mode = connector_info_.modes[mode_index].mode;
        break;
      }
    }
  }

  if (first_cycle_) {
    drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_CRTC, token_.conn_id, token_.crtc_id);
    drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POWER_MODE, token_.conn_id, DRMPowerMode::ON);
  }

  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, &current_mode);
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);

  if (!validate && (hw_layer_info.set_idle_time_ms >= 0)) {
    DLOGI_IF(kTagDriverConfig, "Setting idle timeout to = %d ms",
             hw_layer_info.set_idle_time_ms);
    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_IDLE_TIMEOUT, token_.crtc_id,
                              hw_layer_info.set_idle_time_ms);
  }

  if (hw_panel_info_.mode == kModeCommand) {
    drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_AUTOREFRESH, token_.conn_id, autorefresh_);
  }
}

void HWDeviceDRM::AddSolidfillStage(const HWSolidfillStage &sf, uint32_t plane_alpha) {
  sde_drm::DRMSolidfillStage solidfill;
  solidfill.bounding_rect.left = UINT32(sf.roi.left);
  solidfill.bounding_rect.top = UINT32(sf.roi.top);
  solidfill.bounding_rect.right = UINT32(sf.roi.right);
  solidfill.bounding_rect.bottom = UINT32(sf.roi.bottom);
  solidfill.is_exclusion_rect  = sf.is_exclusion_rect;
  solidfill.plane_alpha = plane_alpha;
  solidfill.z_order = sf.z_order;
  if (!sf.solid_fill_info.bit_depth) {
    solidfill.color_bit_depth = 8;
    solidfill.alpha = (0xff000000 & sf.color) >> 24;
    solidfill.red = (0xff0000 & sf.color) >> 16;
    solidfill.green = (0xff00 & sf.color) >> 8;
    solidfill.blue = 0xff & sf.color;
  } else {
    solidfill.color_bit_depth = sf.solid_fill_info.bit_depth;
    solidfill.alpha = sf.solid_fill_info.alpha;
    solidfill.red = sf.solid_fill_info.red;
    solidfill.green = sf.solid_fill_info.green;
    solidfill.blue = sf.solid_fill_info.blue;
  }
  solid_fills_.push_back(solidfill);
  DLOGI_IF(kTagDriverConfig, "Add a solidfill stage at z_order:%d argb_color:%x plane_alpha:%x",
           solidfill.z_order, solidfill.color, solidfill.plane_alpha);
}

void HWDeviceDRM::SetSolidfillStages() {
  if (hw_resource_.num_solidfill_stages) {
    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_SOLIDFILL_STAGES, token_.crtc_id,
                              reinterpret_cast<uint64_t> (&solid_fills_));
  }
}

DisplayError HWDeviceDRM::Validate(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError err = kErrorNone;
  registry_.Register(hw_layers);
  SetupAtomic(hw_layers, true /* validate */);

  int ret = drm_atomic_intf_->Validate();
  if (ret) {
    DLOGE("failed with error %d for %s", ret, device_name_);
    vrefresh_ = 0;
    err = kErrorHardware;
  }

  registry_.Unregister();
  return err;
}

DisplayError HWDeviceDRM::Commit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError err = kErrorNone;
  registry_.Register(hw_layers);

  if (default_mode_) {
    err = DefaultCommit(hw_layers);
  } else {
    err = AtomicCommit(hw_layers);
  }

  registry_.Next();
  registry_.Unregister();

  return err;
}

DisplayError HWDeviceDRM::DefaultCommit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  stack->retire_fence_fd = -1;
  for (Layer &layer : hw_layer_info.hw_layers) {
    layer.input_buffer.release_fence_fd = -1;
  }

  DRMMaster *master = nullptr;
  int ret = DRMMaster::GetInstance(&master);
  if (ret < 0) {
    DLOGE("Failed to acquire DRMMaster instance");
    return kErrorResources;
  }

  DRMResMgr *res_mgr = nullptr;
  ret = DRMResMgr::GetInstance(&res_mgr);
  if (ret < 0) {
    DLOGE("Failed to acquire DRMResMgr instance");
    return kErrorResources;
  }

  int dev_fd = -1;
  master->GetHandle(&dev_fd);

  uint32_t connector_id = 0;
  res_mgr->GetConnectorId(&connector_id);

  uint32_t crtc_id = 0;
  res_mgr->GetCrtcId(&crtc_id);

  drmModeModeInfo mode;
  res_mgr->GetMode(&mode);

  uint32_t fb_id = registry_.GetFbId(hw_layer_info.hw_layers.at(0).input_buffer.planes[0].fd);
  ret = drmModeSetCrtc(dev_fd, crtc_id, fb_id, 0 /* x */, 0 /* y */, &connector_id,
                       1 /* num_connectors */, &mode);
  if (ret < 0) {
    DLOGE("drmModeSetCrtc failed dev fd %d, fb_id %d, crtc id %d, connector id %d, %s", dev_fd,
          fb_id, crtc_id, connector_id, strerror(errno));
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::AtomicCommit(HWLayers *hw_layers) {
  DTRACE_SCOPED();
  SetupAtomic(hw_layers, false /* validate */);

  int ret = drm_atomic_intf_->Commit(false /* synchronous */, false /* retain_planes*/);
  if (ret) {
    DLOGE("%s failed with error %d crtc %d", __FUNCTION__, ret, token_.crtc_id);
    vrefresh_ = 0;
    return kErrorHardware;
  }

  int release_fence = static_cast<int>(release_fence_);
  int retire_fence = static_cast<int>(retire_fence_);
  DLOGD_IF(kTagDriverConfig, "RELEASE fence created: fd:%d", release_fence);
  DLOGD_IF(kTagDriverConfig, "RETIRE fence created: fd:%d", retire_fence);

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;
  stack->retire_fence_fd = retire_fence;

  for (uint32_t i = 0; i < hw_layer_info.hw_layers.size(); i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    if (hw_rotator_session->mode == kRotatorOffline) {
      hw_rotator_session->output_buffer.release_fence_fd = Sys::dup_(release_fence);
    } else {
      layer.input_buffer.release_fence_fd = Sys::dup_(release_fence);
    }
  }

  hw_layer_info.sync_handle = release_fence;

  if (vrefresh_) {
    // Update current mode index if refresh rate is changed
    drmModeModeInfo current_mode = connector_info_.modes[current_mode_index_].mode;
    for (uint32_t mode_index = 0; mode_index < connector_info_.modes.size(); mode_index++) {
      if ((current_mode.vdisplay == connector_info_.modes[mode_index].mode.vdisplay) &&
          (current_mode.hdisplay == connector_info_.modes[mode_index].mode.hdisplay) &&
          (vrefresh_ == connector_info_.modes[mode_index].mode.vrefresh)) {
        current_mode_index_ = mode_index;
        break;
      }
    }
    vrefresh_ = 0;
  }

  first_cycle_ = false;

  return kErrorNone;
}

DisplayError HWDeviceDRM::Flush() {
  int ret = NullCommit(secure_display_active_ /* synchronous */, false /* retain_planes*/);
  if (ret) {
    DLOGE("failed with error %d", ret);
    return kErrorHardware;
  }

  return kErrorNone;
}

void HWDeviceDRM::SetBlending(const LayerBlending &source, DRMBlendType *target) {
  switch (source) {
    case kBlendingPremultiplied:
      *target = DRMBlendType::PREMULTIPLIED;
      break;
    case kBlendingOpaque:
      *target = DRMBlendType::OPAQUE;
      break;
    case kBlendingCoverage:
      *target = DRMBlendType::COVERAGE;
      break;
    default:
      *target = DRMBlendType::UNDEFINED;
  }
}

void HWDeviceDRM::SetSrcConfig(const LayerBuffer &input_buffer, const HWRotatorMode &mode,
                               uint32_t *config) {
  // In offline rotation case, rotator will handle deinterlacing.
  if (mode != kRotatorOffline) {
    if (input_buffer.flags.interlace) {
      *config |= (0x01 << UINT32(DRMSrcConfig::DEINTERLACE));
    }
  }
}

void HWDeviceDRM::SelectCscType(const LayerBuffer &input_buffer, DRMCscType *type) {
  if (type == NULL) {
    return;
  }

  *type = DRMCscType::kCscTypeMax;
  if (input_buffer.format < kFormatYCbCr420Planar) {
    return;
  }

  switch (input_buffer.color_metadata.colorPrimaries) {
    case ColorPrimaries_BT601_6_525:
    case ColorPrimaries_BT601_6_625:
      *type = ((input_buffer.color_metadata.range == Range_Full) ?
               DRMCscType::kCscYuv2Rgb601FR : DRMCscType::kCscYuv2Rgb601L);
      break;
    case ColorPrimaries_BT709_5:
      *type = DRMCscType::kCscYuv2Rgb709L;
      break;
    case ColorPrimaries_BT2020:
      *type = ((input_buffer.color_metadata.range == Range_Full) ?
                DRMCscType::kCscYuv2Rgb2020FR : DRMCscType::kCscYuv2Rgb2020L);
      break;
    default:
      break;
  }
}

void HWDeviceDRM::SetRect(const LayerRect &source, DRMRect *target) {
  target->left = UINT32(source.left);
  target->top = UINT32(source.top);
  target->right = UINT32(source.right);
  target->bottom = UINT32(source.bottom);
}

void HWDeviceDRM::SetRotation(LayerTransform transform, const HWRotatorMode &mode,
                              uint32_t* rot_bit_mask) {
  // In offline rotation case, rotator will handle flips set via offline rotator interface.
  if (mode == kRotatorOffline) {
    *rot_bit_mask = 0;
    return;
  }

  // In no rotation case or inline rotation case, plane will handle flips
  // In DRM framework rotation is applied in counter-clockwise direction.
  if (mode == kRotatorInline && transform.rotation == 90) {
    // a) rotate 90 clockwise = rotate 270 counter-clockwise in DRM
    // rotate 270 is translated as hflip + vflip + rotate90
    // b) rotate 270 clockwise = rotate 90 counter-clockwise in DRM
    // c) hflip + rotate 90 clockwise = vflip + rotate 90 counter-clockwise in DRM
    // d) vflip + rotate 90 clockwise = hflip + rotate 90 counter-clockwise in DRM
    *rot_bit_mask = UINT32(DRMRotation::ROT_90);
    transform.flip_horizontal = !transform.flip_horizontal;
    transform.flip_vertical = !transform.flip_vertical;
  }

  if (transform.flip_horizontal) {
    *rot_bit_mask |= UINT32(DRMRotation::FLIP_H);
  }

  if (transform.flip_vertical) {
    *rot_bit_mask |= UINT32(DRMRotation::FLIP_V);
  }
}

bool HWDeviceDRM::EnableHotPlugDetection(int enable) {
  return true;
}

DisplayError HWDeviceDRM::SetCursorPosition(HWLayers *hw_layers, int x, int y) {
  DTRACE_SCOPED();
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  struct DRMPPFeatureInfo info = {};

  if (!hw_color_mgr_)
    return kErrorNotSupported;

  for (uint32_t i = 0; i < kMaxNumPPFeatures; i++) {
    std::vector<DRMPPFeatureID> drm_id = {};
    memset(&info, 0, sizeof(struct DRMPPFeatureInfo));
    hw_color_mgr_->ToDrmFeatureId(kDSPP, i, &drm_id);
    if (drm_id.empty())
      continue;

    info.id = drm_id.at(0);

    drm_mgr_intf_->GetCrtcPPInfo(token_.crtc_id, &info);
    vers->version[i] = hw_color_mgr_->GetFeatureVersion(info);
  }
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetPPFeatures(PPFeaturesConfig *feature_list) {
  int ret = 0;
  PPFeatureInfo *feature = NULL;

  if (!hw_color_mgr_)
    return kErrorNotSupported;

  while (true) {
    std::vector<DRMPPFeatureID> drm_id = {};
    DRMPPFeatureInfo kernel_params = {};
    bool crtc_feature = true;

    ret = feature_list->RetrieveNextFeature(&feature);
    if (ret)
      break;

    hw_color_mgr_->ToDrmFeatureId(kDSPP, feature->feature_id_, &drm_id);
    if (drm_id.empty())
      continue;

    kernel_params.id = drm_id.at(0);
    drm_mgr_intf_->GetCrtcPPInfo(token_.crtc_id, &kernel_params);
    if (kernel_params.version == std::numeric_limits<uint32_t>::max())
      crtc_feature = false;
    if (feature) {
      DLOGV_IF(kTagDriverConfig, "feature_id = %d", feature->feature_id_);
      for (DRMPPFeatureID id : drm_id) {
        if (id >= kPPFeaturesMax) {
          DLOGE("Invalid feature id %d", id);
          continue;
        }
        kernel_params.id = id;
        ret = hw_color_mgr_->GetDrmFeature(feature, &kernel_params);
        if (!ret && crtc_feature)
          drm_atomic_intf_->Perform(DRMOps::CRTC_SET_POST_PROC,
                                    token_.crtc_id, &kernel_params);
        else if (!ret && !crtc_feature)
          drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_POST_PROC,
                                    token_.conn_id, &kernel_params);

        hw_color_mgr_->FreeDrmFeatureData(&kernel_params);
      }
    }
  }

  // Once all features were consumed, then destroy all feature instance from feature_list,
  feature_list->Reset();

  return kErrorNone;
}

DisplayError HWDeviceDRM::SetVSyncState(bool enable) {
  return kErrorNotSupported;
}

void HWDeviceDRM::SetIdleTimeoutMs(uint32_t timeout_ms) {
  // TODO(user): This function can be removed after fb is deprecated
}

DisplayError HWDeviceDRM::SetDisplayMode(const HWDisplayMode hw_display_mode) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetRefreshRate(uint32_t refresh_rate) {
  // Check if requested refresh rate is valid
  drmModeModeInfo current_mode = connector_info_.modes[current_mode_index_].mode;
  for (uint32_t mode_index = 0; mode_index < connector_info_.modes.size(); mode_index++) {
    if ((current_mode.vdisplay == connector_info_.modes[mode_index].mode.vdisplay) &&
        (current_mode.hdisplay == connector_info_.modes[mode_index].mode.hdisplay) &&
        (refresh_rate == connector_info_.modes[mode_index].mode.vrefresh)) {
      vrefresh_ = refresh_rate;
      DLOGV_IF(kTagDriverConfig, "Set refresh rate to %d", refresh_rate);
      return kErrorNone;
    }
  }
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetPanelBrightness(int level) {
  DisplayError err = kErrorNone;
  char buffer[kMaxSysfsCommandLength] = {0};

  DLOGV_IF(kTagDriverConfig, "Set brightness level to %d", level);
  int fd = Sys::open_(kBrightnessNode, O_RDWR);
  if (fd < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to open node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    return kErrorFileDescriptor;
  }

  int32_t bytes = snprintf(buffer, kMaxSysfsCommandLength, "%d\n", level);
  ssize_t ret = Sys::pwrite_(fd, buffer, static_cast<size_t>(bytes), 0);
  if (ret <= 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to write to node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    err = kErrorHardware;
  }

  Sys::close_(fd);

  return err;
}

DisplayError HWDeviceDRM::GetPanelBrightness(int *level) {
  DisplayError err = kErrorNone;
  char brightness[kMaxStringLength] = {0};

  if (!level) {
    DLOGV_IF(kTagDriverConfig, "Invalid input, null pointer.");
    return kErrorParameters;
  }

  int fd = Sys::open_(kBrightnessNode, O_RDWR);
  if (fd < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to open brightness node = %s, error = %s", kBrightnessNode,
             strerror(errno));
    return kErrorFileDescriptor;
  }

  if (Sys::pread_(fd, brightness, sizeof(brightness), 0) > 0) {
    *level = atoi(brightness);
    DLOGV_IF(kTagDriverConfig, "Brightness level = %d", *level);
  } else {
    DLOGV_IF(kTagDriverConfig, "Failed to read panel brightness");
    err = kErrorHardware;
  }

  Sys::close_(fd);

  return err;
}

DisplayError HWDeviceDRM::GetHWScanInfo(HWScanInfo *scan_info) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetMaxCEAFormat(uint32_t *max_cea_format) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetS3DMode(HWS3DMode s3d_mode) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetScaleLutConfig(HWScaleLutInfo *lut_info) {
  sde_drm::DRMScalerLUTInfo drm_lut_info = {};
  drm_lut_info.cir_lut = lut_info->cir_lut;
  drm_lut_info.dir_lut = lut_info->dir_lut;
  drm_lut_info.sep_lut = lut_info->sep_lut;
  drm_lut_info.cir_lut_size = lut_info->cir_lut_size;
  drm_lut_info.dir_lut_size = lut_info->dir_lut_size;
  drm_lut_info.sep_lut_size = lut_info->sep_lut_size;
  drm_mgr_intf_->SetScalerLUT(drm_lut_info);

  return kErrorNone;
}

DisplayError HWDeviceDRM::SetMixerAttributes(const HWMixerAttributes &mixer_attributes) {
  if (IsResolutionSwitchEnabled()) {
    return kErrorNotSupported;
  }

  if (!hw_resource_.hw_dest_scalar_info.count) {
    return kErrorNotSupported;
  }

  uint32_t index = current_mode_index_;

  if (mixer_attributes.width > display_attributes_[index].x_pixels ||
      mixer_attributes.height > display_attributes_[index].y_pixels) {
    DLOGW("Input resolution exceeds display resolution! input: res %dx%d display: res %dx%d",
          mixer_attributes.width, mixer_attributes.height, display_attributes_[index].x_pixels,
          display_attributes_[index].y_pixels);
    return kErrorNotSupported;
  }

  uint32_t max_input_width = hw_resource_.hw_dest_scalar_info.max_input_width;
  if (display_attributes_[index].is_device_split) {
    max_input_width *= 2;
  }

  if (mixer_attributes.width > max_input_width) {
    DLOGW("Input width exceeds width limit! input_width %d width_limit %d", mixer_attributes.width,
          max_input_width);
    return kErrorNotSupported;
  }

  float mixer_aspect_ratio = FLOAT(mixer_attributes.width) / FLOAT(mixer_attributes.height);
  float display_aspect_ratio =
      FLOAT(display_attributes_[index].x_pixels) / FLOAT(display_attributes_[index].y_pixels);

  if (display_aspect_ratio != mixer_aspect_ratio) {
    DLOGW("Aspect ratio mismatch! input: res %dx%d display: res %dx%d", mixer_attributes.width,
          mixer_attributes.height, display_attributes_[index].x_pixels,
          display_attributes_[index].y_pixels);
    return kErrorNotSupported;
  }

  float scale_x = FLOAT(display_attributes_[index].x_pixels) / FLOAT(mixer_attributes.width);
  float scale_y = FLOAT(display_attributes_[index].y_pixels) / FLOAT(mixer_attributes.height);
  float max_scale_up = hw_resource_.hw_dest_scalar_info.max_scale_up;
  if (scale_x > max_scale_up || scale_y > max_scale_up) {
    DLOGW(
        "Up scaling ratio exceeds for destination scalar upscale limit scale_x %f scale_y %f "
        "max_scale_up %f",
        scale_x, scale_y, max_scale_up);
    return kErrorNotSupported;
  }

  float mixer_split_ratio = FLOAT(mixer_attributes_.split_left) / FLOAT(mixer_attributes_.width);

  mixer_attributes_ = mixer_attributes;
  mixer_attributes_.split_left = mixer_attributes_.width;
  if (display_attributes_[index].is_device_split) {
    mixer_attributes_.split_left = UINT32(FLOAT(mixer_attributes.width) * mixer_split_ratio);
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::GetMixerAttributes(HWMixerAttributes *mixer_attributes) {
  if (!mixer_attributes) {
    return kErrorParameters;
  }

  *mixer_attributes = mixer_attributes_;

  return kErrorNone;
}

DisplayError HWDeviceDRM::DumpDebugData() {
  string dir_path = "/data/vendor/display/hw_recovery/";
  string device_str = device_name_;

  // Attempt to make hw_recovery dir, it may exist
  if (mkdir(dir_path.c_str(), 0777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path.c_str(), errno,
          strerror(errno));
    return kErrorPermission;
  }
  // If it does exist, ensure permissions are fine
  if (errno == EEXIST && chmod(dir_path.c_str(), 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path.c_str());
    return kErrorPermission;
  }

  string filename = dir_path+device_str+"_HWR_"+to_string(debug_dump_count_);
  ofstream dst(filename);
  ifstream src;
  debug_dump_count_++;

  src.open("/sys/kernel/debug/dri/0/debug/dump");
  dst << "---- Event Logs ----" << std::endl;
  dst << src.rdbuf() << std::endl;
  src.close();

  src.open("/sys/kernel/debug/dri/0/debug/recovery_reg");
  dst << "---- All Registers ----" << std::endl;
  dst << src.rdbuf() << std::endl;
  src.close();

  src.open("/sys/kernel/debug/dri/0/debug/recovery_dbgbus");
  dst << "---- Debug Bus ----" << std::endl;
  dst << src.rdbuf() << std::endl;
  src.close();

  src.open("/sys/kernel/debug/dri/0/debug/recovery_vbif_dbgbus");
  dst << "---- VBIF Debug Bus ----" << std::endl;
  dst << src.rdbuf() << std::endl;
  src.close();

  dst.close();

  DLOGI("Wrote hw_recovery file %s", filename.c_str());

  return kErrorNone;
}

void HWDeviceDRM::GetDRMDisplayToken(sde_drm::DRMDisplayToken *token) const {
  *token = token_;
}

void HWDeviceDRM::UpdateMixerAttributes() {
  uint32_t index = current_mode_index_;

  mixer_attributes_.width = display_attributes_[index].x_pixels;
  mixer_attributes_.height = display_attributes_[index].y_pixels;
  mixer_attributes_.split_left = display_attributes_[index].is_device_split
                                     ? hw_panel_info_.split_info.left_split
                                     : mixer_attributes_.width;
  DLOGI("Mixer WxH %dx%d for %s", mixer_attributes_.width, mixer_attributes_.height, device_name_);
}

void HWDeviceDRM::SetSecureConfig(const LayerBuffer &input_buffer, DRMSecureMode *fb_secure_mode,
                                  DRMSecurityLevel *security_level) {
  *fb_secure_mode = DRMSecureMode::NON_SECURE;
  *security_level = DRMSecurityLevel::SECURE_NON_SECURE;

  if (input_buffer.flags.secure) {
    if (input_buffer.flags.secure_camera) {
      // IOMMU configuration for this framebuffer mode is secure domain & requires
      // only stage II translation, when this buffer is accessed by Display H/W.
      // Secure and non-secure planes can be attached to this CRTC.
      *fb_secure_mode = DRMSecureMode::SECURE_DIR_TRANSLATION;
    } else if (input_buffer.flags.secure_display) {
      // IOMMU configuration for this framebuffer mode is secure domain & requires
      // only stage II translation, when this buffer is accessed by Display H/W.
      // Only secure planes can be attached to this CRTC.
      *fb_secure_mode = DRMSecureMode::SECURE_DIR_TRANSLATION;
      *security_level = DRMSecurityLevel::SECURE_ONLY;
    } else {
      // IOMMU configuration for this framebuffer mode is secure domain & requires both
      // stage I and stage II translations, when this buffer is accessed by Display H/W.
      // Secure and non-secure planes can be attached to this CRTC.
      *fb_secure_mode = DRMSecureMode::SECURE;
    }
  }
}

void HWDeviceDRM::SetTopology(sde_drm::DRMTopology drm_topology, HWTopology *hw_topology) {
  switch (drm_topology) {
    case DRMTopology::SINGLE_LM:          *hw_topology = kSingleLM;        break;
    case DRMTopology::SINGLE_LM_DSC:      *hw_topology = kSingleLMDSC;     break;
    case DRMTopology::DUAL_LM:            *hw_topology = kDualLM;          break;
    case DRMTopology::DUAL_LM_DSC:        *hw_topology = kDualLMDSC;       break;
    case DRMTopology::DUAL_LM_MERGE:      *hw_topology = kDualLMMerge;     break;
    case DRMTopology::DUAL_LM_MERGE_DSC:  *hw_topology = kDualLMMergeDSC;  break;
    case DRMTopology::DUAL_LM_DSCMERGE:   *hw_topology = kDualLMDSCMerge;  break;
    case DRMTopology::PPSPLIT:            *hw_topology = kPPSplit;         break;
    default:                              *hw_topology = kUnknown;         break;
  }
}


void HWDeviceDRM::SetMultiRectMode(const uint32_t flags, DRMMultiRectMode *target) {
  *target = DRMMultiRectMode::NONE;
  if (flags & kMultiRect) {
    *target = DRMMultiRectMode::SERIAL;
    if (flags & kMultiRectParallelMode) {
      *target = DRMMultiRectMode::PARALLEL;
    }
  }
}

void HWDeviceDRM::SetSsppTonemapFeatures(HWPipeInfo *pipe_info) {
  if (pipe_info->dgm_csc_info.op != kNoOp) {
    SDECsc csc = {};
    SetDGMCsc(pipe_info->dgm_csc_info, &csc);
    DLOGV_IF(kTagDriverConfig, "Call Perform DGM CSC Op = %s",
            (pipe_info->dgm_csc_info.op == kSet) ? "Set" : "Reset");
    drm_atomic_intf_->Perform(DRMOps::PLANE_SET_DGM_CSC_CONFIG, pipe_info->pipe_id,
                              reinterpret_cast<uint64_t>(&csc.csc_v1));
  }
  if (pipe_info->inverse_pma_info.op != kNoOp) {
    DLOGV_IF(kTagDriverConfig, "Call Perform Inverse PMA Op = %s",
            (pipe_info->inverse_pma_info.op == kSet) ? "Set" : "Reset");
    drm_atomic_intf_->Perform(DRMOps::PLANE_SET_INVERSE_PMA, pipe_info->pipe_id,
                             (pipe_info->inverse_pma_info.inverse_pma) ? 1: 0);
  }
  SetSsppLutFeatures(pipe_info);
}

void HWDeviceDRM::SetDGMCsc(const HWPipeCscInfo &dgm_csc_info, SDECsc *csc) {
  SetDGMCscV1(dgm_csc_info.csc, &csc->csc_v1);
}

void HWDeviceDRM::SetDGMCscV1(const HWCsc &dgm_csc, sde_drm_csc_v1 *csc_v1) {
  uint32_t i = 0;
  for (i = 0; i < MAX_CSC_MATRIX_COEFF_SIZE; i++) {
    csc_v1->ctm_coeff[i] = dgm_csc.ctm_coeff[i];
    DLOGV_IF(kTagDriverConfig, " DGM csc_v1[%d] = %d", i, csc_v1->ctm_coeff[i]);
  }
  for (i = 0; i < MAX_CSC_BIAS_SIZE; i++) {
    csc_v1->pre_bias[i] = dgm_csc.pre_bias[i];
    csc_v1->post_bias[i] = dgm_csc.post_bias[i];
  }
  for (i = 0; i < MAX_CSC_CLAMP_SIZE; i++) {
    csc_v1->pre_clamp[i] = dgm_csc.pre_clamp[i];
    csc_v1->post_clamp[i] = dgm_csc.post_clamp[i];
  }
}

void HWDeviceDRM::SetSsppLutFeatures(HWPipeInfo *pipe_info) {
  for (HWPipeTonemapLutInfo &lut_info : pipe_info->lut_info) {
    if (lut_info.op != kNoOp) {
      std::shared_ptr<PPFeatureInfo> feature = lut_info.pay_load;
      if (feature == nullptr) {
        DLOGE("Null Pointer for Op = %d lut type = %d", lut_info.op, lut_info.type);
        continue;
      }
      DRMPPFeatureInfo kernel_params = {};
      std::vector<DRMPPFeatureID> drm_id = {};
      PPBlock pp_block = GetPPBlock(lut_info.type);
      hw_color_mgr_->ToDrmFeatureId(pp_block, feature->feature_id_, &drm_id);
      for (DRMPPFeatureID id : drm_id) {
        if (id >= kPPFeaturesMax) {
          DLOGE("Invalid feature id %d", id);
          continue;
        }
        kernel_params.id = id;
        bool disable = (lut_info.op == kReset);
        DLOGV_IF(kTagDriverConfig, "Lut Type = %d PPBlock = %d Op = %s Disable = %d Feature = %p",
                 lut_info.type, pp_block, (lut_info.op ==kSet) ? "Set" : "Reset", disable,
                 feature.get());
        int ret = hw_color_mgr_->GetDrmFeature(feature.get(), &kernel_params, disable);
        if (!ret) {
          drm_atomic_intf_->Perform(DRMOps::PLANE_SET_POST_PROC, pipe_info->pipe_id,
                                    &kernel_params);
          hw_color_mgr_->FreeDrmFeatureData(&kernel_params);
        } else {
          DLOGE("GetDrmFeature failed for Lut type = %d", lut_info.type);
        }
      }
      drm_id.clear();
    }
  }
}

void HWDeviceDRM::AddDimLayerIfNeeded() {
  if (secure_display_active_ && hw_resource_.secure_disp_blend_stage >= 0) {
    HWSolidfillStage sf = {};
    sf.z_order = UINT32(hw_resource_.secure_disp_blend_stage);
    sf.roi = { 0.0, 0.0, FLOAT(mixer_attributes_.width), FLOAT(mixer_attributes_.height) };
    solid_fills_.clear();
    AddSolidfillStage(sf, 0xFF);
    SetSolidfillStages();
  }
}

DisplayError HWDeviceDRM::NullCommit(bool synchronous, bool retain_planes) {
  DTRACE_SCOPED();
  AddDimLayerIfNeeded();
  int ret = drm_atomic_intf_->Commit(synchronous , retain_planes);
  if (ret) {
    DLOGE("failed with error %d", ret);
    return kErrorHardware;
  }

  return kErrorNone;
}

void HWDeviceDRM::DumpConnectorModeInfo() {
  for (uint32_t i = 0; i < (uint32_t)connector_info_.modes.size(); i++) {
    DLOGI("Mode[%d] Name:%s vref:%d hdisp:%d hsync_s:%d hsync_e:%d htotal:%d " \
          "vdisp:%d vsync_s:%d vsync_e:%d vtotal:%d\n", i, connector_info_.modes[i].mode.name,
          connector_info_.modes[i].mode.vrefresh, connector_info_.modes[i].mode.hdisplay,
          connector_info_.modes[i].mode.hsync_start, connector_info_.modes[i].mode.hsync_end,
          connector_info_.modes[i].mode.htotal, connector_info_.modes[i].mode.vdisplay,
          connector_info_.modes[i].mode.vsync_start, connector_info_.modes[i].mode.vsync_end,
          connector_info_.modes[i].mode.vtotal);
  }
}

void HWDeviceDRM::SetFullROI() {
  // Reset the CRTC ROI and connector ROI only for the panel that supports partial update
  if (!hw_panel_info_.partial_update) {
    return;
  }
  uint32_t index = current_mode_index_;
  DRMRect crtc_rects = {0, 0, mixer_attributes_.width, mixer_attributes_.height};
  DRMRect conn_rects = {0, 0, display_attributes_[index].x_pixels,
                         display_attributes_[index].y_pixels};
  drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ROI, token_.crtc_id, 1, &crtc_rects);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_SET_ROI, token_.conn_id, 1, &conn_rects);
}

}  // namespace sdm

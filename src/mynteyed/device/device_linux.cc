// Copyright 2018 Slightech Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "mynteyed/device/device.h"

#ifdef MYNTEYE_OS_LINUX

#include <algorithm>

#include "mynteyed/device/convertor.h"
#include "mynteyed/util/log.h"

MYNTEYE_USE_NAMESPACE

void Device::OnInit() {
  dtc_ = DEPTH_IMG_NON_TRANSFER;
}

// int ret = EtronDI_Get2Image(etron_di_, &dev_sel_info_,
//     (BYTE*)color_img_buf_, (BYTE*)depth_img_buf_,
//     &color_image_size_, &depth_image_size_,
//     &color_serial_number_, &depth_serial_number_, depth_data_type_);

Image::pointer Device::GetImageColor() {
  unsigned int color_img_width  = (unsigned int)(
      stream_color_info_ptr_[color_res_index_].nWidth);
  unsigned int color_img_height = (unsigned int)(
      stream_color_info_ptr_[color_res_index_].nHeight);
  bool is_mjpeg = stream_color_info_ptr_[color_res_index_].bFormatMJPG;

  if (!color_image_buf_) {
    color_image_buf_ = ImageColor::Create(
      is_mjpeg ? ImageFormat::COLOR_MJPG : ImageFormat::COLOR_YUYV,
      color_img_width, color_img_height, true);
  } else {
    color_image_buf_->ResetBuffer();
  }

  int ret = EtronDI_GetColorImage(etron_di_, &dev_sel_info_,
      color_image_buf_->data(), &color_image_size_, &color_serial_number_, 0);

  if (ETronDI_OK != ret) {
    DBG_LOGI("GetImageColor: %d", ret);
    return nullptr;
  }

  if (ir_interleave_enabled_) {
    if (color_interleave_enabled_ &&
        (color_serial_number_ % 2) > 0) {
      return nullptr;
    } else if (!color_interleave_enabled_ &&
        (color_serial_number_ % 2) == 0) {
      return nullptr;
    }
  }

  color_image_buf_->set_valid_size(color_image_size_);
  color_image_buf_->set_frame_id(color_serial_number_);

  return color_image_buf_;
}

Image::pointer Device::GetImageDepth() {
  unsigned int depth_img_width  = (unsigned int)(
      stream_depth_info_ptr_[depth_res_index_].nWidth);
  unsigned int depth_img_height = (unsigned int)(
      stream_depth_info_ptr_[depth_res_index_].nHeight);

  bool depth_raw;
  if (dtc_ == DEPTH_IMG_COLORFUL_TRANSFER ||
      dtc_ == DEPTH_IMG_GRAY_TRANSFER) {
    depth_raw = false;
    if (!depth_image_buf_) {
      depth_buf_ = (unsigned char*)calloc(
          depth_img_width * 2 * depth_img_height * 3, sizeof(unsigned char));
      if (dtc_ == DEPTH_IMG_COLORFUL_TRANSFER) {
        depth_image_buf_ = ImageDepth::Create(ImageFormat::DEPTH_RGB,
            depth_img_width, depth_img_height, true);
      } else {  // DEPTH_IMG_GRAY_TRANSFER
        depth_image_buf_ = ImageDepth::Create(ImageFormat::DEPTH_GRAY_24,
            depth_img_width, depth_img_height, true);
      }
    } else {
      depth_image_buf_->ResetBuffer();
    }
  } else {  // DEPTH_IMG_NON_TRANSFER
    depth_raw = true;
    if (!depth_image_buf_) {
      depth_image_buf_ = ImageDepth::Create(ImageFormat::DEPTH_RAW,
          depth_img_width, depth_img_height, true);
    } else {
      depth_image_buf_->ResetBuffer();
    }
  }

  int ret = EtronDI_GetDepthImage(etron_di_, &dev_sel_info_,
      depth_raw ? depth_image_buf_->data() : depth_buf_,
      &depth_image_size_, &depth_serial_number_, depth_data_type_);

  if (ETronDI_OK != ret) {
    DBG_LOGI("GetImageDepth: %d", ret);
    return nullptr;
  }

  if (ir_interleave_enabled_) {
    if (depth_interleave_enabled_ &&
        (depth_serial_number_ % 2) > 0) {
      return nullptr;
    } else if (!depth_interleave_enabled_ &&
        (depth_serial_number_ % 2) == 0) {
      return nullptr;
    }
  }

  depth_image_buf_->set_frame_id(depth_serial_number_);

  if (depth_raw) {
    return depth_image_buf_;
  } else {
    depth_image_buf_->set_valid_size(depth_image_size_);

    std::copy(depth_buf_, depth_buf_ + depth_image_size_,
        depth_image_buf_->data());
    // EtronDI_Convert_Depth_Y_To_Buffer(etron_di_, &dev_sel_info_,
    //   depth_buf_, depth_image_buf_->data(),
    //   depth_img_width, depth_img_height,
    //   dtc_ == DEPTH_IMG_COLORFUL_TRANSFER ? true : false,
    //   depth_data_type_);
    return depth_image_buf_;
  }
}

int Device::OpenDevice(const DeviceMode& dev_mode) {
  switch (dev_mode) {
    case DeviceMode::DEVICE_COLOR:
      color_device_opened_ = true;
      depth_device_opened_ = false;

      return EtronDI_OpenDevice2(etron_di_, &dev_sel_info_,
          stream_color_info_ptr_[color_res_index_].nWidth,
          stream_color_info_ptr_[color_res_index_].nHeight,
          stream_color_info_ptr_[color_res_index_].bFormatMJPG,
          0, 0, dtc_, false, NULL, &framerate_);
      break;
    case DeviceMode::DEVICE_DEPTH:
      color_device_opened_ = false;
      depth_device_opened_ = true;

      return EtronDI_OpenDevice2(etron_di_, &dev_sel_info_,
          0, 0, false, stream_depth_info_ptr_[depth_res_index_].nWidth,
          stream_depth_info_ptr_[depth_res_index_].nHeight,
          dtc_, false, NULL, &framerate_);
      break;
    case DeviceMode::DEVICE_ALL:
      color_device_opened_ = true;
      depth_device_opened_ = true;

      return EtronDI_OpenDevice2(etron_di_, &dev_sel_info_,
          stream_color_info_ptr_[color_res_index_].nWidth,
          stream_color_info_ptr_[color_res_index_].nHeight,
          stream_color_info_ptr_[color_res_index_].bFormatMJPG,
          stream_depth_info_ptr_[depth_res_index_].nWidth,
          stream_depth_info_ptr_[depth_res_index_].nHeight,
          dtc_, false, NULL, &framerate_);
      break;
    default:
      throw_error("ERROR:: DeviceMode is unknown.");
  }
}

#endif
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
#include <string.h>
#include <fstream>

#include <stdexcept>
#include <string>
#include <chrono>

#include "mynteye/internal/camera_p.h"
#include "mynteye/internal/channels.h"
#include "mynteye/util/log.h"
#include "mynteye/util/rate.h"
#include "mynteye/util/times.h"

MYNTEYE_USE_NAMESPACE

namespace {

void get_stream_size(const StreamMode& stream_mode, int* width, int* height) {
  switch (stream_mode) {
    case StreamMode::STREAM_1280x480:
      *width = 1280;
      *height = 480;
      break;
    case StreamMode::STREAM_1280x720:
      *width = 1280;
      *height = 720;
      break;
    case StreamMode::STREAM_2560x720:
      *width = 2560;
      *height = 720;
      break;
    // case StreamMode::STREAM_2560x960:
    //   *width = 2560;
    //   *height = 960;
    //   break;
    case StreamMode::STREAM_640x480:
      *width = 640;
      *height = 480;
      break;
    default:
      throw new std::runtime_error("StreamMode is unknown");
  }
}

std::string get_stream_format_string(const StreamFormat& stream_format) {
  switch (stream_format) {
    case StreamFormat::STREAM_MJPG:
      return "MJPG";
    case StreamFormat::STREAM_YUYV:
      return "YUYV";
    default:
      throw new std::runtime_error("StreamFormat is unknown");
  }
}

void matrix_3x1(const double (*src1)[3], const double (*src2)[1],
    double (*dst)[1]) {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 1; j++) {
      for (int k = 0; k < 3; k++) {
        dst[i][j] += src1[i][k] * src2[k][j];
      }
    }
  }
}

void matrix_3x3(const double (*src1)[3], const double (*src2)[3],
    double (*dst)[3]) {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      for (int k = 0; k < 3; k++) {
        dst[i][j] += src1[i][k] * src2[k][j];
      }
    }
  }
}

}  // namespace

CameraPrivate::CameraPrivate()
  : etron_di_(nullptr), dev_sel_info_({-1}), rate_(nullptr) {
  DBG_LOGD(__func__);

  Init();
  if (is_hid_exist_) {
    ReadAllInfos();
  }
}

void CameraPrivate::Init() {
  int ret = EtronDI_Init(&etron_di_, false);
  DBG_LOGI("MYNTEYE Init: %d", ret);
  UNUSED(ret);

  stream_color_info_ptr_ =
      (PETRONDI_STREAM_INFO)malloc(sizeof(ETRONDI_STREAM_INFO)*64);
  stream_depth_info_ptr_ =
      (PETRONDI_STREAM_INFO)malloc(sizeof(ETRONDI_STREAM_INFO)*64);
  // default image type
  depth_data_type_ = 9;
  // default frame rate
  framerate_ = 10;

  OnInit();

  is_enable_image_ = {{ImageType::IMAGE_LEFT_COLOR, false},
                      {ImageType::IMAGE_RIGHT_COLOR, false},
                      {ImageType::IMAGE_DEPTH, false}};

  is_process_mode_ = {{ProcessMode::ASSEMBLY, false},
                      {ProcessMode::WARM_DRIFT, false},
                      {ProcessMode::ALL, false}};

  channels_ = std::make_shared<Channels>();
  IsHidExist();
}

CameraPrivate::~CameraPrivate() {
  DBG_LOGD(__func__);
  free(stream_color_info_ptr_);
  free(stream_depth_info_ptr_);

  if (is_hid_exist_) {
    channels_->StopHidTracking();
  }
  if (is_capture_image_) {
    StopCaptureImage();
  }
  Close();
}

void CameraPrivate::GetDevices(std::vector<DeviceInfo>* dev_infos) {
  if (!dev_infos) {
    LOGE("GetDevices: dev_infos is null.");
    return;
  }
  dev_infos->clear();

  int count = EtronDI_GetDeviceNumber(etron_di_);
  DBG_LOGD("GetDevices: %d", count);

  DEVSELINFO dev_sel_info;
  DEVINFORMATION* p_dev_info =
      (DEVINFORMATION*)malloc(sizeof(DEVINFORMATION)*count);  // NOLINT

  for (int i = 0; i < count; i++) {
    dev_sel_info.index = i;

    EtronDI_GetDeviceInfo(etron_di_, &dev_sel_info, p_dev_info+i);

    char sz_buf[256];
    int actual_length = 0;
    if (ETronDI_OK == EtronDI_GetFwVersion(
        etron_di_, &dev_sel_info, sz_buf, 256, &actual_length)) {
      DeviceInfo info;
      info.index = i;
      info.name = p_dev_info[i].strDevName;
      info.type = p_dev_info[i].nDevType;
      info.pid = p_dev_info[i].wPID;
      info.vid = p_dev_info[i].wVID;
      info.chip_id = p_dev_info[i].nChipID;
      info.fw_version = sz_buf;
      dev_infos->push_back(std::move(info));
    }
  }

  free(p_dev_info);
}

void CameraPrivate::GetResolutions(const std::int32_t& dev_index,
    std::vector<StreamInfo>* color_infos,
    std::vector<StreamInfo>* depth_infos) {
  if (!color_infos) {
    LOGE("GetResolutions: color_infos is null.");
    return;
  }
  color_infos->clear();

  if (!depth_infos) {
    LOGE("GetResolutions: depth_infos is null.");
    return;
  }
  depth_infos->clear();

  memset(stream_color_info_ptr_, 0, sizeof(ETRONDI_STREAM_INFO)*64);
  memset(stream_depth_info_ptr_, 0, sizeof(ETRONDI_STREAM_INFO)*64);

  DEVSELINFO dev_sel_info{dev_index};
  EtronDI_GetDeviceResolutionList(etron_di_, &dev_sel_info, 64,
      stream_color_info_ptr_, 64, stream_depth_info_ptr_);

  PETRONDI_STREAM_INFO stream_temp_info_ptr = stream_color_info_ptr_;
  int i = 0;
  while (i < 64) {
    if (stream_temp_info_ptr->nWidth > 0) {
      StreamInfo info;
      info.index = i;
      info.width = stream_temp_info_ptr->nWidth;
      info.height = stream_temp_info_ptr->nHeight;
      info.format = stream_temp_info_ptr->bFormatMJPG ?
          StreamFormat::STREAM_MJPG : StreamFormat::STREAM_YUYV;
      color_infos->push_back(info);
    }
    stream_temp_info_ptr++;
    i++;
  }

  stream_temp_info_ptr = stream_depth_info_ptr_;
  i = 0;
  while (i < 64) {
    if (stream_temp_info_ptr->nWidth > 0) {
      StreamInfo info;
      info.index = i;
      info.width = stream_temp_info_ptr->nWidth;
      info.height = stream_temp_info_ptr->nHeight;
      info.format = stream_temp_info_ptr->bFormatMJPG ?
          StreamFormat::STREAM_MJPG : StreamFormat::STREAM_YUYV;
      depth_infos->push_back(info);
    }
    stream_temp_info_ptr++;
    i++;
  }

  stream_info_dev_index_ = dev_index;
}

void CameraPrivate::GetResolutionIndex(const InitParams& params,
    int* color_res_index,
    int* depth_res_index) {
  return GetResolutionIndex(
      params.dev_index, params.stream_mode,
      params.color_stream_format, params.depth_stream_format,
      color_res_index, depth_res_index);
}

void CameraPrivate::GetResolutionIndex(const std::int32_t& dev_index,
    const StreamMode& stream_mode,
    const StreamFormat& color_stream_format,
    const StreamFormat& depth_stream_format,
    int *color_res_index,
    int *depth_res_index) {
  if (!color_res_index) {
    LOGE("GetResolutionIndex: color_res_index is null.");
    return;
  }
  if (!depth_res_index) {
    LOGE("GetResolutionIndex: depth_res_index is null.");
    return;
  }

  *color_res_index = -1;
  *depth_res_index = -1;

  int width = 0, height = 0;
  if (is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
    get_stream_size(stream_mode_, &width, &height);
  } else {
    get_stream_size(stream_mode, &width, &height);
  }

  memset(stream_color_info_ptr_, 0, sizeof(ETRONDI_STREAM_INFO)*64);
  memset(stream_depth_info_ptr_, 0, sizeof(ETRONDI_STREAM_INFO)*64);

  DEVSELINFO dev_sel_info{dev_index};
  EtronDI_GetDeviceResolutionList(etron_di_, &dev_sel_info, 64,
      stream_color_info_ptr_, 64, stream_depth_info_ptr_);

  PETRONDI_STREAM_INFO stream_temp_info_ptr = stream_color_info_ptr_;
  int i = 0;
  while (i < 64) {
    if (stream_temp_info_ptr->nWidth == width &&
        stream_temp_info_ptr->nHeight == height &&
        color_stream_format == (stream_temp_info_ptr->bFormatMJPG ?
            StreamFormat::STREAM_MJPG : StreamFormat::STREAM_YUYV)) {
      *color_res_index = i;
      break;
    }
    stream_temp_info_ptr++;
    i++;
  }

  if (*color_res_index == -1) {
    LOGE("Error: Color Mode width[%d] height[%d] format[%s] not support. "
        "Please check the resolution list.", width, height,
        get_stream_format_string(color_stream_format));
    *color_res_index = 0;
  }

  stream_temp_info_ptr = stream_depth_info_ptr_;
  i = 0;
  while (i < 64) {
    if (stream_temp_info_ptr->nHeight == height &&
        depth_stream_format == (stream_temp_info_ptr->bFormatMJPG ?
            StreamFormat::STREAM_MJPG : StreamFormat::STREAM_YUYV)) {
      *depth_res_index = i;
      break;
    }
    stream_temp_info_ptr++;
    i++;
  }

  if (*depth_res_index == -1) {
    LOGE("Error: Depth Mode width[%d] height[%d] format[%s] not support. "
        "Please check the resolution list.", width, height,
        get_stream_format_string(depth_stream_format));
    *depth_res_index = 0;
  }
}

ErrorCode CameraPrivate::SetAutoExposureEnabled(bool enabled) {
  bool ok;
  if (enabled) {
    ok = ETronDI_OK == EtronDI_EnableAE(etron_di_, &dev_sel_info_);
  } else {
    ok = ETronDI_OK == EtronDI_DisableAE(etron_di_, &dev_sel_info_);
  }
  if (ok) {
    LOGI("-- Auto-exposure state: %s", enabled ? "enabled" : "disabled");
  } else {
    LOGW("-- %s auto-exposure failed", enabled ? "Enable" : "Disable");
  }
  return ok ? ErrorCode::SUCCESS : ErrorCode::ERROR_FAILURE;
}

ErrorCode CameraPrivate::SetAutoWhiteBalanceEnabled(bool enabled) {
  bool ok;
  if (enabled) {
    ok = ETronDI_OK == EtronDI_EnableAWB(etron_di_, &dev_sel_info_);
  } else {
    ok = ETronDI_OK == EtronDI_DisableAWB(etron_di_, &dev_sel_info_);
  }
  if (ok) {
    LOGI("-- Auto-white balance state: %s", enabled ? "enabled" : "disabled");
  } else {
    LOGW("-- %s auto-white balance failed", enabled ? "Enable" : "Disable");
  }
  return ok ? ErrorCode::SUCCESS : ErrorCode::ERROR_FAILURE;
}

bool CameraPrivate::GetSensorRegister(int id, std::uint16_t address,
    std::uint16_t* value, int flag) {
  if (!IsOpened()) return false;
#ifdef MYNTEYE_OS_WIN
  return ETronDI_OK == EtronDI_GetSensorRegister(etron_di_, &dev_sel_info_, id,
      address, value, flag, 2);
#else
  return ETronDI_OK == EtronDI_GetSensorRegister(etron_di_, &dev_sel_info_, id,
      address, value, flag, SENSOR_BOTH);
#endif
}

bool CameraPrivate::GetHWRegister(std::uint16_t address, std::uint16_t* value,
    int flag) {
  if (!IsOpened()) return false;
  return ETronDI_OK == EtronDI_GetHWRegister(etron_di_, &dev_sel_info_,
      address, value, flag);
}

bool CameraPrivate::GetFWRegister(std::uint16_t address, std::uint16_t* value,
    int flag) {
  if (!IsOpened()) return false;
  return ETronDI_OK == EtronDI_GetFWRegister(etron_di_, &dev_sel_info_, address,
      value, flag);
}

bool CameraPrivate::SetSensorRegister(int id, std::uint16_t address,
    std::uint16_t value, int flag) {
  if (!IsOpened()) return false;
#ifdef MYNTEYE_OS_WIN
  return ETronDI_OK == EtronDI_SetSensorRegister(etron_di_, &dev_sel_info_, id,
      address, value, flag, 2);
#else
  return ETronDI_OK == EtronDI_SetSensorRegister(etron_di_, &dev_sel_info_, id,
      address, value, flag, SENSOR_BOTH);
#endif
}

bool CameraPrivate::SetHWRegister(std::uint16_t address, std::uint16_t value,
    int flag) {
  if (!IsOpened()) return false;
  return ETronDI_OK == EtronDI_SetHWRegister(etron_di_, &dev_sel_info_, address,
      value, flag);
}

bool CameraPrivate::SetFWRegister(std::uint16_t address, std::uint16_t value,
    int flag) {
  if (!IsOpened()) return false;
  return ETronDI_OK == EtronDI_SetFWRegister(etron_di_, &dev_sel_info_, address,
      value, flag);
}

ErrorCode CameraPrivate::Open(const InitParams& params) {
  if (stream_mode_ == StreamMode::STREAM_2560x720 &&
      params.framerate > 30) {
    LOGI("The frame rate chosen is too large, please use a smaller frame rate.");
    return ErrorCode::ERROR_FAILURE;
  } else if (params.framerate > 60) {
    LOGI("The frame rate chosen is too large, please use a smaller frame rate.");
    return ErrorCode::ERROR_FAILURE;
  }

  dev_sel_info_.index = params.dev_index;

  EtronDI_SetDepthDataType(etron_di_, &dev_sel_info_, depth_data_type_);
  DBG_LOGI("SetDepthDataType: %d", depth_data_type_);

  SetAutoExposureEnabled(params.state_ae);
  SetAutoWhiteBalanceEnabled(params.state_awb);

  if (params.framerate > 0) framerate_ = params.framerate;
  LOGI("-- Framerate: %d", framerate_);

  rate_.reset(new Rate(framerate_));

#ifdef MYNTEYE_OS_LINUX
  std::string dtc_name = "Unknown";
  switch (params.depth_mode) {
    case DepthMode::DEPTH_GRAY:
      dtc_ = DEPTH_IMG_GRAY_TRANSFER;
      dtc_name = "Gray";
      break;
    case DepthMode::DEPTH_COLORFUL:
      dtc_ = DEPTH_IMG_COLORFUL_TRANSFER;
      dtc_name = "Colorful";
      break;
    case DepthMode::DEPTH_RAW:
    default:
      dtc_ = DEPTH_IMG_NON_TRANSFER;
      dtc_name = "Raw";
      break;
  }
#endif
  depth_mode_ = params.depth_mode;

  if (params.dev_index != stream_info_dev_index_) {
    std::vector<StreamInfo> color_infos;
    std::vector<StreamInfo> depth_infos;
    GetResolutions(params.dev_index, &color_infos, &depth_infos);
  }

  GetResolutionIndex(params, &color_res_index_, &depth_res_index_);
  LOGI("-- Color Stream: %dx%d %s",
      stream_color_info_ptr_[color_res_index_].nWidth,
      stream_color_info_ptr_[color_res_index_].nHeight,
      stream_color_info_ptr_[color_res_index_].bFormatMJPG ? "MJPG" : "YUYV");
  LOGI("-- Depth Stream: %dx%d %s",
      stream_depth_info_ptr_[depth_res_index_].nWidth,
      stream_depth_info_ptr_[depth_res_index_].nHeight,
      stream_depth_info_ptr_[depth_res_index_].bFormatMJPG ? "MJPG" : "YUYV");

  if (params.ir_intensity >= 0) {
    if (SetFWRegister(0xE0, params.ir_intensity)) {
      LOGI("-- IR intensity: %d", params.ir_intensity);
    } else {
      LOGI("-- IR intensity: %d (failed)", params.ir_intensity);
    }
  }

  ReleaseBuf();

#ifdef MYNTEYE_OS_WIN

  SetHWPostProcess(true);
  // int EtronDI_OpenDeviceEx(
  //     void* pHandleEtronDI,
  //     PDEVSELINFO pDevSelInfo,
  //     int colorStreamIndex,
  //     bool toRgb,
  //     int depthStreamIndex,
  //     int depthStreamSwitch,
  //     EtronDI_ImgCallbackFn callbackFn,
  //     void* pCallbackParam,
  //     int* pFps,
  //     BYTE ctrlMode)

  bool toRgb = false;
  // Depth0: none
  // Depth1: unshort
  // Depth2: ?
  int depthStreamSwitch = EtronDIDepthSwitch::Depth1;
  // 0x01: color and depth frame output synchrously, for depth map module only
  // 0x02: enable post-process, for Depth Map module only
  // 0x04: stitch images if this bit is set, for fisheye spherical module only
  // 0x08: use OpenCL in stitching. This bit effective only when bit-2 is set.
  BYTE ctrlMode = 0x01;

  int ret = EtronDI_OpenDeviceEx(etron_di_, &dev_sel_info_,
      color_res_index_, toRgb,
      depth_res_index_, depthStreamSwitch,
      CameraPrivate::ImgCallback, this, &framerate_, ctrlMode);
#else
  int ret = EtronDI_OpenDevice2(etron_di_, &dev_sel_info_,
      stream_color_info_ptr_[color_res_index_].nWidth,
      stream_color_info_ptr_[color_res_index_].nHeight,
      stream_color_info_ptr_[color_res_index_].bFormatMJPG,
      stream_depth_info_ptr_[depth_res_index_].nWidth,
      stream_depth_info_ptr_[depth_res_index_].nHeight,
      dtc_, false, NULL, &framerate_);
#endif

  if (ETronDI_OK == ret) {
    if (is_hid_exist_) {
      if (!StartHidTracking()) {
        return ErrorCode::ERROR_IMU_OPEN_FAILED;
      }
    }
    StartCaptureImage();
    StartSyntheticImage();
    SyncCameraLogData();
    /*
    unsigned char pdata[100] = {};
    int plen, nbufferSize = 9;
    EtronDI_GetSerialNumber(etron_di_, &dev_sel_info_, pdata, nbufferSize, &plen);
    printf ("pdata = %s, nbufferSize = %d, plen = %d\n", pdata, nbufferSize, plen);
    */
    return ErrorCode::SUCCESS;
  } else {
    dev_sel_info_.index = -1;  // reset flag
    return ErrorCode::ERROR_CAMERA_OPEN_FAILED;
  }
}

bool CameraPrivate::IsOpened() const {
  return dev_sel_info_.index != -1;
}

void CameraPrivate::CheckOpened() const {
  if (!IsOpened()) throw std::runtime_error("Error: Camera not opened.");
}

std::vector<device::StreamData> CameraPrivate::RetrieveImage(const ImageType& type,
    ErrorCode* code) {
  if (!IsOpened()) {
    *code = ErrorCode::ERROR_CAMERA_NOT_OPENED;
    return {};
  }

  switch (type) {
    case ImageType::IMAGE_LEFT_COLOR: {
      std::lock_guard<std::mutex> _(cap_color_mtx_);
      stream_datas_t data = left_color_data_;
      left_color_data_.clear();
      return data;
    } break;
    case ImageType::IMAGE_RIGHT_COLOR: {
      if (!is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
        LOGE("RetrieveImage: Right color is disable.");
        throw new std::runtime_error("RetrieveImage: Right color is disable.");
      }
      std::lock_guard<std::mutex> _(cap_color_mtx_);
      stream_datas_t data = right_color_data_;
      right_color_data_.clear();
      return data;
    } break;
    case ImageType::IMAGE_DEPTH: {
      std::lock_guard<std::mutex> _(cap_depth_mtx_);
      stream_datas_t data = depth_data_;
      depth_data_.clear();
      return data;
    } break;
    default:
      throw new std::runtime_error("RetrieveImage: ImageType is unknown");
  }
}

CameraPrivate::stream_data_t CameraPrivate::RetrieveLatestImage(const ImageType& type,
    ErrorCode* code) {
  if (!IsOpened()) {
    *code = ErrorCode::ERROR_CAMERA_NOT_OPENED;
    return {};
  }

  switch (type) {
    case ImageType::IMAGE_LEFT_COLOR: {
      std::lock_guard<std::mutex> _(cap_color_mtx_);
      if (left_color_data_.empty()) { return {}; }
      auto data = left_color_data_.back();
      left_color_data_.clear();
      return data;
    } break;
    case ImageType::IMAGE_RIGHT_COLOR: {
      std::lock_guard<std::mutex> _(cap_color_mtx_);
      if (right_color_data_.empty()) { return {}; }
      auto data = right_color_data_.back();
      right_color_data_.clear();
      return data;
    } break;
    case ImageType::IMAGE_DEPTH: {
      std::lock_guard<std::mutex> _(cap_depth_mtx_);
      if (depth_data_.empty()) { return {}; }
      auto data = depth_data_.back();
      depth_data_.clear();
      return data;
    } break;
    default:
      throw new std::runtime_error("RetrieveImage: ImageType is unknown");
  }
}

void CameraPrivate::CaptureImageColor(ErrorCode* code) {
  std::unique_lock<std::mutex> _(cap_color_mtx_);
  auto p = RetrieveImageColor(code);
  if (p) {
    auto color = p->Clone();
    image_color_.push_back(color);
    image_color_wait_.notify_one();
  }
}

void CameraPrivate::CaptureImageDepth(ErrorCode* code) {
  std::unique_lock<std::mutex> _(cap_depth_mtx_);
  auto p = RetrieveImageDepth(code);
  if (p) {
    auto depth = p->Clone();
    image_depth_.push_back(depth);
    image_depth_wait_.notify_one();
  }
}

void CameraPrivate::SyntheticImageColor() {
  std::unique_lock<std::mutex> _(cap_color_mtx_);
  image_color_wait_.wait_for(_, std::chrono::seconds(1));

  if (image_color_.empty() || img_info_.empty()) { return; }

  if (image_color_.front()->frame_id() >
      img_info_.back().img_info->frame_id) {
    if (image_color_.size() > 5) { image_color_.clear(); }
    img_info_.clear();
    return;
  } else if (image_color_.back()->frame_id() <
      img_info_.front().img_info->frame_id) {
    if (img_info_.size() > 5) { img_info_.clear(); }
    image_color_.clear();
    return;
  }

  for (auto color : image_color_) {
    for (auto info : img_info_) {
      if (color->frame_id() == info.img_info->frame_id) {
        TransferColor(color, info);
        if (left_color_data_.size() > 30) { left_color_data_.clear(); }
        if (right_color_data_.size() > 30) { right_color_data_.clear(); }
        if (img_info_.size() > 30) { img_info_.clear(); }
      }
    }
  }
  image_color_.clear();
  img_info_.clear();
}

void CameraPrivate::OldSyntheticImageColor() {
  std::unique_lock<std::mutex> _(cap_color_mtx_);
  image_color_wait_.wait_for(_, std::chrono::seconds(1));
  if (image_color_.empty()) { return; }

  for (auto color : image_color_) {
    OldTransferColor(color);
    if (left_color_data_.size() > 30) { left_color_data_.clear(); }
    if (right_color_data_.size() > 30) { right_color_data_.clear(); }
  }
  image_color_.clear();
}

void CameraPrivate::TransferColor(Image::pointer color, img_info_data_t info) {
  if (!is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
    stream_data_t data;
    data.img_info = std::make_shared<ImgInfo>();
    *data.img_info = *info.img_info;
    data.img = color->Clone();
    left_color_data_.push_back(data);
  } else {
    CutPart(ImageType::IMAGE_LEFT_COLOR, color, info);
    CutPart(ImageType::IMAGE_RIGHT_COLOR, color, info);
  }
}

void CameraPrivate::OldTransferColor(Image::pointer color) {
  if (!is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
    stream_data_t data;
    data.img_info = nullptr;
    data.img = color->Clone();
    left_color_data_.push_back(data);
  } else {
    OldCutPart(ImageType::IMAGE_LEFT_COLOR, color);
    OldCutPart(ImageType::IMAGE_RIGHT_COLOR, color);
  }
}

void CameraPrivate::CutPart(ImageType type,
    Image::pointer color, img_info_data_t info) {
  stream_data_t data;
  data.img_info = std::make_shared<ImgInfo>();
  *data.img_info = *info.img_info;
  if (type == ImageType::IMAGE_LEFT_COLOR) {
    data.img = color->CutPart(ImageType::IMAGE_LEFT_COLOR);
    left_color_data_.push_back(data);
  } else if (type == ImageType::IMAGE_RIGHT_COLOR) {
    data.img = color->CutPart(ImageType::IMAGE_RIGHT_COLOR);
    right_color_data_.push_back(data);
  }
}

void CameraPrivate::OldCutPart(ImageType type, Image::pointer color) {
  stream_data_t data;
  data.img_info = nullptr;
  if (type == ImageType::IMAGE_LEFT_COLOR) {
    data.img = color->CutPart(ImageType::IMAGE_LEFT_COLOR);
    left_color_data_.push_back(data);
  } else if (type == ImageType::IMAGE_RIGHT_COLOR) {
    data.img = color->CutPart(ImageType::IMAGE_RIGHT_COLOR);
    right_color_data_.push_back(data);
  }
}

void CameraPrivate::SyntheticImageDepth() {
  std::unique_lock<std::mutex> _(cap_depth_mtx_);
  image_depth_wait_.wait_for(_, std::chrono::seconds(1));
  for (auto depth : image_depth_) {
    stream_data_t data;
    data.img_info = nullptr;
    data.img = depth->Clone();
    depth_data_.push_back(data);
    if (depth_data_.size() > 30) { depth_data_.clear(); }
  }
  image_depth_.clear();
}

void CameraPrivate::StartCaptureImage() {
  is_capture_image_ = true;
  cap_image_thread_ = std::thread([this]() {
    ErrorCode code = ErrorCode::SUCCESS;
    while (is_capture_image_) {
      if (is_enable_image_[ImageType::IMAGE_LEFT_COLOR] ||
          is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
        CaptureImageColor(&code);
      }
      if (is_enable_image_[ImageType::IMAGE_DEPTH]) {
        CaptureImageDepth(&code);
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(1));
    }
  });
}

void CameraPrivate::StopCaptureImage() {
  is_capture_image_ = false;
  cap_image_thread_.join();
  image_color_wait_.notify_all();
  image_depth_wait_.notify_all();
}

void CameraPrivate::StartSyntheticImage() {
  is_synthetic_image_ = true;
  sync_thread_ = std::thread([this]() {
    while (is_capture_image_) {
      if (is_enable_image_[ImageType::IMAGE_LEFT_COLOR] ||
          is_enable_image_[ImageType::IMAGE_RIGHT_COLOR]) {
        if (is_hid_exist_) {
          SyntheticImageColor();
        } else {
          OldSyntheticImageColor();
        }
      }
      if (is_enable_image_[ImageType::IMAGE_DEPTH]) {
        SyntheticImageDepth();
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(1));
    }
  });
}

void CameraPrivate::StopSyntheticImage() {
  is_synthetic_image_ = false;
  sync_thread_.join();
}

void CameraPrivate::Wait() {
  if (rate_) {
    OnPreWait();
    rate_->Sleep();
    OnPostWait();
  }
}

void CameraPrivate::Close() {
  if (dev_sel_info_.index != -1) {
    StopCaptureImage();
    StopSyntheticImage();
    channels_->StopHidTracking();
    EtronDI_CloseDevice(etron_di_, &dev_sel_info_);
    dev_sel_info_.index = -1;
  }
  ReleaseBuf();
  EtronDI_Release(&etron_di_);
}

void CameraPrivate::ReleaseBuf() {
  color_image_buf_ = nullptr;
  depth_image_buf_ = nullptr;
  if (!depth_buf_) {
    delete depth_buf_;
    depth_buf_ = nullptr;
  }
}

bool CameraPrivate::StartHidTracking() {
  channels_->SetImuCallback(std::bind(&CameraPrivate::ImuDataCallback,
        this, std::placeholders::_1));
  channels_->SetImgInfoCallback(std::bind(&CameraPrivate::ImageInfoCallback,
        this, std::placeholders::_1));
  if (!channels_->StartHidTracking()) {
    return false;
  }

  is_imu_open_ = true;
  return true;
}

void CameraPrivate::ImuDataCallback(const ImuPacket &packet) {
  for (auto &&seg : packet.segments) {
    auto &&imu = std::make_shared<ImuData>();
    imu->flag = seg.flag;
    imu->temperature = static_cast<double>(seg.temperature * 0.125 + 23);
    imu->timestamp = seg.timestamp;

    if (imu->flag == 1) {
      imu->accel[0] = seg.accel_or_gyro[0] * 12.f / 0x10000;
      imu->accel[1] = seg.accel_or_gyro[1] * 12.f / 0x10000;
      imu->accel[2] = seg.accel_or_gyro[2] * 12.f / 0x10000;
      imu->gyro[0] = 0;
      imu->gyro[1] = 0;
      imu->gyro[2] = 0;
    } else if (imu->flag == 2) {
      imu->accel[0] = 0;
      imu->accel[1] = 0;
      imu->accel[2] = 0;
      imu->gyro[0] = seg.accel_or_gyro[0] * 2000.f / 0x10000;
      imu->gyro[1] = seg.accel_or_gyro[1] * 2000.f / 0x10000;
      imu->gyro[2] = seg.accel_or_gyro[2] * 2000.f / 0x10000;
    } else {
      imu->Reset();
    }

    if (is_process_mode_[ProcessMode::ASSEMBLY]) {
      ScaleAssemCompensate(imu);
    } else if (is_process_mode_[ProcessMode::WARM_DRIFT]) {
      TempCompensate(imu);
    } else if (is_process_mode_[ProcessMode::ALL]) {
      TempCompensate(imu);
      ScaleAssemCompensate(imu);
    }

    ++motion_count_;
    if (motion_count_ > 20) {
      motion_data_t tmp = {imu};
      cache_imu_data_.push_back(tmp);
      std::lock_guard<std::mutex> _(mtx_imu_);
      imu_data_.insert(imu_data_.end(), cache_imu_data_.begin(),
          cache_imu_data_.end());
      cache_imu_data_.clear();
    }
  }
}

void CameraPrivate::ImageInfoCallback(const ImgInfoPacket &packet) {
  auto &&img_info = std::make_shared<ImgInfo>();

  img_info->frame_id = packet.frame_id;
  img_info->timestamp = packet.timestamp;
  img_info->exposure_time = packet.exposure_time;

  img_info_data_t tmp = {img_info};
  cache_image_info_.push_back(tmp);

  std::lock_guard<std::mutex> _(mtx_img_info_);
  img_info_.insert(img_info_.end(), cache_image_info_.begin(), cache_image_info_.end());
  cache_image_info_.clear();
}

std::vector<device::MotionData> CameraPrivate::GetImuDatas() {
  if (!is_imu_open_)
    LOGE("Imu is not opened !");

  std::lock_guard<std::mutex> _(mtx_imu_);
  motion_datas_t tmp = imu_data_;
  imu_data_.clear();
  return tmp;
}

void CameraPrivate::GetHDCameraLogData() {
  GetCameraLogData(0);
}

void CameraPrivate::GetVGACameraLogData() {
  GetCameraLogData(1);
}

void CameraPrivate::SyncCameraLogData() {
  camera_log_datas_.clear();
  for (int index = 0; index < 2 ; index++) {
    struct CameraCtrlRectLogData camera_log_data;
    eSPCtrl_RectLogData eSPRectLogData;
    EtronDI_GetRectifyMatLogData(etron_di_,
      &dev_sel_info_, &eSPRectLogData, index);
    int i;
    camera_log_data.InImgWidth = eSPRectLogData.InImgWidth;
    camera_log_data.InImgHeight = eSPRectLogData.InImgHeight;
    camera_log_data.OutImgWidth = eSPRectLogData.OutImgWidth;
    camera_log_data.OutImgHeight = eSPRectLogData.OutImgHeight;
    camera_log_data.RECT_ScaleWidth = eSPRectLogData.RECT_ScaleWidth;
    camera_log_data.RECT_ScaleHeight = eSPRectLogData.RECT_ScaleHeight;
    for (i=0; i < 9; i++) {
      camera_log_data.CamMat1[i] = eSPRectLogData.CamMat1[i];
    }
    for (i=0; i < 8; i++) {
        camera_log_data.CamDist1[i] = eSPRectLogData.CamDist1[i];
    }
    for (i=0; i < 9; i++) {
        camera_log_data.CamMat2[i] = eSPRectLogData.CamMat2[i];
    }
    for (i=0; i < 8; i++) {
        camera_log_data.CamDist2[i] = eSPRectLogData.CamDist2[i];
    }
    for (i=0; i < 9; i++) {
        camera_log_data.RotaMat[i] = eSPRectLogData.RotaMat[i];
    }
    for (i=0; i < 3; i++) {
        camera_log_data.TranMat[i] = eSPRectLogData.TranMat[i];
    }
    for (i=0; i < 9; i++) {
        camera_log_data.LRotaMat[i] = eSPRectLogData.LRotaMat[i];
    }
    for (i=0; i < 9; i++) {
        camera_log_data.RRotaMat[i] = eSPRectLogData.RRotaMat[i];
    }
    for (i=0; i < 12; i++) {
        camera_log_data.NewCamMat1[i] = eSPRectLogData.NewCamMat1[i];
    }
    for (i=0; i < 12; i++) {
        camera_log_data.NewCamMat2[i] = eSPRectLogData.NewCamMat2[i];
    }
    camera_log_data.RECT_Crop_Row_BG = eSPRectLogData.RECT_Crop_Row_BG;
    camera_log_data.RECT_Crop_Row_ED = eSPRectLogData.RECT_Crop_Row_ED;
    camera_log_data.RECT_Crop_Col_BG_L = eSPRectLogData.RECT_Crop_Col_BG_L;
    camera_log_data.RECT_Crop_Col_ED_L = eSPRectLogData.RECT_Crop_Col_ED_L;
    camera_log_data.RECT_Scale_Col_M = eSPRectLogData.RECT_Scale_Col_M;
    camera_log_data.RECT_Scale_Col_N = eSPRectLogData.RECT_Scale_Col_N;
    camera_log_data.RECT_Scale_Row_M = eSPRectLogData.RECT_Scale_Row_M;
    camera_log_data.RECT_Scale_Row_N = eSPRectLogData.RECT_Scale_Row_N;
    camera_log_data.RECT_AvgErr = eSPRectLogData.RECT_AvgErr;
    camera_log_data.nLineBuffers = eSPRectLogData.nLineBuffers;
    for (i = 0; i < 16; i++) {
      camera_log_data.ReProjectMat[i] = eSPRectLogData.ReProjectMat[i];
    }
    camera_log_datas_.push_back(camera_log_data);
  }
}

struct CameraCtrlRectLogData  CameraPrivate::GetCameraCtrlData(int index) {
  return camera_log_datas_[index];
}

void CameraPrivate::GetCameraLogData(int index) {
  int nRet;

  // for parse log test
  eSPCtrl_RectLogData eSPRectLogData;
  // EtronDI_GetRectifyLogData in puma
  // EtronDI_GetRectifyMatLogData
  nRet = EtronDI_GetRectifyMatLogData(etron_di_,
    &dev_sel_info_, &eSPRectLogData, index);
  printf("nRet = %d", nRet);

  FILE *pfile;
  char buf[128];
  sprintf(buf, "RectfyLog_PUMA_%d.txt", index); // NOLINT
  pfile = fopen(buf, "wt");
  if (pfile != NULL) {
    int i;
    fprintf(pfile, "InImgWidth = %d\n",        eSPRectLogData.InImgWidth);
    fprintf(pfile, "InImgHeight = %d\n",       eSPRectLogData.InImgHeight);
    fprintf(pfile, "OutImgWidth = %d\n",       eSPRectLogData.OutImgWidth);
    fprintf(pfile, "OutImgHeight = %d\n",      eSPRectLogData.OutImgHeight);
    //
    fprintf(pfile, "RECT_ScaleWidth = %d\n",   eSPRectLogData.RECT_ScaleWidth);
    fprintf(pfile, "RECT_ScaleHeight = %d\n",  eSPRectLogData.RECT_ScaleHeight);
    //
    fprintf(pfile, "CamMat1 = ");
    for (i=0; i < 9; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.CamMat1[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "CamDist1 = ");
    for (i=0; i < 8; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.CamDist1[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "CamMat2 = ");
    for (i=0; i < 9; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.CamMat2[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "CamDist2 = ");
    for (i=0; i < 8; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.CamDist2[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "RotaMat = ");
    for (i=0; i < 9; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.RotaMat[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "TranMat = ");
    for (i=0; i < 3; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.TranMat[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "LRotaMat = ");
    for (i=0; i < 9; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.LRotaMat[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "RRotaMat = ");
    for (i=0; i < 9; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.RRotaMat[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "NewCamMat1 = ");
    for (i=0; i < 12; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.NewCamMat1[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "NewCamMat2 = ");
    for (i=0; i < 12; i++) {
        fprintf(pfile, "%.8f, ",  eSPRectLogData.NewCamMat2[i]);
    }
    fprintf(pfile, "\n");
    //
    fprintf(pfile, "RECT_Crop_Row_BG = %d\n",
      eSPRectLogData.RECT_Crop_Row_BG);
    fprintf(pfile, "RECT_Crop_Row_ED = %d\n",
      eSPRectLogData.RECT_Crop_Row_ED);
    fprintf(pfile, "RECT_Crop_Col_BG_L = %d\n",
      eSPRectLogData.RECT_Crop_Col_BG_L);
    fprintf(pfile, "RECT_Crop_Col_ED_L = %d\n",
      eSPRectLogData.RECT_Crop_Col_ED_L);
    fprintf(pfile, "RECT_Scale_Col_M = %d\n",
      eSPRectLogData.RECT_Scale_Col_M);
    fprintf(pfile, "RECT_Scale_Col_N = %d\n",
      eSPRectLogData.RECT_Scale_Col_N);
    fprintf(pfile, "RECT_Scale_Row_M = %d\n",
      eSPRectLogData.RECT_Scale_Row_M);
    fprintf(pfile, "RECT_Scale_Row_N = %d\n",
      eSPRectLogData.RECT_Scale_Row_N);
    //
    fprintf(pfile, "RECT_AvgErr = %.8f\n", eSPRectLogData.RECT_AvgErr);
    //
    fprintf(pfile, "nLineBuffers = %d\n",  eSPRectLogData.nLineBuffers);
    //
    printf("file ok\n");
    fprintf(pfile, "ReProjectMat = ");
    for (i = 0; i < 16; i++) {
      fprintf(pfile, "%.8f, ", eSPRectLogData.ReProjectMat[i]);
    }
    fprintf(pfile, "\n");
  }
  fclose(pfile);
}

void CameraPrivate::SetCameraLogData(const std::string& file) {
  std::ifstream t;
  int length;
  t.open(file.c_str());
  t.seekg(0, std::ios::end);
  length = t.tellg();
  t.seekg(0, std::ios::beg);
  char* buffer = new char[length];
  t.read(buffer, length);
  t.close();

  int nActualLength = 0;

  if ( ETronDI_OK != EtronDI_SetLogData( etron_di_, &dev_sel_info_,
    (unsigned char*)buffer, length, &nActualLength, 0)) {
    printf("error when setLogData\n");
  }
  delete[] buffer;
  SyncCameraLogData();
}

struct CameraCtrlRectLogData CameraPrivate::GetHDCameraCtrlData() {
  return GetCameraCtrlData(0);
}
struct CameraCtrlRectLogData CameraPrivate::GetVGACameraCtrlData() {
  return GetCameraCtrlData(1);
}

void CameraPrivate::SetImageMode(const ImageMode& mode) {
  switch (mode) {
    case ImageMode::IMAGE_RAW:
      depth_data_type_ = 9; // ETronDI_DEPTH_DATA_11_BITS_RAW
      break;
    case ImageMode::IMAGE_RECTIFIED:
      depth_data_type_ = 4; // ETronDI_DEPTH_DATA_11_BITS
      break;
    default:
      throw new std::runtime_error("ImageMode is unknown");
  }
}

void CameraPrivate::EnableImageType(const ImageType& type) {
  switch (type) {
    case ImageType::IMAGE_LEFT_COLOR:
      is_enable_image_[type] = true;
      break;
    case ImageType::IMAGE_RIGHT_COLOR:
      is_enable_image_[type] = true;
      stream_mode_ = StreamMode::STREAM_2560x720;
      break;
    case ImageType::IMAGE_DEPTH:
      is_enable_image_[type] = true;
      break;
    case ImageType::ALL:
      EnableImageType(ImageType::IMAGE_LEFT_COLOR);
      EnableImageType(ImageType::IMAGE_RIGHT_COLOR);
      EnableImageType(ImageType::IMAGE_DEPTH);
      break;
    default:
      LOGE("EnableImageType:: ImageType is unknown.");
  }
}

void CameraPrivate::ReadAllInfos() {
  device_params_ = std::make_shared<DeviceParams>();

  Channels::imu_params_t imu_params;
  if (!channels_->GetFiles(device_params_.get(), &imu_params)) {
    LOGE("%s %d:: Read device infos failed. Please upgrade"
        "your firmware to the latest version.", __FILE__, __LINE__);
    return;
  }

  LOGI("\nDevice info: name: %s", device_params_->name.c_str());
  LOGI("             serial_number: %s", device_params_->serial_number.c_str());
  LOGI("             firmware_version: %s",
      device_params_->firmware_version.to_string().c_str());
  LOGI("             hardware_version: %s",
      device_params_->hardware_version.to_string().c_str());
  LOGI("             spec_version: %s",
      device_params_->spec_version.to_string().c_str());
  LOGI("             lens_type: %s",
      device_params_->lens_type.to_string().c_str());
  LOGI("             imu_type: %s",
      device_params_->imu_type.to_string().c_str());
  LOGI("             nominal_baseline: %u", device_params_->nominal_baseline);

  if (imu_params.ok) {
    SetMotionIntrinsics({imu_params.in_accel, imu_params.in_gyro});
    SetMotionExtrinsics(imu_params.ex_left_to_imu);
    // std::cout << GetMotionIntrinsics() << std::endl;
    // std::cout << GetMotionExtrinsics() << std::endl;
  } else {
    LOGE("%s %d:: Motion intrinsics & extrinsics not exist",
        __FILE__, __LINE__);
  }
}

std::shared_ptr<DeviceParams> CameraPrivate::GetInfo() const {
  return device_params_;
}

std::string CameraPrivate::GetInfo(const Info &info) const {
  if (!device_params_) {
    LOGE("%s %d:: Device information not found", __FILE__, __LINE__);
    return "";
  }
  switch (info) {
    case Info::DEVICE_NAME:
      return device_params_->name;
    case Info::SERIAL_NUMBER:
      return device_params_->serial_number;
    case Info::FIRMWARE_VERSION:
      return device_params_->firmware_version.to_string();
    case Info::HARDWARE_VERSION:
      return device_params_->hardware_version.to_string();
    case Info::SPEC_VERSION:
      return device_params_->spec_version.to_string();
    case Info::LENS_TYPE:
      return device_params_->lens_type.to_string();
    case Info::IMU_TYPE:
      return device_params_->imu_type.to_string();
    case Info::NOMINAL_BASELINE:
      return std::to_string(device_params_->nominal_baseline);
    default:
      LOGE("%s %d:: Unknown device info", __FILE__, __LINE__);
      return "";
  }
}

MotionIntrinsics CameraPrivate::GetMotionIntrinsics() const {
  if (motion_intrinsics_) {
    return *motion_intrinsics_;
  } else {
    LOGE("%s %d:: Motion intrinsics not found", __FILE__, __LINE__);
    return {};
  }
}

Extrinsics CameraPrivate::GetMotionExtrinsics() const {
  if (motion_from_extrinsics_) {
    return *motion_from_extrinsics_;
  } else {
    LOGE("%s %d:: Motion extrinsics not found", __FILE__, __LINE__);
    return {};
  }
}

void CameraPrivate::SetMotionIntrinsics(const MotionIntrinsics &in) {
  if (!motion_intrinsics_) {
    motion_intrinsics_ = std::make_shared<MotionIntrinsics>();
  }
  *motion_intrinsics_ = in;
}

void CameraPrivate::SetMotionExtrinsics(const Extrinsics &ex) {
  if (!motion_from_extrinsics_) {
    motion_from_extrinsics_ = std::make_shared<Extrinsics>();
  }
  *motion_from_extrinsics_ = ex;
}

void CameraPrivate::EnableImuProcessMode(const ProcessMode &mode) {
  switch (mode) {
    case ProcessMode::ASSEMBLY:
      is_process_mode_[mode] = true;
      break;
    case ProcessMode::WARM_DRIFT:
      is_process_mode_[mode] = true;
      break;
    case ProcessMode::ALL:
      is_process_mode_[mode] = true;
      break;
    default:
      break;
  }
}

void CameraPrivate::TempCompensate(std::shared_ptr<ImuData> data) {
  if (nullptr == motion_intrinsics_) {
    return;
  }

  double temp = data->temperature;
  if (data->flag == 1) {
    data->accel[0] -= motion_intrinsics_->accel.x[1] * temp
      + motion_intrinsics_->accel.x[0];
    data->accel[1] -= motion_intrinsics_->accel.y[1] * temp
      + motion_intrinsics_->accel.y[0];
    data->accel[2] -= motion_intrinsics_->accel.z[1] * temp
      + motion_intrinsics_->accel.z[0];
  } else if (data->flag == 2) {
    data->gyro[0] -= motion_intrinsics_->gyro.x[1] * temp
      + motion_intrinsics_->gyro.x[0];
    data->gyro[1] -= motion_intrinsics_->gyro.y[1] * temp
      + motion_intrinsics_->gyro.y[0];
    data->gyro[2] -= motion_intrinsics_->gyro.z[1] * temp
      + motion_intrinsics_->gyro.z[0];
  }
}

void CameraPrivate::ScaleAssemCompensate(std::shared_ptr<ImuData> data) {
  if (nullptr == motion_intrinsics_) {
    return;
  }

  double dst[3][3] = {0};
  if (data->flag == 1) {
    matrix_3x3(motion_intrinsics_->accel.scale,
        motion_intrinsics_->accel.assembly, dst);
    double s[3][1] = {0};
    double d[3][1] = {0};
    for (int i = 0; i < 3; i++) {
      s[i][0] = data->accel[i];
    }
    matrix_3x1(dst, s, d);
    for (int i = 0; i < 3; i++) {
      data->accel[i] = d[i][0];
    }
  } else if (data->flag == 2) {
    matrix_3x3(motion_intrinsics_->gyro.scale,
        motion_intrinsics_->gyro.assembly, dst);
    double s[3][1] = {0};
    double d[3][1] = {0};
    for (int i = 0; i < 3; i++) {
      s[i][0] = data->gyro[i];
    }
    matrix_3x1(dst, s, d);
    for (int i = 0; i < 3; i++) {
      data->gyro[i] = d[i][0];
    }
  }
}

void CameraPrivate::IsHidExist() {
  is_hid_exist_ = channels_->IsHidExist();
}

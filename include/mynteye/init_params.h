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
#ifndef MYNTEYE_INIT_PARAMS_H_
#define MYNTEYE_INIT_PARAMS_H_
#pragma once

#include <string>

#include "mynteye/stubs/global.h"
#include "mynteye/types.h"

MYNTEYE_BEGIN_NAMESPACE

/**
 * Initialization Parameters.
 */
struct MYNTEYE_API InitParams {
  /**
   * Device index.
   */
  std::int32_t dev_index;

  /**
   * Framerate, default 10.
   */
  std::int32_t framerate;

  /**
   * Depth mode, default DEPTH_COLORFUL.
   */
  DepthMode depth_mode;

  /**
   * Stream mode of color & depth, default STREAM_1280x720.
   */
  StreamMode stream_mode;

  /**
   * Stream format of color, default STREAM_YUYV.
   */
  StreamFormat color_stream_format;

  /**
   * Stream format of depth, default STREAM_YUYV.
   */
  StreamFormat depth_stream_format;

  /**
   * Auto-exposure, default true.
   */
  bool state_ae;

  /**
   * Auto-white balance, default true.
   */
  bool state_awb;

  /**
   * IR (Infrared), default 0.
   */
  std::uint8_t ir_intensity;

  /** Constructor. */
  InitParams();
  explicit InitParams(const std::int32_t& dev_index);

  /** Destructor. */
  ~InitParams();
};

MYNTEYE_END_NAMESPACE

#endif  // MYNTEYE_INIT_PARAMS_H_

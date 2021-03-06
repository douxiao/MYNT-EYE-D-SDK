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
#ifndef MYNTEYE_TYPES_H_
#define MYNTEYE_TYPES_H_
#pragma once

#include <cstdint>
#include <ostream>

#include "mynteye/stubs/global.h"

MYNTEYE_BEGIN_NAMESPACE

/**
 * @ingroup enumerations
 * @brief List error codes.
 */
enum class ErrorCode : std::int32_t {
  /** Standard code for successful behavior. */
  SUCCESS = 0,
  /** Standard code for unsuccessful behavior. */
  ERROR_FAILURE,
  /**
   * File cannot be opened for not exist, not a regular file or
   * any other reason.
   */
  ERROR_FILE_OPEN_FAILED,
  /** Camera cannot be opened for not plugged or any other reason. */
  ERROR_CAMERA_OPEN_FAILED,
  /** Camera is not opened now. */
  ERROR_CAMERA_NOT_OPENED,
  /** Camera retrieve the image failed. */
  ERROR_CAMERA_RETRIEVE_FAILED,
  /** Imu cannot be opened for not plugged or any other reason. */
  ERROR_IMU_OPEN_FAILED,
  /** Imu receive data timeout */
  ERROR_IMU_RECV_TIMEOUT,
  /** Imu receive data error */
  ERROR_IMU_DATA_ERROR,
  /** Last guard. */
  ERROR_CODE_LAST
};

/**
 * @ingroup enumerations
 * @brief List image types.
 */
enum class ImageType : std::int32_t {
  /** LEFT Color. */
  IMAGE_LEFT_COLOR,
  /** RIGHT Color. */
  IMAGE_RIGHT_COLOR,
  /** Depth. */
  IMAGE_DEPTH,
  /** All. */
  ALL,
};

/**
 * @ingroup enumerations
 * @brief List image formats.
 */
enum class ImageFormat : std::int32_t {
  IMAGE_BGR_24,   // 8UC3
  IMAGE_RGB_24,   // 8UC3
  IMAGE_GRAY_8,   // 8UC1
  IMAGE_GRAY_16,  // 16UC1
  IMAGE_GRAY_24,  // 8UC3
  IMAGE_YUYV,     // 8UC2
  IMAGE_MJPG,
  // color
  COLOR_BGR   = IMAGE_BGR_24,  // > COLOR_RGB
  COLOR_RGB   = IMAGE_RGB_24,  // > COLOR_BGR
  COLOR_YUYV  = IMAGE_YUYV,    // > COLOR_BGR, COLOR_RGB
  COLOR_MJPG  = IMAGE_MJPG,    // > COLOR_BGR, COLOR_RGB
  // depth
  DEPTH_RAW     = IMAGE_GRAY_16,  // > DEPTH_GRAY
  DEPTH_GRAY    = IMAGE_GRAY_8,
  DEPTH_GRAY_24 = IMAGE_GRAY_24,
  DEPTH_BGR     = IMAGE_BGR_24,   // > DEPTH_RGB
  DEPTH_RGB     = IMAGE_RGB_24,   // > DEPTH_BGR
  /** Last guard. */
  IMAGE_FORMAT_LAST
};

/**
 * @ingroup enumerations
 * @brief List depth modes.
 */
enum class DepthMode : std::int32_t {
  DEPTH_RAW      = 0,  // ImageFormat::DEPTH_RAW
  DEPTH_GRAY     = 1,  // ImageFormat::DEPTH_GRAY_24
  DEPTH_COLORFUL = 2,  // ImageFormat::DEPTH_RGB
  DEPTH_MODE_LAST
};

/**
 * @ingroup enumerations
 * @brief List stream mode.
 */
enum class StreamMode : std::int32_t {
  STREAM_1280x720 = 0,
  STREAM_2560x720 = 1,
  STREAM_1280x480 = 2,
  STREAM_640x480  = 3,
  STREAM_MODE_LAST
};

/**
 * @ingroup enumerations
 * @brief List stream formats.
 */
enum class StreamFormat : std::int32_t {
  STREAM_MJPG = 0,
  STREAM_YUYV = 1,
  STREAM_FORMAT_LAST
};

MYNTEYE_API
std::ostream& operator<<(std::ostream& os, const StreamFormat& code);

/**
 * @ingroup enumerations
 * @brief List image mode.
 */
enum class ImageMode : std::int32_t {
  IMAGE_RAW,
  IMAGE_RECTIFIED
};

/**
 * @ingroup enumerations
 * @brief Camera info fields are read-only strings that can be queried from the
 * device.
 */
enum class Info : std::uint8_t {
  /** Device name */
  DEVICE_NAME,
  /** Serial number */
  SERIAL_NUMBER,
  /** Firmware version */
  FIRMWARE_VERSION,
  /** Hardware version */
  HARDWARE_VERSION,
  /** Spec version */
  SPEC_VERSION,
  /** Lens type */
  LENS_TYPE,
  /** IMU type */
  IMU_TYPE,
  /** Nominal baseline */
  NOMINAL_BASELINE,
  /** Last guard */
  LAST
};

enum class ProcessMode : std::uint8_t {
  ASSEMBLY,
  WARM_DRIFT,
  ALL
};

struct MYNTEYE_API CameraCtrlRectLogData {
	union {
		unsigned char uByteArray[1024];/**< union data defined as below struct { }*/
		struct {
			unsigned short	InImgWidth;/**< Input image width(SideBySide image) */
			unsigned short	InImgHeight;/**< Input image height */
			unsigned short	OutImgWidth;/**< Output image width(SideBySide image) */
			unsigned short	OutImgHeight;/**< Output image height */
			int		        RECT_ScaleEnable;/**< Rectified image scale */
			int		        RECT_CropEnable;/**< Rectified image crop */
			unsigned short	RECT_ScaleWidth;/**< Input image width(Single image) *RECT_Scale_Col_N /RECT_Scale_Col_M */
			unsigned short	RECT_ScaleHeight;/**< Input image height(Single image) *RECT_Scale_Row_N /RECT_Scale_Row_M */
			float	        CamMat1[9];/**< Left Camera Matrix
								fx, 0, cx, 0, fy, cy, 0, 0, 1
								fx,fy : focus  ; cx,cy : principle point */
			float	        CamDist1[8];/**< Left Camera Distortion Matrix
								k1, k2, p1, p2, k3, k4, k5, k6
								k1~k6 : radial distort ; p1,p2 : tangential distort */
			float			CamMat2[9];/**< Right Camera Matrix
								fx, 0, cx, 0, fy, cy, 0, 0, 1
								fx,fy : focus  ; cx,cy : principle point */
			float			CamDist2[8];/**< Right Camera Distortion Matrix
								k1, k2, p1, p2, k3, k4, k5, k6
								k1~k6 : radial distort ; p1,p2 : tangential distort */
			float			RotaMat[9];/**< Rotation matrix between the left and right camera coordinate systems.
								| [0] [1] [2] |       |Xcr|
								| [3] [4] [5] |   *   |Ycr|            => cr = right camera coordinate
								| [6] [7] [8] |       |Zcr| */
			float			TranMat[3];/**< Translation vector between the coordinate systems of the cameras.
								|[0]|      |Xcr|
								|[1]|   +  |Ycr|	             => cr = right camera coordinate
								|[2]|      |Zcr| */
			float			LRotaMat[9];/**< 3x3 rectification transform (rotation matrix) for the left camera.
								| [0] [1] [2] |       |Xcl|
								| [3] [4] [5] |   *   |Ycl|            => cl = left camera coordinate
								| [6] [7] [8] |       |Zcl| */
			float			RRotaMat[9];/**< 3x3 rectification transform (rotation matrix) for the left camera.
								| [0] [1] [2] |       |Xcr|
								| [3] [4] [5] |   *   |Ycr|            => cr = right camera coordinate
								| [6] [7] [8] |       |Zcr| */
			float			NewCamMat1[12];/**< 3x4 projection matrix in the (rectified) coordinate systems for the left camera.
								fx' 0 cx' 0 0 fy' cy' 0 0 0 1 0
								fx',fy' : rectified focus ; cx', cy; : rectified principle point */
			float			NewCamMat2[12];/**< 3x4 projection matrix in the (rectified) coordinate systems for the rightt camera.
								fx' 0 cx' TranMat[0]* 0 fy' cy' 0 0 0 1 0
								fx',fy' : rectified focus ; cx', cy; : rectified principle point */
			unsigned short	RECT_Crop_Row_BG;/**< Rectidied image crop row begin */
			unsigned short	RECT_Crop_Row_ED;/**< Rectidied image crop row end */
			unsigned short	RECT_Crop_Col_BG_L;/**< Rectidied image crop column begin */
			unsigned short	RECT_Crop_Col_ED_L;/**< Rectidied image crop column end */
			unsigned char	RECT_Scale_Col_M;/**< Rectified image scale column factor M */
			unsigned char	RECT_Scale_Col_N;/**< Rectified image scale column factor N
								Rectified image scale column ratio =  Scale_Col_N/ Scale_Col_M */
			unsigned char	RECT_Scale_Row_M;/**< Rectified image scale row factor M */
			unsigned char	RECT_Scale_Row_N;/**< Rectified image scale row factor N */
			float			RECT_AvgErr;/**< Reprojection error */
			unsigned short	nLineBuffers;/**< Linebuffer for Hardware limitation < 60 */
            float ReProjectMat[16];
		};
	};
};

/**
 * @ingroup datatypes
 * @brief Image information
 */
struct MYNTEYE_API ImgInfo {
  /** Image frame id */
  std::uint16_t frame_id;

  /** Image timestamp */
  std::uint32_t timestamp;

  /** Image exposure time */
  std::uint16_t exposure_time;

  void Reset() {
    frame_id = 0;
    timestamp = 0;
    exposure_time = 0;
  }

  ImgInfo() {
    Reset();
  }
  ImgInfo(const ImgInfo &other) {
    frame_id = other.frame_id;
    timestamp = other.timestamp;
    exposure_time = other.exposure_time;
  }
  ImgInfo &operator=(const ImgInfo &other) {
    frame_id = other.frame_id;
    timestamp = other.timestamp;
    exposure_time = other.exposure_time;
    return *this;
  }
};

/**
 * @ingroup datatypes
 * @brief Imu data
 */
struct MYNTEYE_API ImuData {
  /**
   * Data type
   * 1: accelerometer
   * 2: gyroscope
   * */
  std::uint8_t flag;

  /** Imu gyroscope or accelerometer or frame timestamp */
  std::uint64_t timestamp;

  /** temperature */
  double temperature;

  /** Imu accelerometer data for 3-axis: X, Y, X. */
  double accel[3];

  /** Imu gyroscope data for 3-axis: X, Y, Z. */
  double gyro[3];

  void Reset() {
    flag = 0;
    timestamp = 0;
    temperature = 0;
    std::fill(accel, accel + 3, 0);
    std::fill(gyro, gyro + 3, 0);
  }

  ImuData() {
    Reset();
  }
};

/**
 * @ingroup calibration
 * IMU intrinsics: scale, drift and variances.
 */
struct MYNTEYE_API ImuIntrinsics {
  /**
   * Scale matrix.
   * \code
   *   Scale X     cross axis  cross axis
   *   cross axis  Scale Y     cross axis
   *   cross axis  cross axis  Scale Z
   * \endcode
   */
  double scale[3][3];
  /** Assembly error [3][3] */
  double assembly[3][3];
  /* Zero-drift: X, Y, Z */
  double drift[3];

  /** Noise density variances */
  double noise[3];
  /** Random walk variances */
  double bias[3];


  // std::uint8_t reserve[100];

  /** Warm drift
   *  \code
   *    0 - Constant value
   *    1 - Slope
   *  \endcode
   */
  double x[2];
  double y[2];
  double z[2];
};

MYNTEYE_API
std::ostream &operator<<(std::ostream &os, const ImuIntrinsics &in);

/**
 * @ingroup calibration
 * Motion intrinsics, including accelerometer and gyroscope.
 */
struct MYNTEYE_API MotionIntrinsics {
  ImuIntrinsics accel; /**< Accelerometer intrinsics */
  ImuIntrinsics gyro;  /**< Gyroscope intrinsics */
};

MYNTEYE_API
std::ostream &operator<<(std::ostream &os, const MotionIntrinsics &in);

/**
 * @ingroup calibration
 * Extrinsics, represent how the different datas are connected.
 */
struct MYNTEYE_API Extrinsics {
  double rotation[3][3]; /**< Rotation matrix */
  double translation[3]; /**< Translation vector */

  /**
   * Inverse this extrinsics.
   * @return the inversed extrinsics.
   */
  Extrinsics Inverse() const {
    return {{{rotation[0][0], rotation[1][0], rotation[2][0]},
             {rotation[0][1], rotation[1][1], rotation[2][1]},
             {rotation[0][2], rotation[1][2], rotation[2][2]}},
            {-translation[0], -translation[1], -translation[2]}};
  }
};

MYNTEYE_API
std::ostream &operator<<(std::ostream &os, const Extrinsics &ex);

MYNTEYE_END_NAMESPACE

#endif  // MYNTEYE_TYPES_H_

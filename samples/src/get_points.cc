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
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/visualization/cloud_viewer.h>

#include <iostream>
#include <opencv2/highgui/highgui.hpp>

#include "mynteye/camera.h"
#include "mynteye/utils.h"

#include "util/cam_utils.h"
#include "util/counter.h"
#include "util/cv_painter.h"

typedef pcl::PointXYZRGBA PointT;
typedef pcl::PointCloud<PointT> PointCloud;

PointCloud::Ptr cloud ( new PointCloud );
pcl::visualization::PCLVisualizer viewer("point cloud viewer");

// TODO replace the real camera param
const float camera_factor = 1000.0;

double camera_cx = 682.3;
double camera_cy = 254.9;
double camera_fx = 979.8;
double camera_fy = 942.8;

// show point cloud
void show_points(cv::Mat rgb, cv::Mat depth) {
  // loop the mat
  for (int m = 0; m < depth.rows; m++) {
    for (int n = 0; n < depth.cols; n++) {
      // get depth value at (m, n)
      std::uint16_t d = depth.ptr<std::uint16_t>(m)[n];
      // when d is equal 0 or 4096 means no depth
      if (d == 0 || d == 4096)
        continue;

      PointT p;

      // get point x y z
      p.z = static_cast<float>(d) / camera_factor;
      p.x = (n - camera_cx) * p.z / camera_fx;
      p.y = (m - camera_cy) * p.z / camera_fy;

      // get colors
      p.b = rgb.ptr<uchar>(m)[n * 3];
      p.g = rgb.ptr<uchar>(m)[n * 3 + 1];
      p.r = rgb.ptr<uchar>(m)[n * 3 + 2];

      // add point to cloud
      cloud->points.push_back(p);
    }
  }

  pcl::visualization::PointCloudColorHandlerRGBField<PointT>color(cloud);
  viewer.updatePointCloud<PointT>(cloud, color, "sample cloud");
  viewer.spinOnce();
  // clear points
  cloud->points.clear();
}

int main(int argc, char const* argv[]) {
  mynteye::Camera cam;
  mynteye::DeviceInfo dev_info;
  if (!mynteye::util::select(cam, &dev_info)) {
    return 1;
  }
  mynteye::util::print_stream_infos(cam, dev_info.index);

  std::cout << "Open device: " << dev_info.index << ", "
      << dev_info.name << std::endl << std::endl;

  // Warning: Color stream format MJPG doesn't work.
  mynteye::InitParams params(dev_info.index);
  params.depth_mode = mynteye::DepthMode::DEPTH_RAW;
  // params.stream_mode = StreamMode::STREAM_1280x720;
  params.ir_intensity = 4;

  cam.EnableImageType(mynteye::ImageType::IMAGE_LEFT_COLOR);
  cam.EnableImageType(mynteye::ImageType::IMAGE_DEPTH);

  mynteye::StreamMode streamMode = cam.GetStreamMode();

  if (streamMode == mynteye::StreamMode::STREAM_1280x720
      || streamMode == mynteye::StreamMode::STREAM_2560x720) {
    camera_cy = 254.9 * 2;
  } else if (streamMode == mynteye::StreamMode::STREAM_1280x480
      || streamMode == mynteye::StreamMode::STREAM_640x480) {
    camera_cy = 254.9;
  } else {
    camera_cy = 0;
  }

  if (streamMode == mynteye::StreamMode::STREAM_1280x720
      || streamMode == mynteye::StreamMode::STREAM_1280x480) {
    camera_cx = 682.3;
  } else if (streamMode == mynteye::StreamMode::STREAM_2560x720) {
    camera_cx = 682.3 * 2;
  } else if (streamMode == mynteye::StreamMode::STREAM_640x480) {
    camera_cx = 682.3 / 2;
  } else {
    camera_cx = 0;
  }

  camera_fx = 979.8;
  camera_fy = 942.8;

  cam.Open(params);

  std::cout << std::endl;
  if (!cam.IsOpened()) {
    std::cerr << "Error: Open camera failed" << std::endl;
    return 1;
  }
  std::cout << "Open device success" << std::endl << std::endl;

  std::cout << "Press ESC/Q on Windows to terminate" << std::endl;

  {
    viewer.setBackgroundColor(0, 0, 0);
    viewer.addCoordinateSystem(1.0);
    viewer.initCameraParameters();
    viewer.addPointCloud<PointT>(cloud, "sample cloud");
    viewer.setCameraPosition(0, 0, -2, 0, -1, 0, 0);
    viewer.setSize(1280, 720);
  }

  cv::namedWindow("color");
  cv::namedWindow("depth");

  mynteye::util::Counter counter;
  for (;;) {
    counter.Update();

    auto image_color = cam.RetrieveImage(mynteye::ImageType::IMAGE_LEFT_COLOR);
    auto image_depth = cam.RetrieveImage(mynteye::ImageType::IMAGE_DEPTH);
    if (image_color.img && image_depth.img) {
      cv::Mat color = image_color.img->To(mynteye::ImageFormat::COLOR_BGR)->ToMat();
      cv::Mat depth = image_depth.img->To(mynteye::ImageFormat::DEPTH_RAW)->ToMat();
      mynteye::util::draw(color, mynteye::util::to_string(counter.fps(), 5, 1),
          mynteye::util::TOP_RIGHT);
      cv::imshow("color", color);
      cv::imshow("depth", depth);
      show_points(color, depth);
    }

    char key = static_cast<char>(cv::waitKey(1));
    if (key == 27 || key == 'q' || key == 'Q') {  // ESC/Q
      break;
    }
    cam.Wait();  // keep frequency
  }

  cam.Close();
  cv::destroyAllWindows();
  return 0;
}

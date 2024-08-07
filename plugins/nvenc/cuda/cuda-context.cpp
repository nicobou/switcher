/*
 * This file is part of switcher-nvenc.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "./cuda-context.hpp"
#include "./cu-res.hpp"
#include "switcher/utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {

CudaContext::CudaContext(uint32_t device_id) {
  if (!CuRes(cuInit(0))) return;
  int dev_count = 0;
  if (!CuRes(cuDeviceGetCount(&dev_count))) return;
  char name[256];
  int min = 0, maj = 0;
  CUdevice cdev = 0;
  if (CuRes(cuDeviceGet(&cdev, device_id)) && CuRes(cuDeviceGetName(name, sizeof(name), cdev)) &&
      CuRes(cuDeviceComputeCapability(&maj, &min, cdev)))
    cuda_dev_ = cdev;
  else
    return;

  if (!CuRes(cuCtxCreate(&cuda_ctx_, 0, cuda_dev_))) cuda_ctx_ = nullptr;
}

CudaContext::~CudaContext() {
  if (safe_bool_idiom()) {
    CuRes(cuCtxDestroy(cuda_ctx_));
  }
}

std::vector<std::pair<int, std::string>> CudaContext::get_devices() {
  std::vector<std::pair<int, std::string>> res;
  if (!CuRes(cuInit(0))) return res;
  int dev_count = 0;
  if (!CuRes(cuDeviceGetCount(&dev_count))) return res;
  char name[256];
  int min = 0, maj = 0;
  CUdevice cdev = 0;
  for (int i = 0; i < dev_count; ++i) {
    if (CuRes(cuDeviceGet(&cdev, i)) && CuRes(cuDeviceGetName(name, sizeof(name), cdev)) &&
        CuRes(cuDeviceComputeCapability(&maj, &min, cdev))) {
      if (((maj << 4) + min) >= 0x30) res.push_back(std::make_pair(i, std::string(name)));
    }
  }
  return res;
}

std::string CudaContext::get_support_description() {
  std::string res;
  if (!CuRes(cuInit(0))) return "fail to init cuda: NVENC decoding is not possible";
  int dev_count = 0;
  if (!CuRes(cuDeviceGetCount(&dev_count)))
    return "no cuda device found: NVENC decoding is not possible";
  char name[256];
  int min = 0, maj = 0;
  CUdevice cdev = 0;
  for (int i = 0; i < dev_count; ++i) {
    if (CuRes(cuDeviceGet(&cdev, i)) && CuRes(cuDeviceGetName(name, sizeof(name), cdev)) &&
        CuRes(cuDeviceComputeCapability(&maj, &min, cdev))) {
      res += "GPU #" + std::to_string(i) +
             " supports NVENC: " + std::string((((maj << 4) + min) >= 0x30) ? "yes" : "no") + " (" +
             name + ") (Compute SM " + std::to_string(maj) + "." + std::to_string(min) + ")\n";
    }
  }
  return res;
}

}  // namespace quiddities
}  // namespace switcher

# Third-party notices

## NVIDIA Optical Flow API interface headers

Files:

- `src/third_party/nvidia/nvOpticalFlowCommon.h`
- `src/third_party/nvidia/nvOpticalFlowD3D11.h`

Copyright (c) 2018-2023 NVIDIA Corporation (`nvOpticalFlowCommon.h`)

Copyright (c) 2019-2023 NVIDIA Corporation (`nvOpticalFlowD3D11.h`)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The NVIDIA driver runtime (`nvofapi64.dll`) is not redistributed by Dayaah
Capture. It is loaded from the user's installed NVIDIA display driver when the
optional Frame Generation selector is enabled.

## AMD Advanced Media Framework interface headers

Files under `src/third_party/amd/amf/include` are the official AMD Advanced
Media Framework 1.5.3 public interface headers.

AMD copyright notices are preserved in each header. They are distributed under the MIT license reproduced in
`src/third_party/amd/amf/LICENSE.txt`. The AMD runtime (`amfrt64.dll`) is not
redistributed by Dayaah Capture; it is loaded dynamically from the installed
Radeon display driver when an AMD AMF FRC mode is selected.

## Optional NVIDIA Video Effects / VFG runtime

The source archive and regular Windows archive do not contain the proprietary
NVIDIA Video Effects runtime. The separately labeled `NVIDIA-VFG` archive may
bundle the official, unmodified NVIDIA VFX Python wheel runtime version 0.2.0.0
from `https://pypi.nvidia.com`, including `NVVideoEffects.dll`, `NVCVImage.dll`,
the Video Frame Generation feature library and their required CUDA/NGX runtime
libraries. Its NVIDIA software license, product-specific terms, model license
and third-party notices are included beside those binaries under
`runtime/nvidia-vfg/licenses`. Those files remain governed by NVIDIA's terms,
not by Dayaah Capture's MIT license.

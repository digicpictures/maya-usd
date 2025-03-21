//
// Copyright 2016 Pixar
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
//
#include <mayaUsd/utils/colorSpace.h>

#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/pyResultConversions.h>
#include <pxr/pxr.h>
#include <pxr_python.h>

using namespace PXR_BOOST_PYTHON_NAMESPACE;

PXR_NAMESPACE_USING_DIRECTIVE

void wrapColorSpace()
{
    def("ConvertLinearToMaya", MayaUsd::utils::ConvertLinearToMaya<GfVec3f>);
    def("ConvertLinearToMaya", MayaUsd::utils::ConvertLinearToMaya<GfVec3d>);
    def("ConvertLinearToMaya", MayaUsd::utils::ConvertLinearToMaya<GfVec4f>);
    def("ConvertLinearToMaya", MayaUsd::utils::ConvertLinearToMaya<GfVec4d>);
    def("ConvertMayaToLinear", MayaUsd::utils::ConvertMayaToLinear<GfVec3f>);
    def("ConvertMayaToLinear", MayaUsd::utils::ConvertMayaToLinear<GfVec3d>);
    def("ConvertMayaToLinear", MayaUsd::utils::ConvertMayaToLinear<GfVec4f>);
    def("ConvertMayaToLinear", MayaUsd::utils::ConvertMayaToLinear<GfVec4d>);
}

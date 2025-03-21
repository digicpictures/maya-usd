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
#include "usdMaya/editUtil.h"

#include <mayaUsd/utils/util.h>

#include <pxr/base/tf/pyEnum.h>
#include <pxr/base/tf/pyResultConversions.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr_python.h>

#include <maya/MFnAssembly.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

#include <string>
#include <vector>

using namespace PXR_BOOST_PYTHON_NAMESPACE;
using namespace PXR_BOOST_NAMESPACE;

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

static PXR_BOOST_PYTHON_NAMESPACE::object
_GetEditFromString(const std::string& assemblyPath, const std::string& editString)
{
    MObject assemblyObj;
    MStatus status = UsdMayaUtil::GetMObjectByName(assemblyPath, assemblyObj);
    if (status != MS::kSuccess) {
        TF_CODING_ERROR("EditUtil.GetEditFromString: assembly dag path expected, not "
                        "found!");
        return PXR_BOOST_PYTHON_NAMESPACE::object();
    }

    const MFnAssembly assemblyFn(assemblyObj, &status);
    if (status != MS::kSuccess) {
        TF_CODING_ERROR("EditUtil.GetEditFromString: assembly dag path expected, not "
                        "found!");
        return PXR_BOOST_PYTHON_NAMESPACE::object();
    }

    SdfPath                       editPath;
    UsdMayaEditUtil::AssemblyEdit assemEdit;
    if (!UsdMayaEditUtil::GetEditFromString(assemblyFn, editString, &editPath, &assemEdit)) {
        TF_CODING_ERROR("EditUtil.GetEditFromString: invalid edit");
        return PXR_BOOST_PYTHON_NAMESPACE::object();
    }

    return PXR_BOOST_PYTHON_NAMESPACE::make_tuple(editPath, assemEdit);
}

static PXR_BOOST_PYTHON_NAMESPACE::object _GetEditsForAssembly(const std::string& assemblyPath)
{
    MObject assemblyObj;
    MStatus status = UsdMayaUtil::GetMObjectByName(assemblyPath, assemblyObj);
    if (status != MS::kSuccess) {
        TF_CODING_ERROR("EditUtil.GetEditsForAssembly: assembly dag path expected, not "
                        "found!");
        return PXR_BOOST_PYTHON_NAMESPACE::object();
    }

    UsdMayaEditUtil::PathEditMap assemEdits;
    std::vector<std::string>     invalidEdits;
    UsdMayaEditUtil::GetEditsForAssembly(assemblyObj, &assemEdits, &invalidEdits);

    PXR_BOOST_PYTHON_NAMESPACE::dict editDict;
    TF_FOR_ALL(pathEdits, assemEdits)
    {
        PXR_BOOST_PYTHON_NAMESPACE::list editList;
        TF_FOR_ALL(edit, pathEdits->second) { editList.append(*edit); }

        editDict[pathEdits->first] = editList;
    }

    return PXR_BOOST_PYTHON_NAMESPACE::make_tuple(editDict, invalidEdits);
}

static bool _GetAssemblyEditsFromDict(
    PXR_BOOST_PYTHON_NAMESPACE::dict& assemEditDict,
    UsdMayaEditUtil::PathEditMap*     assemEdits)
{
    PXR_BOOST_PYTHON_NAMESPACE::list keys = assemEditDict.keys();
    for (int i = 0; i < len(keys); ++i) {
        PXR_BOOST_PYTHON_NAMESPACE::extract<SdfPath> extractedKey(keys[i]);
        if (!extractedKey.check()) {
            TF_CODING_ERROR("EditUtil.ApplyEditsToProxy: SdfPath key expected, not "
                            "found!");
            return false;
        }

        SdfPath path = extractedKey;

        UsdMayaEditUtil::AssemblyEditVec pathEdits;

        PXR_BOOST_PYTHON_NAMESPACE::extract<PXR_BOOST_PYTHON_NAMESPACE::list> extractedList(
            assemEditDict[path]);

        if (!extractedList.check()) {
            TF_CODING_ERROR("EditUtil.ApplyEditsToProxy: list value expected, not "
                            "found!");
            return false;
        }

        PXR_BOOST_PYTHON_NAMESPACE::list editList = extractedList;
        for (int j = 0; j < len(extractedList); ++j) {
            PXR_BOOST_PYTHON_NAMESPACE::extract<UsdMayaEditUtil::AssemblyEdit> extractedEdit(
                editList[j]);

            if (!extractedEdit.check()) {
                TF_CODING_ERROR("EditUtil.ApplyEditsToProxy: AssemblyEdit expected in "
                                "list, not found!");
                return false;
            }

            pathEdits.push_back(extractedEdit);
        }

        (*assemEdits)[path] = pathEdits;
    }

    return true;
}

static PXR_BOOST_PYTHON_NAMESPACE::object
_ApplyEditsToProxy(PXR_BOOST_PYTHON_NAMESPACE::dict& assemEditDict, const UsdPrim& proxyRootPrim)
{
    UsdMayaEditUtil::PathEditMap assemEdits;
    if (!_GetAssemblyEditsFromDict(assemEditDict, &assemEdits)) {
        return PXR_BOOST_PYTHON_NAMESPACE::object();
    }

    std::vector<std::string> failedEdits;
    UsdMayaEditUtil::ApplyEditsToProxy(assemEdits, proxyRootPrim, &failedEdits);

    return PXR_BOOST_PYTHON_NAMESPACE::make_tuple(failedEdits.empty(), failedEdits);
}

} // anonymous namespace

void wrapEditUtil()
{
    scope EditUtil = class_<UsdMayaEditUtil, PXR_BOOST_PYTHON_NAMESPACE::noncopyable>(
                         "EditUtil", "UsdMaya edit utilities")
                         .def("GetEditFromString", &_GetEditFromString)
                         .staticmethod("GetEditFromString")
                         .def("GetEditsForAssembly", &_GetEditsForAssembly)
                         .staticmethod("GetEditsForAssembly")
                         .def("ApplyEditsToProxy", &_ApplyEditsToProxy)
                         .staticmethod("ApplyEditsToProxy");

    enum_<UsdMayaEditUtil::EditOp>("EditOp")
        .value("OP_TRANSLATE", UsdMayaEditUtil::OP_TRANSLATE)
        .value("OP_ROTATE", UsdMayaEditUtil::OP_ROTATE)
        .value("OP_SCALE", UsdMayaEditUtil::OP_SCALE);

    enum_<UsdMayaEditUtil::EditSet>("EditSet")
        .value("SET_ALL", UsdMayaEditUtil::SET_ALL)
        .value("SET_X", UsdMayaEditUtil::SET_X)
        .value("SET_Y", UsdMayaEditUtil::SET_Y)
        .value("SET_Z", UsdMayaEditUtil::SET_Z);

    using AssemblyEdit = UsdMayaEditUtil::AssemblyEdit;
    class_<AssemblyEdit>("AssemblyEdit", "Assembly edit")
        .def_readwrite("editString", &AssemblyEdit::editString)
        .def_readwrite("op", &AssemblyEdit::op)
        .def_readwrite("set", &AssemblyEdit::set)
        .add_property(
            "value",
            make_getter(&AssemblyEdit::value, return_value_policy<return_by_value>()),
            make_setter(&AssemblyEdit::value));
}

//
// Copyright 2020 Autodesk
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
#include "UsdTransform3dMatrixOp.h"

#include <usdUfe/ufe/UsdSceneItem.h>
#include <usdUfe/ufe/UsdUndoableCommand.h>
#include <usdUfe/ufe/Utils.h>
#include <usdUfe/ufe/trf/UsdSetXformOpUndoableCommandBase.h>
#include <usdUfe/ufe/trf/UsdTransform3dSetObjectMatrix.h>
#include <usdUfe/ufe/trf/XformOpUtils.h>
#include <usdUfe/undo/UsdUndoBlock.h>
#include <usdUfe/undo/UsdUndoableItem.h>

#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <ufe/transform3dUndoableCommands.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using UsdUfe::UsdSetXformOpUndoableCommandBase;

std::vector<UsdGeomXformOp>::const_iterator
findMatrixOp(const std::vector<UsdGeomXformOp>& xformOps)
{
    auto opName = UsdUfe::getTransform3dMatrixOpName();
    return std::find_if(xformOps.cbegin(), xformOps.cend(), [opName](const UsdGeomXformOp& op) {
        return (op.GetOpType() == UsdGeomXformOp::TypeTransform)
            && (!opName || std::string(opName) == op.GetOpName());
    });
}

// Given a starting point i (inclusive), is there a non-matrix transform op in
// the vector?
bool findNonMatrix(
    const std::vector<UsdGeomXformOp>::const_iterator& i,
    const std::vector<UsdGeomXformOp>&                 xformOps)
{
    return std::find_if(
               i,
               xformOps.cend(),
               [](const UsdGeomXformOp& op) {
                   return op.GetOpType() != UsdGeomXformOp::TypeTransform;
               })
        != xformOps.cend();
}

// Compute the inverse of the cumulative transform for the argument xform ops.
GfMatrix4d xformInv(
    const std::vector<UsdGeomXformOp>::const_iterator& begin,
    const std::vector<UsdGeomXformOp>::const_iterator& end,
    const Ufe::Path&                                   path)
{
    auto nbOps = std::distance(begin, end);
    if (nbOps == 0) {
        return GfMatrix4d { 1 };
    }
    std::vector<UsdGeomXformOp> ops(nbOps);
    ops.assign(begin, end);

    GfMatrix4d m { 1 };
    if (!UsdGeomXformable::GetLocalTransformation(&m, ops, UsdUfe::getTime(path))) {
        std::string msg = TfStringPrintf(
            "Local transformation computation for item %s failed.", path.string().c_str());
        throw std::runtime_error(msg.c_str());
    }

    return m.GetInverse();
}

// Class for setMatrixCmd() implementation.
class UsdSetMatrix4dUndoableCmd : public UsdUfe::UsdUndoableCommand<Ufe::SetMatrix4dUndoableCommand>
{
public:
    UsdSetMatrix4dUndoableCmd(const Ufe::Path& path, const Ufe::Matrix4d& newM)
        : UsdUfe::UsdUndoableCommand<Ufe::SetMatrix4dUndoableCommand>(path)
        , _newM(newM)
    {
    }

    ~UsdSetMatrix4dUndoableCmd() override { }

    bool set(const Ufe::Matrix4d&) override
    {
        // No-op: Maya does not set matrices through interactive manipulation.
        TF_WARN("Illegal call to UsdSetMatrix4dUndoableCmd::set()");
        return true;
    }

    void executeImplementation() override
    {
        // Use editTransform3d() to set a single matrix transform op.
        // transform3d() returns a whole-object interface, which may include
        // other transform ops.
        auto t3d = Ufe::Transform3d::editTransform3d(sceneItem());
        if (!TF_VERIFY(t3d)) {
            return;
        }
        t3d->setMatrix(_newM);
    }

private:
    const Ufe::Matrix4d _newM;
};

// Factor out common code for translate, rotate, scale undoable commands.
class MatrixOpUndoableCmdBase : public UsdSetXformOpUndoableCommandBase
{
    UsdGeomXformOp _op;

public:
    MatrixOpUndoableCmdBase(
        const Ufe::Path&      path,
        const UsdGeomXformOp& op,
        const UsdTimeCode&    writeTime)
        : UsdSetXformOpUndoableCommandBase(path, writeTime)
        , _op(op)
    {
    }

    void createOpIfNeeded(UsdUfe::UsdUndoableItem& undoableItem) override
    {
        UsdUfe::UsdUndoBlock undoBlock(&undoableItem);
        GfMatrix4d           matrix = _op.GetOpTransform(writeTime());
        _op.GetAttr().Set(matrix, writeTime());
    }

    void setValue(const VtValue& v, const UsdTimeCode& writeTime) override
    {
        // Note: the value passed in is either the initial value returned
        //       by the getValue function below or a new value passed to
        //       the set function below. In both cases, we are guaranteed
        //       that it will be a GfMatrix4d.
        _op.GetAttr().Set(v, writeTime);
    }

    VtValue getValue(const UsdTimeCode& readTime) const override
    {
        return VtValue(_op.GetOpTransform(readTime));
    }
};

USDUFE_VERIFY_CLASS_SETUP(UsdSetXformOpUndoableCommandBase, MatrixOpUndoableCmdBase);

// Command to set the translation on a scene item by setting a matrix transform
// op at an arbitrary position in the transform op stack.
class MatrixOpTranslateUndoableCmd : public MatrixOpUndoableCmdBase
{
public:
    MatrixOpTranslateUndoableCmd(
        const Ufe::Path&      path,
        const UsdGeomXformOp& op,
        const UsdTimeCode&    writeTime)
        : MatrixOpUndoableCmdBase(path, op, writeTime)
        // Note: we use the time of the proxy shape, not the writeTime,
        //       because when the command is created, the time is not
        //       passed and we are always receiving the USD DefaultTime(),
        //       which may not be the time sample we are editing.
        , _opTransform(op.GetOpTransform(UsdUfe::getTime(path)))
    {
    }

    // Executes the command by setting the translation onto the transform op.
    bool set(double x, double y, double z) override
    {
        _opTransform.SetTranslateOnly(GfVec3d(x, y, z));
        updateNewValue(VtValue(_opTransform));
        return true;
    }

private:
    GfMatrix4d _opTransform;
};

class MatrixOpRotateUndoableCmd : public MatrixOpUndoableCmdBase
{

public:
    MatrixOpRotateUndoableCmd(
        const Ufe::Path&      path,
        const UsdGeomXformOp& op,
        const UsdTimeCode&    writeTime)
        : MatrixOpUndoableCmdBase(path, op, writeTime)
    {
        // Note: we use the time of the proxy shape, not the writeTime,
        //       because when the command is created, the time is not
        //       passed and we are always receiving the USD DefaultTime(),
        //       which may not be the time sample we are editing.
        GfMatrix4d opTransform = op.GetOpTransform(UsdUfe::getTime(path));

        // Other matrix decomposition code from AL:
        // from
        // https://github.com/AnimalLogic/maya-usd/blob/8852bdbb1fc904ac80543cd6103489097fa00154/lib/usd/utils/MayaTransformAPI.cpp#L979-L1055
        GfMatrix4d unusedR, unusedP;
        GfVec3d    s;
        if (!opTransform.Factor(&unusedR, &s, &fU, &fT, &unusedP)) {
            std::string msg
                = TfStringPrintf("Cannot decompose transform for op %s", op.GetOpName().GetText());
            throw std::runtime_error(msg.c_str());
        }

        fS = GfMatrix4d(GfVec4d(s[0], s[1], s[2], 1.0));
    }

    // Executes the command by setting the rotation onto the transform op.
    bool set(double x, double y, double z) override
    {
        // Expect XYZ Euler angles in degrees.
        GfMatrix3d r(
            GfRotation(GfVec3d::XAxis(), x) * GfRotation(GfVec3d::YAxis(), y)
            * GfRotation(GfVec3d::ZAxis(), z));

        fU.SetRotate(r);

        GfMatrix4d opTransform = (fS * fU).SetTranslateOnly(fT);
        updateNewValue(VtValue(opTransform));
        return true;
    }

private:
    GfVec3d    fT;
    GfMatrix4d fS, fU;
};

class MatrixOpScaleUndoableCmd : public MatrixOpUndoableCmdBase
{

public:
    MatrixOpScaleUndoableCmd(
        const Ufe::Path&      path,
        const UsdGeomXformOp& op,
        const UsdTimeCode&    writeTime)
        : MatrixOpUndoableCmdBase(path, op, writeTime)
    {
        GfMatrix4d opTransform = op.GetOpTransform(writeTime);

        // Other matrix decomposition code from AL:
        // from
        // https://github.com/AnimalLogic/maya-usd/blob/8852bdbb1fc904ac80543cd6103489097fa00154/lib/usd/utils/MayaTransformAPI.cpp#L979-L1055
        GfMatrix4d unusedR, unusedP;
        GfVec3d    unusedS;
        if (!opTransform.Factor(&unusedR, &unusedS, &fU, &fT, &unusedP)) {
            std::string msg
                = TfStringPrintf("Cannot decompose transform for op %s", op.GetOpName().GetText());
            throw std::runtime_error(msg.c_str());
        }
    }

    // Executes the command by setting the scale onto the transform op.
    bool set(double x, double y, double z) override
    {
        GfMatrix4d opTransform = (GfMatrix4d(GfVec4d(x, y, z, 1.0)) * fU).SetTranslateOnly(fT);
        updateNewValue(VtValue(opTransform));
        return true;
    }

private:
    GfVec3d    fT;
    GfMatrix4d fU;
};

} // namespace

namespace USDUFE_NS_DEF {

USDUFE_VERIFY_CLASS_SETUP(UsdTransform3dBase, UsdTransform3dMatrixOp);

UsdTransform3dMatrixOp::UsdTransform3dMatrixOp(
    const UsdSceneItem::Ptr& item,
    const UsdGeomXformOp&    op)
    : UsdTransform3dBase(item)
    , _op(op)
{
}

/* static */
UsdTransform3dMatrixOp::Ptr
UsdTransform3dMatrixOp::create(const UsdSceneItem::Ptr& item, const UsdGeomXformOp& op)
{
    return std::make_shared<UsdTransform3dMatrixOp>(item, op);
}

Ufe::Vector3d UsdTransform3dMatrixOp::translation() const { return getTranslation(matrix()); }

Ufe::Vector3d UsdTransform3dMatrixOp::rotation() const { return getRotation(matrix()); }

Ufe::Vector3d UsdTransform3dMatrixOp::scale() const { return getScale(matrix()); }

Ufe::TranslateUndoableCommand::Ptr
UsdTransform3dMatrixOp::translateCmd(double x, double y, double z)
{
    if (!isAttributeEditAllowed(TfToken("xformOp:translate")))
        return nullptr;

    return std::make_shared<MatrixOpTranslateUndoableCmd>(path(), _op, UsdTimeCode::Default());
}

Ufe::RotateUndoableCommand::Ptr UsdTransform3dMatrixOp::rotateCmd(double x, double y, double z)
{
    if (!isAttributeEditAllowed(TfToken("xformOp:rotateXYZ")))
        return nullptr;

    return std::make_shared<MatrixOpRotateUndoableCmd>(path(), _op, UsdTimeCode::Default());
}

Ufe::ScaleUndoableCommand::Ptr UsdTransform3dMatrixOp::scaleCmd(double x, double y, double z)
{
    if (!isAttributeEditAllowed(TfToken("xformOp:scale")))
        return nullptr;

    return std::make_shared<MatrixOpScaleUndoableCmd>(path(), _op, UsdTimeCode::Default());
}

Ufe::SetMatrix4dUndoableCommand::Ptr UsdTransform3dMatrixOp::setMatrixCmd(const Ufe::Matrix4d& m)
{
    if (!isAttributeEditAllowed(_op.GetName()))
        return nullptr;

    return std::make_shared<UsdSetMatrix4dUndoableCmd>(path(), m);
}

void UsdTransform3dMatrixOp::setMatrix(const Ufe::Matrix4d& m)
{
    if (!isAttributeEditAllowed(_op.GetName()))
        return;

    _op.Set(toUsd(m));
}

Ufe::Matrix4d UsdTransform3dMatrixOp::matrix() const
{
    return toUfe(_op.GetOpTransform(getTime(path())));
}

Ufe::Matrix4d UsdTransform3dMatrixOp::segmentInclusiveMatrix() const
{
    // Get the parent transform plus all ops including the requested one.
    auto              time = getTime(path());
    UsdGeomXformCache xformCache(time);
    auto              parent = xformCache.GetParentToWorldTransform(prim());
    auto              local = computeLocalInclusiveTransform(prim(), _op, time);
    return toUfe(local * parent);
}

Ufe::Matrix4d UsdTransform3dMatrixOp::segmentExclusiveMatrix() const
{
    // Get the parent transform plus all ops excluding the requested one.
    auto              time = getTime(path());
    UsdGeomXformCache xformCache(time);
    auto              parent = xformCache.GetParentToWorldTransform(prim());
    auto              local = computeLocalExclusiveTransform(prim(), _op, time);
    return toUfe(local * parent);
}

//------------------------------------------------------------------------------
// UsdTransform3dMatrixOpHandler
//------------------------------------------------------------------------------

UsdTransform3dMatrixOpHandler::UsdTransform3dMatrixOpHandler(
    const Ufe::Transform3dHandler::Ptr& nextHandler)
    : Ufe::Transform3dHandler()
    , _nextHandler(nextHandler)
{
}

/*static*/
UsdTransform3dMatrixOpHandler::Ptr
UsdTransform3dMatrixOpHandler::create(const Ufe::Transform3dHandler::Ptr& nextHandler)
{
    return std::make_shared<UsdTransform3dMatrixOpHandler>(nextHandler);
}

Ufe::Transform3d::Ptr
UsdTransform3dMatrixOpHandler::transform3d(const Ufe::SceneItem::Ptr& item) const
{
    // We must create a Transform3d interface to edit the whole object,
    // e.g. setting the local transformation matrix for the complete object.
    auto usdItem = downcast(item);
    if (!usdItem) {
        return nullptr;
    }

    UsdGeomXformable xformable(usdItem->prim());
    bool             unused;
    auto             xformOps = xformable.GetOrderedXformOps(&unused);

    // If there is a single matrix transform op in the transform stack, then
    // transform3d() and editTransform3d() are equivalent: use that matrix op.
    if (xformOps.size() == 1 && xformOps.front().GetOpType() == UsdGeomXformOp::TypeTransform) {
        return UsdTransform3dMatrixOp::create(usdItem, xformOps.front());
    }

    // Find the matrix op to be transformed.
    auto i = findMatrixOp(xformOps);

    // If no matrix was found, pass on to the next handler.
    if (i == xformOps.cend()) {
        return _nextHandler ? _nextHandler->transform3d(item) : nullptr;
    }

    // If we've found a matrix op, but there is a more local non-matrix op in
    // the stack, the more local op should be used.  This will happen e.g. if a
    // pivot edit was done on a matrix op stack.
    //
    // Special case for Maya:
    //   Since matrix ops don't support pivot edits, a fallback Maya stack will
    //   be added, and from that point on the fallback Maya stack must be used.
    if (findNonMatrix(i, xformOps)) {
        return _nextHandler ? _nextHandler->transform3d(item) : nullptr;
    }

    // At this point we know we have a matrix op to transform, and that it is
    // not alone on the transform op stack.  Wrap a matrix op Transform3d
    // interface for that matrix into a UsdTransform3dSetObjectMatrix object.
    // Ml is the transformation before the matrix op, Mr is the transformation
    // after the matrix op.
    auto mlInv = xformInv(xformOps.cbegin(), i, item->path());
    auto mrInv = xformInv(i + 1, xformOps.cend(), item->path());

    return UsdTransform3dSetObjectMatrix::create(
        UsdTransform3dMatrixOp::create(usdItem, *i), mlInv, mrInv);
}

Ufe::Transform3d::Ptr UsdTransform3dMatrixOpHandler::editTransform3d(
    const Ufe::SceneItem::Ptr&      item,
    const Ufe::EditTransform3dHint& hint) const
{
    auto usdItem = downcast(item);
    if (!usdItem) {
        return nullptr;
    }

    // Beware: the default UsdGeomXformOp constructor
    // https://github.com/PixarAnimationStudios/USD/blob/71b4baace2044ea4400ba802e91667f9ebe342f0/pxr/usd/usdGeom/xformOp.h#L148
    // leaves the _opType enum data member uninitialized, which as per
    // https://stackoverflow.com/questions/6842799/enum-variable-default-value/6842821
    // is undefined behavior, so a default constructed UsdGeomXformOp cannot be
    // used as a UsdGeomXformOp::TypeInvalid sentinel value.  PPT, 10-Aug-20.

    // We try to edit a matrix op in the prim's transform op stack.  If a
    // matrix op has been specified, it will be used if found.  If a matrix op
    // has not been specified, we edit the first matrix op in the stack.  If
    // the matrix op is not found, or there is no matrix op in the stack, let
    // the next Transform3d handler in the chain handle the request.
    UsdGeomXformable xformable(usdItem->prim());
    bool             unused;
    auto             xformOps = xformable.GetOrderedXformOps(&unused);

    // Find the matrix op to be transformed.
    auto i = findMatrixOp(xformOps);

    // If no matrix was found, pass on to the next handler.
    if (i == xformOps.cend()) {
        return _nextHandler ? _nextHandler->editTransform3d(item, hint) : nullptr;
    }

    // If we've found a matrix op, but there is a more local non-matrix op in
    // the stack, the more local op should be used.  This will happen e.g. if a
    // pivot edit was done on a matrix op stack.
    //
    // Special case for Maya:
    //   Since matrix ops don't support pivot edits, a fallback Maya stack will
    //   be added, and from that point on the fallback Maya stack must be used.
    //
    // Also, pass pivot edits on to the next handler, since we can't handle them.
    if (findNonMatrix(i, xformOps) || (hint.type() == Ufe::EditTransform3dHint::RotatePivot)
        || (hint.type() == Ufe::EditTransform3dHint::ScalePivot)) {
        return _nextHandler ? _nextHandler->editTransform3d(item, hint) : nullptr;
    }
    return UsdTransform3dMatrixOp::create(usdItem, *i);
}

} // namespace USDUFE_NS_DEF

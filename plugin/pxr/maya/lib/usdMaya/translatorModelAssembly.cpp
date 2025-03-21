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
#include "usdMaya/translatorModelAssembly.h"

#include "usdMaya/editUtil.h"
#include "usdMaya/referenceAssembly.h"

#include <mayaUsd/fileio/jobs/jobArgs.h>
#include <mayaUsd/fileio/primReaderArgs.h>
#include <mayaUsd/fileio/primReaderContext.h>
#include <mayaUsd/fileio/primWriterArgs.h>
#include <mayaUsd/fileio/primWriterContext.h>
#include <mayaUsd/fileio/translators/translatorUtil.h>
#include <mayaUsd/fileio/translators/translatorXformable.h>
#include <mayaUsd/undo/OpUndoItems.h>
#include <mayaUsd/utils/stageCache.h>
#include <mayaUsd/utils/util.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stageCacheContext.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdUtils/pipeline.h>

#include <maya/MDagModifier.h>
#include <maya/MFnAssembly.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MString.h>

#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    ((FilePathPlugName, "filePath"))
    ((PrimPathPlugName, "primPath"))
    ((KindPlugName, "kind"))
    ((MayaProxyShapeNameSuffix, "Proxy"))

    // XXX: These should eventually be replaced/removed when the proxy shape
    // node supports all variantSets and not just modelingVariant.
    (variantKey)
    (modelingVariant)
);
// clang-format on

TF_DEFINE_ENV_SETTING(
    USDMAYA_UNLOAD_REFERENCED_MODELS,
    true,
    "If true, referenced models will not be loaded.  If false, this will "
    "fallback to the load policy on the stage.");

/* static */
bool UsdMayaTranslatorModelAssembly::Create(
    const UsdMayaPrimWriterArgs& args,
    UsdMayaPrimWriterContext*    context)
{
    UsdStageRefPtr stage = context->GetUsdStage();
    SdfPath        authorPath = context->GetAuthorPath();
    UsdTimeCode    usdTime = context->GetTimeCode();

    context->SetExportsGprims(false);
    context->SetPruneChildren(true);
    context->SetModelPaths({ authorPath });

    UsdPrim prim = stage->DefinePrim(authorPath);
    if (!prim) {
        TF_RUNTIME_ERROR(
            "Failed to create prim for USD reference assembly at path <%s>", authorPath.GetText());
        return false;
    }

    // only write references when time is default
    if (!usdTime.IsDefault()) {
        return true;
    }

    // Guard against a situation where the prim being referenced has
    // xformOp's specified in its xformOpOrder but the reference assembly
    // in Maya has an identity transform. We would normally skip writing out
    // the xformOpOrder, but that isn't correct since we would inherit the
    // xformOpOrder, which we don't want.
    // Instead, always write out an empty xformOpOrder if the transform writer
    // did not write out an xformOpOrder in its constructor. This guarantees
    // that we get an identity transform as expected (instead of inheriting).
    bool                        resetsXformStack;
    UsdGeomXformable            xformable(prim);
    std::vector<UsdGeomXformOp> orderedXformOps = xformable.GetOrderedXformOps(&resetsXformStack);
    if (orderedXformOps.empty() && !resetsXformStack) {
        xformable.CreateXformOpOrderAttr().Block();
    }

    const MDagPath& currPath = args.GetMDagPath();

    // because of how we generate these things and node collapsing, sometimes
    // the currPath is for the USD reference assembly and some times it's for
    // the USD proxy shape.
    const MFnDagNode assemblyNode(currPath.transform());

    if (TfGetEnvSetting(USDMAYA_UNLOAD_REFERENCED_MODELS)) {
        // Before we author the reference, we set the load policy on the path to
        // *not* load.  The role of this is to author the reference -- we do not
        // need that part of the scene to be loaded and composed into our current
        // stage.
        stage->Unload(prim.GetPath());
    }

    MStatus status;
    MPlug   usdRefFilepathPlg = assemblyNode.findPlug(_tokens->FilePathPlugName.GetText(), &status);
    if (status == MS::kSuccess) {
        UsdReferences refs = prim.GetReferences();
        std::string   refAssetPath(usdRefFilepathPlg.asString().asChar());

        std::string resolvedRefPath = stage->ResolveIdentifierToEditTarget(refAssetPath);

        if (!resolvedRefPath.empty()) {
            std::string refPrimPathStr;
            MPlug       usdRefPrimPathPlg
                = assemblyNode.findPlug(_tokens->PrimPathPlugName.GetText(), &status);
            if (status == MS::kSuccess) {
                refPrimPathStr = usdRefPrimPathPlg.asString().asChar();
            }

            if (refPrimPathStr.empty()) {
                refs.AddReference(refAssetPath);
            } else {
                SdfPath refPrimPath(refPrimPathStr);

                if (refPrimPath.IsRootPrimPath()) {
                    refs.AddReference(SdfReference(refAssetPath, refPrimPath));
                } else {
                    TF_RUNTIME_ERROR(
                        "Not creating reference for assembly node '%s' "
                        "with non-root prim path <%s>",
                        assemblyNode.fullPathName().asChar(),
                        refPrimPath.GetText());
                }
            }
        } else {
            const std::string errorMsg = TfStringPrintf(
                "Could not resolve reference '%s'; creating placeholder "
                "Xform for <%s>",
                refAssetPath.c_str(),
                authorPath.GetText());
            TF_RUNTIME_ERROR(errorMsg);
            prim.SetDocumentation(errorMsg);
        }
    }

    auto registeredVariantSets = UsdUtilsGetRegisteredVariantSets();
    if (!registeredVariantSets.empty()) {
        // import variant selections: we only import the "persistent" ones.
        for (const auto& regVarSet : registeredVariantSets) {
            switch (regVarSet.selectionExportPolicy) {
            case UsdUtilsRegisteredVariantSet::SelectionExportPolicy::Never:
            case UsdUtilsRegisteredVariantSet::SelectionExportPolicy::IfAuthored: continue;
            case UsdUtilsRegisteredVariantSet::SelectionExportPolicy::Always: break;
            }

            const std::string& variantSetName = regVarSet.name;
            std::string        variantSetPlugName = TfStringPrintf(
                "%s%s", UsdMayaVariantSetTokens->PlugNamePrefix.GetText(), variantSetName.c_str());

            MPlug modelingVariantPlg = assemblyNode.findPlug(variantSetPlugName.c_str(), &status);
            if (status == MS::kSuccess) {
                MString variant;
                modelingVariantPlg.getValue(variant);
                prim.GetVariantSet(variantSetName).SetVariantSelection(variant.asChar());
            }
        }
    } else {
        // export all that we can.
        if (UsdMayaReferenceAssembly* usdRefAssem
            = dynamic_cast<UsdMayaReferenceAssembly*>(assemblyNode.userNode())) {
            for (const auto& varSels : usdRefAssem->GetVariantSetSelections()) {
                const std::string& variantSetName = varSels.first;
                const std::string& variant = varSels.second;
                prim.GetVariantSet(variantSetName).SetVariantSelection(variant);
            }
        }
    }

    // Apply assembly edits, if any are present.
    UsdMayaEditUtil::PathEditMap assemblyEdits;
    std::vector<std::string>     invalidEdits;
    UsdMayaEditUtil::GetEditsForAssembly(assemblyNode.object(), &assemblyEdits, &invalidEdits);

    if (!invalidEdits.empty()) {
        TF_WARN(
            "The following invalid assembly edits were found while exporting "
            "%s node '%s':\n"
            "    %s",
            UsdMayaReferenceAssemblyTokens->MayaTypeName.GetText(),
            assemblyNode.fullPathName().asChar(),
            TfStringJoin(invalidEdits, "\n    ").c_str());
    }

    if (!assemblyEdits.empty()) {
        std::vector<std::string> failedEdits;
        const bool               needsLoadAndUnload = !prim.IsLoaded();

        // the prim must be loaded in order to apply edits.
        if (needsLoadAndUnload) {
            prim.Load();
        }
        UsdMayaEditUtil::ApplyEditsToProxy(assemblyEdits, prim, &failedEdits);
        // restore it to its original unloaded state.
        if (needsLoadAndUnload) {
            prim.Unload();
        }

        if (!failedEdits.empty()) {
            TF_WARN(
                "The following assembly edits could not be applied under the "
                "USD prim '%s' while exporting %s node '%s':\n"
                "    %s",
                prim.GetPath().GetText(),
                UsdMayaReferenceAssemblyTokens->MayaTypeName.GetText(),
                assemblyNode.fullPathName().asChar(),
                TfStringJoin(failedEdits, "\n    ").c_str());
        }
    } else if (args.GetExportRefsAsInstanceable()) {
        // Note that assemblies with edits cannot be instanceable.

        // When bug/128076 is addressed, the IsGroup() check will become
        // unnecessary and obsolete.
        // Until then, we have to check the "group"-ness of the prim's kind
        // explicitly, since UsdPrim::IsGroup() can only return true if
        // IsModel() also returns true, and that will not be the case until the
        // end of the export after the model hierarchy has been fixed up.
        TfToken kind;
        UsdModelAPI(prim).GetKind(&kind);
        if (!prim.HasAuthoredInstanceable()
            && !KindRegistry::GetInstance().IsA(kind, KindTokens->group)) {
            prim.SetInstanceable(true);
        }
    }

    return true;
}

static bool _GetAssetInfo(const UsdPrim& prim, std::string* assetIdentifier, SdfPath* assetPrimPath)
{
    UsdModelAPI  usdModel(prim);
    SdfAssetPath identifier;
    if (!usdModel.GetAssetIdentifier(&identifier)) {
        return false;
    }

    *assetIdentifier = identifier.GetAssetPath();
    // We are assuming the target asset will have defaultPrim.
    *assetPrimPath = SdfPath();
    return true;
}

static bool
_GetReferenceInfo(const UsdPrim& prim, std::string* assetIdentifier, SdfPath* assetPrimPath)
{
    SdfReferenceListOp             refsOp;
    SdfReferenceListOp::ItemVector refs;
    prim.GetMetadata(SdfFieldKeys->References, &refsOp);
    refsOp.ApplyOperations(&refs);

    // this logic is not robust.  awaiting bug 99278.
    if (!refs.empty()) {
        const SdfReference& ref = refs[0];
        *assetIdentifier = ref.GetAssetPath();
        *assetPrimPath = ref.GetPrimPath();
        return true;
    }

    return false;
}

/* static */
bool UsdMayaTranslatorModelAssembly::ShouldImportAsAssembly(
    const UsdPrim& usdImportRootPrim,
    const UsdPrim& prim,
    std::string*   assetIdentifier,
    SdfPath*       assetPrimPath)
{
    if (!prim) {
        return false;
    }

    if (!prim.IsModel()) {
        return false;
    }

    if (prim == usdImportRootPrim) {
        return false;
    }

    // First we check if we're bringing in an asset (and not a reference to an
    // asset).
    if (_GetAssetInfo(prim, assetIdentifier, assetPrimPath)) {
        return true;
    }

    // If we can't find any assetInfo, fall back to checking the reference.
    if (_GetReferenceInfo(prim, assetIdentifier, assetPrimPath)) {
        return true;
    }

    return false;
}

static std::map<std::string, std::string> _GetVariantSelections(const UsdPrim& prim)
{
    std::map<std::string, std::string> varSels;
    UsdVariantSets                     varSets = prim.GetVariantSets();
    std::vector<std::string>           varSetNames = varSets.GetNames();
    TF_FOR_ALL(iter, varSetNames)
    {
        const std::string& varSetName = *iter;
        std::string        varSel = varSets.GetVariantSelection(varSetName);
        if (!varSel.empty()) {
            varSels[varSetName] = varSel;
        }
    }
    return varSels;
}

/* static */
bool UsdMayaTranslatorModelAssembly::Read(
    const UsdPrim&               prim,
    const std::string&           assetIdentifier,
    const SdfPath&               assetPrimPath,
    const MObject&               parentNode,
    const UsdMayaPrimReaderArgs& args,
    UsdMayaPrimReaderContext*    context,
    const TfToken&               assemblyRep)
{
    // This translator does not apply if assemblyRep == "Import".
    if (assemblyRep == UsdMayaJobImportArgsTokens->Import) {
        return false;
    }

    UsdStageCacheContext stageCacheContext(UsdMayaStageCache::Get(
        UsdStage::InitialLoadSet::LoadAll, UsdMayaStageCache::ShareMode::Shared));
    UsdStageRefPtr       usdStage = UsdStage::Open(assetIdentifier);
    if (!usdStage) {
        TF_RUNTIME_ERROR("Cannot open USD file %s", assetIdentifier.c_str());
        return false;
    }

    usdStage->SetEditTarget(usdStage->GetSessionLayer());

    UsdPrim modelPrim;
    if (!assetPrimPath.IsEmpty()) {
        modelPrim = usdStage->GetPrimAtPath(assetPrimPath);
    } else {
        modelPrim = usdStage->GetDefaultPrim();
    }

    if (!modelPrim) {
        TF_RUNTIME_ERROR("Could not find model prim in USD file %s", assetIdentifier.c_str());
        return false;
    }

    // We have to create the new assembly node with the assembly command as
    // opposed to using MDagModifier's createNode() or any other method. That
    // seems to be the only way to ensure that the assembly's namespace and
    // container are setup correctly.
    //
    // TODO UNDO: does this need to be undoable and how to record this in an OpUndoItem?
    const std::string assemblyCmd = TfStringPrintf(
        "import maya.cmds; maya.cmds.assembly(name=\'%s\', type=\'%s\')",
        prim.GetName().GetText(),
        UsdMayaReferenceAssemblyTokens->MayaTypeName.GetText());
    MString newAssemblyName;
    MStatus status = MGlobal::executePythonCommand(assemblyCmd.c_str(), newAssemblyName);
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Now we get the MObject for the assembly node we just created.
    MObject assemblyObj;
    status = UsdMayaUtil::GetMObjectByName(newAssemblyName, assemblyObj);
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Re-parent the assembly node underneath parentNode.
    MDagModifier& dagMod = MayaUsd::MDagModifierUndoItem::create("Assembly reparenting");
    status = dagMod.reparentNode(assemblyObj, parentNode);
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Read xformable attributes from the UsdPrim on to the assembly node.
    UsdGeomXformable xformable(prim);
    UsdMayaTranslatorXformable::Read(xformable, assemblyObj, args, context);

    MFnDependencyNode depNodeFn(assemblyObj, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Set the filePath and primPath attributes.
    MPlug filePathPlug = depNodeFn.findPlug(_tokens->FilePathPlugName.GetText(), true, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    status = dagMod.newPlugValueString(filePathPlug, assetIdentifier.c_str());
    CHECK_MSTATUS_AND_RETURN(status, false);

    MPlug primPathPlug = depNodeFn.findPlug(_tokens->PrimPathPlugName.GetText(), true, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    status = dagMod.newPlugValueString(primPathPlug, modelPrim.GetPath().GetText());
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Set the kind attribute.
    TfToken     modelKind;
    UsdModelAPI usdModel(modelPrim);
    if (!usdModel.GetKind(&modelKind) || modelKind.IsEmpty()) {
        modelKind = KindTokens->component;
    }

    MPlug kindPlug = depNodeFn.findPlug(_tokens->KindPlugName.GetText(), true, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    status = dagMod.newPlugValueString(kindPlug, modelKind.GetText());
    CHECK_MSTATUS_AND_RETURN(status, false);

    // Apply variant selections.
    std::map<std::string, std::string> variantSelections = _GetVariantSelections(prim);
    TF_FOR_ALL(iter, variantSelections)
    {
        std::string variantSetName = iter->first;
        std::string variantSelection = iter->second;

        std::string variantSetPlugName = TfStringPrintf(
            "%s%s", UsdMayaVariantSetTokens->PlugNamePrefix.GetText(), variantSetName.c_str());
        MPlug varSetPlug = depNodeFn.findPlug(variantSetPlugName.c_str(), true, &status);
        if (status != MStatus::kSuccess) {
            MFnTypedAttribute typedAttrFn;
            MObject           attrObj = typedAttrFn.create(
                variantSetPlugName.c_str(),
                variantSetPlugName.c_str(),
                MFnData::kString,
                MObject::kNullObj,
                &status);
            CHECK_MSTATUS_AND_RETURN(status, false);
            status = depNodeFn.addAttribute(attrObj);
            CHECK_MSTATUS_AND_RETURN(status, false);
            varSetPlug = depNodeFn.findPlug(variantSetPlugName.c_str(), true, &status);
            CHECK_MSTATUS_AND_RETURN(status, false);
        }
        status = dagMod.newPlugValueString(varSetPlug, variantSelection.c_str());
        CHECK_MSTATUS_AND_RETURN(status, false);
    }

    status = dagMod.doIt();
    CHECK_MSTATUS_AND_RETURN(status, false);

    if (context) {
        context->RegisterNewMayaNode(prim.GetPath().GetString(), assemblyObj);
        context->SetPruneChildren(true);
    }

    // If a representation was supplied, activate it.
    if (!assemblyRep.IsEmpty()) {
        MFnAssembly assemblyFn(assemblyObj, &status);
        CHECK_MSTATUS_AND_RETURN(status, false);
        if (assemblyFn.canActivate(&status)) {
            status = assemblyFn.activate(assemblyRep.GetText());
            CHECK_MSTATUS_AND_RETURN(status, false);
        }
    }

    // XXX: right now, we lose any edits that may be introduced from
    // the current file on top of the asset we're bringing as an assembly.
    // see bug 125359.

    return true;
}

/* static */
bool UsdMayaTranslatorModelAssembly::ReadAsProxy(
    const UsdPrim&                            prim,
    const std::map<std::string, std::string>& variantSetSelections,
    MObject                                   parentNode,
    const UsdMayaPrimReaderArgs&              args,
    UsdMayaPrimReaderContext*                 context)
{
    if (!prim) {
        return false;
    }

    const SdfPath primPath = prim.GetPath();

    MStatus status;

    // Create a transform node for the proxy node under its parent node.
    MObject transformObj;
    if (!UsdMayaTranslatorUtil::CreateTransformNode(
            prim, parentNode, args, context, &status, &transformObj)) {
        return false;
    }

    // Create the proxy shape node.
    MDagModifier& dagMod = MayaUsd::MDagModifierUndoItem::create("Proxy shape creation");
    MObject       proxyObj
        = dagMod.createNode(UsdMayaProxyShapeTokens->MayaTypeName.GetText(), transformObj, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    status = dagMod.doIt();
    CHECK_MSTATUS_AND_RETURN(status, false);
    TF_VERIFY(!proxyObj.isNull());
    const std::string proxyShapeNodeName = TfStringPrintf(
        "%s%s", prim.GetName().GetText(), _tokens->MayaProxyShapeNameSuffix.GetText());
    status = dagMod.renameNode(proxyObj, proxyShapeNodeName.c_str());
    CHECK_MSTATUS_AND_RETURN(status, false);
    if (context) {
        const SdfPath shapePrimPath = primPath.AppendChild(TfToken(proxyShapeNodeName));
        context->RegisterNewMayaNode(shapePrimPath.GetString(), proxyObj);
    }

    // Set the filePath and primPath attributes.
    MFnDependencyNode depNodeFn(proxyObj, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    MPlug filePathPlug = depNodeFn.findPlug(_tokens->FilePathPlugName.GetText(), true, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    const std::string rootLayerRealPath = prim.GetStage()->GetRootLayer()->GetRealPath();
    status = dagMod.newPlugValueString(filePathPlug, rootLayerRealPath.c_str());
    CHECK_MSTATUS_AND_RETURN(status, false);

    MPlug primPathPlug = depNodeFn.findPlug(_tokens->PrimPathPlugName.GetText(), true, &status);
    CHECK_MSTATUS_AND_RETURN(status, false);
    status = dagMod.newPlugValueString(primPathPlug, primPath.GetText());
    CHECK_MSTATUS_AND_RETURN(status, false);

    // XXX: For now, the proxy shape only support modelingVariant with the
    // 'variantKey' attribute. Eventually, it should support any/all
    // variantSets.
    const std::map<std::string, std::string>::const_iterator varSetIter
        = variantSetSelections.find(_tokens->modelingVariant.GetString());
    if (varSetIter != variantSetSelections.end()) {
        const std::string modelingVariantSelection = varSetIter->second;
        MPlug variantKeyPlug = depNodeFn.findPlug(_tokens->variantKey.GetText(), true, &status);
        CHECK_MSTATUS_AND_RETURN(status, false);
        status = dagMod.newPlugValueString(variantKeyPlug, modelingVariantSelection.c_str());
        CHECK_MSTATUS_AND_RETURN(status, false);
    }

    status = dagMod.doIt();
    CHECK_MSTATUS_AND_RETURN(status, false);

    if (context) {
        context->SetPruneChildren(true);
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

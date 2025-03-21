#!/usr/bin/env python

#
# Copyright 2021 Autodesk
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import fixturesUtils

from usdUtils import createSimpleXformScene, createDuoXformScene

from maya import OpenMaya as OM
from maya import OpenMayaAnim as OMA

import mayaUsd.lib

import ufeUtils
import mayaUtils
import mayaUsd.ufe

from pxr import Usd, UsdGeom, Gf, Sdf, Vt, UsdSkel

from maya import cmds
from maya import standalone
from maya.api import OpenMaya as om

import ufe

import unittest

from testUtils import assertVectorAlmostEqual

import os
import sys

class EditAsMayaTestCase(unittest.TestCase):
    '''Test edit as Maya: bring USD data into Maya to edit.

    Also known as Pull to DG.
    '''

    pluginsLoaded = False

    @classmethod
    def setUpClass(cls):
        fixturesUtils.readOnlySetUpClass(__file__, loadPlugin=False)
        cls.inputPath = fixturesUtils.setUpClass(__file__)
        sys.path.append(cls.inputPath)

        if not cls.pluginsLoaded:
            cls.pluginsLoaded = mayaUtils.isMayaUsdPluginLoaded()

    @classmethod
    def tearDownClass(cls):
        standalone.uninitialize()

    def setUp(self):
        cmds.file(new=True, force=True)

    def testCannotEditAsMayaAnAncestor(self):
        '''Test that trying to edit an ancestor is not allowed.'''

        (ps, aXlateOp, aXlation, aUsdUfePathStr, aUsdUfePath, aUsdItem,
             bXlateOp, bXlation, bUsdUfePathStr, bUsdUfePath, bUsdItem) = createSimpleXformScene()

        # Edit "B" Prim as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(bUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(bUsdUfePathStr))

        # Verify that its ancestor "A" Prim cannot be edited as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.editAsMaya(aUsdUfePathStr))

    @unittest.skipIf(os.getenv('HAS_ORPHANED_NODES_MANAGER', '0') != '1', 'Test only available when UFE supports the orphaned nodes manager')
    def testRenameAncestorOfEditAsMaya(self):
        '''Test that renaming an ancestor correctly updates the internal data.'''

        (_, _, _, aUsdUfePathStr, aUsdUfePath, _,
             _, _, _, _, _) = createSimpleXformScene()

        # Edit "A" Prim as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(aUsdUfePathStr))

        def validateEditAsMayaMetadata():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            self.assertEqual(aMayaPath.nbSegments(), 1)
            mayaToUsd = ufe.PathMappingHandler.pathMappingHandler(aMayaItem)
            aFromHostUfePath = mayaToUsd.fromHost(aMayaPath)
            self.assertEqual(ufe.PathString.string(aFromHostUfePath), aUsdUfePathStr)
            aDagPath = om.MSelectionList().add(ufe.PathString.string(aMayaPath)).getDagPath(0)
            aPullUfePath = cmds.getAttr(str(aDagPath) + ".Pull_UfePath")
            self.assertEqual(aPullUfePath, aUsdUfePathStr)
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))

        validateEditAsMayaMetadata()

        cmds.rename("stage1", "waka")

        aUsdUfePathStr = aUsdUfePathStr.replace("stage1", "waka").replace("stageShape1", "wakaShape")
        validateEditAsMayaMetadata()

        # Verify we can merge "A" Maya data after the rename of the stage.
        with mayaUsd.lib.OpUndoItemList():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.mergeToUsd(ufe.PathString.string(aMayaPath)))

    @unittest.skipIf(os.getenv('HAS_ORPHANED_NODES_MANAGER', '0') != '1', 'Test only available when UFE supports the orphaned nodes manager')
    def testReparentAncestorOfEditAsMaya(self):
        '''Test that reparenting an ancestor correctly updates the internal data.'''

        cmds.CreateLocator()
        locName = "locator1"
        
        (_, _, _, aUsdUfePathStr, aUsdUfePath, _,
             _, _, _, _, _) = createSimpleXformScene()

        # Edit "A" Prim as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(aUsdUfePathStr))

        def validateEditAsMayaMetadata():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            self.assertEqual(aMayaPath.nbSegments(), 1)
            mayaToUsd = ufe.PathMappingHandler.pathMappingHandler(aMayaItem)
            aFromHostUfePath = mayaToUsd.fromHost(aMayaPath)
            self.assertEqual(ufe.PathString.string(aFromHostUfePath), aUsdUfePathStr)
            aDagPath = om.MSelectionList().add(ufe.PathString.string(aMayaPath)).getDagPath(0)
            aPullUfePath = cmds.getAttr(str(aDagPath) + ".Pull_UfePath")
            self.assertEqual(aPullUfePath, aUsdUfePathStr)
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))

        validateEditAsMayaMetadata()

        # Note: the parent command changes the selection, so preserve and restore it.
        selToPreserve = ufe.GlobalSelection.get().front()
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.discardEdits("A"))
        cmds.parent("stage1", locName)
        ufe.GlobalSelection.get().clear()
        ufe.GlobalSelection.get().append(selToPreserve)

        # Edit "A" Prim as Maya data.
        aUsdUfePathStr = "|" + locName + aUsdUfePathStr
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(aUsdUfePathStr))
            validateEditAsMayaMetadata()

        # Verify we can merge "A" Maya data after the rename of the stage.
        with mayaUsd.lib.OpUndoItemList():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.mergeToUsd(ufe.PathString.string(aMayaPath)))

    @unittest.skipIf(os.getenv('HAS_ORPHANED_NODES_MANAGER', '0') != '1', 'Test only available when UFE supports the orphaned nodes manager')
    def testReparentUsdAncestorOfEditAsMaya(self):
        '''Test that reparenting an usd ancestor correctly updates the internal data.'''
        def validateEditAsMayaMetadata(mayaItem, ufePathStr):
            mayaPath = mayaItem.path()
            self.assertEqual(mayaPath.nbSegments(), 1)
            mayaToUsd = ufe.PathMappingHandler.pathMappingHandler(mayaItem)
            fromHostUfePath = mayaToUsd.fromHost(mayaPath)
            self.assertEqual(ufe.PathString.string(fromHostUfePath), ufePathStr)
            dagPath = om.MSelectionList().add(ufe.PathString.string(mayaPath)).getDagPath(0)
            pullUfePath = cmds.getAttr(str(dagPath) + ".Pull_UfePath")
            self.assertEqual(pullUfePath, ufePathStr)
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(ufePathStr))

        def verifyDagXformAndVis(dagPath, expectedXlation, expectedVis):
            mXformMatrix = om.MTransformationMatrix(dagPath.inclusiveMatrix())
            mXlation = mXformMatrix.translation(om.MSpace.kObject)
            assertVectorAlmostEqual(self, mXlation, expectedXlation)
            self.assertEqual(dagPath.isVisible(), expectedVis)

        (ps,
         aXlateOp, aUsdXlation, aUsdUfePathStr, _, _,
         bXlateOp, bUsdXlation, bUsdUfePathStr, _, _) = createDuoXformScene()

        proxyShapePathStr = ufe.PathString.string(ps.path())
        stage = mayaUsd.ufe.getStage(proxyShapePathStr)
        editedPrim = stage.DefinePrim("/A/Edited", "Xform")

        with mayaUsd.lib.OpUndoItemList():
            editedUfePathStr = "{},{}".format(proxyShapePathStr, editedPrim.GetPath())
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(editedUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(editedUfePathStr))

        editedMayaItem = ufe.GlobalSelection.get().front()
        editedMayaPathStr = ufe.PathString.string(editedMayaItem.path())
        editedDagPath = om.MSelectionList().add(editedMayaPathStr).getDagPath(0)

        verifyDagXformAndVis(editedDagPath, aUsdXlation, True)
        validateEditAsMayaMetadata(editedMayaItem, editedUfePathStr)

        # Reparent /A under /B
        cmds.parent(aUsdUfePathStr, bUsdUfePathStr)

        # Make /B invisible and translate it.
        bPrim = mayaUsd.ufe.ufePathToPrim(bUsdUfePathStr)
        UsdGeom.Imageable(bPrim).CreateVisibilityAttr().Set(UsdGeom.Tokens.invisible)

        bOffset = Gf.Vec3d(100, 100, 100)
        bXlateOp.Set(bXlateOp.Get() + bOffset)

        # After reparent "/A/Edited" xform is conserved and inherits b offset
        # /B invisibility is also inherited.
        verifyDagXformAndVis(editedDagPath, aUsdXlation + bOffset, False)
        validateEditAsMayaMetadata(editedMayaItem, bUsdUfePathStr + "/A/Edited")

        # Verify we can merge "Edited" Maya data after the reparent
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.mergeToUsd(editedMayaPathStr))

    def testEditAsMayaPreserveUsdSkel(self):
        '''Test that edit does not change the usd skel prim data.'''

        path = os.path.join(self.inputPath, "skelCube.usda")

        proxyShapeDagPath, usdStage = mayaUtils.createProxyFromFile(path)

        proxyRoot = proxyShapeDagPath + ",/Root"
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(proxyRoot))
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(proxyRoot))

        aMayaItem = ufe.GlobalSelection.get().front()
        options = { "writeDefaults": True, "exportSkels": "auto", "exportSkin": "auto" }
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.mergeToUsd(ufe.PathString.string(aMayaItem.path()), options))

        meshPrim = usdStage.GetPrimAtPath('/Root/Cube')
        skelPrim = usdStage.GetPrimAtPath('/Root/Skeleton')
        self.assertTrue(skelPrim)
        self.assertTrue(meshPrim)
        meshBinding = UsdSkel.BindingAPI.Get(usdStage, meshPrim.GetPath())
        self.assertTrue(meshBinding)
        self.assertTrue(meshBinding.GetSkeletonRel().GetTargets()[0], skelPrim.GetPath())

    def testEditAsMayaPreserveTimeline(self):
        '''Test that edit does not change the timeline start and end.'''

        (ps, aXlateOp, aXlation, aUsdUfePathStr, aUsdUfePath, aUsdItem,
             bXlateOp, bXlation, bUsdUfePathStr, bUsdUfePath, bUsdItem) = createSimpleXformScene()

        timeUnit = OM.MTime.uiUnit()

        startTime = OM.MTime(30, timeUnit)
        minTime = OM.MTime(40, timeUnit)
        maxTime = OM.MTime(50, timeUnit)
        endTime = OM.MTime(60, timeUnit)

        OMA.MAnimControl.setMinMaxTime(minTime, maxTime)
        OMA.MAnimControl.setAnimationStartEndTime(startTime, endTime)

        def verifyTimeline():
            self.assertEqual(minTime.value(), OMA.MAnimControl.minTime().value())
            self.assertEqual(maxTime.value(), OMA.MAnimControl.maxTime().value())
            self.assertEqual(startTime.value(), OMA.MAnimControl.animationStartTime().value())
            self.assertEqual(endTime.value(), OMA.MAnimControl.animationEndTime().value())

        verifyTimeline()

        # Edit "B" Prim as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(bUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(bUsdUfePathStr))

        verifyTimeline()

    def testTransformEditAsMaya(self):
        '''Edit a USD transform as a Maya object.'''

        (ps, xlateOp, xlation, aUsdUfePathStr, aUsdUfePath, aUsdItem,
         _, _, _, _, _) = createSimpleXformScene()

        # Edit aPrim as Maya data.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(aUsdUfePathStr))

        # Test the path mapping services.
        #
        # Unfortunately, toHost is unimplemented as of 20-Sep-2021.  Use the
        # selection to retrieve the Maya object.
        #
        # usdToMaya = ufe.PathMappingHandler.pathMappingHandler(aUsdItem)
        # aMayaUfePath = usdToMaya.toHost(aUsdUfePath)
        # self.assertEqual(aMayaUfePath.nbSegments(), 1)

        aMayaItem = ufe.GlobalSelection.get().front()
        aMayaPath = aMayaItem.path()
        self.assertEqual(aMayaPath.nbSegments(), 1)
        mayaToUsd = ufe.PathMappingHandler.pathMappingHandler(aMayaItem)
        self.assertEqual(mayaToUsd.fromHost(aMayaPath), aUsdUfePath)

        # Confirm the hierarchy is preserved through the Hierarchy interface:
        # one child, the parent of the pulled item is the proxy shape, and
        # the proxy shape has the pulled item as a child, not the original USD
        # scene item.
        aMayaHier = ufe.Hierarchy.hierarchy(aMayaItem)
        self.assertEqual(len(aMayaHier.children()), 1)
        self.assertEqual(ps, aMayaHier.parent())
        psHier = ufe.Hierarchy.hierarchy(ps)
        self.assertIn(aMayaItem, psHier.children())
        self.assertNotIn(aUsdItem, psHier.children())

        # Confirm the translation has been transferred, and that the local
        # transformation is only a translation.
        aDagPath = om.MSelectionList().add(ufe.PathString.string(aMayaPath)).getDagPath(0)
        aFn= om.MFnTransform(aDagPath)
        self.assertEqual(aFn.translation(om.MSpace.kObject), om.MVector(*xlation))
        mayaMatrix = aFn.transformation().asMatrix()
        usdMatrix = xlateOp.GetOpTransform(mayaUsd.ufe.getTime(aUsdUfePathStr))
        mayaValues = [v for v in mayaMatrix]
        usdValues = [v for row in usdMatrix for v in row]

        assertVectorAlmostEqual(self, mayaValues, usdValues)

    def testEditAsMayaUndoRedo(self):
        '''Edit a USD transform as a Maya object and apply undo and redo.'''

        (ps, xlateOp, xlation, aUsdUfePathStr, aUsdUfePath, aUsdItem,
         _, _, _, _, _) = createSimpleXformScene()

        aPrim = mayaUsd.ufe.ufePathToPrim(aUsdUfePathStr)

        # Edit aPrim as Maya data.
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(aUsdUfePathStr))

        # Make a selection before edit as Maya.
        cmds.select('persp')
        previousSn = cmds.ls(sl=True, ufe=True, long=True)

        cmds.mayaUsdEditAsMaya(aUsdUfePathStr)

        def getMayaPathStr():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            aMayaPathStr = ufe.PathString.string(aMayaPath)
            return aMayaPathStr

        aMayaPathStr = getMayaPathStr()

        def verifyEditedScene():
            aMayaItem = ufe.GlobalSelection.get().front()
            aMayaPath = aMayaItem.path()
            aMayaPathStr = ufe.PathString.string(aMayaPath)

            # Confirm the hierarchy is preserved through the Hierarchy interface:
            # one child, the parent of the pulled item is the proxy shape, and
            # the proxy shape has the pulled item as a child, not the original USD
            # scene item.
            aMayaHier = ufe.Hierarchy.hierarchy(aMayaItem)
            self.assertEqual(len(aMayaHier.children()), 1)
            self.assertEqual(ps, aMayaHier.parent())
            psHier = ufe.Hierarchy.hierarchy(ps)
            self.assertIn(aMayaItem, psHier.children())
            self.assertNotIn(aUsdItem, psHier.children())

            # Confirm the translation has been transferred, and that the local
            # transformation is only a translation.
            aDagPath = om.MSelectionList().add(ufe.PathString.string(aMayaPath)).getDagPath(0)
            aFn= om.MFnTransform(aDagPath)
            self.assertEqual(aFn.translation(om.MSpace.kObject), om.MVector(*xlation))
            mayaMatrix = aFn.transformation().asMatrix()
            usdMatrix = xlateOp.GetOpTransform(mayaUsd.ufe.getTime(aUsdUfePathStr))
            mayaValues = [v for v in mayaMatrix]
            usdValues = [v for row in usdMatrix for v in row]

            assertVectorAlmostEqual(self, mayaValues, usdValues)

            # Selection is on the edited Maya object.
            sn = cmds.ls(sl=True, ufe=True, long=True)
            self.assertEqual(len(sn), 1)
            self.assertEqual(sn[0], aMayaPathStr)

            # Read the pull information from the pulled prim.
            aMayaPullPathStr = mayaUsd.lib.PrimUpdaterManager.readPullInformation(aPrim)
            self.assertEqual(aMayaPathStr, aMayaPullPathStr)

        verifyEditedScene()

        # Undo
        cmds.undo()

        def verifyNoLongerEdited():
            # Maya node is removed.
            with self.assertRaises(RuntimeError):
                om.MSelectionList().add(aMayaPathStr)
            # Selection is restored.
            self.assertEqual(cmds.ls(sl=True, ufe=True, long=True), previousSn)
            # No more pull information on the prim.
            self.assertEqual(len(mayaUsd.lib.PrimUpdaterManager.readPullInformation(aPrim)), 0)

        verifyNoLongerEdited()
        
        # Redo
        cmds.redo()

        verifyEditedScene()

    @unittest.skipUnless(ufeUtils.ufeFeatureSetVersion() >= 4, 'Test only available in UFE v4 or greater')
    def testEditAsMayaUIInfo(self):
        '''Edit a USD transform as a Maya Object and test the UI Info.'''

        # Maya UI info handler
        rid = ufe.RunTimeMgr.instance().getId('Maya-DG')
        ufeUIInfo = ufe.UIInfoHandler.uiInfoHandler(rid)
        self.assertIsNotNone(ufeUIInfo)

        (ps, aXlateOp, aXlation, aUsdUfePathStr, aUsdUfePath, aUsdItem,
             bXlateOp, bXlation, bUsdUfePathStr, bUsdUfePath, bUsdItem) = createSimpleXformScene()
        aPrim = mayaUsd.ufe.ufePathToPrim(aUsdUfePathStr)

        # Edit bPrim as Maya data. This will auto-select the item after so get the Maya scene item
        # for this edited object.
        cmds.mayaUsdEditAsMaya(bUsdUfePathStr)
        bMayaItem = ufe.GlobalSelection.get().front()

        # Initially there should be no special color or icon mode.
        # Note: operator= in Python creates a new variable that shares the reference
        #       of the original object. So don't create initTextFgClr from ci.textFgColor.
        ci = ufe.CellInfo()
        ci.textFgColor = ufe.Color3f(0.5, 0.5, 0.5)
        initTextFgClr = ufe.Color3f(ci.textFgColor.r(), ci.textFgColor.g(), ci.textFgColor.b())
        ufeUIInfo.treeViewCellInfo(bMayaItem, ci)
        self.assertEqual(initTextFgClr, ci.textFgColor)
        icon = ufeUIInfo.treeViewIcon(bMayaItem)
        self.assertEqual(ufe.UIInfoHandler.Normal, icon.mode)

        # Deactivating the parent of this pulled item will orphan it.
        # This will set a disabled text foreground color and a disabled mode for the icon.
        aPrim.SetActive(False)
        ufeUIInfo.treeViewCellInfo(bMayaItem, ci)
        self.assertFalse(initTextFgClr == ci.textFgColor)
        icon = ufeUIInfo.treeViewIcon(bMayaItem)
        self.assertEqual('', icon.baseIcon)
        self.assertEqual(ufe.UIInfoHandler.Disabled, icon.mode)

    def testIllegalEditAsMaya(self):
        '''Trying to edit as Maya on object that doesn't support it.'''
        
        import mayaUsd_createStageWithNewLayer

        proxyShapePathStr = mayaUsd_createStageWithNewLayer.createStageWithNewLayer()
        stage = mayaUsd.lib.GetPrim(proxyShapePathStr).GetStage()
        blendShape = stage.DefinePrim('/BlendShape1', 'BlendShape')
        scope = stage.DefinePrim('/Scope1', 'Scope')
        self.assertIsNotNone(scope)
        mesh = stage.DefinePrim('/Mesh1', 'Mesh')
        self.assertIsNotNone(mesh)
        mat = stage.DefinePrim('/Material1', 'Material')
        self.assertIsNotNone(mat)
        instanced = stage.DefinePrim('/Instanced', 'Xform')
        self.assertIsNotNone(instanced)
        proto = stage.DefinePrim('/Proto', 'Xform')
        self.assertIsNotNone(proto)
        protoMesh = stage.DefinePrim('/Proto/Mesh', 'Mesh')
        self.assertIsNotNone(protoMesh)
        instanced.GetReferences().AddInternalReference('/Proto')
        instanced.SetInstanceable(True)

        blendShapePathStr = proxyShapePathStr + ',/BlendShape1'
        scopePathStr = proxyShapePathStr + ',/Scope1'

        # Blend shape cannot be edited as Maya: it has no importer.
        with mayaUsd.lib.OpUndoItemList():
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(blendShapePathStr))
            self.assertFalse(mayaUsd.lib.PrimUpdaterManager.editAsMaya(blendShapePathStr))

        # Scope cannot be edited as Maya: it has no exporter.
        # Unfortunately, as of 17-Nov-2021, we cannot determine how a prim will
        # round-trip, so we cannot use the information that scope has no
        # exporter.
        # with mayaUsd.lib.OpUndoItemList():
        #     self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(scopePathStr))
        #     self.assertFalse(mayaUsd.lib.PrimUpdaterManager.editAsMaya(scopePathStr))
        
        # Mesh can be edited as Maya.
        self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(proxyShapePathStr + ',/Mesh1'))

        # Material cannot be edited as Maya: it explicitly disables this
        # capability.
        self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(proxyShapePathStr + ',/Material1'))

        # Instance proxies cannot be edited as Maya: it explicitly disables this
        # capability.
        self.assertFalse(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(proxyShapePathStr + ',/Instanced/Proto/Mesh'))

    def testSessionLayer(self):
        '''Verify that the edit gets on the sessionLayer instead of the editTarget layer.'''
        
        import mayaUsd_createStageWithNewLayer

        proxyShapePathStr = mayaUsd_createStageWithNewLayer.createStageWithNewLayer()
        stage = mayaUsd.lib.GetPrim(proxyShapePathStr).GetStage()
        sessionLayer = stage.GetSessionLayer()
        prim = stage.DefinePrim('/A', 'Xform')

        primPathStr = proxyShapePathStr + ',/A'

        self.assertTrue(stage.GetSessionLayer().empty)

        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(primPathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(primPathStr))

        self.assertFalse(stage.GetSessionLayer().empty)

        kPullPrimMetadataKey = "Maya:Pull:DagPath"
        self.assertEqual(prim.GetCustomDataByKey(kPullPrimMetadataKey), "|__mayaUsd__|AParent|A")

        # Discard Maya edits, but there is nothing to discard.
        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.discardEdits("A"))

        self.assertTrue(stage.GetSessionLayer().empty)

        self.assertEqual(prim.GetCustomDataByKey(kPullPrimMetadataKey), None)

    def testTargetLayer(self):
        '''Verify that the target layer is not moved after Edit As Maya.'''
        
        import mayaUsd_createStageWithNewLayer

        proxyShapePathStr = mayaUsd_createStageWithNewLayer.createStageWithNewLayer()
        stage = mayaUsd.lib.GetPrim(proxyShapePathStr).GetStage()
        rootLayer = stage.GetRootLayer()

        currentLayer = stage.GetEditTarget().GetLayer()
        self.assertEqual(currentLayer, rootLayer) # Current layer should be the Anonymous Root Layer

        sessionLayer = stage.GetSessionLayer()
        prim = stage.DefinePrim('/A', 'Xform')

        primPathStr = proxyShapePathStr + ',/A'

        self.assertTrue(stage.GetSessionLayer().empty)

        otherLayerId = cmds.mayaUsdLayerEditor(rootLayer.identifier, edit=True, addAnonymous="otherLayer")[0]
        otherLayer = Sdf.Layer.Find(otherLayerId)

        stage.SetEditTarget(otherLayer)

        currentLayer = stage.GetEditTarget().GetLayer()
        self.assertEqual(currentLayer, otherLayer) # Current layer should be the Other Layer

        with mayaUsd.lib.OpUndoItemList():
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.canEditAsMaya(primPathStr))
            self.assertTrue(mayaUsd.lib.PrimUpdaterManager.editAsMaya(primPathStr))

        self.assertFalse(stage.GetSessionLayer().empty)

        currentLayer = stage.GetEditTarget().GetLayer()
        self.assertEqual(currentLayer, otherLayer) # Current layer should still be the Other Layer


if __name__ == '__main__':
    unittest.main(verbosity=2)

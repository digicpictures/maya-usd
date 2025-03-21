#!/usr/bin/env python

#
# Copyright 2022 Autodesk
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
import mayaUtils
import testUtils
import usdUtils

from pxr import Usd, Sdf

from maya import cmds
from maya import standalone

import ufe
import os

import unittest

class LightTestCase(unittest.TestCase):
    '''Verify the Light UFE translate interface, for multiple runtimes.
    
    UFE Feature : Light
    Maya Feature : light
    Action : set & read light parameters
    Applied On Selection : No
    Undo/Redo Test : No
    Expect Results To Test :
        - Setting a value through Ufe correctly updates USD
        - Reading a value through Ufe gets the correct value
    Edge Cases :
        - None.
    '''

    pluginsLoaded = False
    
    @classmethod
    def setUpClass(cls):
        fixturesUtils.readOnlySetUpClass(__file__, loadPlugin=False)

        if not cls.pluginsLoaded:
            cls.pluginsLoaded = mayaUtils.isMayaUsdPluginLoaded()

    @classmethod
    def tearDownClass(cls):
        standalone.uninitialize()

    def setUp(self):
        ''' Called initially to set up the maya test environment '''
        # Load plugins
        self.assertTrue(self.pluginsLoaded)

        # value from UsdGeomLinearUnits
        self.inchesToCm = 2.54
        self.mmToCm = 0.1

    def _StartTest(self, testName):
        cmds.file(force=True, new=True)
        self._testName = testName
        testFile = testUtils.getTestScene("light", self._testName + ".usda")
        shapeNode, shapeStage = mayaUtils.createProxyFromFile(testFile)
        globalSelection = ufe.GlobalSelection.get()
        globalSelection.clear()
        return shapeNode, shapeStage

    def _TestSpotLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Spot)
        self._TestIntensity(ufeLight, usdLight)
        self._TestDiffuse(ufeLight, usdLight)
        self._TestSpecular(ufeLight, usdLight)
        self._TestShadowEnable(ufeLight, usdLight)
        self._TestColor(ufeLight, usdLight)
        self._TestShadowColor(ufeLight, usdLight)
        self._TestSphereProps(ufeLight, usdLight)
        self._TestConeProps(ufeLight, usdLight)
        self.assertEqual(None, ufeLight.directionalInterface())
        self.assertEqual(None, ufeLight.areaInterface())

    def _TestPointLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        if (os.getenv('UFE_VOLUME_LIGHTS_SUPPORT', 'FALSE') == 'TRUE'):
            # With Ufe volume light support point light will be treated as a special kind of
            # sphere light where the gizmo will be handled in Maya.
            self.assertEqual(ufeLight.type(), ufe.Light.Sphere)
        else:
            self.assertEqual(ufeLight.type(), ufe.Light.Point)
        self._TestIntensity(ufeLight, usdLight)
        self._TestDiffuse(ufeLight, usdLight)
        self._TestSpecular(ufeLight, usdLight)
        self._TestShadowEnable(ufeLight, usdLight)
        self._TestColor(ufeLight, usdLight)
        self._TestShadowColor(ufeLight, usdLight)
        self._TestSphereProps(ufeLight, usdLight)
        self.assertEqual(None, ufeLight.coneInterface())
        self.assertEqual(None, ufeLight.directionalInterface())
        self.assertEqual(None, ufeLight.areaInterface())

    def _TestDirectionalLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Directional)
        self._TestIntensity(ufeLight, usdLight)
        self._TestDiffuse(ufeLight, usdLight)
        self._TestSpecular(ufeLight, usdLight)
        self._TestShadowEnable(ufeLight, usdLight)
        self._TestColor(ufeLight, usdLight)
        self._TestShadowColor(ufeLight, usdLight)
        self._TestDirectionalProps(ufeLight, usdLight)
        self.assertEqual(None, ufeLight.coneInterface())
        self.assertEqual(None, ufeLight.sphereInterface())
        self.assertEqual(None, ufeLight.areaInterface())

    def _TestAreaLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Area)
        self._TestIntensity(ufeLight, usdLight)
        self._TestDiffuse(ufeLight, usdLight)
        self._TestSpecular(ufeLight, usdLight)
        self._TestShadowEnable(ufeLight, usdLight)
        self._TestColor(ufeLight, usdLight)
        self._TestShadowColor(ufeLight, usdLight)
        self._TestAreaProps(ufeLight, usdLight)
        self.assertEqual(None, ufeLight.coneInterface())
        self.assertEqual(None, ufeLight.sphereInterface())
        self.assertEqual(None, ufeLight.directionalInterface())

    # Test VolumeLight support
    def _TestCylinderLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Cylinder)
        self.assertEqual(None, ufeLight.diskInterface())
        self.assertEqual(None, ufeLight.domeInterface())
   
    def _TestDiskLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Disk)
        self.assertEqual(None, ufeLight.cylinderInterface())
        self.assertEqual(None, ufeLight.domeInterface())

    def _TestDomeLight(self, ufeLight, usdLight):
        # Trust that the USD API works correctly, validate that UFE gives us
        # the same answers
        self.assertEqual(ufeLight.type(), ufe.Light.Dome)
        self.assertEqual(None, ufeLight.cylinderInterface())
        self.assertEqual(None, ufeLight.diskInterface())
    
    def _TestIntensity(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:intensity')
        self.assertAlmostEqual(usdAttr.Get(), ufeLight.intensity())

        usdAttr.Set(0.5)
        self.assertAlmostEqual(0.5, ufeLight.intensity())

        ufeLight.intensity(100)
        self.assertAlmostEqual(usdAttr.Get(), 100)

    def _TestDiffuse(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:diffuse')
        self.assertAlmostEqual(usdAttr.Get(), ufeLight.diffuse())

        usdAttr.Set(0.5)
        self.assertAlmostEqual(0.5, ufeLight.diffuse())

        ufeLight.diffuse(0.9)
        self.assertAlmostEqual(usdAttr.Get(), 0.9)

    def _TestSpecular(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:specular')
        self.assertAlmostEqual(usdAttr.Get(), ufeLight.specular())

        usdAttr.Set(0.5)
        self.assertAlmostEqual(0.5, ufeLight.specular())

        ufeLight.specular(0.9)
        self.assertAlmostEqual(usdAttr.Get(), 0.9)

    def _TestShadowEnable(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:shadow:enable')
        self.assertEqual(usdAttr.Get(), ufeLight.shadowEnable())

        usdAttr.Set(False)
        self.assertEqual(False, ufeLight.shadowEnable())

        ufeLight.shadowEnable(True)
        self.assertAlmostEqual(usdAttr.Get(), True)        

    def _TestColor(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:color')
        self.assertAlmostEqual(usdAttr.Get()[0], ufeLight.color().r())
        self.assertAlmostEqual(usdAttr.Get()[1], ufeLight.color().g())
        self.assertAlmostEqual(usdAttr.Get()[2], ufeLight.color().b())

    def _TestShadowColor(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:shadow:color')
        self.assertAlmostEqual(usdAttr.Get()[0], ufeLight.shadowColor().r())
        self.assertAlmostEqual(usdAttr.Get()[1], ufeLight.shadowColor().g())
        self.assertAlmostEqual(usdAttr.Get()[2], ufeLight.shadowColor().b())

    def _TestSphereProps(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:radius')
        self.assertAlmostEqual(usdAttr.Get(), ufeLight.sphereInterface().sphereProps().radius)
        self.assertEqual(usdAttr.Get() == 0, ufeLight.sphereInterface().sphereProps().asPoint)

    def _TestConeProps(self, ufeLight, usdLight):
        usdAttrFocus = usdLight.GetAttribute('inputs:shaping:focus')
        usdAttrAngle = usdLight.GetAttribute('inputs:shaping:cone:angle')
        usdAttrSoftness = usdLight.GetAttribute('inputs:shaping:cone:softness')
        self.assertAlmostEqual(usdAttrFocus.Get(), ufeLight.coneInterface().coneProps().focus)
        self.assertAlmostEqual(usdAttrAngle.Get(), ufeLight.coneInterface().coneProps().angle)
        self.assertAlmostEqual(usdAttrSoftness.Get(), ufeLight.coneInterface().coneProps().softness)

    def _TestDirectionalProps(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:angle')
        self.assertAlmostEqual(usdAttr.Get(), ufeLight.directionalInterface().angle())

    def _TestAreaProps(self, ufeLight, usdLight):
        usdAttr = usdLight.GetAttribute('inputs:normalize')
        self.assertEqual(usdAttr.Get(), ufeLight.areaInterface().normalize())        

    def testUsdLight(self):
        shapeNode, _ = self._StartTest('SimpleLight')

        mayaPathSegment = mayaUtils.createUfePathSegment(shapeNode)
        
        # test spot light
        spotlightUsdPathSegment = usdUtils.createUfePathSegment('/lights/spotLight')
        spotlightPath = ufe.Path([mayaPathSegment, spotlightUsdPathSegment])
        spotlightItem = ufe.Hierarchy.createItem(spotlightPath)

        ufeSpotLight = ufe.Light.light(spotlightItem)
        usdSpotLight = usdUtils.getPrimFromSceneItem(spotlightItem)
        self._TestSpotLight(ufeSpotLight, usdSpotLight)

        # test point light
        pointlightUsdPathSegment = usdUtils.createUfePathSegment('/lights/pointLight')
        pointlightPath = ufe.Path([mayaPathSegment, pointlightUsdPathSegment])
        pointlightItem = ufe.Hierarchy.createItem(pointlightPath)

        ufePointLight = ufe.Light.light(pointlightItem)
        usdPointLight = usdUtils.getPrimFromSceneItem(pointlightItem)
        self._TestPointLight(ufePointLight, usdPointLight)        

        # test directional light
        directionallightUsdPathSegment = usdUtils.createUfePathSegment('/lights/directionalLight')
        directionallightPath = ufe.Path([mayaPathSegment, directionallightUsdPathSegment])
        directionallightItem = ufe.Hierarchy.createItem(directionallightPath)

        ufeDirectionalLight = ufe.Light.light(directionallightItem)
        usdDirectionalLight = usdUtils.getPrimFromSceneItem(directionallightItem)
        self._TestDirectionalLight(ufeDirectionalLight, usdDirectionalLight)

        # test area light
        arealightUsdPathSegment = usdUtils.createUfePathSegment('/lights/areaLight')
        arealightPath = ufe.Path([mayaPathSegment, arealightUsdPathSegment])
        arealightItem = ufe.Hierarchy.createItem(arealightPath)

        ufeAreaLight = ufe.Light.light(arealightItem)
        usdAreaLight = usdUtils.getPrimFromSceneItem(arealightItem)
        self._TestAreaLight(ufeAreaLight, usdAreaLight)

    @unittest.skipUnless(os.getenv('UFE_VOLUME_LIGHTS_SUPPORT', 'FALSE') == 'TRUE', 'UFE has volume light support.')
    def testUsdVolumeLights(self):
        shapeNode, _ = self._StartTest('SimpleLight')
        mayaPathSegment = mayaUtils.createUfePathSegment(shapeNode)
        # test cylinder light
        cylinderlightUsdPathSegment = usdUtils.createUfePathSegment('/lights/cylinderLight')
        cylinderlightPath = ufe.Path([mayaPathSegment, cylinderlightUsdPathSegment])
        cylinderlightItem = ufe.Hierarchy.createItem(cylinderlightPath)

        if (hasattr(ufe, "Light_v5_5")):
            ufeCylinderLight = ufe.Light_v5_5.light(cylinderlightItem)
        else:
            ufeCylinderLight = ufe.Light.light(cylinderlightItem)
        usdCylinderLight = usdUtils.getPrimFromSceneItem(cylinderlightItem)
        self._TestCylinderLight(ufeCylinderLight, usdCylinderLight)

        # test disk light
        disklightUsdPathSegment = usdUtils.createUfePathSegment('/lights/diskLight')
        disklightPath = ufe.Path([mayaPathSegment, disklightUsdPathSegment])
        disklightItem = ufe.Hierarchy.createItem(disklightPath)

        if (hasattr(ufe, "Light_v5_5")):
            ufeDiskLight = ufe.Light_v5_5.light(disklightItem)
        else:
            ufeDiskLight = ufe.Light.light(disklightItem)
        usdDiskLight = usdUtils.getPrimFromSceneItem(disklightItem)
        self._TestDiskLight(ufeDiskLight, usdDiskLight)

        # test dome light
        domelightUsdPathSegment = usdUtils.createUfePathSegment('/lights/domeLight')
        domelightPath = ufe.Path([mayaPathSegment, domelightUsdPathSegment])
        domelightItem = ufe.Hierarchy.createItem(domelightPath)

        if (hasattr(ufe, "Light_v5_5")):
            ufeDomeLight = ufe.Light_v5_5.light(domelightItem)
        else:
            ufeDomeLight = ufe.Light.light(domelightItem)
        usdDomeLight = usdUtils.getPrimFromSceneItem(domelightItem)
        self._TestDomeLight(ufeDomeLight, usdDomeLight)

    def testLoadingLight(self):
        '''
        Verify that the act of loading a stage with lights does not dirty the stage.
        '''
        shapeNode, stage = self._StartTest('SimpleLight')
        mayaPathSegment = mayaUtils.createUfePathSegment(shapeNode)

        # Verify the stage is not dirty
        def verifyClean():
            layer: Sdf.Layer = stage.GetRootLayer()
            self.assertFalse(layer.dirty)
        
        verifyClean()

        # Access the cylinder light shadow enable attribute.
        noAttrlightUsdPathSegment = usdUtils.createUfePathSegment('/lights/noAttrLight')
        noAttrlightPath = ufe.Path([mayaPathSegment, noAttrlightUsdPathSegment])
        noAttrlightItem = ufe.Hierarchy.createItem(noAttrlightPath)

        ufeLight = ufe.Light.light(noAttrlightItem)
        self.assertFalse(ufeLight.shadowEnable())

        verifyClean()


if __name__ == '__main__':
    unittest.main(verbosity=2)

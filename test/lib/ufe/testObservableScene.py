#!/usr/bin/env python

#
# Copyright 2020 Autodesk
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

from maya import standalone
from maya import cmds
import mayaUtils

from pxr import Usd
import ufe

from mayaUsd import lib as mayaUsdLib

import os
import sys
import unittest

class TestSceneItem(ufe.SceneItem):
    def __init__(self, path):
        super(TestSceneItem, self).__init__(path)
    
    def nodeType(self):
        return "TestSceneItem"

class TestObserver(ufe.Observer):
    def __init__(self):
        self.add = 0
        self.delete = 0
        self.pathChange = 0
        self.subtreeInvalidate = 0
        self.composite = 0
        super(TestObserver, self).__init__()

    def __call__(self, notification):
        if isinstance(notification, ufe.ObjectAdd):
            self.add += 1
        if isinstance(notification, ufe.ObjectDelete):
            self.delete += 1
        if isinstance(notification, ufe.ObjectPathChange):
            self.pathChange += 1
        if isinstance(notification, ufe.SubtreeInvalidate):
            self.subtreeInvalidate += 1
        if isinstance(notification, ufe.SceneCompositeNotification):
            self.composite += 1

    def notifications(self):
        return [self.add, self.delete, self.pathChange, self.subtreeInvalidate, self.composite]

class UFEObservableSceneTest(unittest.TestCase):
    
    pluginsLoaded = False
    
    @classmethod
    def setUpClass(cls):
        fixturesUtils.readOnlySetUpClass(__file__, loadPlugin=False)
        
        if not cls.pluginsLoaded:
            cls.pluginsLoaded = mayaUtils.isMayaUsdPluginLoaded()

    @classmethod
    def tearDownClass(cls):
        standalone.uninitialize()

    def checkNotifications(self, testObserver, listNotifications):
        self.assertTrue(testObserver.add == listNotifications[0])
        self.assertTrue(testObserver.delete == listNotifications[1])
        self.assertTrue(testObserver.pathChange == listNotifications[2])
        self.assertTrue(testObserver.subtreeInvalidate == listNotifications[3])
        self.assertTrue(testObserver.composite == listNotifications[4])

    def testObservableScene(self):
        # Setup
        ca = ufe.PathComponent("a")
        cb = ufe.PathComponent("b")
        cc = ufe.PathComponent("c")

        sa = ufe.PathSegment([ca], 1, '/')
        sab = ufe.PathSegment([ca, cb], 1, '|')
        sc = ufe.PathSegment([cc], 2, '/')

        a = ufe.Path(sa)
        b = ufe.Path(sab)
        c = ufe.Path([sab, sc])

        itemA = TestSceneItem(a)
        itemB = TestSceneItem(b)
        itemC = TestSceneItem(c)
        # End Setup

        # No observers from the test yet, but Maya could have observers
        # created on startup
        initialNbObservers = ufe.Scene.nbObservers()

        snObs = TestObserver()

        # Add observer to the scene.
        ufe.Scene.addObserver(snObs)

        # Order of expected notifications. No notifications yet.
        self.checkNotifications(snObs, [0,0,0,0,0,0])

        self.assertEqual(ufe.Scene.nbObservers() - initialNbObservers, 1)
        self.assertTrue(ufe.Scene.hasObserver(snObs))

        ufe.Scene.notify(ufe.ObjectAdd(itemA))

        # we should now have an ObjectAdd notification
        self.checkNotifications(snObs, [1,0,0,0,0,0])

        # HS 2020: can't pass ufe.scene to NotificationGuard??
        
        # Composite notifications with guard.
        # with ufe.NotificationGuard(ufe.Scene):
        #     ufe.Scene.notify(ufe.ObjectAdd(itemB))
        #     ufe.Scene.notify(ufe.ObjectAdd(itemC))

    def testInertPrimAddRemoveNotifications(self):
        
        cmds.file(new=True, force=True)
        
        usdFilePath = cmds.internalVar(utd=1) + '/testInertPrimAddRemove.usda'
        stage = Usd.Stage.CreateNew(usdFilePath)
        
        fooPath = '/foo'
        stage.DefinePrim(fooPath, 'Xform')
        
        # Save out the file, and bring it back into Maya under a proxy shape.
        stage.GetRootLayer().Save()
        proxyShape = cmds.createNode('mayaUsdProxyShape')
        cmds.setAttr('mayaUsdProxyShape1.filePath', usdFilePath, type='string')

        stage = mayaUsdLib.GetPrim(proxyShape).GetStage()

        # Work on the session layer, to easily clear changes.
        sessionLayer = stage.GetSessionLayer()        
        stage.SetEditTarget(sessionLayer)
        
        snObs = TestObserver()
        ufe.Scene.addObserver(snObs)
                
        # Deactivate the prim...expect object delete notif.
        fooPrim = stage.GetPrimAtPath(fooPath)
        fooPrim.SetActive(False)
        self.checkNotifications(snObs, [0,1,0,0,0,0])
        
        # Clear session layer, expect an object added notif, as the inactive
        # state was cleared from the session layer.
        sessionLayer.Clear()
        self.checkNotifications(snObs, [1,1,0,0,0,0])
        

if __name__ == '__main__':
    unittest.main(verbosity=2)

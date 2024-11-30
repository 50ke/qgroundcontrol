/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.FactSystem
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Vehicle

// 3D Viewer modules
import Viewer3D
import QtMultimedia
import org.freedesktop.gstreamer.Qt6GLVideoItem

Item {
    id: _root

    // These should only be used by MainRootWindow
    property var planController:    _planController
    property var guidedController:  _guidedController

    // Properties of UTM adapter
    property bool utmspSendActTrigger: false

    PlanMasterController {
        id:                     _planController
        flyView:                true
        Component.onCompleted:  start()
    }

    property bool   _mainWindowIsMap:       mapControl.pipState.state === mapControl.pipState.fullState
    property bool   _isFullWindowItemDark:  _mainWindowIsMap ? mapControl.isSatelliteMap : true
    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _missionController:     _planController.missionController
    property var    _geoFenceController:    _planController.geoFenceController
    property var    _rallyPointController:  _planController.rallyPointController
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property var    _guidedController:      guidedActionsController
    property var    _guidedActionList:      guidedActionList
    property var    _guidedValueSlider:     guidedValueSlider
    property var    _widgetLayer:           widgetLayer
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property rect   _centerViewport:        Qt.rect(0, 0, width, height)
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30
    property var    _mapControl:            mapControl

    property real   _fullItemZorder:    0
    property real   _pipItemZorder:     QGroundControl.zOrderWidgets

    function _calcCenterViewPort() {
        var newToolInset = Qt.rect(0, 0, width, height)
        toolstrip.adjustToolInset(newToolInset)
    }

    function dropMessageIndicatorTool() {
        toolbar.dropMessageIndicatorTool();
    }

    QGCToolInsets {
        id:                     _toolInsets
        leftEdgeBottomInset:    _pipView.leftEdgeBottomInset
        bottomEdgeLeftInset:    _pipView.bottomEdgeLeftInset
    }

    FlyViewToolBar {
        id:         toolbar
        visible:    !QGroundControl.videoManager.fullScreen
    }

    Item {
        id:                 mapHolder
        anchors.top:        toolbar.bottom
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.right:      parent.right

        FlyViewMap {
            id:                     mapControl
            planMasterController:   _planController
            rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
            pipView:                _pipView
            pipMode:                !_mainWindowIsMap
            toolInsets:             customOverlay.totalToolInsets
            mapName:                "FlightDisplayView"
            enabled:                !viewer3DWindow.isOpen
        }

        Rectangle{
            width: 150
            height: 200
            color: "#212529"
            opacity: 0.8
            radius: 2
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 300
            Column{
                anchors.fill: parent
                spacing: 2
                leftPadding: 10
                topPadding: 10
                Text {
                    id: currentOilQuantityId
                    color: "#FFFFFF"
                    text: qsTr("当前油量: 0L")
                }
                Text {
                    id: totalOilQuantityId
                    color: "#FFFFFF"
                    text: qsTr("全部油量: 0L")
                }
                Text {
                    id: remainingOilQuantityId
                    color: "#FFFFFF"
                    text: qsTr("剩余油量: 0L")
                }
                Text {
                    id: remainingMileageId
                    color: "#FFFFFF"
                    text: qsTr("剩余里程: 0Km")
                }
                Text {
                    id: drivingMileageId
                    color: "#FFFFFF"
                    text: qsTr("行驶里程: 0Km")
                }
                Text {
                    id: remainingElectricityId
                    color: "#FFFFFF"
                    text: qsTr("剩余电量: 0%")
                }
                Text {
                    id: batteryTemperatureId
                    color: "#FFFFFF"
                    text: qsTr("电池温度: 0°")
                }
                Item {
                    width: 10
                    height: 10
                }
                Row{
                    spacing: 5
                    RoundButton{
                        width: 32
                        height: 48
                        display: AbstractButton.TextUnderIcon
                        icon.source: "qrc:/qmlimages/vehicle-d.svg"
                        text: "D"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        onDoubleClicked: {
                            QGroundControl.mqttManager.changeGear(0)
                        }
                    }
                    RoundButton{
                        width: 32
                        height: 48
                        display: AbstractButton.TextUnderIcon
                        icon.source: "qrc:/qmlimages/vehicle-n.svg"
                        text: "N"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        onDoubleClicked: {
                            QGroundControl.mqttManager.changeGear(1)
                        }
                    }
                    RoundButton{
                        width: 32
                        height: 48
                        display: AbstractButton.TextUnderIcon
                        icon.source: "qrc:/qmlimages/vehicle-r.svg"
                        text: "R"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        onDoubleClicked: {
                            QGroundControl.mqttManager.changeGear(2)
                        }
                    }
                }
            }
        }

        Connections {
            target: QGroundControl.mqttManager
            function onUpdateMessage(data){
                currentOilQuantityId.text = "当前油量: %1L".arg(data["Oil"]["CurrentOilQuantity"])
                totalOilQuantityId.text = "全部油量: %1L".arg(data["Oil"]["TotalOilQuantity"])
                remainingOilQuantityId.text = "剩余油量: %1L".arg(data["Oil"]["RemainingOilQuantity"])
                remainingMileageId.text = "剩余里程: %1Km".arg(data["Oil"]["RemainingMileage"])
                drivingMileageId.text = "行驶里程: %1Km".arg(data["Oil"]["DrivingMileage"])
                remainingElectricityId.text = "剩余电量: %1%".arg(data["Electricity"]["RemainingElectricity"])
                batteryTemperatureId.text = "电池温度: %1°".arg(data["Electricity"]["BatteryTemperature"])
            }
        }

        // =====================雷达
        FlyViewLidar {
            id:         lidarControl
            pipView:  _lidarPipView
        }

        PipView {
            id:                     _lidarPipView
            anchors.right:           parent.right
            anchors.bottom:         parent.bottom
            anchors.margins:        _toolsMargin
            item1IsFullSettingsKey: "MainFlyWindowIsMap"
            item1:                  mapControl
            item2:                  QGroundControl.lidarManager.hasVideo ? lidarControl : null
            show:                   QGroundControl.lidarManager.hasVideo && !QGroundControl.lidarManager.fullScreen &&
                                        (lidarControl.pipState.state === lidarControl.pipState.pipState || mapControl.pipState.state === mapControl.pipState.pipState)
            z:                      QGroundControl.zOrderWidgets

            property real leftEdgeBottomInset: visible ? width + anchors.margins : 0
            property real bottomEdgeLeftInset: visible ? height + anchors.margins : 0
        }

        // ======================相机
        FlyViewVideo {
            id:         videoControl
            pipView:    _pipView
        }

        PipView {
            id:                     _pipView
            anchors.left:           parent.left
            anchors.bottom:         parent.bottom
            anchors.margins:        _toolsMargin
            item1IsFullSettingsKey: "MainFlyWindowIsMap"
            item1:                  mapControl
            item2:                  QGroundControl.videoManager.hasVideo ? videoControl : null
            show:                   QGroundControl.videoManager.hasVideo && !QGroundControl.videoManager.fullScreen &&
                                        (videoControl.pipState.state === videoControl.pipState.pipState || mapControl.pipState.state === mapControl.pipState.pipState)
            z:                      QGroundControl.zOrderWidgets

            property real leftEdgeBottomInset: visible ? width + anchors.margins : 0
            property real bottomEdgeLeftInset: visible ? height + anchors.margins : 0
        }

        FlyViewWidgetLayer {
            id:                     widgetLayer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            z:                      _fullItemZorder + 2 // we need to add one extra layer for map 3d viewer (normally was 1)
            parentToolInsets:       _toolInsets
            mapControl:             _mapControl
            visible:                !QGroundControl.videoManager.fullScreen
            utmspActTrigger:        utmspSendActTrigger
            isViewer3DOpen:         viewer3DWindow.isOpen
        }

        FlyViewCustomLayer {
            id:                 customOverlay
            anchors.fill:       widgetLayer
            z:                  _fullItemZorder + 2
            parentToolInsets:   widgetLayer.totalToolInsets
            mapControl:         _mapControl
            visible:            !QGroundControl.videoManager.fullScreen
        }

        // Development tool for visualizing the insets for a paticular layer, show if needed
        FlyViewInsetViewer {
            id:                     widgetLayerInsetViewer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            z:                      widgetLayer.z + 1
            insetsToView:           widgetLayer.totalToolInsets
            visible:                false
        }

        GuidedActionsController {
            id:                 guidedActionsController
            missionController:  _missionController
            actionList:         _guidedActionList
            guidedValueSlider:     _guidedValueSlider
        }

        GuidedActionList {
            id:                         guidedActionList
            anchors.margins:            _margins
            anchors.bottom:             parent.bottom
            anchors.horizontalCenter:   parent.horizontalCenter
            z:                          QGroundControl.zOrderTopMost
            guidedController:           _guidedController
        }

        //-- Guided value slider (e.g. altitude)
        GuidedValueSlider {
            id:                 guidedValueSlider
            anchors.margins:    _toolsMargin
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            z:                  QGroundControl.zOrderTopMost
            visible:            false
        }

        Viewer3D{
            id:                     viewer3DWindow
            anchors.fill:           parent
        }
    }
}

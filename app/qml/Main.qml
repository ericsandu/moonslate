import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Moonslate 1.0

ApplicationWindow {
    id: root
    width: 1000
    height: 600
    visible: true
    title: qsTr("Moonslate Live Translator")
    color: "#121212"

    AppController {
        id: controller
    }

    header: ToolBar {
        background: Rectangle { color: "#121212" }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12

            Label {
                text: "Moonslate \u2014 Live Translator"
                color: "#E0E0E0"
                font.pixelSize: 26
                font.bold: true
                Layout.fillWidth: true
            }

            ToolButton {
                text: "\u2699\uFE0F"
                font.pixelSize: 22
                flat: true
                onClicked: settingsMenu.popup()

                Menu {
                    id: settingsMenu

                    Menu {
                        title: "Language"

                        Instantiator {
                            model: controller.languageNames
                            delegate: MenuItem {
                                text: modelData
                                checkable: true
                                checked: controller.currentLanguageName === modelData
                                onTriggered: controller.selectLanguage(modelData)
                            }
                            onObjectAdded: (index, object) => parent.insertItem(index, object)
                            onObjectRemoved: (index, object) => parent.removeItem(object)
                        }
                    }

                    Menu {
                        title: "Moonshine Model"

                        Instantiator {
                            model: controller.modelSizes
                            delegate: MenuItem {
                                text: modelData
                                checkable: true
                                checked: controller.currentModelSize === modelData
                                onTriggered: controller.selectModelSize(modelData)
                            }
                            onObjectAdded: (index, object) => parent.insertItem(index, object)
                            onObjectRemoved: (index, object) => parent.removeItem(object)
                        }
                    }

                    MenuItem {
                        text: "Custom Vocabulary..."
                        onTriggered: vocabularyDialog.open()
                    }
                }
            }

            Button {
                id: recordButton
                enabled: controller.ready
                checkable: true
                checked: controller.recording
                onToggled: controller.recording = checked
                implicitHeight: 44
                implicitWidth: 200
                background: Rectangle {
                    radius: 8
                    color: recordButton.checked ? "#4CAF50" : "#F44336"
                }
                contentItem: Text {
                    text: controller.statusText
                    color: "white"
                    font.pixelSize: 15
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    width: recordButton.width
                }
                ToolTip.visible: hovered && (controller.statusText === "Download error" || controller.statusText === "Pipeline error")
                ToolTip.text: controller.statusText
                ToolTip.delay: 200
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        TranscriptPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "English (Original)"
            titleColor: "#9E9E9E"
            paneColor: "#1E1E1E"
            borderColor: "#333333"
            metaColor: "#666666"
        }

        TranscriptPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: controller.translationLabel
            titleColor: "#4CAF50"
            paneColor: "#1A2E1A"
            borderColor: "#2E5A2E"
            metaColor: "#4CAF50"
            isTranslation: true
        }
    }

    component TranscriptPane: Pane {
        id: pane
        property string title
        property color titleColor
        property color paneColor
        property color borderColor
        property color metaColor
        property bool isTranslation: false

        background: Rectangle {
            color: pane.paneColor
            border.color: pane.borderColor
            radius: 8
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 5

            Label {
                text: pane.title
                color: pane.titleColor
                font.pixelSize: 15
                font.bold: pane.isTranslation
            }

            ListView {
                id: logView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 12
                ScrollBar.vertical: ScrollBar {}

                model: ListModel {
                    id: logModel
                    Component.onCompleted: {
                        controller.transcriptReady.connect(function(original, translated, execTime) {
                            logModel.append({
                                line: pane.isTranslation ? translated : original,
                                meta: pane.isTranslation ? execTime : Qt.formatDateTime(new Date(), "hh:mm:ss")
                            });
                            logView.positionViewAtEnd();
                        });
                    }
                }

                delegate: ColumnLayout {
                    width: logView.width
                    spacing: 2

                    Text {
                        text: meta
                        color: pane.metaColor
                        font.pixelSize: 11
                    }
                    Rectangle {
                        color: pane.borderColor
                        height: 1
                        opacity: 0.6
                        Layout.fillWidth: true
                    }
                    Text {
                        text: line
                        color: "#F5F5F5"
                        font.pixelSize: 17
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    Dialog {
        id: vocabularyDialog
        title: "Custom Vocabulary"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 560)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: controller.keyterms = vocabularyText.text

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Comma-separated names and jargon to bias transcription towards.\n" +
                      "Moonshine nudges the decoder towards these terms with no retraining,\n" +
                      "so they come out spelled the way you write them here:"
                color: "#E0E0E0"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                TextArea {
                    id: vocabularyText
                    text: controller.keyterms
                    wrapMode: TextArea.Wrap
                    color: "#F5F5F5"
                    background: Rectangle { color: "#1E1E1E"; radius: 6; border.color: "#333333" }
                }
            }
        }
    }
}

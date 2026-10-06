import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui
import "Context.js" as Context

Item {
  id: root

  property var requester: null
  property string prompt: ""
  property var context: null
  property bool detailsExpanded: false
  readonly property bool asking: requester !== null
  readonly property string socketPath: (Quickshell.env("XDG_RUNTIME_DIR") || "/run/user/" + Quickshell.env("UID")) + "/omarchy-ssh-askpass.sock"

  function reply(socket, result) {
    if (!socket || !socket.connected) return
    socket.write(JSON.stringify(result) + "\n")
    socket.flush()
    socket.connected = false
  }

  function clearRequest() {
    requester = null
    prompt = ""
    context = null
    detailsExpanded = false
    passphrase.text = ""
    contentScroll.contentY = 0
    focusTimer.stop()
  }

  function finish(value) {
    if (!requester) return
    var socket = requester
    clearRequest()
    reply(socket, value === null ? {ok: false} : {ok: true, response: value})
  }

  function focusInput() {
    Qt.callLater(function() { if (root.asking) passphrase.forceActiveFocus() })
  }

  function toggleDetails() {
    detailsExpanded = !detailsExpanded
    focusInput()
  }

  function receive(socket, raw) {
    if (requester) {
      reply(socket, {ok: false, error: "busy"})
      return
    }
    if (raw.length > 32768 || Context.byteLength(raw) < 0 || Context.byteLength(raw) > 32768) {
      reply(socket, {ok: false, error: "invalid request"})
      return
    }
    var request
    try { request = JSON.parse(raw) } catch (e) {
      reply(socket, {ok: false, error: "invalid request"})
      return
    }
    if (!request || typeof request !== "object" || Array.isArray(request)
        || typeof request.prompt !== "string" || request.prompt.length > 1024) {
      reply(socket, {ok: false, error: "invalid prompt"})
      return
    }
    clearRequest()
    prompt = request.prompt
    context = Context.validate(request.context)
    requester = socket
    focusInput()
  }

  SocketServer {
    active: true
    path: root.socketPath
    handler: Socket {
      id: client
      parser: SplitParser {
        onRead: function(line) { root.receive(client, line) }
      }
      onConnectedChanged: {
        if (!connected && root.requester === client) root.clearRequest()
      }
    }
  }

  Timer {
    id: focusTimer
    interval: 150
    repeat: false
    onTriggered: if (root.asking) passphrase.forceActiveFocus()
  }

  PanelWindow {
    id: panel
    visible: root.asking
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"
    WlrLayershell.namespace: "omarchy-ssh-askpass"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    exclusionMode: ExclusionMode.Ignore
    onVisibleChanged: if (visible) focusTimer.restart()

    Rectangle {
      anchors.fill: parent
      color: Color.polkit.scrim
    }

    BorderSurface {
      id: card
      objectName: "askpassCard"
      width: Math.max(0, Math.min(Style.space(480), panel.width - Style.gapsOut * 2))
      height: Math.max(0, Math.min(contentColumn.implicitHeight + footer.implicitHeight
                                 + Style.space(12) + Style.spacing.panelPadding * 2,
                                 panel.height - Style.gapsOut * 2))
      anchors.centerIn: parent
      color: Color.polkit.background
      radius: Style.cornerRadius
      borderSpec: Border.surfaceSpec("polkit", "border", Color.polkit.border, Math.max(1, Style.space(2)), "border-alpha")
      padding: Style.spacing.panelPadding

      Item {
        anchors.fill: parent
        anchors.margins: Style.spacing.panelPadding

        Flickable {
          id: contentScroll
          objectName: "askpassScroll"
          anchors { top: parent.top; left: parent.left; right: parent.right; bottom: footer.top; bottomMargin: Style.space(12) }
          clip: true
          contentWidth: width
          contentHeight: contentColumn.implicitHeight
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.VerticalFlick

          Column {
            id: contentColumn
            width: contentScroll.width
            spacing: Style.space(10)

            Text {
              width: parent.width
              text: Context.heading(root.prompt, root.context)
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.text
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
              font.bold: true
            }

            Text {
              width: parent.width
              text: "Observed requester: " + Context.requester(root.context)
                    + "\nOperation: " + Context.operation(root.context, root.prompt)
                    + (root.context && root.context.destination
                       ? "\nSupplied destination or alias: " + root.context.destination : "")
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.text
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
            }

            Text {
              width: parent.width
              text: Context.explanation(root.prompt, root.context)
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.text
              opacity: 0.75
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
            }

            Text {
              objectName: "askpassPrompt"
              width: parent.width
              text: "Original SSH prompt:\n" + root.prompt
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.text
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
            }

            Text {
              id: detailsToggle
              objectName: "askpassDetailsToggle"
              width: parent.width
              text: root.detailsExpanded ? "▾ Hide process details" : "▸ Show process details"
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.accent
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
              font.underline: activeFocus
              activeFocusOnTab: true
              Keys.onReturnPressed: root.toggleDetails()
              Keys.onEnterPressed: root.toggleDetails()
              Keys.onSpacePressed: root.toggleDetails()
              Keys.onEscapePressed: root.finish(null)
              MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleDetails()
              }
            }

            Text {
              objectName: "askpassDetails"
              visible: root.detailsExpanded
              width: parent.width
              text: Context.details(root.context)
              textFormat: Text.PlainText
              wrapMode: Text.WrapAnywhere
              color: Color.polkit.text
              opacity: 0.75
              font.family: Style.font.menuFamily
              font.pixelSize: Style.font.bodySmall
            }
          }
        }

        // A visible thumb advertises overflow without another UI dependency.
        Rectangle {
          visible: contentScroll.contentHeight > contentScroll.height && contentScroll.height > 0
          width: Math.max(2, Style.space(2))
          height: Math.max(Style.space(12), contentScroll.height * contentScroll.height / Math.max(1, contentScroll.contentHeight))
          anchors.right: parent.right
          y: contentScroll.y + Math.max(0, Math.min(1, contentScroll.contentY / Math.max(1, contentScroll.contentHeight - contentScroll.height))) * (contentScroll.height - height)
          color: Color.polkit.accent
          opacity: 0.5
          radius: width / 2
        }

        Column {
          id: footer
          anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
          spacing: Style.space(6)

          TextInput {
            id: passphrase
            objectName: "askpassInput"
            width: parent.width
            height: Style.space(42)
            verticalAlignment: TextInput.AlignVCenter
            echoMode: TextInput.Password
            passwordCharacter: "\u2022"
            color: Color.polkit.text
            selectionColor: Util.alpha(Color.polkit.accent, 0.45)
            selectedTextColor: Color.polkit.text
            font.family: Style.font.menuFamily
            font.pixelSize: Style.font.iconLarge
            clip: true
            enabled: root.asking
            focus: root.asking
            activeFocusOnTab: true
            onAccepted: root.finish(text)
            Keys.onEscapePressed: root.finish(null)
          }

          Text {
            width: parent.width
            text: "Enter to continue  ·  Esc to cancel"
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Color.polkit.text
            opacity: 0.55
            font.family: Style.font.menuFamily
            font.pixelSize: Style.font.bodySmall
          }
        }
      }
    }
  }
}

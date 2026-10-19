import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import "Model.js" as Model

Panel {
  id: root
  moduleName: "omarchy.monitor"
  ipcTarget: "omarchy.monitor"
  manageIpc: false

  property string controlExecutable: "omarchy-monitor-control"
  property var displays: []
  property var pending: null
  property string backendError: ""
  property string commandError: ""
  property string focusedDisplay: ""
  property var expandedDisplays: ({})
  property var editors: ({})
  property var scaleOptions: ({})
  property real wheelAccumulator: 0
  readonly property int enabledCount: displays.filter(function(d) { return Model.isActive(d) }).length
  readonly property bool changing: actionProc.running || pending !== null
  readonly property string errorMessage: commandError || backendError
  readonly property var textSizeStops: [9, 10, 11, 12, 14, 16, 20]
  property int textSizePreview: -1

  function currentTextIndex() {
    var px = textSizePreview >= 0 ? textSizePreview : Style.font.baseSize
    var nearest = 0
    for (var i = 1; i < textSizeStops.length; i++)
      if (Math.abs(textSizeStops[i] - px) < Math.abs(textSizeStops[nearest] - px)) nearest = i
    return nearest
  }
  function setTextSize(px) {
    if (textSizeProc.running) return
    textSizePreview = px
    textSizeProc.command = ["omarchy-display-text-size", String(px)]
    textSizeProc.running = true
  }
  function isLaptop(name) { return /^(eDP|LVDS|DSI)-/.test(name) }
  function isExpanded(name) { return isLaptop(name) || expandedDisplays[name] === true }
  function toggleExpanded(name) {
    if (isLaptop(name)) return
    var next = Object.assign({}, expandedDisplays)
    next[name] = !isExpanded(name)
    expandedDisplays = next
  }
  function editor(name) {
    return Object.assign({ kind: "", side: "left", alignment: "center", reference: "" }, editors[name] || {})
  }
  function updateEditor(name, fields) {
    var next = Object.assign({}, editors)
    next[name] = Object.assign({}, editor(name), fields)
    editors = next
  }
  function toggleEditor(name, kind) {
    updateEditor(name, { kind: editor(name).kind === kind ? "" : kind })
  }
  function loadScaleOptions() {
    if (scaleProc.running) return
    var request = Model.nextScaleRequest(displays, scaleOptions)
    if (!request) return
    scaleProc.resolutionKey = request.key
    scaleProc.command = [controlExecutable, "scale-options", String(request.width), String(request.height)]
    scaleProc.running = true
  }
  function retryScaleOptions(key) {
    var next = Object.assign({}, scaleOptions)
    delete next[key]
    scaleOptions = next
    loadScaleOptions()
  }
  function refresh() {
    if (!stateProc.running && !actionProc.running) stateProc.running = true
  }
  function acceptState(state) {
    if (!state || !Array.isArray(state.monitors)) throw new Error("Risposta monitor non valida")
    focusedDisplay = ""
    for (var i = 0; i < state.monitors.length; i++)
      if (state.monitors[i].focused) focusedDisplay = state.monitors[i].name
    var next = Model.presentMonitors(state.monitors)
    // Keep inline selections and slider gestures intact between status polls.
    if (JSON.stringify(next) !== JSON.stringify(displays)) displays = next
    pending = state.pending || null
    backendError = state.error || ""
    loadScaleOptions()
  }
  function runAction(args) {
    if (actionProc.running) return
    commandError = ""
    actionProc.command = [controlExecutable].concat(args)
    actionProc.running = true
  }
  function applyChange(name, action, value, reference, alignment) {
    if (changing) return
    var args = ["apply", name, action]
    if (value !== undefined) args.push(String(value))
    if (reference !== undefined) args.push(String(reference))
    if (alignment !== undefined) args.push(String(alignment))
    runAction(args)
  }
  function setBrightness(name, value) {
    if (!name) return
    Quickshell.execDetached(["omarchy-brightness-display", "--no-osd", "--monitor", name, String(value)])
  }

  ShellIpc {
    target: "omarchy.monitor"
    function open() { root.open() }
    function close() { root.close() }
    function toggle() { root.toggle() }
    function show() { root.open() }
    function hide() { root.close() }
    function brightness(percent: string): string {
      root.setBrightness(root.focusedDisplay, percent + "%")
      return "ok"
    }
    function state(): string {
      // Keep the legacy diagnostic field without carrying relationships in UI state.
      var displays = root.displays.map(function(display) {
        return Object.assign({}, display, { relation: null })
      })
      return JSON.stringify({ displays: displays, pending: root.pending, error: root.errorMessage,
        expanded: root.displays.filter(function(d) { return root.isExpanded(d.name) }).map(function(d) { return d.name }) })
    }
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight
  Component.onCompleted: refresh()
  onOpenedChanged: if (opened) refresh()

  Process {
    id: stateProc
    command: [root.controlExecutable, "status"]
    stdout: StdioCollector { id: stateOutput; waitForEnd: true }
    stderr: StdioCollector { id: stateErrors; waitForEnd: true }
    onExited: function(exitCode, exitStatus) {
      if (exitCode !== 0) {
        root.backendError = String(stateErrors.text).trim() || "Impossibile leggere lo stato dei monitor."
        return
      }
      try { root.acceptState(JSON.parse(stateOutput.text)) }
      catch (error) { root.backendError = "Impossibile leggere lo stato dei monitor: " + error }
    }
  }
  Process {
    id: actionProc
    stdout: StdioCollector { id: actionOutput; waitForEnd: true }
    stderr: StdioCollector { id: actionErrors; waitForEnd: true }
    onExited: function(exitCode, exitStatus) {
      try {
        var result = JSON.parse(actionOutput.text)
        if (result.error) root.commandError = String(result.error)
        if (Array.isArray(result.monitors)) root.acceptState(result)
        else if (result.pending !== undefined) root.pending = result.pending
        if (result.pending && result.pending.phase === "confirming") root.close()
      } catch (error) {
        if (exitCode === 0) root.commandError = "Risposta non valida dal controllo monitor."
      }
      if (exitCode !== 0 && !root.commandError)
        root.commandError = String(actionErrors.text).trim() || "La modifica del monitor non è riuscita."
      root.refresh()
    }
  }
  Process {
    id: scaleProc
    property string resolutionKey: ""
    stdout: StdioCollector { id: scaleOutput; waitForEnd: true }
    stderr: StdioCollector { id: scaleErrors; waitForEnd: true }
    onExited: function(exitCode, exitStatus) {
      var entry = { values: [], error: "" }
      try {
        if (exitCode !== 0) throw new Error(String(scaleErrors.text).trim() || "Impossibile leggere le scale disponibili.")
        var values = JSON.parse(scaleOutput.text)
        if (!Array.isArray(values) || !values.length || values.some(function(value) {
          return typeof value !== "number" || !isFinite(value) || value <= 0
        })) throw new Error("Risposta non valida per le scale disponibili.")
        entry.values = values
      } catch (error) {
        entry.error = String(error.message || error)
      }
      // Cache failures too: polling must not repeatedly spawn a failing helper.
      var next = Object.assign({}, root.scaleOptions)
      next[resolutionKey] = entry
      root.scaleOptions = next
      Qt.callLater(root.loadScaleOptions)
    }
  }
  Timer {
    interval: 1000
    running: root.opened
    repeat: true
    onTriggered: root.refresh()
  }
  Process {
    id: textSizeProc
    stdout: StdioCollector { waitForEnd: true }
    onExited: function(exitCode, exitStatus) { root.textSizePreview = -1 }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: Quickshell.screens.length > 1 ? "󰍺" : "󰍹"
    onPressed: function(b) { root.toggle() }
    onWheelMoved: function(delta) {
      var wheel = Util.wheelSteps(root.wheelAccumulator, delta)
      root.wheelAccumulator = wheel.remainder
      if (wheel.steps !== 0) root.setBrightness(root.focusedDisplay, wheel.steps > 0 ? "+5%" : "5%-")
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(460))
    contentHeight: panel.fittedContentHeight(statusColumn.implicitHeight + panelColumn.implicitHeight + Style.space(14))

    Item {
      id: keyCatcher
      anchors.fill: parent
      focus: true
      Keys.onEscapePressed: root.close()

      Column {
        id: statusColumn
        anchors.top: parent.top
        width: parent.width
        spacing: Style.space(8)

        Text {
          textFormat: Text.PlainText
          text: "Monitor"
          color: root.bar.foreground
          font.family: root.bar.fontFamily
          font.pixelSize: Style.font.title
          font.bold: true
        }
        Text {
          textFormat: Text.PlainText
          visible: root.errorMessage !== ""
          width: parent.width
          text: root.errorMessage
          wrapMode: Text.WrapAnywhere
          color: root.bar.foreground
          font.family: root.bar.fontFamily
          font.pixelSize: Style.font.caption
        }
        Text {
          textFormat: Text.PlainText
          visible: actionProc.running && root.pending === null
          text: "Applicazione in corso…"
          color: root.bar.foreground
          font.family: root.bar.fontFamily
          font.pixelSize: Style.font.caption
        }
        PanelSeparator { width: parent.width; foreground: root.bar.foreground }
      }

      ScrollView {
        id: scrollArea
        anchors.top: statusColumn.bottom
        anchors.topMargin: Style.space(14)
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: panelColumn.implicitHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

        Column {
          id: panelColumn
          width: scrollArea.availableWidth
          spacing: Style.space(14)

          Column {
            width: parent.width
            spacing: Style.space(6)
            Item {
              width: parent.width
              implicitHeight: Math.max(textSizeHeader.implicitHeight, textSizeValue.implicitHeight)
              PanelSectionHeader {
                id: textSizeHeader
                text: "DIMENSIONE TESTO"
                foreground: root.bar.foreground
                fontFamily: root.bar.fontFamily
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
              }
              Text {
                textFormat: Text.PlainText
                id: textSizeValue
                text: (textSizeSlider.dragging ? root.textSizeStops[Math.round(textSizeSlider.liveValue)]
                  : (root.textSizePreview >= 0 ? root.textSizePreview : Style.font.baseSize)) + "px"
                color: Qt.darker(root.bar.foreground, 1.4)
                font.family: root.bar.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
              }
            }
            PanelSlider {
              id: textSizeSlider
              width: parent.width
              bar: root.bar
              minimum: 0
              maximum: root.textSizeStops.length - 1
              step: 1
              integer: true
              tickCount: root.textSizeStops.length
              value: root.currentTextIndex()
              onReleased: function(value) { root.setTextSize(root.textSizeStops[Math.round(value)]) }
            }
          }
          Repeater {
            model: root.displays
            MonitorCard {
              required property var modelData
              display: modelData
              width: panelColumn.width
            }
          }
        }
      }
    }
  }

  component ActionButton: Button {
    foreground: root.bar.foreground
    fontFamily: root.bar.fontFamily
    fontSize: Style.font.caption
    bordered: true
    focusable: true
    opacity: enabled ? 1 : 0.45
  }
  component SectionLabel: PanelSectionHeader {
    foreground: root.bar.foreground
    fontFamily: root.bar.fontFamily
  }
  component MonitorCard: Column {
    id: card
    required property var display
    readonly property bool laptop: root.isLaptop(display.name)
    readonly property bool expanded: root.isExpanded(display.name)
    readonly property bool active: Model.isActive(display)
    readonly property var editorState: root.editor(display.name)
    readonly property var references: root.displays.filter(function(d) { return d.name !== card.display.name && Model.isActive(d) })
    readonly property string referenceName: Model.referenceName(references, editorState.reference)
    readonly property var modes: Model.availableModes(display)
    readonly property var scaleChoices: root.scaleOptions[Model.scaleKey(display)]
    property int brightness: -1
    spacing: Style.space(10)

    function readBrightness() {
      if (laptop && expanded && active && !brightnessRead.running && !brightnessWrite.running)
        brightnessRead.running = true
    }
    Component.onCompleted: readBrightness()
    onExpandedChanged: readBrightness()
    Connections {
      target: root
      function onOpenedChanged() { if (root.opened) card.readBrightness() }
    }
    Process {
      id: brightnessRead
      command: ["omarchy-brightness-display", "--monitor", card.display.name]
      stdout: StdioCollector {
        waitForEnd: true
        onStreamFinished: {
          var value = Number(String(text).trim())
          card.brightness = String(text).trim() !== "" && isFinite(value) ? value : -1
        }
      }
    }
    Process {
      id: brightnessWrite
      stdout: StdioCollector { waitForEnd: true }
      onRunningChanged: if (!running) card.readBrightness()
    }

    PanelSeparator { width: parent.width; foreground: root.bar.foreground }
    Button {
      width: parent.width
      text: Model.monitorLabel(card.display)
      iconText: card.laptop ? "󰌢" : (card.expanded ? "󰅀" : "󰅂")
      tooltipText: card.laptop ? "Impostazioni dello schermo integrato" : "Mostra o nascondi le impostazioni"
      foreground: root.bar.foreground
      fontFamily: root.bar.fontFamily
      leftAlign: true
      focusable: true
      active: card.expanded
      onClicked: root.toggleExpanded(card.display.name)
    }
    Text {
      textFormat: Text.PlainText
      width: parent.width
      text: card.display.disabled ? "Disattivato" : card.active
        ? card.display.width + " × " + card.display.height + " · " + Math.round(card.display.refreshRate) + " Hz · " + Model.normalizeScale(card.display.scale) + "×"
        : "Nessuna modalità video attiva · scegli una risoluzione"
      wrapMode: Text.WordWrap
      color: Qt.darker(root.bar.foreground, 1.3)
      font.family: root.bar.fontFamily
      font.pixelSize: Style.font.caption
    }

    Column {
      visible: card.expanded
      width: parent.width
      spacing: Style.space(10)

      Flow {
        width: parent.width
        spacing: Style.spacing.xs
        enabled: !root.changing
        ActionButton {
          text: "Posizione"
          active: card.editorState.kind === "position"
          enabled: card.active && card.references.length > 0
          onClicked: root.toggleEditor(card.display.name, "position")
        }
        ActionButton {
          text: "Rotazione"
          active: card.editorState.kind === "rotation"
          enabled: card.active
          onClicked: root.toggleEditor(card.display.name, "rotation")
        }
        ActionButton {
          text: "Risoluzione"
          active: card.editorState.kind === "mode"
          enabled: !card.display.disabled && card.modes.length > 0
          onClicked: root.toggleEditor(card.display.name, "mode")
        }
        ActionButton {
          text: "Scala"
          active: card.editorState.kind === "scale"
          enabled: card.active
          onClicked: root.toggleEditor(card.display.name, "scale")
        }
        ActionButton {
          text: card.display.disabled ? "Attiva" : "Disattiva"
          enabled: card.display.disabled || !card.active || root.enabledCount > 1
          onClicked: root.applyChange(card.display.name, card.display.disabled ? "enable" : "disable")
        }
      }

      Column {
        visible: card.editorState.kind === "position"
        width: parent.width
        spacing: Style.space(8)
        enabled: !root.changing && card.active
        SectionLabel { text: "RISPETTO A" }
        Flow {
          width: parent.width
          spacing: Style.spacing.xs
          Repeater {
            model: card.expanded && card.editorState.kind === "position" ? card.references : []
            ActionButton {
              required property var modelData
              text: Model.monitorLabel(modelData)
              active: card.referenceName === modelData.name
              onClicked: root.updateEditor(card.display.name, { reference: modelData.name })
            }
          }
        }
        SectionLabel { text: "POSIZIONE" }
        Flow {
          width: parent.width
          spacing: Style.spacing.xs
          Repeater {
            model: card.expanded && card.editorState.kind === "position" ? [ { value: "left", label: "Sinistra" }, { value: "right", label: "Destra" },
              { value: "up", label: "Sopra" }, { value: "down", label: "Sotto" } ] : []
            ActionButton {
              required property var modelData
              text: modelData.label
              active: card.editorState.side === modelData.value
              onClicked: root.updateEditor(card.display.name, { side: modelData.value })
            }
          }
        }
        SectionLabel { text: "ALLINEAMENTO" }
        Flow {
          width: parent.width
          spacing: Style.spacing.xs
          Repeater {
            model: card.expanded && card.editorState.kind === "position" ? Model.alignmentOptions(card.editorState.side) : []
            ActionButton {
              required property var modelData
              text: modelData.label
              active: card.editorState.alignment === modelData.value
              onClicked: root.updateEditor(card.display.name, { alignment: modelData.value })
            }
          }
        }
        ActionButton {
          text: "Applica posizione"
          enabled: card.referenceName !== ""
          onClicked: root.applyChange(card.display.name, "position", card.editorState.side, card.referenceName, card.editorState.alignment)
        }
      }

      Column {
        visible: card.editorState.kind === "rotation"
        width: parent.width
        spacing: Style.space(8)
        enabled: !root.changing && card.active
        SectionLabel { text: "ROTAZIONE" }
        Flow {
          width: parent.width
          spacing: Style.spacing.xs
          Repeater {
            model: card.expanded && card.editorState.kind === "rotation" ? [0, 1, 2, 3] : []
            ActionButton {
              required property int modelData
              text: (modelData * 90) + "°"
              active: Number(card.display.transform) === modelData
              onClicked: root.applyChange(card.display.name, "rotation", modelData)
            }
          }
        }
      }

      Column {
        visible: card.editorState.kind === "mode"
        width: parent.width
        spacing: Style.space(6)
        enabled: !root.changing && !card.display.disabled
        SectionLabel { text: "RISOLUZIONE E FREQUENZA" }
        Repeater {
          model: card.expanded && card.editorState.kind === "mode" ? card.modes : []
          ActionButton {
            required property string modelData
            width: parent.width
            text: Model.modeLabel(modelData)
            active: Model.isCurrentMode(modelData, card.display)
            onClicked: root.applyChange(card.display.name, "mode", modelData.replace(/Hz$/, ""))
          }
        }
      }

      Column {
        visible: card.editorState.kind === "scale" && card.active
        width: parent.width
        spacing: Style.space(8)
        enabled: !root.changing && card.active
        SectionLabel { text: "SCALA · " + Model.normalizeScale(card.display.scale) + "×" }
        Flow {
          width: parent.width
          spacing: Style.spacing.xs
          Repeater {
            model: card.expanded && card.editorState.kind === "scale" && card.scaleChoices ? card.scaleChoices.values : []
            ActionButton {
              required property real modelData
              text: Model.normalizeScale(modelData) + "×"
              active: Model.isCurrentScale(modelData, card.display)
              onClicked: root.applyChange(card.display.name, "scale-preset", modelData)
            }
          }
        }
        Text {
          textFormat: Text.PlainText
          visible: !card.scaleChoices || card.scaleChoices.error !== ""
          width: parent.width
          text: card.scaleChoices ? card.scaleChoices.error : "Caricamento scale…"
          wrapMode: Text.WordWrap
          color: Qt.darker(root.bar.foreground, 1.3)
          font.family: root.bar.fontFamily
          font.pixelSize: Style.font.caption
        }
        ActionButton {
          visible: !!card.scaleChoices && card.scaleChoices.error !== ""
          text: "Riprova"
          onClicked: root.retryScaleOptions(Model.scaleKey(card.display))
        }
      }

      SectionLabel {
        visible: card.brightness >= 0 && card.active
        text: "LUMINOSITÀ · " + Math.round(card.brightness) + "%"
      }
      PanelSlider {
        visible: card.brightness >= 0 && card.active
        width: parent.width
        bar: root.bar
        minimum: 1
        maximum: 100
        integer: true
        step: 1
        value: card.brightness
        onReleased: function(value) {
          if (brightnessWrite.running) return
          card.brightness = Math.round(value)
          brightnessWrite.command = ["omarchy-brightness-display", "--no-osd", "--monitor", card.display.name, card.brightness + "%"]
          brightnessWrite.running = true
        }
      }
    }
  }
}

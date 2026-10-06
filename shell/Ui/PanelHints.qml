import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import "PanelHintModel.js" as HintModel

Item {
  id: hints
  required property var barHost
  property var panelOwner: null
  property var popup: null
  property var keyTarget: null
  property var keyReceiver: null
  property var originalForwarding: []
  property bool originalClip: false
  property var labels: HintModel.create()
  property var entries: []
  property string prefix: ""
  property string adjusting: ""
  property bool scanning: false
  property var heldKeys: ({})

  function childrenOf(item) {
    var result = []
    try { if (item.children) for (var i = 0; i < item.children.length; i++) result.push(item.children[i]) } catch (_) {}
    try { if (item.resources) for (var j = 0; j < item.resources.length; j++) result.push(item.resources[j]) } catch (_) {}
    return result
  }
  function findPopup(owner) {
    var queue = [owner], seen = []
    while (queue.length && seen.length < 600) {
      var item = queue.shift()
      if (!item || seen.indexOf(item) >= 0) continue
      seen.push(item)
      try {
        if (item.open === true && item.focusTarget && item.cardOrigin !== undefined) return item
        queue = queue.concat(childrenOf(item))
      } catch (_) {}
    }
    return null
  }
  function disposeEntry(entry) {
    try { if (entry.label && entry.padded) entry.label.rightPadding = entry.originalPadding } catch (_) {}
    try { if (entry.badge) entry.badge.destroy() } catch (_) {}
    try { if (entry.fittedRow) entry.fittedRow.scale = entry.originalScale } catch (_) {}
    try { if (entry.widenedItem) entry.widenedItem.width = entry.originalWidth } catch (_) {}
  }
  function detach() {
    try { if (keyTarget) { keyTarget.Keys.forwardTo = originalForwarding; keyTarget.clip = originalClip } } catch (_) {}
    for (var i = 0; i < entries.length; i++) disposeEntry(entries[i])
    if (keyReceiver) keyReceiver.destroy()
    entries = []
    keyReceiver = null
    keyTarget = null
    popup = null
    panelOwner = null
    prefix = ""
    adjusting = ""
    labels = HintModel.create()
    heldKeys = ({})
  }
  function attach(owner, panel) {
    detach()
    panelOwner = owner
    popup = panel
    keyTarget = panel.focusTarget
    originalClip = keyTarget.clip
    keyTarget.clip = true
    originalForwarding = Array.prototype.slice.call(keyTarget.Keys.forwardTo)
    keyReceiver = receiverComponent.createObject(keyTarget)
    keyTarget.Keys.forwardTo = [keyReceiver].concat(originalForwarding)
  }
  function inputBlocked() {
    try { return !popup || !popup.open || keyTarget.blocked === true || panelOwner.passwordSsid !== undefined && panelOwner.passwordSsid !== "" || entries.some(function(e) { return e.kind === "field" && e.item.activeFocus }) } catch (_) { return true }
  }
  function textItems(item, depth) {
    if (!item || depth <= 0) return []
    var result = []
    try {
      if (item.objectName === "omarchy-panel-hint") return []
      if (typeof item.text === "string" && item.font !== undefined && item.rightPadding !== undefined && item.cursorPosition === undefined && item.visible && item.text !== "") result.push(item)
      var children = item.children || []
      for (var i = 0; i < children.length; i++) result = result.concat(textItems(children[i], depth - 1))
    } catch (_) {}
    return result
  }
  function chooseLabel(item, kind) {
    var candidates = textItems(kind === "mouse" ? item.parent : item, 6)
    if (!candidates.length && (kind === "slider" || kind === "toggle")) candidates = textItems(item.parent, 5)
    if (kind === "slider" || kind === "toggle") {
      var ancestor = item.parent.parent
      for (var level = 0; ancestor && level < 3; level++, ancestor = ancestor.parent) candidates = candidates.concat(textItems(ancestor, 5))
    }
    var best = null, score = Infinity
    for (var i = 0; i < candidates.length; i++) {
      var label = candidates[i], text = String(label.text)
      var exact = item.text !== undefined && text === item.text || kind === "button" && text === item.iconText || kind === "mouse" && label === item.parent
      var iconOnly = /^[\s\uE000-\uF8FF\uD800-\uDFFF]+$/.test(text)
      if (!exact && (text.length < 2 || iconOnly || kind !== "tap" && /^[\d\s.%×]+(?:px)?$/.test(text))) continue
      if (kind === "button" && item.text === "" && text === item.iconText) exact = true
      if (iconOnly && !(kind === "button" && text === item.iconText || kind === "mouse" && label === item.parent)) continue
      var point = label.mapToItem(item, 0, label.height / 2)
      var rank = Math.abs(point.y - item.height / 2) * 5 - Math.min(text.length, 40)
      if (item.text !== undefined && text === item.text) rank -= 1000
      if (rank < score) { best = label; score = rank }
    }
    return best
  }
  function semanticRow(item) {
    var current = item.parent
    for (var i = 0; current && i < 6; i++, current = current.parent)
      if (current.net !== undefined || current.dev !== undefined || current.node !== undefined || current.display && current.display.name !== undefined) return current
    return null
  }
  function describe(item, kind) {
    var row = semanticRow(item), semantic = "", operation = kind
    if (row && row.net) {
      operation = kind === "mouse" && item.parent !== row ? "wifi-forget" : "wifi"
      semantic = String(row.net.ssid || "")
    } else if (row && row.dev) {
      operation = kind === "mouse" ? "bluetooth" : kind
      semantic = String(row.dev.address || "")
    } else if (row && row.display) {
      semantic = String(row.display.name)
    } else if (row && row.node) {
      semantic = String(row.node.id)
      if (kind === "mouse" && (item.parent === row || row.streamVolume !== undefined))
        operation = row.streamVolume !== undefined ? "stream-mute" : row.toString().indexOf("SourceRow") >= 0 ? "audio-source" : "audio-sink"
      else semantic += ":" + item.toString().replace(/0x[0-9a-f]+/g, "")
    }
    if (semantic && kind !== "mouse") semantic += ":" + String(item.text || item.tooltipText || kind)
    return { item: item, kind: kind, operation: operation, row: row,
      identity: HintModel.keyFor(operation, semantic, item.toString()), label: kind === "field" ? item : operation === "wifi-forget" ? textItems(item.parent, 2)[0] || null : chooseLabel(item, kind) }
  }
  function collect(item, result, seen) {
    if (!item || seen.indexOf(item) >= 0) return
    seen.push(item)
    try {
      if (item.objectName === "omarchy-panel-hint" || item.visible === false || item.enabled === false) return
      if (typeof item.tapped === "function" && item.gesturePolicy !== undefined && item.parent) {
        result.push(describe(item.parent, "tap"))
        return
      }
      if (item.width > 0 && item.height > 0) {
        if (item.cursorPosition !== undefined && item.echoMode !== undefined && typeof item.forceActiveFocus === "function") { result.push(describe(item, "field")); return }
        if (typeof item.released === "function" && item.minimum !== undefined && item.maximum !== undefined && item.liveValue !== undefined) { result.push(describe(item, "slider")); return }
        if (typeof item.toggled === "function" && item.checked !== undefined && item.interactive !== false && item.busy !== true) { result.push(describe(item, "toggle")); return }
        if (typeof item.clicked === "function" && item.cursorShape === undefined && item.fontFamily !== undefined) { result.push(describe(item, "button")); return }
        if (item.acceptedButtons !== undefined && item.containsMouse !== undefined && item.pressedButtons !== undefined && item.acceptedButtons !== Qt.NoButton && item.cursorShape === Qt.PointingHandCursor) { result.push(describe(item, "mouse")); return }
      }
      var children = childrenOf(item)
      for (var i = 0; i < children.length; i++) collect(children[i], result, seen)
    } catch (_) {}
  }
  function fitButton(target, row, original) {
    row.scale = Qt.binding(function() {
      var available = Math.max(0, target.width - target.leftPadding - target.rightPadding - target._reservedBorderLeft - target._reservedBorderRight)
      return Math.min(original, available / Math.max(1, row.width))
    })
  }
  function fitTap(row, original) {
    row.scale = Qt.binding(function() { return Math.min(original, Math.max(0, row.parent.width) / Math.max(1, row.width)) })
  }
  function scan() {
    if (scanning) return
    scanning = true
    try {
      var owner = barHost.activePopout, panel = owner ? findPopup(owner) : null
      if (!panel) { if (panelOwner) detach(); return }
      if (panelOwner !== owner || popup !== panel) attach(owner, panel)
      var found = []
      collect(keyTarget, found, [])
      var old = {}, next = [], seenIds = {}, usedLabels = []
      for (var i = 0; i < entries.length; i++) old[entries[i].identity] = entries[i]
      for (var j = 0; j < found.length; j++) {
        var entry = found[j]
        if (seenIds[entry.identity]) continue
        seenIds[entry.identity] = true
        if (entry.label && usedLabels.indexOf(entry.label) >= 0) entry.label = null
        if (entry.label) usedLabels.push(entry.label)
        var previous = old[entry.identity]
        if (previous && previous.item === entry.item && previous.label === entry.label) {
          entry = previous
          delete old[entry.identity]
        } else {
          if (previous) { disposeEntry(previous); delete old[entry.identity] }
          entry.code = HintModel.codeFor(labels, entry.identity)
          if (!entry.code) continue
          if (entry.operation === "wifi-forget" && entry.label) {
            entry.widenedItem = entry.item.parent
            entry.originalWidth = entry.widenedItem.width
            entry.widenedItem.width = entry.originalWidth + Style.space(30)
          }
          entry.originalPadding = entry.label ? entry.label.rightPadding : 0
          // Padding reserves natural button width and space in elided names.
          entry.padded = !!entry.label && (entry.originalPadding === 0 || entry.kind === "field")
          if (entry.padded) entry.label.rightPadding = entry.originalPadding + Style.space(30)
          if (entry.kind === "button" && entry.label && entry.label.parent === entry.item) {
            entry.widenedItem = entry.item
            entry.originalWidth = entry.item.width
            entry.item.width = entry.originalWidth + Style.space(30)
          }
          if (entry.kind === "button" && entry.label && entry.label.parent !== entry.item) {
            entry.fittedRow = entry.label.parent
            entry.originalScale = entry.fittedRow.scale
            fitButton(entry.item, entry.fittedRow, entry.originalScale)
          }
          if (entry.kind === "tap" && entry.label && entry.label.parent === entry.item && entry.item.toString().indexOf("Row") >= 0) {
            entry.fittedRow = entry.item
            entry.originalScale = entry.item.scale
            fitTap(entry.item, entry.originalScale)
          }
          entry.badge = badgeComponent.createObject(entry.label || entry.item, { code: entry.code, identity: entry.identity, padded: entry.padded, control: entry.item })
        }
        next.push(entry)
      }
      for (var identity in old) disposeEntry(old[identity])
      entries = next
      if (adjusting && !entries.some(function(e) { return e.identity === adjusting })) adjusting = ""
    } catch (error) { console.warn("panel hints:", error) }
    finally { scanning = false }
  }
  function reveal(item) {
    var current = item.parent
    for (var i = 0; current && i < 12; i++, current = current.parent) {
      try {
        if (current.contentItem && current.contentY !== undefined) {
          var point = item.mapToItem(current.contentItem, 0, 0), wanted = current.contentY
          if (point.y < wanted) wanted = point.y
          else if (point.y + item.height > wanted + current.height) wanted = point.y + item.height - current.height
          current.contentY = Math.max(0, Math.min(Math.max(0, current.contentHeight - current.height), wanted))
          return
        }
      } catch (_) {}
    }
  }
  function activate(code) {
    scan() // Resolve the current delegate immediately before acting.
    var entry = entries.filter(function(e) { return e.code === code })[0]
    prefix = ""
    if (!entry || inputBlocked()) return false
    try {
      if (!entry.item.visible || !entry.item.enabled) return false
      reveal(entry.item)
      if (entry.kind === "slider") { adjusting = entry.identity; return true }
      adjusting = ""
      if (entry.kind === "field") { heldKeys = ({}); entry.item.forceActiveFocus(); return true }
      if (entry.kind === "button") { entry.item.clicked(); return true }
      if (entry.kind === "toggle") { entry.item.toggled(); return true }
      var row = entry.row
      if (entry.operation === "wifi" || entry.operation === "wifi-forget") {
        var ssid = String(row.net.ssid), network = panelOwner.networkForSsid(ssid)
        if (!network) return false
        if (entry.operation === "wifi-forget") panelOwner.forget(network)
        else if (network.connected) panelOwner.disconnectRow(ssid)
        else if (row.requiresCredentials && !network.known) panelOwner.openPasswordPrompt(ssid)
        else panelOwner.connectDirectly(ssid)
        return true
      }
      if (entry.operation === "bluetooth") {
        var device = panelOwner.deviceFor(row)
        if (!device || String(device.address) !== String(row.dev.address)) return false
        if (device.connected) panelOwner.disconnectDevice(device)
        else panelOwner.connectDevice(device)
        return true
      }
      if (entry.operation === "audio-source") { panelOwner.setDefaultSource(row.node); return true }
      if (entry.operation === "audio-sink") { panelOwner.setDefaultSink(row.node); return true }
      if (entry.operation === "stream-mute" && row.node && row.node.audio) { row.node.audio.muted = !row.node.audio.muted; return true }
      // Static custom controls keep their mouse handler. No full screen
      // overlay or wl-kbptr process is involved.
      var p = entry.item.mapToItem(null, entry.item.width / 2, entry.item.height / 2)
      Quickshell.execDetached(["omarchy-panel-hint-click", String(Math.round(p.x + popup.screen.x)), String(Math.round(p.y + popup.screen.y)), entry.kind === "tap" && panelOwner.moduleName === "omarchy.clock" ? "2" : "1"])
      return true
    } catch (error) { console.warn("panel hint activation:", error); return false }
  }
  function adjust(delta, edge) {
    scan()
    var entry = entries.filter(function(e) { return e.identity === adjusting && e.kind === "slider" })[0]
    if (!entry) { adjusting = ""; return false }
    var slider = entry.item
    var next = edge === "start" ? slider.minimum : edge === "end" ? slider.maximum
      : Math.max(slider.minimum, Math.min(slider.maximum, slider.liveValue + delta * slider.step))
    if (slider.integer) next = Math.round(next)
    slider.liveValue = next
    slider.moved(next)
    slider.released(next)
    return true
  }
  function handleKey(event) {
    if (inputBlocked() || event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
    if (heldKeys[event.key] && !event.isAutoRepeat) { event.accepted = true; return }
    heldKeys[event.key] = true
    if (event.key === Qt.Key_Escape && (prefix || adjusting)) { prefix = ""; adjusting = ""; event.accepted = true; return }
    if (event.key === Qt.Key_Backspace && prefix) { prefix = prefix.slice(0, -1); event.accepted = true; return }
    if (adjusting) {
      if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) { event.accepted = adjust(-1, ""); return }
      if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) { event.accepted = adjust(1, ""); return }
      if (event.key === Qt.Key_Home) { event.accepted = adjust(0, "start"); return }
      if (event.key === Qt.Key_End) { event.accepted = adjust(0, "end"); return }
      if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { adjusting = ""; event.accepted = true; return }
    }
    var letter = String(event.text || "").toUpperCase()
    if (!/^[A-Z]$/.test(letter)) return
    event.accepted = true
    if (event.isAutoRepeat) return
    var candidate = prefix + letter
    if (candidate.length === 2) activate(candidate)
    else prefix = candidate
  }

  Connections { target: hints.barHost; function onActivePopoutChanged() { hints.scan() } }
  Timer { interval: 150; running: !!hints.barHost.activePopout; repeat: true; onTriggered: hints.scan() }
  Component.onDestruction: detach()
  Component {
    id: receiverComponent
    Item {
      objectName: "omarchy-panel-hint"
      Keys.onPressed: function(event) { hints.handleKey(event) }
      Keys.onReleased: function(event) { delete hints.heldKeys[event.key] }
    }
  }
  Component {
    id: badgeComponent
    Rectangle {
      objectName: "omarchy-panel-hint"
      required property string code
      required property string identity
      property bool padded: false
      required property var control
      readonly property bool highlighted: hints.adjusting === identity || hints.prefix !== "" && code.indexOf(hints.prefix) === 0
      visible: !hints.inputBlocked()
      z: 50
      clip: true
      width: Math.min(parent.width, label.implicitWidth + Style.space(6))
      height: label.implicitHeight + Style.space(2)
      x: padded ? Math.min(Math.max(0, parent.width - width), Math.max(0, parent.width - parent.rightPadding + Style.space(3))) : Math.max(0, parent.width - width)
      y: Math.max(0, (parent.height - height) / 2)
      radius: Style.space(2)
      color: highlighted ? Color.accent : Color.popups.background
      border.width: 1
      border.color: highlighted ? Color.accent : Qt.darker(Color.foreground, 1.8)
      Text {
        textFormat: Text.PlainText
        id: label
        anchors.centerIn: parent
        text: parent.code
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
        color: parent.highlighted ? Color.background : Color.foreground
      }
    }
  }
  ShellIpc {
    target: "omarchy.panel-shortcuts"
    function state(): string {
      hints.scan()
      return JSON.stringify({ panel: hints.panelOwner ? hints.panelOwner.moduleName || "" : "", prefix: hints.prefix,
        adjusting: hints.adjusting, shortcuts: hints.entries.map(function(e) { return { code: e.code, identity: e.identity, kind: e.kind, label: e.label ? e.label.text : "" } }) })
    }
  }
}

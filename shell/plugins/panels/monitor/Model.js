function clampBrightness(value) {
  var n = Number(value)
  if (!isFinite(n)) return 1
  return Math.max(1, Math.min(100, Math.round(n)))
}

function normalizeScale(scale) {
  var n = parseFloat(String(scale || ""))
  if (!isFinite(n)) return ""
  return String(Math.round(n * 100) / 100)
}

// The native controller owns scale policy. Cache its choices by physical mode;
// scale, rotation, connector changes and status polls do not invalidate them.
function scaleKey(display) {
  if (!isActive(display)) return ""
  return display.width + "x" + display.height
}

function nextScaleRequest(displays, cached) {
  for (var i = 0; i < displays.length; i++) {
    var display = displays[i]
    var key = scaleKey(display)
    if (key && !Object.prototype.hasOwnProperty.call(cached, key))
      return { key: key, width: display.width, height: display.height }
  }
  return null
}

function isCurrentScale(scale, display) {
  return Math.abs(Number(scale) - Number(display.scale)) < 0.000001
}

function brightnessName(percent) {
  var p = Math.round(percent)
  if (p >= 95) return "Sun blast"
  if (p >= 80) return "Solar flare"
  if (p >= 65) return "Golden hour"
  if (p >= 45) return "Even day"
  if (p >= 30) return "Soft glow"
  if (p >= 20) return "Lamp light"
  if (p >= 10) return "Candlelit"
  return "Night owl"
}

function parseDisplays(raw) {
  var displays = []
  try {
    displays = raw ? JSON.parse(String(raw)) : []
  } catch (e) {
    displays = []
  }
  if (!Array.isArray(displays)) displays = []

  var count = 0
  for (var i = 0; i < displays.length; i++) {
    if (displays[i] && displays[i].enabled) count++
  }

  return {
    displays: displays,
    enabledDisplayCount: count
  }
}

if (typeof module !== "undefined") {
  module.exports = {
    clampBrightness: clampBrightness,
    normalizeScale: normalizeScale,
    scaleKey: scaleKey,
    nextScaleRequest: nextScaleRequest,
    isCurrentScale: isCurrentScale,
    brightnessName: brightnessName,
    parseDisplays: parseDisplays
  }
}

function isActive(display) {
  return !!display && !display.disabled && Number(display.width) > 0 && Number(display.height) > 0
}

function monitorLabel(display) {
  var name = /^(eDP|LVDS|DSI)-/.test(display.name) ? "Portatile" : (display.model || display.description || "Monitor")
  return name + " · " + display.name
}

function presentMonitors(monitors) {
  return monitors.map(function(display) {
    return {
      name: display.name,
      model: display.model || "",
      description: display.description || "",
      width: display.width,
      height: display.height,
      refreshRate: display.refreshRate,
      scale: display.scale,
      transform: display.transform || 0,
      disabled: !!display.disabled,
      availableModes: display.availableModes || []
    }
  }).sort(function(a, b) {
    var aLaptop = /^(eDP|LVDS|DSI)-/.test(a.name)
    var bLaptop = /^(eDP|LVDS|DSI)-/.test(b.name)
    if (aLaptop !== bLaptop) return aLaptop ? -1 : 1
    return a.name.localeCompare(b.name)
  })
}

function referenceName(references, requested) {
  for (var i = 0; i < references.length; i++)
    if (references[i].name === requested) return requested
  return references.length ? references[0].name : ""
}

function alignmentOptions(side) {
  var horizontal = side === "left" || side === "right"
  return [
    { value: "start", label: horizontal ? "In alto" : "A sinistra" },
    { value: "center", label: "Al centro" },
    { value: "end", label: horizontal ? "In basso" : "A destra" }
  ]
}

function availableModes(display) {
  var modes = display.availableModes || []
  return modes.filter(function(mode, index) { return modes.indexOf(mode) === index })
}

function modeLabel(mode) {
  return String(mode).replace("x", " × ").replace("@", " · ").replace(/Hz$/, " Hz")
}

function isCurrentMode(mode, display) {
  var parts = /^(\d+)x(\d+)@([0-9.]+)(?:Hz)?$/.exec(String(mode))
  return !!parts && Number(parts[1]) === Number(display.width) && Number(parts[2]) === Number(display.height)
    && Math.abs(Number(parts[3]) - Number(display.refreshRate)) < 0.05
}

if (typeof module !== "undefined") {
  Object.assign(module.exports, {
    isActive: isActive,
    monitorLabel: monitorLabel,
    presentMonitors: presentMonitors,
    referenceName: referenceName,
    alignmentOptions: alignmentOptions,
    availableModes: availableModes,
    modeLabel: modeLabel,
    isCurrentMode: isCurrentMode
  })
}

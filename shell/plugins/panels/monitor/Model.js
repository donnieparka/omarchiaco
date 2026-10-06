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

function gcd(a, b) {
  while (b) {
    var remainder = a % b
    a = b
    b = remainder
  }
  return a
}

function cleanScale(scale, width, height) {
  var requested = Number(scale)
  var modeWidth = Number(width)
  var modeHeight = Number(height)
  if (!isFinite(requested) || !isFinite(modeWidth) || !isFinite(modeHeight)
      || requested <= 0 || modeWidth <= 0 || modeHeight <= 0) return ""

  var divisor = gcd(Math.round(modeWidth * 120), Math.round(modeHeight * 120))
  var scaleUnits = Math.round(requested * 120)
  if (scaleUnits > divisor) scaleUnits = divisor
  while (divisor % scaleUnits !== 0) scaleUnits++
  return normalizeScale(scaleUnits / 120)
}

function matchingScaleIndex(scales, currentScale, width, height) {
  var current = Number(currentScale)
  if (!Array.isArray(scales) || !isFinite(current)) return -1

  var bestIndex = -1
  var bestDistance = Infinity
  var normalizedCurrent = normalizeScale(current)
  for (var i = 0; i < scales.length; i++) {
    if (cleanScale(scales[i], width, height) !== normalizedCurrent) continue

    var distance = Math.abs(Number(scales[i]) - current)
    if (distance < bestDistance) {
      bestIndex = i
      bestDistance = distance
    }
  }
  return bestIndex
}

function availableScales(scales, width, height) {
  if (!Array.isArray(scales) || Number(width) <= 0 || Number(height) <= 0) return scales || []

  var byEffectiveScale = {}
  for (var i = 0; i < scales.length; i++) {
    var requested = Number(scales[i])
    var effective = Number(cleanScale(requested, width, height))

    if (!isFinite(requested) || !isFinite(effective)) continue

    var key = normalizeScale(effective)
    var existing = byEffectiveScale[key]
    if (!existing || Math.abs(requested - effective) < existing.distance) {
      byEffectiveScale[key] = {
        value: String(scales[i]),
        index: i,
        distance: Math.abs(requested - effective)
      }
    }
  }

  return Object.keys(byEffectiveScale)
    .map(function(key) { return byEffectiveScale[key] })
    .sort(function(a, b) { return a.index - b.index })
    .map(function(candidate) { return candidate.value })
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
    cleanScale: cleanScale,
    matchingScaleIndex: matchingScaleIndex,
    availableScales: availableScales,
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
      relation: display.relation || null,
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

// Reserve labels by semantic identity until the panel closes. A new device
// never inherits the label of an entry that disappeared.
function create() { return { next: 0, codes: {} } }
function codeFor(state, identity) {
  if (state.codes[identity]) return state.codes[identity]
  if (state.next >= 676) return ""
  var number = state.next++
  var code = String.fromCharCode(65 + Math.floor(number / 26)) + String.fromCharCode(65 + number % 26)
  state.codes[identity] = code
  return code
}
function keyFor(kind, semanticId, objectId) { return kind + ":" + (semanticId || objectId) }
if (typeof module !== "undefined") module.exports = { create: create, codeFor: codeFor, keyFor: keyFor }

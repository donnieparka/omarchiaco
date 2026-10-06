.pragma library

// Count UTF-8 bytes and reject unpaired UTF-16 surrogates before displaying metadata.
function byteLength(value) {
  try { return encodeURIComponent(value).replace(/%[0-9A-F]{2}/g, "x").length }
  catch (error) { return -1 }
}

function metadataString(value) {
  if (typeof value !== "string" || byteLength(value) < 0 || byteLength(value) > 1024)
    return null
  return value.replace(/[\u0000-\u001f\u007f-\u009f]/g, " ")
}

function validate(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)
      || !Array.isArray(value.processes) || value.processes.length > 16)
    return null

  var result = {processes: []}
  for (var i = 0; i < value.processes.length; ++i) {
    var process = value.processes[i]
    if (!process || typeof process !== "object" || Array.isArray(process)
        || typeof process.pid !== "number" || process.pid % 1 !== 0
        || process.pid <= 0 || process.pid > 2147483647)
      return null
    var executable = metadataString(process.executable)
    if (executable === null || executable.trim().length === 0) return null
    result.processes.push({pid: process.pid, executable: executable})
  }
  var optional = ["cwd", "destination", "operation"]
  for (var j = 0; j < optional.length; ++j) {
    var key = optional[j]
    if (value[key] === undefined) continue
    var text = metadataString(value[key])
    if (text === null || text.trim().length === 0) return null
    result[key] = text
  }
  if (result.operation !== undefined
      && ["clone", "fetch", "pull", "push", "ls-remote"].indexOf(result.operation) < 0)
    return null
  return result
}

function basename(executable) {
  var slash = executable.lastIndexOf("/")
  return executable.slice(slash + 1)
}

function requester(context) {
  if (!context || context.processes.length === 0) return "Unknown"
  var names = [basename(context.processes[0].executable)]
  // Show only identities actually present in the chain. Generic runtimes and
  // shells remain available in details, without being named as desktop apps.
  var generic = ["sh", "bash", "zsh", "fish", "dash", "env", "node", "nodejs",
                 "python", "python3", "ruby", "perl", "systemd", "init", "timeout"]
  for (var i = 1; i < context.processes.length && names.length < 3; ++i) {
    var name = basename(context.processes[i].executable)
    if (generic.indexOf(name) < 0 && names.indexOf(name) < 0) names.push(name)
  }
  return names.join(" ← ")
}

function kind(prompt) {
  if (/^Enter passphrase(?: for [^\r\n]+)?:[ \t]*$/i.test(prompt)) return "key"
  if (/(?:^password:|(?:'s|’s) password:)[ \t]*$/i.test(prompt)) return "password"
  return "unknown"
}

function heading(prompt, context) {
  var requestKind = kind(prompt)
  var caller = context && context.processes.length
      ? basename(context.processes[0].executable) : ""
  if (requestKind === "key" && caller === "ssh-add") return "Unlock a key for the SSH agent"
  if (requestKind === "key") return "Unlock a local SSH key"
  if (requestKind === "password") return "Enter the account password"
  return "SSH authentication"
}

function explanation(prompt, context) {
  if (kind(prompt) === "key")
    return "This passphrase unlocks a private key on this machine."
  if (kind(prompt) === "password")
    return "SSH is asking for the account password described below."
  return "Request purpose: Unknown. Read the original prompt below."
}

function operation(context, prompt) {
  if (context && context.operation) return "Git " + context.operation
  if (context && context.processes.length
      && basename(context.processes[0].executable) === "ssh-add"
      && kind(prompt) === "key")
    return "Unlock or add a key to the SSH agent"
  return "Unknown"
}

function details(context) {
  var lines = ["Observed process chain, caller first:"]
  if (!context || context.processes.length === 0) lines.push("Unknown")
  else for (var i = 0; i < context.processes.length; ++i) {
    var process = context.processes[i]
    lines.push(process.executable + "  ·  PID " + process.pid)
  }
  lines.push("", "Caller working directory:", context && context.cwd ? context.cwd : "Unknown")
  return lines.join("\n")
}

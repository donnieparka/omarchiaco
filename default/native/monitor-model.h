#ifndef MONITOR_MODEL_H
#define MONITOR_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/* Inputs are borrowed and never modified; results are owned, NULL on error.
 * State v1:
 * {version:1, outputs:{"model:<model>":{id,name,model,description,width,height,
 *   refreshRate,scale,transform,x,y,disabled,mode,desiredDisabled?,modeFallback?}},
 *  relations:[], connected:[active_id,...], fallback:false}
 * Models must be nonempty and unique among connected outputs. A model retains
 * its preferences across connector, description and serial changes. Duplicate
 * models are rejected rather than assigned connector-dependent identities.
 * A matching model profile wins over legacy desc:/name: records. Legacy records
 * migrate only by exact saved description on reconnect; disconnected records
 * remain pending in state, with no attempt to infer a model from their names.
 * Initial inactive observations are not saved as preferences. Unknown models
 * try an advertised preferred mode at scale 1, transform 0, enabled, with an
 * initial collision-free position. Previously saved disabled=true without
 * desiredDisabled remains off while live is off, until explicit enable; it
 * cannot cancel an already active output. Explicit desiredDisabled is honored.
 * Plans are proposals: persist state only after the controller confirms them.
 * modeFallback marks an unavailable desired mode. State retains desired mode
 * geometry while plans use a supported mode and normalized scale. Recovery or
 * explicit mode selection clears the marker; explicit scale changes persist.
 * scale normalizes custom values; scale-preset rejects any normalization of the
 * requested value against the current mode, including stale UI preset choices.
 * Each output owns absolute coordinates. Position actions calculate only the
 * target's coordinates once from the live reference. Explicit scale/mode/rotation
 * changes import healthy live geometry, keep an active embedded output fixed
 * (otherwise the output nearest the origin), and recalculate other coordinates
 * using old neighbour sides, gaps and alignments and new logical sizes. This
 * temporary placement tree is not saved. Legacy relations/fallback are cleared;
 * disconnect and disable never rearrange other outputs. Overlapping layouts
 * are rejected, not silently repositioned.
 * monitor_reconcile(false) imports healthy live position/mode/scale/rotation.
 * Inactive observations cannot replace saved geometry. Reconcile(true) restores
 * saved geometry. Width/height are physical dimensions, before transform/scale.
 */
/* Owned numeric array of exact whole-pixel presets, ascending, without rounding:
 * 1, 1.25, 1.5, 1.6, 1.75, 2, 2.5, 3, 3.5, 4. Pure; no live/state inputs.
 * Physical width/height must each be in 1..65536; NULL with an error otherwise.
 */
json_object *monitor_scale_options(int width, int height, char *error, size_t error_size);
json_object *monitor_initial_state(json_object *live);
json_object *monitor_plan(json_object *live, json_object *state,
                          const char *name, const char *action,
                          const char *value, const char *reference,
                          const char *alignment, char *error, size_t error_size);
json_object *monitor_reconcile(json_object *live, json_object *state,
                               bool topology_changed, char *error,
                               size_t error_size);
/* Pass pre-plan state to identify ownership of old managed rules. Emit only
 * currently connected monitors, routed by their current connector. Disconnected,
 * stale and ambiguously owned managed rules are dropped. Preserve unknown rule
 * properties only for an identified owner, and preserve surrounding user Lua.
 */
char *monitor_configuration(const char *original, json_object *planned_monitors,
                            json_object *state, char *error, size_t error_size);
#endif

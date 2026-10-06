#ifndef MONITOR_MODEL_H
#define MONITOR_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/* Inputs are borrowed and never modified; results are owned, NULL on error.
 * State v1:
 * {version:1, outputs:{id:{id,name,description,width,height,refreshRate,
 *   scale,transform,x,y,disabled,mode}},
 *  relations:[{monitor:id,reference:id,direction:"left|right|up|down",
 *              alignment:"start|center|end"}], connected:[active_id,...],
 *  fallback:bool}
 * IDs are "desc:" + unique description, or "name:" + connector when ambiguous.
 * outputs contains durable desired geometry, including disconnected outputs.
 * Temporary compact layouts do not overwrite these saved coordinates.
 * fallback tracks a recent removal; reconnecting or arranging manually restores
 * the saved layout, regardless of unrelated monitors in historical outputs.
 * A plan is {monitors:[Hyprland-compatible objects, plus id and mode],state:...}.
 * width/height are physical mode dimensions; transform is applied before scale.
 * monitor_reconcile(false) imports live mode/scale/rotation, then resolves edges.
 * monitor_reconcile(true) restores saved geometry and handles connection changes.
 */
json_object *monitor_initial_state(json_object *live);
json_object *monitor_plan(json_object *live, json_object *state,
                          const char *name, const char *action,
                          const char *value, const char *reference,
                          const char *alignment, char *error, size_t error_size);
json_object *monitor_reconcile(json_object *live, json_object *state,
                               bool topology_changed, char *error,
                               size_t error_size);
char *monitor_configuration(const char *original, json_object *planned_monitors,
                            char *error, size_t error_size);
#endif

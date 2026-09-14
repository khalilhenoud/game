/**
 * @file test_level.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2026-08-06
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <game/debug/flags.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/logic/player.h>
#include <game/rendering/render_data2.h>
#include <entity/level/level.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/scene/scene.h>
#include <level/sublevel_asset.h>
#include <library/asset/asset_ref.h>
#include <library/containers/chashmap.h>
#include <library/framerate_controller/framerate_controller.h>
#include <props/camera.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>
#include <spatial/bvh/bvh.h>

#define TILDE   0xC0
#define KEY_EXIT_LEVEL           '0'


static framerate_controller_t *controller;
static uint32_t exit_level = 0;
static int32_t disable_input;
static pipeline_t pipeline;
static camera_t camera;
static packaged_sublevel_render_data_t *render_data;
static font_runtime_t* font;
static uint32_t font_image_id;
static bvh_t *bvh;

// NOTE: usage example
// extract_folder(&sublevel_ref.path, &asset_folder);
// TODO: move this to the string library
static
uint32_t
count_occurrence(const char *str, char delim)
{
  assert(str);

  {
    uint32_t count = 0;
    const char *ptr = str - 1;
    do {
      ptr = strchr(++ptr, delim);
    } while (ptr != NULL && ++count);

    return count;
  }
}

static
ptrdiff_t
find_occurrence_at_hit(const char *str, char delim, uint32_t hits)
{
  assert(str);

  {
    uint32_t count = 0;
    const char *ptr = str - 1;
    do {
      ptr = strchr(++ptr, delim);
    } while (ptr != NULL && ++count < hits);

    return ptr - str;
  }
}

// TODO: Unused, consider moving to the string library,
// return the folder where all other assets relative to this exist.
static
void
extract_folder(const cstring_t *source, cstring_t *target)
{
  assert(strlen(source->str) < 512);

  {
    char delim = '\\';
    uint32_t count = count_occurrence(source->str, delim);
    uint32_t pos = find_occurrence_at_hit(source->str, delim, count - 1);
    char str[512] = {};
    memcpy(str, source->str, pos);
    cstring_setup2(target, str);
  }
}

static sublevel_asset_t *sublevel;
static asset_ref_t sublevel_ref;
static chashmap_t ref_assets_map;
static chashmap_t status_map;
const static uint32_t asset_map_reservation = 256;

static
void
load_recursive(
  chashmap_t *ref_assets_map,
  const asset_ref_t *asset_ref)
{
  assert(!chashmap_is_def(ref_assets_map));

  uint32_t index;
  chashmap_contains(ref_assets_map, *asset_ref, asset_ref_t, index);
  if (index != CHASHTABLE_INVALID_INDEX)
    return;

  vtable_t *vtable = get_vtable(asset_ref->type_id);
  loader_t loader = vtable->fn_get_loader();
  void *data = NULL;
  loader(&data, asset_ref, &g_default_allocator);
  uint32_t count = vtable->fn_type_asset_count(data);

  chashmap_insert(
    ref_assets_map, *asset_ref, asset_ref_t, data, void*);

  if (!count)
    return;

  cvector_t asset_refs;
  cvector_setup2(&asset_refs, asset_ref_t*);
  cvector_resize(&asset_refs, count);
  vtable->fn_type_get_assets(data, asset_refs.data);

  for (uint32_t i = 0; i < asset_refs.size; ++i) {
    asset_ref_t **ref = cvector_as(&asset_refs, i, asset_ref_t*);
    load_recursive(ref_assets_map, *ref);
  }
}

static
void
unload_assets(chashmap_t *ref_assets_map)
{
  asset_ref_t *asset_ref = NULL;
  void **data = NULL;

  for (
    chashmap_iterator_t iter = chashmap_begin(ref_assets_map);
    !chashmap_iter_equal(iter, chashmap_end(ref_assets_map));
    chashmap_advance(&iter)) {
      asset_ref = chashmap_key(&iter, asset_ref_t);
      data = chashmap_value(&iter, void *);
      vtable_t *vtable = get_vtable(asset_ref->type_id);
      deloader_t deloader = vtable->fn_get_deloader();
      deloader(data, asset_ref, &g_default_allocator);
    }
  chashmap_cleanup(ref_assets_map, NULL);
}

static
void
vec3f_set_all(vector3f *vec, float val)
{
  vec->data[0] = vec->data[1] = vec->data[2] = val;
}

static
void
setup_default_camera( matrix4f *transform)
{
  vec3f_set_all(&camera.position, 0.f);
  mult_set_m4f_p3f(transform, &camera.position);

  camera.lookat_direction.data[0] =
  camera.lookat_direction.data[1] = 0.f;
  camera.lookat_direction.data[2] = -1.f;
  camera.up_vector.data[0] =
  camera.up_vector.data[2] = 0.f;
  camera.up_vector.data[1] = 1.f;
}

static
void
setup_default_light(sublevel_asset_t *sublevel)
{
  if (sublevel->lights.size)
    return;

  light_t *light = NULL;
  cvector_resize(&sublevel->lights, 1);
  light = cvector_as(&sublevel->lights, 0, light_t);
  cstring_setup(&light->name, "test", &g_default_allocator);
  vector3f_set_3f(&light->position, 0.f, 200.f, 0.f);
  vector3f_set_3f(&light->direction, -1.f, -1.f, -1.f);
  normalize_set_v3f(&light->direction);
  vector3f_set_3f(&light->up, 0.f, 0.f, 1.f);
  light->diffuse.data[0] = light->diffuse.data[1] = light->diffuse.data[2] =
  light->diffuse.data[3] = 1.f;
  light->specular = light->diffuse;
  light->ambient = light->diffuse;
  light->type = LIGHT_TYPE_DIRECTIONAL;
}

static
void
load_level(
  const level_context_t context,
  const allocator_t *allocator)
{
  cstring_setup2(&sublevel_ref.path, "F:\\data\\level1\\sublevels\\e1m1.bin");
  sublevel_ref.type_id = get_type_id(sublevel_asset_t);

  chashmap_def(&ref_assets_map);
  chashmap_setup2(&ref_assets_map, asset_ref_t, void *);
  chashmap_reserve(&ref_assets_map, asset_map_reservation);
  load_recursive(&ref_assets_map, &sublevel_ref);

  chashmap_def(&status_map);
  chashmap_setup2(&status_map, asset_ref_t, uint32_t);
  chashmap_reserve(&status_map, asset_map_reservation);

  void **data = NULL;
  chashmap_at(&ref_assets_map, sublevel_ref, asset_ref_t, void *, data);
  sublevel = *(sublevel_asset_t **)data;

  setup_default_camera(&sublevel->transform);
  setup_default_light(sublevel);
  bvh = &sublevel->bvh;
  render_data = prep_render_data(sublevel, &ref_assets_map, &status_map);

  // font = cvector_as(&render_data->font_data.fonts, 0, font_runtime_t);
  // font_image_id = *cvector_as(&render_data->font_data.texture_ids, 0, uint32_t);

  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);

  player_init(
    sublevel->metadata.player_start,
    sublevel->metadata.player_angle,
    &camera,
    bvh);

  controller = controller_allocate(allocator, 60, 1u);
  exit_level = 0;
  disable_input = 0;
}

static
void
update_level(const allocator_t* allocator)
{
  uint64_t fps = (uint64_t)controller_end(controller);
  float dt = (float)controller_start(controller);

  input_update();
  clear_color_and_depth_buffers();
  // render_packaged_scene_data(render_data, &pipeline, camera);

  if (is_key_triggered(TILDE)) {
    disable_input = !disable_input;
    show_mouse_cursor((int32_t)disable_input);
  }

  if (!disable_input) {
    // update_debug_flags();
    // player_update(dt);
    // draw_debug_text_frame(&pipeline, font, font_image_id);
    // draw_debug_face_frame(&pipeline, g_debug_flags.disable_depth_debug);
  } else if (is_key_triggered(KEY_EXIT_LEVEL))
    exit_level = 1;

  // render_basic_controls(font, font_image_id, &pipeline, dt, fps, disable_input);
  flush_operations();
}

static
void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  cleanup_sublevel_render_data(render_data, &status_map);
  unload_assets(&ref_assets_map);
}

static
uint32_t
should_unload(void)
{
  return exit_level;
}

void
construct_test_level(level_t* level)
{
  assert(level);

  level->load = load_level;
  level->update = update_level;
  level->unload = unload_level;
  level->should_unload = should_unload;
}
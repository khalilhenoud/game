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
#include <game/levels/test_mesh_level.h>
#include <game/debug/flags.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/logic/camera.h>
#include <game/rendering/render_data2.h>
#include <entity/level/level.h>
#include <font/font_asset.h>
#include <library/asset/asset_ref.h>
#include <library/containers/chashmap.h>
#include <library/framerate_controller/framerate_controller.h>
#include <mesh/mesh_asset.h>
#include <mesh/skinned_mesh_asset.h>
#include <props/camera.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>
#include <skeleton/skeleton_asset.h>

#define TILDE                     0xC0
#define KEY_EXIT_LEVEL            '0'
#define KEY_STRAFE_LEFT           'A'
#define KEY_STRAFE_RIGHT          'D'
#define KEY_MOVE_FWD              'W'
#define KEY_MOVE_BACK             'S'
#define KEY_MOVE_UP               'Q'
#define KEY_MOVE_DOWN             'E'
#define REFERENCE_FRAME_TIME      0.033f


static framerate_controller_t *controller;
static uint32_t exit_level = 0;
static int32_t disable_input;
static pipeline_t pipeline;
static camera_t camera;
static vector3f velocity;
static const float velocity_limit = 10.f;
static const float acceleration = 5.f;
static const float friction = 2.f;

static asset_ref_t font_ref;
static font_asset_t *font;
static packaged_font_render_data_t *font_render_data;
static asset_ref_t mesh_ref;
static mesh_asset_t *mesh;
static packaged_mesh_render_data_t *mesh_render_data;
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
setup_default_camera()
{
  vector3f_set_3f(&camera.position, 0.f, 0, 100);

  camera.lookat_direction.data[0] =
  camera.lookat_direction.data[1] = 0.f;
  camera.lookat_direction.data[2] = -1.f;
  camera.up_vector.data[0] =
  camera.up_vector.data[2] = 0.f;
  camera.up_vector.data[1] = 1.f;
}

static
void
load_level(
  const level_context_t context,
  const allocator_t *allocator)
{
  cstring_setup2(&font_ref.path, "F:\\data\\shared\\fonts\\Malgun Gothic.bin");
  font_ref.type_id = get_type_id(font_asset_t);

  cstring_setup2(&mesh_ref.path, "F:\\data\\shared\\meshes\\Suzanne.bin");
  mesh_ref.type_id = get_type_id(mesh_asset_t);

  chashmap_def(&ref_assets_map);
  chashmap_setup2(&ref_assets_map, asset_ref_t, void *);
  chashmap_reserve(&ref_assets_map, asset_map_reservation);
  load_recursive(&ref_assets_map, &font_ref);
  load_recursive(&ref_assets_map, &mesh_ref);

  chashmap_def(&status_map);
  chashmap_setup2(&status_map, asset_ref_t, uint32_t);
  chashmap_reserve(&status_map, asset_map_reservation);

  void **data = NULL;
  chashmap_at(&ref_assets_map, font_ref, asset_ref_t, void *, data);
  font = *(font_asset_t **)data;

  chashmap_at(&ref_assets_map, mesh_ref, asset_ref_t, void *, data);
  mesh = *(mesh_asset_t **)data;

  setup_default_camera();
  font_render_data = prep_font_render_data(font, &ref_assets_map, &status_map);
  mesh_render_data = prep_mesh_render_data(mesh, &ref_assets_map, &status_map);

  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);

  controller = controller_allocate(allocator, 60, 1u);
  exit_level = 0;
  disable_input = 0;
  velocity.data[0] = velocity.data[1] = velocity.data[2] = 0.f;
}

inline
void
clamp(float *f, float min, float max)
{
  *f = (*f < min) ? min : *f;
  *f = (*f > max) ? max : *f;
}

static
void
update_velocity(float delta_time)
{
  float multiplier = delta_time / REFERENCE_FRAME_TIME;
  float l_friction = friction * multiplier;

  if (is_key_pressed(KEY_STRAFE_LEFT))
    velocity.data[0] -= acceleration * multiplier;

  if (is_key_pressed(KEY_STRAFE_RIGHT))
    velocity.data[0] += acceleration * multiplier;

  if (is_key_pressed(KEY_MOVE_FWD))
    velocity.data[2] += acceleration * multiplier;

  if (is_key_pressed(KEY_MOVE_BACK))
    velocity.data[2] -= acceleration * multiplier;

  if (is_key_pressed(KEY_MOVE_UP))
    velocity.data[1] += acceleration * multiplier;

  if (is_key_pressed(KEY_MOVE_DOWN))
    velocity.data[1] -= acceleration * multiplier;

  {
    vector3f mul;
    vector3f_set_1f(&mul, 1.f);
    mul.data[0] = velocity.data[0] > 0.f ? -1.f : 1.f;
    mul.data[1] = velocity.data[1] > 0.f ? -1.f : 1.f;
    mul.data[2] = velocity.data[2] > 0.f ? -1.f : 1.f;

    velocity.data[0] += mul.data[0] * l_friction;
    velocity.data[0] = (mul.data[0] == -1.f) ?
    fmax(velocity.data[0], 0.f) : fmin(velocity.data[0], 0.f);

    velocity.data[2] += mul.data[2] * l_friction;
    velocity.data[2] = (mul.data[2] == -1.f) ?
    fmax(velocity.data[2], 0.f) : fmin(velocity.data[2], 0.f);

    velocity.data[1] += mul.data[1] * l_friction;
    velocity.data[1] = (mul.data[1] == -1.f) ?
      fmax(velocity.data[1], 0.f) :
      fmin(velocity.data[1], 0.f);
  }

  clamp(velocity.data + 0, -velocity_limit, velocity_limit);
  clamp(velocity.data + 2, -velocity_limit, velocity_limit);
  clamp(velocity.data + 1, -velocity_limit, velocity_limit);
}

static
vector3f
get_world_relative_velocity(float delta_time)
{
  float multiplier = delta_time / REFERENCE_FRAME_TIME;
  matrix4f cross_p;
  vector3f ortho_xz;
  vector3f relative = { 0.f, 0.f, 0.f };
  vector3f lookat_xz = { 0.f, 0.f, 0.f };
  float length;

  // cross the m_camera up vector with the flipped look at direction
  matrix4f_cross_product(&cross_p, &camera.up_vector);
  ortho_xz = mult_v3f(&camera.lookat_direction, -1.f);
  ortho_xz = mult_m4f_v3f(&cross_p, &ortho_xz);
  // only keep the xz components.
  ortho_xz.data[1] = 0;

  // we need ortho_xz.
  relative.data[0] += ortho_xz.data[0] * velocity.data[0] * multiplier;
  relative.data[2] += ortho_xz.data[2] * velocity.data[0] * multiplier;
  relative.data[1] += velocity.data[1] * multiplier;

  lookat_xz.data[0] = camera.lookat_direction.data[0];
  lookat_xz.data[2] = camera.lookat_direction.data[2];
  length = length_v3f(&lookat_xz);

  if (!IS_ZERO_MP(length)) {
    float vely = velocity.data[2];
    lookat_xz.data[0] /= length;
    lookat_xz.data[2] /= length;
    relative.data[0] += lookat_xz.data[0] * vely * multiplier;
    relative.data[2] += lookat_xz.data[2] * vely * multiplier;
  }

  return relative;
}

static
void
update(float delta_time)
{
  camera_update(&camera, delta_time);
  update_velocity(delta_time);

  {
    vector3f displacement = get_world_relative_velocity(delta_time);
    add_set_v3f(&camera.position, &displacement);
  }
}

static
void
update_level(const allocator_t* allocator)
{
  uint64_t fps = (uint64_t)controller_end(controller);
  float dt = (float)controller_start(controller);

  input_update();
  clear_color_and_depth_buffers();
  matrix4f identity;
  matrix4f_set_identity(&identity);
  render_mesh_render_data(mesh_render_data, &pipeline, &camera, &identity);

  if (is_key_triggered(TILDE)) {
    disable_input = !disable_input;
    show_mouse_cursor((int32_t)disable_input);
  }

  if (!disable_input)
    update(dt);
  else if (is_key_triggered(KEY_EXIT_LEVEL))
    exit_level = 1;

  render_basic_controls2(
    font, font_render_data->texture_id, &pipeline, dt, fps, disable_input);
  flush_operations();
}

static
void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  cleanup_font_render_data(font_render_data, &status_map);
  cleanup_mesh_render_data(mesh_render_data, &status_map);
  unload_assets(&ref_assets_map);
}

static
uint32_t
should_unload(void)
{
  return exit_level;
}

void
construct_test_mesh_level(level_t* level)
{
  assert(level);

  level->load = load_level;
  level->update = update_level;
  level->unload = unload_level;
  level->should_unload = should_unload;
}
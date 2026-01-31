/**
 * @file anim_preview.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2026-01-30
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <assert.h>
#include <game/debug/flags.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/logic/camera.h>
#include <game/rendering/render_data.h>
#include <entity/level/level.h>
#include <entity/mesh/color.h>
#include <entity/mesh/mesh.h>
#include <entity/mesh/mesh_utils.h>
#include <entity/mesh/skinned_mesh.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/scene/animation.h>
#include <entity/scene/animation_utils.h>
#include <entity/scene/camera.h>
#include <entity/scene/light.h>
#include <entity/scene/scene.h>
#include <library/framerate_controller/framerate_controller.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>

#define TILDE   0xC0
#define KEY_EXIT_LEVEL           '0'
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
static camera_t* camera;
static scene_t* scene;
static packaged_scene_render_data_t* render_data;
static font_runtime_t* font;
static uint32_t font_image_id;
static anim_sequence_t* anim_sq;
static vector3f velocity;
static const float velocity_limit = 10.f;
static const float acceleration = 5.f;
static const float friction = 2.f;

// TODO: tmp, you can remove this function once refactoring comes into play.
// this returns the first skinned_mesh, animation combo for testing.
anim_sequence_t*
get_anim_sequence(
  scene_t *scene,
  const allocator_t *allocator)
{
  assert(scene && allocator);

  if (!scene->animation_repo.size || !scene->skinned_mesh_repo.size)
    return NULL;

  {
    animation_t *animation = cvector_as(&scene->animation_repo, 0, animation_t);
    skinned_mesh_t *skinned_mesh = cvector_as(
      &scene->skinned_mesh_repo, 0, skinned_mesh_t);
    return play_anim(animation, skinned_mesh, allocator);
  }
}

static
void
load_level(
  const level_context_t context,
  const allocator_t *allocator)
{
  char room[256] = {0};
  sprintf(room, "rooms\\%s", context.level);
  scene = load_scene(context.data_set, room, context.level, allocator);
  create_default_camera(scene, camera);
  create_default_light(scene, allocator);

  render_data = load_scene_render_data(scene, allocator);
  prep_packaged_render_data(context.data_set, room, render_data, allocator);

  camera = cvector_as(&render_data->camera_data, 0, camera_t);
  font = cvector_as(&render_data->font_data.fonts, 0, font_runtime_t);
  font_image_id = *cvector_as(&render_data->font_data.texture_ids, 0, uint32_t);

  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);

  controller = controller_allocate(allocator, 60, 1u);
  anim_sq = get_anim_sequence(scene, allocator);
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
  matrix4f_cross_product(&cross_p, &camera->up_vector);
  ortho_xz = mult_v3f(&camera->lookat_direction, -1.f);
  ortho_xz = mult_m4f_v3f(&cross_p, &ortho_xz);
  // only keep the xz components.
  ortho_xz.data[1] = 0;

  // we need ortho_xz.
  relative.data[0] += ortho_xz.data[0] * velocity.data[0] * multiplier;
  relative.data[2] += ortho_xz.data[2] * velocity.data[0] * multiplier;
  relative.data[1] += velocity.data[1] * multiplier;

  lookat_xz.data[0] = camera->lookat_direction.data[0];
  lookat_xz.data[2] = camera->lookat_direction.data[2];
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
  camera_update(camera, delta_time);
  update_velocity(delta_time);

  {
    vector3f displacement = get_world_relative_velocity(delta_time);
    add_set_v3f(&camera->position, &displacement);
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

  // TMP: remove once testing is done.
  if (anim_sq) {
    update_anim(anim_sq, dt);

    {
      // blit the skinned_mesh vertices into the render_data skinned mesh.
      mesh_render_data_t *sk_render_data = cvector_as(
        &render_data->skinned_mesh_data.skinned_mesh_render_data,
        0,
        mesh_render_data_t);

      mesh_t *mesh = get_anim_mesh(anim_sq);
      uint32_t total_count = sk_render_data->vertex_count * 3;
      memcpy(sk_render_data->vertices, mesh->vertices.data, total_count);
      memcpy(sk_render_data->normals, mesh->normals.data, total_count);
    }
  }

  render_packaged_scene_data(render_data, &pipeline, camera);

  // disable/enable input with '~' key.
  if (is_key_triggered(TILDE)) {
    disable_input = !disable_input;
    show_mouse_cursor((int32_t)disable_input);
  }

  if (!disable_input)
    update(dt);
  else if (is_key_triggered(KEY_EXIT_LEVEL))
    exit_level = 1;

  render_basic_controls(font, font_image_id, &pipeline, dt, fps, disable_input);
  flush_operations();
}

static
void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  scene_free(scene, allocator);
  cleanup_packaged_render_data(render_data, allocator);

  if (anim_sq)
    stop_anim(anim_sq);
}

static
uint32_t
should_unload(void)
{
  return exit_level;
}

void
construct_anim_preview_level(level_t* level)
{
  assert(level);

  level->load = load_level;
  level->update = update_level;
  level->unload = unload_level;
  level->should_unload = should_unload;
}

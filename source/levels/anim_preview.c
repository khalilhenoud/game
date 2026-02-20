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
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/logic/camera.h>
#include <game/rendering/render.h>
#include <entity/level/level.h>
#include <entity/mesh/material.h>
#include <entity/mesh/skinned_mesh.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/scene/animation.h>
#include <entity/scene/animation_utils.h>
#include <entity/scene/camera.h>
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
static camera_t *camera;
static scene_t *scene;
static scene_resources_t *scene_resources;
static font_runtime_t *font;
static uint32_t font_image_id;
static anim_sequence_t *anim_sq;
static vector3f velocity;
static const float velocity_limit = 10.f;
static const float acceleration = 5.f;
static const float friction = 2.f;

// NOTE: for testing only, assumes one skeletal mesh and animation in scene, and
// that they are tied together.
static
anim_sequence_t *
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
render_skinned_mesh(
  scene_t *scene,
  void *_skinned_mesh,
  uint32_t texture_id,
  pipeline_t *pipeline);

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

  scene_resources = render_setup(context.data_set, room, scene, allocator);
  register_render_callback(
    get_type_id(skinned_mesh_t), render_skinned_mesh, scene_resources);
  camera = cvector_as(&scene->camera_repo, 0, camera_t);
  camera->position.data[2] += 200;
  camera->position.data[1] += 100;
  font = get_default_runtime_font(scene, scene_resources, &font_image_id);

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

  // NOTE: testing purposes
  if (anim_sq)
    update_anim(anim_sq, dt);

  render(scene, &pipeline, camera, scene_resources);

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
render_mesh(
  scene_t *scene,
  mesh_t *mesh,
  uint32_t texture_id,
  pipeline_t *pipeline)
{
  mesh_render_data_t mesh_data;
  mesh_data.vertices = mesh->vertices.data;
  mesh_data.normals = mesh->normals.data;
  mesh_data.uv_coords = mesh->uvs.data;
  mesh_data.vertex_count = (mesh->vertices.size)/3;
  mesh_data.indices = mesh->indices.data;
  mesh_data.indices_count = mesh->indices.size;
  if (mesh->materials.used) {
    material_t *material = cvector_as(
      &scene->material_repo, mesh->materials.indices[0], material_t);
    size_t size = sizeof(mesh_data.ambient.data);
    memcpy(mesh_data.ambient.data, material->ambient.data, size);
    memcpy(mesh_data.specular.data, material->specular.data, size);
    memcpy(mesh_data.diffuse.data, material->diffuse.data, size);
  } else {
    // set a default ambient color
    mesh_data.ambient.data[0] =
    mesh_data.ambient.data[1] = mesh_data.ambient.data[2] = 0.5f;
    mesh_data.ambient.data[3] = 1.f;
    // copy the ambient default color into the diffuse and specular
    uint32_t size = sizeof(mesh_data.diffuse.data);
    memcpy(mesh_data.diffuse.data, mesh_data.ambient.data, size);
    memcpy(mesh_data.specular.data, mesh_data.ambient.data, size);
  }
  draw_meshes(&mesh_data, &texture_id, 1, pipeline);
}

static
void
render_bones(
  skinned_mesh_t *skinned_mesh,
  uint32_t node_index,
  skel_node_t *node,
  matrix4f transform,
  pipeline_t *pipeline)
{
  const color_t green = { 0.f, 1.f, 0.f, 1.f };
  cvector_t *nodes = &skinned_mesh->skeleton.nodes;
  float vertices[6];
  point3f origin;
  memset(&origin, 0, sizeof(point3f));
  mult_set_m4f_p3f(&transform, &origin);
  memcpy(vertices, origin.data, sizeof(float) * 3);

  for (uint32_t i = 0; i < node->skel_nodes.size; ++i) {
    uint32_t index = *cvector_as(&node->skel_nodes, i, uint32_t);
    skel_node_t *child = cvector_as(nodes, index, skel_node_t);
    #if 0
    matrix4f child_transform = mult_m4f(&transform, &child->transform);
    #else
    matrix4f child_transform = get_anim_bone_transform(anim_sq, index);
    child_transform = mult_m4f(&transform, &child_transform);
    #endif

    point3f dest;
    memset(&dest, 0, sizeof(point3f));
    mult_set_m4f_p3f(&child_transform, &dest);
    memcpy(vertices + 3, dest.data, sizeof(float) * 3);
    draw_lines(vertices, 2, green, 2, pipeline);

    render_bones(skinned_mesh, index, child, child_transform, pipeline);
  }
}

static
void
render_skinned_mesh(
  scene_t *scene,
  void *_skinned_mesh,
  uint32_t texture_id,
  pipeline_t *pipeline)
{
  skinned_mesh_t *skinned_mesh = _skinned_mesh;
  render_mesh(scene, &skinned_mesh->mesh, texture_id, pipeline);

  disable_depth_test();
  {
    cvector_t *nodes = &skinned_mesh->skeleton.nodes;
    skel_node_t *root = cvector_as(nodes, 0, skel_node_t);
    #if 0
    render_bones(skinned_mesh, 0, root, root->transform, pipeline);
    #else
    render_bones(
      skinned_mesh, 0, root, get_anim_bone_transform(anim_sq, 0), pipeline);
    #endif
  }
  enable_depth_test();
}

static
void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  scene_free(scene, allocator);
  render_cleanup(scene_resources, allocator);

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

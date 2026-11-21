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
#include <game/levels/test_anim_level.h>
#include <game/debug/flags.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/logic/camera.h>
#include <game/rendering/render_data.h>
#include <animation/animation_asset.h>
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
#define TOGGLE_SKELETON_RENDERING '1'
#define SPEED_UP_ANIM             '5'
#define SPEED_DOWN_ANIM           '4'
#define NEXT_ANIM_SEQUENCE        '8'
#define TOGGLE_ROOT_MOTION        '9'
#define REFERENCE_FRAME_TIME      0.033f


typedef struct anim_clip_t anim_clip_t;

static framerate_controller_t *controller;
static uint32_t exit_level = 0;
static int32_t disable_input;
static pipeline_t pipeline;
static camera_t camera;
static vector3f velocity;
static const float velocity_limit = 10.f;
static const float acceleration = 5.f;
static const float friction = 2.f;
static int32_t disable_skeleton_rendering = 1;
static uint32_t root_motion = 1;
static float anim_multiplier = 1.f;
static int32_t anim_sequence = 0;

static asset_ref_t font_ref;
static font_asset_t *font;
static packaged_font_render_data_t *font_render_data;
static asset_ref_t skinned_mesh_ref;
static skinned_mesh_asset_t *skinned_mesh;
static mesh_asset_t *skinned_mesh_mesh;
static packaged_mesh_render_data_t *skinned_mesh_mesh_render_data;
static asset_ref_t skeleton_ref;
static skeleton_asset_t *skeleton;
static asset_ref_t animation_ref;
static animation_asset_t *animation;
static asset_ref_t animation_ref2;
static animation_asset_t *animation2;
static chashmap_t ref_assets_map;
static chashmap_t status_map;
static anim_clip_t *anim_clip;
const static uint32_t asset_map_reservation = 256;

////////////////////////////////////////////////////////////////////////////////
typedef
struct anim_clip_t {
  animation_asset_t *anim;
  skeleton_asset_t *skeleton;
  skinned_mesh_asset_t *skinned_mesh;
  mesh_asset_t *mesh;
  cvector_t local_transforms;        // node local transforms
  cvector_t final_transforms;        // bone final transforms
  cvector_t vertices;
  cvector_t normals;
  float time;
} anim_clip_t;

static
anim_clip_t *
play_anim(
  animation_asset_t *anim,
  skeleton_asset_t *skeleton,
  skinned_mesh_asset_t *skinned_mesh,
  mesh_asset_t *mesh)
{
  anim_clip_t *anim_cp = g_default_allocator.mem_alloc(sizeof(anim_clip_t));
  anim_cp->anim = anim;
  anim_cp->skeleton = skeleton;
  anim_cp->skinned_mesh = skinned_mesh;
  anim_cp->mesh = mesh;
  anim_cp->time = 0.f;

  cvector_setup2(&anim_cp->vertices, float);
  cvector_resize(&anim_cp->vertices, mesh->vertices.size);

  cvector_setup2(&anim_cp->normals, float);
  cvector_resize(&anim_cp->normals, mesh->normals.size);

  cvector_setup2(&anim_cp->local_transforms, matrix4f);
  cvector_resize(&anim_cp->local_transforms, skeleton->nodes.size);

  cvector_setup2(&anim_cp->final_transforms, matrix4f);
  cvector_resize(&anim_cp->final_transforms, skinned_mesh->bones.size);

  return anim_cp;
}

static
void
stop_anim(anim_clip_t *anim_cp)
{
  cvector_cleanup2(&anim_cp->vertices);
  cvector_cleanup2(&anim_cp->normals);
  cvector_cleanup2(&anim_cp->local_transforms);
  cvector_cleanup2(&anim_cp->final_transforms);
  g_default_allocator.mem_free(anim_cp);
}

static
void
update_bone_transforms(
  anim_clip_t *anim_cp,
  float anim_time,
  const matrix4f *root_inverse,
  skeleton_node_t *node,
  const matrix4f *parent_transform,
  const uint32_t index,
  const uint32_t root_motion);

static
void
update_vertices(anim_clip_t *anim_cp);

static
void
update_anim(
  anim_clip_t *anim_cp,
  float delta_t,
  uint32_t root_motion)
{
  animation_asset_t *anim = anim_cp->anim;
  skeleton_asset_t *skeleton = anim_cp->skeleton;
  const float anim_tick_sec = anim->ticks_per_second;
  const float ticks_per_sec = anim_tick_sec != 0.f ? anim_tick_sec : 25.f;
  float time_in_ticks = (anim_cp->time += delta_t) * ticks_per_sec;
  float anim_time = fmod(time_in_ticks, anim->duration);

  skeleton_node_t *root = cvector_as(&skeleton->nodes, 0, skeleton_node_t);
  matrix4f root_inverse = inverse_m4f(&root->transform);
  matrix4f identity;
  matrix4f_set_identity(&identity);
  update_bone_transforms(
    anim_cp,
    anim_time,
    &root_inverse,
    root,
    &identity,
    0,
    root_motion);

  update_vertices(anim_cp);
}

static
void
update_vertices(anim_clip_t *anim_cp)
{
  animation_asset_t *anim = anim_cp->anim;
  skinned_mesh_asset_t *skinned_mesh = anim_cp->skinned_mesh;
  mesh_asset_t *mesh = anim_cp->mesh;
  cvector_t *vertex_to_bones = &skinned_mesh->vertex_to_bones;

  float *vertices = (float *)anim_cp->vertices.data;
  float *normals = (float *)anim_cp->normals.data;
  float *original = (float *)mesh->vertices.data;
  float *original_normals = (float *)mesh->normals.data;
  for (uint32_t i = 0, size = mesh->vertices.size / 3; i < size; ++i) {
    float total_weight = 0.f;
    uint32_t idx = i * 3;
    point3f vertex = { original[idx], original[idx + 1], original[idx + 2] };
    point3f normal = {
      original_normals[idx],
      original_normals[idx + 1],
      original_normals[idx + 2] };
    point3f skinned = { 0.f, 0.f, 0.f };
    point3f skinned_normal = { 0.f, 0.f, 0.f };

    bone_id_weight_t *to_bones = cvector_as(
      vertex_to_bones, i, bone_id_weight_t);
    const uint32_t bone_size = sizeof(to_bones->id)/sizeof(uint32_t);
    vertices[i * 3 + 0] = vertices[i * 3 + 1] = vertices[i * 3 + 2] = 0.f;
    normals[i * 3 + 0] = normals[i * 3 + 1] = normals[i * 3 + 2] = 0.f;

    for (uint32_t j = 0; j < bone_size; ++j) {
      if (to_bones->weight[j] <= 0.f)
        break;

      matrix4f *final_transform = cvector_as(
        &anim_cp->final_transforms, to_bones->id[j], matrix4f);
      point3f intermediate = { 0.f, 0.f, 0.f };
      matrix4f inverse_transpose = *final_transform;
      point3f intermediate_normal = { 0.f, 0.f, 0.f };

      inverse_set_m4f(&inverse_transpose);
      transpose_set_m4f(&inverse_transpose);

      intermediate = mult_m4f_p3f(final_transform, &vertex);
      intermediate = mult_v3f(&intermediate, to_bones->weight[j]);
      add_set_v3f(&skinned, &intermediate);

      intermediate_normal = mult_m4f_v3f(&inverse_transpose, &normal);
      intermediate_normal = mult_v3f(
        &intermediate_normal, to_bones->weight[j]);
      add_set_v3f(&skinned_normal, &intermediate_normal);

      total_weight += to_bones->weight[j];
    }

    assert(IS_SAME_NP(total_weight, 1.f));

    vertices[idx + 0] = skinned.data[0];
    vertices[idx + 1] = skinned.data[1];
    vertices[idx + 2] = skinned.data[2];

    // NOTE: redundant, changes are minimal. But for correctness I kept it.
    normalize_set_v3f(&skinned_normal);

    normals[idx + 0] = skinned_normal.data[0];
    normals[idx + 1] = skinned_normal.data[1];
    normals[idx + 2] = skinned_normal.data[2];
  }
}

// NOTE: Since this is cpu only, root_inverse is not used right now, keeping it
// for later use
static
void
update_bone_transforms(
  anim_clip_t *anim_cp,
  float anim_time,
  const matrix4f *root_inverse,
  skeleton_node_t *node,
  const matrix4f *parent_transform,
  const uint32_t index,
  const uint32_t root_motion)
{
  animation_asset_t *anim = anim_cp->anim;
  skeleton_asset_t *skeleton = anim_cp->skeleton;
  skinned_mesh_asset_t *skinned_mesh = anim_cp->skinned_mesh;
  skinned_mesh_bone_t *bone = node->bone_index == UINT32_MAX ?
    NULL :
    cvector_as(&skinned_mesh->bones, node->bone_index, skinned_mesh_bone_t);

  matrix4f global_transform;
  matrix4f *local = cvector_as(&anim_cp->local_transforms, index, matrix4f);

  // override local_transform if a valid channel exists
  animation_node_t *channel = get_animation_channel(anim, &node->name);
  matrix4f local_transform = node->transform;
  if (channel) {
    uint32_t index;
    matrix4f s_r_t[3];
    matrix4f_set_identity(s_r_t + 0);
    matrix4f_set_identity(s_r_t + 1);
    matrix4f_set_identity(s_r_t + 2);

    index = get_channel_scale_index(channel, anim_time);
    if (index != UINT32_MAX) {
      scale_key_t *k_s = cvector_as(&channel->scale_keys, index++, scale_key_t);
      scale_key_t *k_e = cvector_as(&channel->scale_keys, index, scale_key_t);
      float lerp_factor = (anim_time - k_s->time) / (k_e->time - k_s->time);
      vector3f scale = lerp_v3f(k_s->value, k_e->value, lerp_factor);
      matrix4f_scale(s_r_t + 0, scale.data[0], scale.data[1], scale.data[2]);
    } else
      matrix4f_set_identity(s_r_t + 0);

    index = get_channel_rotation_index(channel, anim_time);
    if (index != UINT32_MAX) {
      rotation_key_t *r_s = cvector_as(
        &channel->rotation_keys, index++, rotation_key_t);
      rotation_key_t *r_e = cvector_as(
        &channel->rotation_keys, index, rotation_key_t);
      float lerp_factor = (anim_time - r_s->time) / (r_e->time - r_s->time);
      quatf r = slerp_quatf(r_s->value, r_e->value, lerp_factor);
      s_r_t[1] = quatf_to_matrix4f(r);
    } else
      matrix4f_set_identity(s_r_t + 1);

    index = get_channel_translation_index(channel, anim_time);
    if (index != UINT32_MAX) {
      position_key_t *p_s = cvector_as(
        &channel->position_keys, index++, position_key_t);
      position_key_t *p_e = cvector_as(
        &channel->position_keys, index, position_key_t);
      float lerp_factor = (anim_time - p_s->time) / (p_e->time - p_s->time);
      vector3f pos = lerp_v3f(p_s->value, p_e->value, lerp_factor);
      matrix4f_translation(s_r_t + 2, pos.data[0], pos.data[1], pos.data[2]);
    } else
      matrix4f_set_identity(s_r_t + 2);

    {
      matrix4f intermediate;
      intermediate = mult_m4f(s_r_t + 1, s_r_t + 0);
      local_transform = mult_m4f(s_r_t + 2, &intermediate);
    }
  }

  if (!root_motion) {
    local_transform.data[3] = node->transform.data[3];
    local_transform.data[7] = node->transform.data[7];
    local_transform.data[11] = node->transform.data[11];
  }

  global_transform = mult_m4f(parent_transform, &local_transform);
  *local = local_transform;

  if (bone) {
    matrix4f *final_transform = cvector_as(
      &anim_cp->final_transforms, node->bone_index, matrix4f);
    *final_transform = mult_m4f(&global_transform, &bone->offset_matrix);
  }

  for (uint32_t i = 0; i < node->children.size; ++i) {
    uint32_t child_index = *cvector_as(&node->children, i, uint32_t);
    skeleton_node_t *child = cvector_as(
      &skeleton->nodes, child_index, skeleton_node_t);
    update_bone_transforms(
      anim_cp,
      anim_time,
      root_inverse,
      child,
      &global_transform,
      child_index,
      root_motion);
  }
}

static
matrix4f
get_node_local_transform(anim_clip_t *anim_cp, uint32_t index)
{
  return *cvector_as(&anim_cp->local_transforms, index, matrix4f);
}

static
matrix4f
get_node_local_transform_fixed(uint32_t index)
{
  return *cvector_as(&anim_clip->local_transforms, index, matrix4f);
}

static
float *
get_skin(anim_clip_t *anim_cp)
{
  return (float *)anim_cp->vertices.data;
}

static
float *
get_skin_normals(anim_clip_t *anim_cp)
{
  return (float *)anim_cp->normals.data;
}

static
void
reset_time(anim_clip_t *anim_cp)
{
  anim_cp->time = 0.f;
}
////////////////////////////////////////////////////////////////////////////////

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
render_basic_controls(
  font_asset_t *font,
  uint32_t font_image_id,
  pipeline_t *pipeline,
  float dt_seconds,
  uint64_t frame_rate,
  int32_t disable_input)
{
  if (disable_input) {
    const char* text[1];

    text[0] = "press [0] to return to room selection";
    render_text_to_screen(
      font,
      font_image_id,
      pipeline,
      text,
      1,
      white,
      0.f, 0.f);
  } else {
    const char* text[6];
    char delta_str[128] = { 0 };
    char frame_str[128] = { 0 };

    sprintf(delta_str, "delta: %f", dt_seconds);
    sprintf(frame_str, "fps: %llu", frame_rate);

    // display simple instructions.
    text[0] = delta_str;
    text[1] = frame_str;
    text[2] = "----------------";
    text[3] = "[C] RESET CAMERA";
    text[4] = "[~] CAMERA UNLOCK/LOCK";
    text[5] = "[1/2/WASD/EQ] CAMERA SPEED/MOVEMENT";
    render_text_to_screen(
      font,
      font_image_id,
      pipeline,
      text,
      6,
      white,
      0.f, 0.f);
  }
}

// "http://stackoverflow.com/questions/12943164/replacement-for-gluperspective-with-glfrustrum"
static
void
setup_view_projection_pipeline(
  const int32_t viewport_width,
  const int32_t viewport_height,
  pipeline_t *pipeline)
{
  float view_width = (float)viewport_width;
  float view_height = (float)viewport_height;
  float aspect = view_width / view_height;
  float znear = 0.1f, zfar = 4000.f;
  float fh = (float)tan((double)60.f / 2.f / 180.f * K_PI) * znear;
  float fw = fh * aspect;
  pipeline_set_default(pipeline);
  set_viewport(pipeline, 0.f, 0.f, view_width, view_height);
  update_viewport(pipeline);
  set_perspective(pipeline, -fw, fw, -fh, fh, znear, zfar);
  update_projection(pipeline);
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
  vector3f_set_3f(&camera.position, 0.f, 100, 200);

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

  cstring_setup2(
    &skinned_mesh_ref.path, "F:\\data\\shared\\skinned_meshes\\Ch36-lib.bin");
  skinned_mesh_ref.type_id = get_type_id(skinned_mesh_asset_t);

  cstring_setup2(
    &skeleton_ref.path, "F:\\data\\shared\\skeletons\\Ch36-lib.bin");
  skeleton_ref.type_id = get_type_id(skeleton_asset_t);

  cstring_setup2(
    &animation_ref.path,
    "F:\\data\\shared\\anim1\\animations\\mixamorig1_Hips.bin");
  animation_ref.type_id = get_type_id(animation_asset_t);

  cstring_setup2(
    &animation_ref2.path,
    "F:\\data\\shared\\anim2\\animations\\mixamorig1_Hips.bin");
  animation_ref2.type_id = get_type_id(animation_asset_t);


  chashmap_def(&ref_assets_map);
  chashmap_setup2(&ref_assets_map, asset_ref_t, void *);
  chashmap_reserve(&ref_assets_map, asset_map_reservation);
  load_recursive(&ref_assets_map, &font_ref);
  load_recursive(&ref_assets_map, &skinned_mesh_ref);
  load_recursive(&ref_assets_map, &skeleton_ref);
  load_recursive(&ref_assets_map, &animation_ref);
  load_recursive(&ref_assets_map, &animation_ref2);

  chashmap_def(&status_map);
  chashmap_setup2(&status_map, asset_ref_t, uint32_t);
  chashmap_reserve(&status_map, asset_map_reservation);

  void **data = NULL;
  chashmap_at(&ref_assets_map, font_ref, asset_ref_t, void *, data);
  font = *(font_asset_t **)data;

  chashmap_at(&ref_assets_map, skinned_mesh_ref, asset_ref_t, void *, data);
  skinned_mesh = *(skinned_mesh_asset_t **)data;

  chashmap_at(&ref_assets_map, skinned_mesh->mesh, asset_ref_t, void *, data);
  skinned_mesh_mesh = *(mesh_asset_t **)data;

  chashmap_at(&ref_assets_map, skeleton_ref, asset_ref_t, void *, data);
  skeleton = *(skeleton_asset_t **)data;

  chashmap_at(&ref_assets_map, animation_ref, asset_ref_t, void *, data);
  animation = *(animation_asset_t **)data;

  chashmap_at(&ref_assets_map, animation_ref2, asset_ref_t, void *, data);
  animation2 = *(animation_asset_t **)data;

  setup_default_camera();
  font_render_data = prep_font_render_data(font, &ref_assets_map, &status_map);
  skinned_mesh_mesh_render_data = prep_mesh_render_data(
    skinned_mesh_mesh, &ref_assets_map, &status_map);

  setup_view_projection_pipeline(
    context.viewport.width, context.viewport.height, &pipeline);
  show_mouse_cursor(0);

  controller = controller_allocate(allocator, 60, 1u);
  anim_clip = play_anim(
    anim_sequence == 0 ? animation : animation2,
    skeleton, skinned_mesh, skinned_mesh_mesh);
  exit_level = 0;
  disable_input = 0;
  velocity.data[0] = velocity.data[1] = velocity.data[2] = 0.f;
  disable_skeleton_rendering = 1;
  root_motion = 1;
  anim_multiplier = 1.f;
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
render_animated_mesh()
{
  matrix4f identity;
  mesh_render_data_t mesh_data;
  size_t size = sizeof(mesh_data.ambient.data);
  mesh_render_data_t *ptr_mesh_data = cvector_as(
    &skinned_mesh_mesh_render_data->mesh_render_data, 0, mesh_render_data_t);
  mesh_asset_t *mesh = anim_clip->mesh;
  mesh_data.vertices = get_skin(anim_clip);
  mesh_data.normals = get_skin_normals(anim_clip);
  mesh_data.uv_coords = mesh->uvs.data;
  mesh_data.vertex_count = (mesh->vertices.size)/3;
  mesh_data.indices = mesh->indices.data;
  mesh_data.indices_count = mesh->indices.size;
  memcpy(mesh_data.ambient.data, ptr_mesh_data->ambient.data, size);
  memcpy(mesh_data.specular.data, ptr_mesh_data->specular.data, size);
  memcpy(mesh_data.diffuse.data, ptr_mesh_data->diffuse.data, size);
  matrix4f_set_identity(&identity);
  render_animated_skinned_mesh_render_data(
    &mesh_data,
    skinned_mesh_mesh_render_data->texture_ids.data,
    get_node_local_transform_fixed,
    skeleton,
    disable_skeleton_rendering,
    &pipeline,
    &camera,
    &identity);
}

static
void
render_advanced_controls()
{
  if (disable_input)
    return;

  {
    char array[200] = {0};
    snprintf(array, 200, "[4/5] SLOWDOWN SPEED UP ANIM %.1f", anim_multiplier);
    add_debug_text_to_frame(array, green, 0.f, 120.f);
    add_debug_text_to_frame("[8] NEXT_ANIM_SEQUENCE", green, 0.f, 140.f);
    add_debug_text_to_frame(
      "[1] TOGGLE SKELETON RENDERING",
      disable_skeleton_rendering ? red : green,
      0.f, 160.f);
    add_debug_text_to_frame(
      "[9] TOGGLE ROOT MOTION",
      root_motion ? green : red,
      0.f, 180.f);
    draw_debug_text_frame(&pipeline, font, font_render_data->texture_id);
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
  update_anim(anim_clip, dt * anim_multiplier, root_motion);
  render_animated_mesh();

  if (is_key_triggered(TILDE)) {
    disable_input = !disable_input;
    show_mouse_cursor((int32_t)disable_input);
  }

  if (is_key_triggered(TOGGLE_SKELETON_RENDERING))
    disable_skeleton_rendering = !disable_skeleton_rendering;

  if (is_key_pressed(SPEED_UP_ANIM)) {
    anim_multiplier += 0.05f;
    anim_multiplier = anim_multiplier > 2.f ? 2.f : anim_multiplier;
  }

  if (is_key_pressed(SPEED_DOWN_ANIM)) {
    anim_multiplier -= 0.05f;
    anim_multiplier = anim_multiplier < 0.1f ? 0.1f : anim_multiplier;
  }

  if (is_key_triggered(TOGGLE_ROOT_MOTION)) {
    root_motion = !root_motion;
    reset_time(anim_clip);
  }

  if (is_key_triggered(NEXT_ANIM_SEQUENCE)) {
    anim_sequence = (anim_sequence + 1) % 2;
    stop_anim(anim_clip);
    anim_clip = play_anim(
      anim_sequence == 0 ? animation : animation2,
      skeleton, skinned_mesh, skinned_mesh_mesh);
  }

  if (!disable_input)
    update(dt);
  else if (is_key_triggered(KEY_EXIT_LEVEL))
    exit_level = 1;

  render_advanced_controls();
  render_basic_controls(
    font, font_render_data->texture_id, &pipeline, dt, fps, disable_input);
  flush_operations();
}

static
void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  cleanup_font_render_data(font_render_data, &status_map);
  cleanup_mesh_render_data(skinned_mesh_mesh_render_data, &status_map);
  unload_assets(&ref_assets_map);

  stop_anim(anim_clip);
}

static
uint32_t
should_unload(void)
{
  return exit_level;
}

void
construct_test_anim_level(level_t* level)
{
  assert(level);

  level->load = load_level;
  level->update = update_level;
  level->unload = unload_level;
  level->should_unload = should_unload;
}
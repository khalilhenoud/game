/**
 * @file generic_level.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2026-01-27
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <assert.h>
#include <game/debug/flags.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/logic/player.h>
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
#include <entity/scene/node.h>
#include <entity/scene/light.h>
#include <entity/scene/scene.h>
#include <entity/spatial/bvh.h>
#include <library/framerate_controller/framerate_controller.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>

#define TILDE   0xC0
#define KEY_EXIT_LEVEL           '0'


static framerate_controller_t *controller;
static uint32_t exit_level = 0;
static int32_t disable_input;
static pipeline_t pipeline;
static camera_t* camera;
static scene_t* scene;
static packaged_scene_render_data_t* scene_render_data;
static font_runtime_t* font;
static uint32_t font_image_id;
static bvh_t* bvh;
static anim_sequence_t* anim_sq;

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

  scene_render_data = load_scene_render_data(scene, allocator);
  prep_packaged_render_data(
    context.data_set, room, scene_render_data, allocator);

  camera = cvector_as(&scene_render_data->camera_data, 0, camera_t);

  font = cvector_as(&scene_render_data->font_data.fonts, 0, font_runtime_t);
  font_image_id = *cvector_as(
    &scene_render_data->font_data.texture_ids, 0, uint32_t);

  if (scene->bvh_repo.size)
    bvh = cvector_as(&scene->bvh_repo, 0, bvh_t);
  else
    bvh = NULL;

  anim_sq = get_anim_sequence(scene, allocator);

  exit_level = 0;
  disable_input = 0;

  pipeline_set_default(&pipeline);
  set_viewport(
    &pipeline, 0.f, 0.f,
    (float)context.viewport.width, (float)context.viewport.height);
  update_viewport(&pipeline);

  {
    // "http://stackoverflow.com/questions/12943164/replacement-for-gluperspective-with-glfrustrum"
    float znear = 0.1f, zfar = 4000.f;
    float aspect = (float)context.viewport.width / context.viewport.height;
    float fh = (float)tan((double)60.f / 2.f / 180.f * K_PI) * znear;
    float fw = fh * aspect;
    set_perspective(&pipeline, -fw, fw, -fh, fh, znear, zfar);
    update_projection(&pipeline);
  }

  show_mouse_cursor(0);

  controller = controller_allocate(allocator, 60, 1u);

  player_init(
    scene->metadata.player_start,
    scene->metadata.player_angle,
    camera,
    bvh);
}

void
update_level(const allocator_t* allocator)
{
  uint64_t frame_rate = (uint64_t)controller_end(controller);
  float dt_seconds = (float)controller_start(controller);

  input_update();
  clear_color_and_depth_buffers();

  // TMP: remove once testing is done.
  if (anim_sq) {
    update_anim(anim_sq, dt_seconds);

    {
      // blit the skinned_mesh vertices into the scene_render_data skinned mesh.
      mesh_render_data_t *render_data = cvector_as(
        &scene_render_data->skinned_mesh_data.skinned_mesh_render_data,
        0,
        mesh_render_data_t);

      mesh_t *mesh = get_anim_mesh(anim_sq);
      uint32_t total_count = render_data->vertex_count * 3;
      memcpy(render_data->vertices, mesh->vertices.data, total_count);
      memcpy(render_data->normals, mesh->normals.data, total_count);
    }
  }

  render_packaged_scene_data(scene_render_data, &pipeline, camera);

  {
    // disable/enable input with '~' key.
    if (is_key_triggered(TILDE)) {
      disable_input = !disable_input;
      show_mouse_cursor((int32_t)disable_input);
    }

    if (!disable_input) {
      update_debug_flags();
      player_update(dt_seconds);
      draw_debug_text_frame(&pipeline, font, font_image_id);
      draw_debug_face_frame(&pipeline, g_debug_flags.disable_depth_debug);
    } else {
      if (is_key_triggered(KEY_EXIT_LEVEL))
        exit_level = 1;
    }
  }

  if (disable_input) {
    const char* text[1];

    text[0] = "press [0] to return to room selection";
    render_text_to_screen(
      font,
      font_image_id,
      &pipeline,
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
      &pipeline,
      text,
      6,
      white,
      0.f, 0.f);
  }

  flush_operations();
}

void
unload_level(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  scene_free(scene, allocator);
  cleanup_packaged_render_data(scene_render_data, allocator);

  if (anim_sq)
    stop_anim(anim_sq);
}

uint32_t
should_unload(void)
{
  return exit_level;
}

void
construct_generic_level(level_t* level)
{
  assert(level);

  level->load = load_level;
  level->update = update_level;
  level->unload = unload_level;
  level->should_unload = should_unload;
}

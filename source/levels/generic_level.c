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
static packaged_scene_render_data_t* render_data;
static font_runtime_t* font;
static uint32_t font_image_id;
static bvh_t* bvh;

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
  bvh = (scene->bvh_repo.size) ? cvector_as(&scene->bvh_repo, 0, bvh_t) : NULL;

  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);

  player_init(
    scene->metadata.player_start,
    scene->metadata.player_angle,
    camera,
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
  render_packaged_scene_data(render_data, &pipeline, camera);

  if (is_key_triggered(TILDE)) {
    disable_input = !disable_input;
    show_mouse_cursor((int32_t)disable_input);
  }

  if (!disable_input) {
    update_debug_flags();
    player_update(dt);
    draw_debug_text_frame(&pipeline, font, font_image_id);
    draw_debug_face_frame(&pipeline, g_debug_flags.disable_depth_debug);
  } else if (is_key_triggered(KEY_EXIT_LEVEL))
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
}

static
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

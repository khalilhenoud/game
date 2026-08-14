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
#include <game/rendering/render_data.h>
#include <entity/level/level.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/scene/scene.h>
#include <level/sublevel_asset.h>
#include <library/asset/asset_ref.h>
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
static camera_t* camera;
// static scene_t* scene;
static packaged_scene_render_data_t* render_data;
static font_runtime_t* font;
static uint32_t font_image_id;
static bvh_t* bvh;
asset_ref_t sublevel;

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

static
void
load_level(
  const level_context_t context,
  const allocator_t *allocator)
{
  cstring_t target;
  cstring_setup2(&sublevel.path, "F:\\data\\level1\\sublevels\\e3m1.bin");
  sublevel.type_id = get_type_id(sublevel_asset_t);

  extract_folder(&sublevel.path, &target);

  uint32_t id = get_type_id(sublevel_asset_t);

  // NOTE: it is the use of these functions that forces the linker to call
  vtable_t *vtable = get_vtable(sublevel.type_id);
  loader_t loader = vtable->fn_get_loader();//sublevel_asset_get_loader();//
  deloader_t deloader = vtable->fn_get_deloader();//sublevel_asset_get_deloader();//
  void *data = NULL;
  loader(&data, &sublevel, &g_default_allocator);
  sublevel_asset_t *assetptr = data;

  deloader(&data, &sublevel, &g_default_allocator);

  cstring_cleanup2(&target);
  asset_ref_cleanup(&sublevel, &g_default_allocator);



  // char room[256] = {0};
  // sprintf(room, "rooms\\%s", context.level);
  // scene = load_scene(context.data_set, room, context.level, allocator);
  // create_default_camera(scene, camera);
  // create_default_light(scene, allocator);

  // render_data = load_scene_render_data(scene, allocator);
  // prep_packaged_render_data(context.data_set, room, render_data, allocator);

  // camera = cvector_as(&render_data->camera_data, 0, camera_t);
  // font = cvector_as(&render_data->font_data.fonts, 0, font_runtime_t);
  // font_image_id = *cvector_as(&render_data->font_data.texture_ids, 0, uint32_t);
  // bvh = (scene->bvh_repo.size) ? cvector_as(&scene->bvh_repo, 0, bvh_t) : NULL;

  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);

  // player_init(
  //   scene->metadata.player_start,
  //   scene->metadata.player_angle,
  //   camera,
  //   bvh);

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
  // scene_free(scene, allocator);
  // cleanup_packaged_render_data(render_data, allocator);
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
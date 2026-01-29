/**
 * @file room_select.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2024-01-06
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <assert.h>
#include <game/debug/text.h>
#include <game/input/input.h>
#include <game/levels/utils.h>
#include <game/rendering/load_font.h>
#include <game/rendering/load_image.h>
#include <entity/level/level.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/runtime/texture.h>
#include <entity/runtime/texture_utils.h>
#include <library/filesystem/filesystem.h>
#include <library/framerate_controller/framerate_controller.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>


void
set_level_to_load(const char *source);

static framerate_controller_t *controller;
static int32_t exit_room_select = -1;
static pipeline_t pipeline;
static dir_entries_t rooms;
static texture_runtime_t texture_runtime;
static uint32_t texture_id;
static font_runtime_t font_runtime;

static
void
load_rooms(const char *dataset)
{
  char directory[260];
  sprintf(directory, "%srooms\\*", dataset);
  get_subdirectories(directory, &rooms);
}

static
void
load_font_data(
  const level_context_t context,
  const allocator_t *allocator)
{
  populate_default_font(&font_runtime.font, allocator);
  load_font_inplace(
    context.data_set, &font_runtime.font, &font_runtime, allocator);
  cstring_setup(
    &texture_runtime.texture.path, font_runtime.font.image_file.str, allocator);
  load_image_buffer(context.data_set, &texture_runtime, allocator);
  texture_id = upload_to_gpu(
    texture_runtime.texture.path.str,
    texture_runtime.buffer.data,
    texture_runtime.width,
    texture_runtime.height,
    (renderer_image_format_t)texture_runtime.format);
}

static
void
unload_font_data(const allocator_t* allocator)
{
  free_font_runtime_internal(&font_runtime, allocator);
  free_texture_runtime_internal(&texture_runtime, allocator);
  evict_from_gpu(texture_id);
}

void
load_room_select(
  const level_context_t context,
  const allocator_t *allocator)
{
  load_font_data(context, allocator);
  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);
  controller = controller_allocate(allocator, 60, 1u);

  load_rooms(context.data_set);
  exit_room_select = -1;
}

void
update_room_select(const allocator_t* allocator)
{
  controller_end(controller);
  controller_start(controller);

  input_update();
  clear_color_and_depth_buffers();

  {
    char rooms_names[1024][260];
    const char* text[1024];
    uint32_t used = rooms.used;

    for (uint32_t i = 0; i < rooms.used; ++i) {
      if (strcmp(rooms.dir_names[i], "room_select") == 0)
        sprintf(rooms_names[i], "[%s] %s", "-", rooms.dir_names[i]);
      else
        sprintf(rooms_names[i], "[%u] %s", (i + 1), rooms.dir_names[i]);
      text[i] = rooms_names[i];
    }

    render_text_to_screen(
      &font_runtime,
      texture_id,
      &pipeline,
      text,
      used,
      white,
      20.f, 0.f);
  }

  // only support 9 levels for now.
  for (int32_t i = 0; i < 9; ++i) {
    if (
      is_key_triggered('1' + i) &&
      i < (int32_t)rooms.used &&
      strcmp(rooms.dir_names[i], "room_select") != 0) {
        exit_room_select = i;
        break;
      }
  }

  flush_operations();
}

void
unload_room_select(const allocator_t* allocator)
{
  controller_free(controller, allocator);
  unload_font_data(allocator);
}

uint32_t
should_unload_room_select(void)
{
  if (exit_room_select == -1)
    return 0;
  else {
    set_level_to_load(rooms.dir_names[exit_room_select]);
    return 1;
  }
}

void
construct_level_selector(level_t* level)
{
  assert(level);

  level->load = load_room_select;
  level->update = update_room_select;
  level->unload = unload_room_select;
  level->should_unload = should_unload_room_select;
}
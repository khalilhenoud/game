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

#define ARROW_UP            0x26
#define ARROW_DOWN          0x28
#define SPACE               0x20
#define KEY_K               0x4B


static framerate_controller_t *controller;
static int32_t exit_room_select = -1;
static uint32_t exit_room_option = 0;
static pipeline_t pipeline;
static dir_entries_t rooms;
static texture_runtime_t texture;
static uint32_t tex_id;
static font_runtime_t font;
static const char *data_set;

void
set_level_to_load(const char *source, uint32_t option);

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
  populate_default_font(&font.font, allocator);
  load_font_inplace(context.data_set, &font.font, &font, allocator);
  cstring_setup(&texture.texture.path, font.font.image_file.str, allocator);
  load_image_buffer(context.data_set, &texture, allocator);
  tex_id = upload_to_gpu(
    texture.texture.path.str,
    texture.buffer.data,
    texture.width,
    texture.height,
    (renderer_image_format_t)texture.format);
}

static
void
unload_font_data(const allocator_t *allocator)
{
  free_font_runtime_internal(&font, allocator);
  free_texture_runtime_internal(&texture, allocator);
  evict_from_gpu(tex_id);
}

static
void
load_room_select(
  const level_context_t context,
  const allocator_t *allocator)
{
  data_set = context.data_set;

  load_font_data(context, allocator);
  setup_view_projection_pipeline(&context, &pipeline);
  show_mouse_cursor(0);
  controller = controller_allocate(allocator, 60, 1u);

  load_rooms(data_set);
  exit_room_select = -1;
}

static
void
room_selection()
{
  static int32_t index = 0;
  char name[1024];
  const char *text[1];
  uint32_t used = rooms.used;

  // small intro text
  sprintf(name, "%s", "up down to navigate. space, k to select");
  text[0] = name;
  render_text_to_screen(&font, tex_id, &pipeline, text, 1, white, 5.f, 0.f);
  sprintf(name, "%s", "-------------------------------------");
  text[0] = name;
  render_text_to_screen(&font, tex_id, &pipeline, text, 1, white, 5.f, 10.f);

  for (uint32_t i = 0, j = 0; i < rooms.used; ++i) {
    if (strcmp(rooms.dir_names[i], "room_select") == 0)
      continue;
    else
      sprintf(name, "%s", rooms.dir_names[i]);
    text[0] = name;

    render_text_to_screen(
      &font,
      tex_id,
      &pipeline,
      text,
      1,
      i == index ? green : white,
      15.f, j * 20.f + 30.f);

    if (i == index) {
      sprintf(name, "%s", ">");
      text[0] = name;
      render_text_to_screen(
        &font,
        tex_id,
        &pipeline,
        text,
        1,
        green,
        5.f, j * 20.f + 30.f);
    }

    j++;
  }

  if (is_key_triggered(ARROW_UP)) {
    --index;
    if (!strcmp(rooms.dir_names[index], "room_select"))
      --index;
  }

  if (is_key_triggered(ARROW_DOWN)) {
    ++index;
    if (!strcmp(rooms.dir_names[index], "room_select"))
      ++index;
  }

  index = index < 0 ? 0 : index;
  index = index > (rooms.used - 1) ? (rooms.used - 1) : index;

  if (is_key_triggered(SPACE)) {
    if (strcmp(rooms.dir_names[index], "room_select") != 0)
      exit_room_select = index;
      exit_room_option = 0;
  } else if (is_key_triggered(KEY_K)) {
    if (strcmp(rooms.dir_names[index], "room_select") != 0)
      exit_room_select = index;
      exit_room_option = 1;
  }
}

static
void
update_room_select(const allocator_t *allocator)
{
  controller_end(controller);
  controller_start(controller);

  input_update();
  clear_color_and_depth_buffers();
  room_selection();
  flush_operations();
}

static
void
unload_room_select(const allocator_t *allocator)
{
  controller_free(controller, allocator);
  unload_font_data(allocator);
}

static
uint32_t
should_unload_room_select(void)
{
  if (exit_room_select == -1)
    return 0;
  else {
    set_level_to_load(rooms.dir_names[exit_room_select], exit_room_option);
    return 1;
  }
}

void
construct_level_selector(level_t *level)
{
  assert(level);

  level->load = load_room_select;
  level->update = update_room_select;
  level->unload = unload_room_select;
  level->should_unload = should_unload_room_select;
}
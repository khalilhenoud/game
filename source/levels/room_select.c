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
#include <game/rendering/render_data.h>
#include <entity/level/level.h>
#include <font/font_asset.h>
#include <library/asset/asset_ref.h>
#include <library/containers/chashmap.h>
#include <library/filesystem/filesystem.h>
#include <library/framerate_controller/framerate_controller.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>

#define ARROW_UP            0x26
#define ARROW_DOWN          0x28
#define SPACE               0x20


static framerate_controller_t *controller;
static int32_t exit_room_select = -1;
static uint32_t exit_room_option = 0;
static pipeline_t pipeline;
static dir_entries_t rooms;
static const char *data_set;

static packaged_font_render_data_t *font_render_data;
static asset_ref_t font_ref;
static font_asset_t *font;

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
load_room_select(
  const level_context_t context,
  const allocator_t *allocator)
{
  data_set = context.data_set;

  cstring_setup2(&font_ref.path, "F:\\data\\shared\\fonts\\Malgun Gothic.bin");
  font_ref.type_id = get_type_id(font_asset_t);

  chashmap_def(&ref_assets_map);
  chashmap_setup2(&ref_assets_map, asset_ref_t, void *);
  chashmap_reserve(&ref_assets_map, asset_map_reservation);
  load_recursive(&ref_assets_map, &font_ref);

  chashmap_def(&status_map);
  chashmap_setup2(&status_map, asset_ref_t, uint32_t);
  chashmap_reserve(&status_map, asset_map_reservation);

  void **data = NULL;
  chashmap_at(&ref_assets_map, font_ref, asset_ref_t, void *, data);
  font = *(font_asset_t **)data;

  font_render_data = prep_font_render_data(font, &ref_assets_map, &status_map);

  setup_view_projection_pipeline(
    context.viewport.width, context.viewport.height, &pipeline);
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
  sprintf(name, "%s", "up down to navigate. space to select");
  text[0] = name;
  render_text_to_screen(
    font, font_render_data->texture_id, &pipeline, text, 1, white, 5.f, 0.f);
  sprintf(name, "%s", "-------------------------------------");
  text[0] = name;
  render_text_to_screen(
    font, font_render_data->texture_id, &pipeline, text, 1, white, 5.f, 10.f);

  for (uint32_t i = 0, j = 0; i < rooms.used; ++i) {
    if (strcmp(rooms.dir_names[i], "room_select") == 0)
      continue;
    else
      sprintf(name, "%s", rooms.dir_names[i]);
    text[0] = name;

    render_text_to_screen(
      font,
      font_render_data->texture_id,
      &pipeline,
      text,
      1,
      i == index ? green : white,
      15.f, j * 20.f + 30.f);

    if (i == index) {
      sprintf(name, "%s", ">");
      text[0] = name;
      render_text_to_screen(
        font,
        font_render_data->texture_id,
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
    if (strcmp(rooms.dir_names[index], "room_select") != 0) {
      if (strcmp(rooms.dir_names[index], "a_test_level") == 0)
        exit_room_option = 2;
      else if (strcmp(rooms.dir_names[index], "a_test_anim_level") == 0)
        exit_room_option = 3;
      else if (strcmp(rooms.dir_names[index], "a_test_mesh_level") == 0)
        exit_room_option = 4;
      else
        exit_room_option = 0;
      exit_room_select = index;
    }
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
  cleanup_font_render_data(font_render_data, &status_map);
  unload_assets(&ref_assets_map);
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
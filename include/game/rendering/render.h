/**
 * @file render.h
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif


typedef struct allocator_t allocator_t;
typedef struct camera_t camera_t;
typedef struct pipeline_t pipeline_t;
typedef struct scene_resources_t scene_resources_t;
typedef struct scene_t scene_t;
typedef void (*render_callback_t)(scene_t *, void *, uint32_t, pipeline_t *);

font_runtime_t *
get_default_runtime_font(
  scene_t *scene,
  scene_resources_t *resources,
  uint32_t *font_texture_id);

void
register_render_callback(
  uint32_t type_id,
  render_callback_t render_callback,
  scene_resources_t *resources);

// parse scene for all resources and load then upload them to the gpu
scene_resources_t *
render_setup(
  const char *data_set,
  const char *folder,
  scene_t *scene,
  const allocator_t *allocator);

void
render(
  scene_t *scene,
  pipeline_t *pipeline,
  camera_t *camera,
  scene_resources_t *resources);

void
render_cleanup(
  scene_resources_t *resources,
  const allocator_t *allocator);

#ifdef __cplusplus
}
#endif

#endif
/**
 * @file utils.h
 * @author khalilhenoud@gmail.com
 * @brief convert a bin scene into an entity scene.
 * @version 0.1
 * @date 2023-07-16
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef BIN_TO_ENTITY_H
#define BIN_TO_ENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <entity/mesh/color.h>


typedef struct allocator_t allocator_t;
typedef struct camera_t camera_t;
typedef struct font_t font_t;
typedef struct font_runtime_t font_runtime_t;
typedef struct level_context_t level_context_t;
typedef struct pipeline_t pipeline_t;
typedef struct scene_t scene_t;

scene_t*
load_scene(
  const char* dataset,
  const char* folder,
  const char* file,
  const allocator_t* allocator);

void
populate_default_font(font_t *font, const allocator_t *allocator);

void
create_default_camera(scene_t *scene, camera_t *camera);

void
create_default_light(
  scene_t *scene,
  const allocator_t *allocator);

void
setup_view_projection_pipeline(
  const level_context_t *context,
  pipeline_t *pipeline);

void
render_basic_controls(
  font_runtime_t *font,
  uint32_t font_image_id,
  pipeline_t *pipeline,
  float dt_seconds,
  uint64_t frame_rate,
  int32_t enabled);

#ifdef __cplusplus
}
#endif

#endif
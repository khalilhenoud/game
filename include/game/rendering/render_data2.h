/**
 * @file render_data2.h
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2023-09-23
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef SCENE_RENDER_DATA2
#define SCENE_RENDER_DATA2

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <library/containers/cvector.h>
#include <math/matrix4f.h>
#include <props/color.h>


typedef struct camera_t camera_t;
typedef struct pipeline_t pipeline_t;
typedef struct sublevel_asset_t sublevel_asset_t;
typedef struct font_asset_t font_asset_t;
typedef struct chashmap_t chashmap_t;

typedef
struct packaged_mesh_render_data_t {
  cvector_t mesh_render_data;             // mesh_render_data_t
  cvector_t texture_assets;               // asset_ref_t *
  cvector_t texture_ids;                  // uint32_t
} packaged_mesh_render_data_t;

typedef
struct packaged_sublevel_render_data_t {
  packaged_mesh_render_data_t mesh_data;
  cvector_t light_data;                   // renderer_light_t
} packaged_sublevel_render_data_t;

typedef
struct packaged_font_render_data_t {
  asset_ref_t *texture_asset;
  uint32_t texture_id;
} packaged_font_render_data_t;

void
cleanup_render_data(
  packaged_sublevel_render_data_t *render_data,
  chashmap_t *status_map);

packaged_sublevel_render_data_t *
prep_render_data(
  sublevel_asset_t *sublevel,
  chashmap_t *assets_map,
  chashmap_t *status_map);

void
cleanup_font_render_data(
  packaged_font_render_data_t *render_data,
  chashmap_t *status_map);

packaged_font_render_data_t *
prep_font_render_data(
  font_asset_t *font,
  chashmap_t *assets_map,
  chashmap_t *status_map);

void
render_render_data(
  packaged_sublevel_render_data_t *render_data,
  pipeline_t *pipeline,
  camera_t *camera,
  matrix4f *root);

#ifdef __cplusplus
}
#endif

#endif
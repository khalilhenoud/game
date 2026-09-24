/**
 * @file render_data2.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2023-09-23
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <assert.h>
#include <string.h>
#include <game/rendering/render_data2.h>
#include <font/font_asset.h>
#include <level/sublevel_asset.h>
#include <library/allocator/allocator.h>
#include <library/asset/asset_ref.h>
#include <library/containers/chashmap.h>
#include <material/bulk_material_asset.h>
#include <material/indexed_material_asset.h>
#include <material/material_asset.h>
#include <mesh/bulk_mesh_asset.h>
#include <mesh/mesh_asset.h>
#include <props/camera.h>
#include <props/color.h>
#include <props/light.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>
#include <texture/texture_asset.h>


// NOTE: Investigate if this is important (put breakpoint and test).
static
void
normalize_color(color_t *color)
{
  float val = 0.f;
  for (uint32_t i = 0; i < 3; ++i)
    val += color->data[i] * color->data[i];
  val = sqrtf(val);
  if (!IS_ZERO_MP(val)) {
    for (uint32_t i = 0; i < 3; ++i)
      color->data[i] /= val;
  }
}

inline
void
copy_vec3f(float dest[], const float source[])
{
  memcpy(dest, source, sizeof(float) * 3);
}

inline
void
copy_vec4f(float dest[], const float source[])
{
  memcpy(dest, source, sizeof(float) * 4);
}

inline
void
set_default_ambient(color_t *ambient)
{
  ambient->data[0] = ambient->data[1] = ambient->data[2] = 0.5f;
  ambient->data[3] = 1.f;
}

inline
void
set_color4f(color_t *color, float r, float g, float b, float a)
{
  color->data[0] = r;
  color->data[1] = g;
  color->data[2] = b;
  color->data[3] = a;
}


inline
void
populate_mesh_render_data(const mesh_asset_t *mesh, mesh_render_data_t *data)
{
  uint32_t array_size = sizeof(float) * mesh->vertices.size;
  data->vertex_count = (mesh->vertices.size)/3;
  data->vertices = g_default_allocator.mem_alloc(array_size);
  memcpy(data->vertices, mesh->vertices.data, array_size);
  data->normals = g_default_allocator.mem_alloc(array_size);
  memcpy(data->normals, mesh->normals.data, array_size);
  data->uv_coords = g_default_allocator.mem_alloc(array_size);
  memcpy(data->uv_coords, mesh->uvs.data, array_size);

  array_size = sizeof(uint32_t) * mesh->indices.size;
  data->indices_count = mesh->indices.size;
  data->indices = g_default_allocator.mem_alloc(array_size);
  memcpy(data->indices, mesh->indices.data, array_size);

  set_default_ambient(&data->ambient);
  copy_vec4f(data->diffuse.data, data->ambient.data);
  copy_vec4f(data->specular.data, data->ambient.data);
}

static
void
load_sublevel_mesh_data(
  sublevel_asset_t *sublevel,
  packaged_mesh_render_data_t *mesh_data,
  chashmap_t *assets_map,
  chashmap_t *status_map)
{
  assert(sublevel && mesh_data && assets_map && status_map);

  // 1 texture per mesh right now
  cvector_t *data = &mesh_data->mesh_render_data;
  cvector_setup2(data, mesh_render_data_t);
  cvector_resize(data, sublevel->meshes.meshes.size);

  cvector_t *assets = &mesh_data->texture_assets;
  cvector_setup2(assets, asset_ref_t *);
  cvector_resize(assets, sublevel->meshes.meshes.size);

  cvector_t *ids = &mesh_data->texture_ids;
  cvector_setup2(ids, uint32_t);
  cvector_resize(ids, sublevel->meshes.meshes.size);
  memset(ids->data, 0, sizeof(uint32_t) * ids->size);

  for (uint32_t i = 0; i < sublevel->meshes.meshes.size; ++i) {
    mesh_asset_t *mesh = cvector_as(&sublevel->meshes.meshes, i, mesh_asset_t);
    mesh_render_data_t *r_data = cvector_as(data, i, mesh_render_data_t);
    populate_mesh_render_data(mesh, r_data);

    if (mesh->materials.size == 1) {
      asset_ref_t *material_ref = cvector_as(&mesh->materials, 0, asset_ref_t);
      if (material_ref->type_id == get_type_id(indexed_material_asset_t)) {
        void **data = NULL;
        chashmap_at(assets_map, *material_ref, asset_ref_t, void *, data);
        indexed_material_asset_t *i_material = NULL;
        i_material = *(indexed_material_asset_t **)data;

        chashmap_at(
          assets_map, i_material->bulk_material_ref, asset_ref_t, void *, data);
        bulk_material_asset_t *b_material = NULL;
        b_material = *(bulk_material_asset_t **)data;

        material_asset_t *material = cvector_as(
          &b_material->materials, i_material->index, material_asset_t);

        // 1 texture per mesh
        if (material->textures.size == 1) {
          asset_ref_t *t_ref = cvector_as(&material->textures, 0, asset_ref_t);
          chashmap_at(assets_map, *t_ref, asset_ref_t, void *, data);
          texture_asset_t *texture = *(texture_asset_t **)data;

          // set the ref
          asset_ref_t **texture_asset = cvector_as(assets, i, asset_ref_t *);
          *texture_asset = t_ref;

          // upload if it isn't, and set the id.
          uint32_t index;
          chashmap_contains(status_map, *t_ref, asset_ref_t, index);
          if (index == CHASHTABLE_INVALID_INDEX) {
            uint32_t id = upload_to_gpu(
              NULL,
              texture->buffer.data,
              texture->width,
              texture->height,
              (renderer_image_format_t)texture->format);
            chashmap_insert(status_map, *t_ref, asset_ref_t, id, uint32_t);
            *cvector_as(ids, i, uint32_t) = id;
          } else {
            uint32_t *ptr_id = NULL;
            chashmap_at(status_map, *t_ref, asset_ref_t, uint32_t, ptr_id);
            assert(ptr_id);
            *cvector_as(ids, i, uint32_t) = *ptr_id;
          }
        }
      } else
        assert(0);
    }
  }
}

static
void
load_sublevel_light_data(
  sublevel_asset_t *sublevel,
  cvector_t *light_data)
{
  assert(sublevel && light_data);

  cvector_setup2(light_data, renderer_light_t);
  cvector_resize(light_data, sublevel->lights.size);

  for (uint32_t i = 0; i < light_data->size; ++i) {
    renderer_light_t *target = cvector_as(light_data, i, renderer_light_t);
    light_t *source = cvector_as(&sublevel->lights, i, light_t);

    target->attenuation_constant = source->attenuation_constant;
    target->attenuation_linear = source->attenuation_linear;
    target->attenuation_quadratic = source->attenuation_quadratic;
    target->inner_cone = source->inner_cone;
    target->outer_cone = source->outer_cone;
    target->type = (renderer_light_type_t)source->type;
    copy_vec4f(target->diffuse.data, source->diffuse.data);
    copy_vec4f(target->specular.data, source->specular.data);
    copy_vec4f(target->ambient.data, source->ambient.data);
    normalize_color(&target->diffuse);
    normalize_color(&target->specular);
    normalize_color(&target->ambient);
    copy_vec3f(target->position.data, source->position.data);
    copy_vec3f(target->direction.data, source->direction.data);
    copy_vec3f(target->up.data, source->up.data);

    // enable the light on the renderer side
    enable_light(i);
  }
}

void
cleanup_render_data(
  packaged_sublevel_render_data_t *render_data,
  chashmap_t *status_map)
{
  // evict all the loaded textures, from the gpu
  for (
    chashmap_iterator_t iter = chashmap_begin(status_map);
    !chashmap_iter_equal(iter, chashmap_end(status_map));
    chashmap_advance(&iter)) {
      uint32_t id = *chashmap_value(&iter, uint32_t);
      if (id)
        evict_from_gpu(id);
    }

  for (uint32_t i = 0; i < render_data->light_data.size; ++i)
    disable_light(i);

  cvector_cleanup2(&render_data->mesh_data.mesh_render_data);
  cvector_cleanup2(&render_data->mesh_data.texture_assets);
  cvector_cleanup2(&render_data->mesh_data.texture_ids);
  cvector_cleanup2(&render_data->light_data);
  g_default_allocator.mem_free(render_data);
}

packaged_sublevel_render_data_t *
prep_render_data(
  sublevel_asset_t *sublevel,
  chashmap_t *assets_map,
  chashmap_t *status_map)
{
  assert(sublevel && assets_map && status_map);

  packaged_sublevel_render_data_t *render_data =
    g_default_allocator.mem_alloc(sizeof(packaged_sublevel_render_data_t));
  memset(render_data, 0, sizeof(packaged_sublevel_render_data_t));

  load_sublevel_mesh_data(
    sublevel, &render_data->mesh_data, assets_map, status_map);
  load_sublevel_light_data(sublevel, &render_data->light_data);
  return render_data;
}

static
void
set_light_properties_internal(
  camera_t *camera,
  packaged_sublevel_render_data_t *render_data,
  pipeline_t *pipeline)
{
#if 0
  // we could pick and enable only the closest lights...
  for (uint32_t i = 0; i < render_data->light_data.count; ++i) {
    renderer_light_t* light = render_data->light_data.lights + i;
    set_light_properties(i, light, pipeline);
  }
#else
  renderer_light_t light;
  memset(&light, 0, sizeof(renderer_light_t));
  light.type = RENDERER_LIGHT_TYPE_DIRECTIONAL;
  vector3f_set_3f(&light.position, 0.f, 1.f, 0.f);
  vector3f_set_3f(&light.direction, 0.f, 0.f, 0.f);
  vector3f_set_3f(&light.up, 0.f, 0.f, 0.f);
  light.attenuation_constant = 1;
  light.attenuation_linear = 0.001f;
  set_color4f(&light.ambient, 1.f, 1.f, 1.f, 1.f);
  set_color4f(&light.diffuse, 1.f, 1.f, 1.f, 1.f);
  set_color4f(&light.specular, 0.f, 0.f, 0.f, 1.f);
  set_light_properties(0, &light, pipeline);
  vector3f_set_3f(&light.position, 1.f, 0.f, 0.f);
  set_color4f(&light.ambient, 0.2f, 0.2f, 0.2f, 1.f);
  set_light_properties(1, &light, pipeline);
#endif
}

void
render_render_data(
  packaged_sublevel_render_data_t *render_data,
  pipeline_t *pipeline,
  camera_t *camera,
  matrix4f *root)
{
  assert(render_data && pipeline && camera);

  matrix4f out;
  memset(&out, 0, sizeof(matrix4f));
  camera_view_matrix(camera, &out);
  set_matrix_mode(pipeline, MODELVIEW);
  load_identity(pipeline);
  post_multiply(pipeline, &out);

  set_light_properties_internal(camera, render_data, pipeline);

  // TODO(khalil): is the order accurate? are the light pretransformed, check
  push_matrix(pipeline);
  pre_multiply(pipeline, root);

  draw_meshes(
    render_data->mesh_data.mesh_render_data.data,
    render_data->mesh_data.texture_ids.data,
    render_data->mesh_data.mesh_render_data.size,
    pipeline);

  pop_matrix(pipeline);
}

void
cleanup_font_render_data(
  packaged_font_render_data_t *render_data,
  chashmap_t *status_map)
{
  g_default_allocator.mem_free(render_data);
}

packaged_font_render_data_t *
prep_font_render_data(
  font_asset_t *font,
  chashmap_t *assets_map,
  chashmap_t *status_map)
{
  assert(font && assets_map && status_map);

  packaged_font_render_data_t *render_data =
    g_default_allocator.mem_alloc(sizeof(packaged_font_render_data_t));
  memset(render_data, 0, sizeof(packaged_font_render_data_t));

  render_data->texture_asset = &font->texture_ref;

  void **data = NULL;
  chashmap_at(assets_map, font->texture_ref, asset_ref_t, void *, data);
  texture_asset_t *texture = *(texture_asset_t **)data;

  // upload if it isn't, and set the id.
  uint32_t index;
  chashmap_contains(status_map, font->texture_ref, asset_ref_t, index);
  if (index == CHASHTABLE_INVALID_INDEX) {
    uint32_t id = upload_to_gpu(
      NULL,
      texture->buffer.data,
      texture->width,
      texture->height,
      (renderer_image_format_t)texture->format);
    chashmap_insert(status_map, font->texture_ref, asset_ref_t, id, uint32_t);
    render_data->texture_id = id;
  } else {
    uint32_t *ptr_id = NULL;
    chashmap_at(status_map, font->texture_ref, asset_ref_t, uint32_t, ptr_id);
    assert(ptr_id);
    render_data->texture_id = *ptr_id;
  }

  return render_data;
}
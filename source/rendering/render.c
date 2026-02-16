/**
 * @file render.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <stdint.h>
#include <string.h>
#include <game/rendering/load_font.h>
#include <game/rendering/load_image.h>
#include <game/rendering/render.h>
#include <entity/mesh/color.h>
#include <entity/mesh/material.h>
#include <entity/mesh/mesh.h>
#include <entity/mesh/mesh_utils.h>
#include <entity/mesh/skinned_mesh.h>
#include <entity/mesh/texture.h>
#include <entity/runtime/font.h>
#include <entity/runtime/font_utils.h>
#include <entity/runtime/texture.h>
#include <entity/runtime/texture_utils.h>
#include <entity/scene/camera.h>
#include <entity/scene/light.h>
#include <entity/scene/node.h>
#include <entity/scene/scene.h>
#include <library/allocator/allocator.h>
#include <library/containers/chashmap.h>
#include <library/core/core.h>
#include <library/string/cstring.h>
#include <renderer/pipeline.h>
#include <renderer/renderer_opengl.h>


// NOTE: using a hashmap is slow, but for simplicity it will have to do for now.
typedef
struct texture_resource_t {
  texture_runtime_t runtime;
  uint32_t id;
} texture_resource_t;

typedef
struct font_resource_t {
  font_runtime_t runtime;
  texture_resource_t texture_resource;
} font_resource_t;

typedef
struct scene_resources_t {
  chashmap_t textures;
  chashmap_t fonts;
  chashmap_t render_callbacks;
} scene_resources_t;

static
void
texture_resource_cleanup(void *elem_ptr, const allocator_t* allocator)
{
  texture_resource_t *casted = (texture_resource_t *)elem_ptr;
  free_texture_runtime_internal(&casted->runtime, allocator);
  if (casted->id)
    evict_from_gpu(casted->id);
}

static
void
font_resource_cleanup(void *elem_ptr, const allocator_t* allocator)
{
  font_resource_t *casted = (font_resource_t *)elem_ptr;
  free_font_runtime_internal(&casted->runtime, allocator);
  free_texture_runtime_internal(&casted->texture_resource.runtime, allocator);
  if (casted->texture_resource.id)
    evict_from_gpu(casted->texture_resource.id);
}

font_runtime_t *
get_default_runtime_font(
  scene_t *scene,
  scene_resources_t *resources,
  uint32_t *font_texture_id)
{
  assert(scene && scene->font_repo.size && resources && font_texture_id);

  {
    font_resource_t *font_resource_ptr = NULL;
    font_t *font = cvector_as(&scene->font_repo, 0, font_t);

    chashmap_at(
      &resources->fonts,
      font->data_file, cstring_t, font_resource_t, font_resource_ptr);

    *font_texture_id = font_resource_ptr->texture_resource.id;
    return &font_resource_ptr->runtime;
  }
}

void
register_render_callback(
  uint32_t type_id,
  render_callback_t render_callback,
  scene_resources_t *resources)
{
  assert(type_id && render_callback && resources);

  chashmap_insert(
    &resources->render_callbacks,
    type_id, uint32_t,
    render_callback, render_callback_t);
}

scene_resources_t *
render_setup(
  const char *data_set,
  const char *folder,
  scene_t *scene,
  const allocator_t *allocator)
{
  char texture_path[1024] = { 0 };
  assert(data_set && folder && scene && allocator);

  snprintf(
    texture_path, sizeof(texture_path), "%s\\%s\\textures\\", data_set, folder);

  {
    scene_resources_t *resources = NULL;
    texture_resource_t texture_resource, *texture_resource_ptr = NULL;
    font_resource_t font_resource, *font_resource_ptr = NULL;

    memset(&texture_resource, 0, sizeof(texture_resource_t));
    memset(&font_resource, 0, sizeof(font_resource_t));

    resources = allocator->mem_alloc(
      sizeof(scene_resources_t));

    chashmap_setup(
      &resources->render_callbacks,
      get_type_data(uint32_t), get_type_data(render_callback_t),
      allocator, 0.6f);

    chashmap_setup(
      &resources->textures,
      get_type_data(cstring_t), get_type_data(texture_resource_t),
      allocator, 0.6f);

    chashmap_setup(
      &resources->fonts,
      get_type_data(cstring_t), get_type_data(font_resource_t),
      allocator, 0.6f);

    for (uint32_t i = 0; i < scene->texture_repo.size; ++i) {
      texture_t *texture = cvector_as(&scene->texture_repo, i, texture_t);
      chashmap_insert(
        &resources->textures,
        texture->path, cstring_t,
        texture_resource, texture_resource_t);

      chashmap_at(
        &resources->textures,
        texture->path, cstring_t, texture_resource_t, texture_resource_ptr);

      cstring_setup2(
        &texture_resource_ptr->runtime.texture.path,
        texture->path.str);

      load_image_buffer(
        texture_path, &texture_resource_ptr->runtime, allocator);

      {
        texture_runtime_t *texture_runtime = &texture_resource_ptr->runtime;
        texture_resource_ptr->id = upload_to_gpu(
          texture_runtime->texture.path.str,
          texture_runtime->buffer.data,
          texture_runtime->width,
          texture_runtime->height,
          (renderer_image_format_t)texture_runtime->format);
      }
    }

    for (uint32_t i = 0; i < scene->font_repo.size; ++i) {
      font_t *font = cvector_as(&scene->font_repo, i, font_t);

      chashmap_insert(
        &resources->fonts,
        font->data_file, cstring_t,
        font_resource, font_resource_t);

      chashmap_at(
        &resources->fonts,
        font->data_file, cstring_t, font_resource_t, font_resource_ptr);

      cstring_setup2(
        &font_resource_ptr->texture_resource.runtime.texture.path,
        font->image_file.str);

      load_image_buffer(
        data_set, &font_resource_ptr->texture_resource.runtime, allocator);

      {
        texture_runtime_t *texture_runtime =
          &font_resource_ptr->texture_resource.runtime;
        font_resource_ptr->texture_resource.id = upload_to_gpu(
          texture_runtime->texture.path.str,
          texture_runtime->buffer.data,
          texture_runtime->width,
          texture_runtime->height,
          (renderer_image_format_t)texture_runtime->format);
      }

      // setup the font runtime
      cstring_setup2(
        &font_resource_ptr->runtime.font.data_file,
        font->data_file.str);

      cstring_setup2(
        &font_resource_ptr->runtime.font.image_file,
        font->image_file.str);

      load_font_inplace(
        data_set,
        &font_resource_ptr->runtime.font,
        &font_resource_ptr->runtime,
        allocator);
    }

    for (uint32_t i = 0; i < scene->light_repo.size; ++i)
      enable_light(i);

    return resources;
  }
}

static
void
set_lights(scene_t *scene, pipeline_t *pipeline)
{
#if 0
  for (uint32_t i = 0; i < render_data->light_data.size; ++i) {
    renderer_light_t* light = render_data->light_data.lights + i;
    set_light_properties(i, light, pipeline);
  }
#else
  renderer_light_t light;
  memset(&light, 0, sizeof(renderer_light_t));
  light.type = RENDERER_LIGHT_TYPE_DIRECTIONAL;
  light.position.data[1] = 1;
  light.attenuation_constant = 1;
  light.attenuation_linear = 0.001f;
  light.ambient.data[0] =
    light.ambient.data[1] =
    light.ambient.data[2] =
    light.ambient.data[3] = 1.f;
  light.diffuse.data[0] =
    light.diffuse.data[1] =
    light.diffuse.data[2] =
    light.diffuse.data[3] = 1.f;
  light.specular.data[3] = 1.f;
  set_light_properties(0, &light, pipeline);
  light.position.data[0] = 1;
  light.position.data[1] = 0;
  light.position.data[2] = 0;
  light.ambient.data[0] =
  light.ambient.data[1] =
  light.ambient.data[2] = 0.2f;
  set_light_properties(1, &light, pipeline);
#endif
}

static
uint32_t
get_mesh_render_texture_id(
  scene_t *scene,
  mesh_t *mesh,
  scene_resources_t *resources)
{
  if (mesh->materials.used) {
    uint32_t index = mesh->materials.indices[0];
    texture_t *texture = cvector_as(
      &scene->texture_repo, index, texture_t);
    texture_resource_t *ptr;
    chashmap_at(
      &resources->textures,
      texture->path, cstring_t,
      texture_resource_t, ptr);
    return ptr->id;
  }
  return 0;
}

static
void
render_mesh(
  scene_t *scene,
  mesh_t *mesh,
  render_callback_t *render_callback,
  scene_resources_t *resources,
  pipeline_t *pipeline)
{
  uint32_t texture_id = get_mesh_render_texture_id(scene, mesh, resources);
  if (render_callback)
    (*render_callback)(scene, mesh, texture_id, pipeline);
  else {
    mesh_render_data_t mesh_data;
    mesh_data.vertices = mesh->vertices.data;
    mesh_data.normals = mesh->normals.data;
    mesh_data.uv_coords = mesh->uvs.data;
    mesh_data.vertex_count = (mesh->vertices.size)/3;
    mesh_data.indices = mesh->indices.data;
    mesh_data.indices_count = mesh->indices.size;
    if (mesh->materials.used) {
      material_t *material = cvector_as(
        &scene->material_repo, mesh->materials.indices[0], material_t);
      size_t size = sizeof(mesh_data.ambient.data);
      memcpy(mesh_data.ambient.data, material->ambient.data, size);
      memcpy(mesh_data.specular.data, material->specular.data, size);
      memcpy(mesh_data.diffuse.data, material->diffuse.data, size);
    } else {
      // set a default ambient color
      mesh_data.ambient.data[0] =
      mesh_data.ambient.data[1] = mesh_data.ambient.data[2] = 0.5f;
      mesh_data.ambient.data[3] = 1.f;
      // copy the ambient default color into the diffuse and specular
      uint32_t size = sizeof(mesh_data.diffuse.data);
      memcpy(mesh_data.diffuse.data, mesh_data.ambient.data, size);
      memcpy(mesh_data.specular.data, mesh_data.ambient.data, size);
    }
    draw_meshes(&mesh_data, &texture_id, 1, pipeline);
  }
}

static
void
render_skinned_mesh(
  scene_t *scene,
  skinned_mesh_t *skinned_mesh,
  render_callback_t *render_callback,
  scene_resources_t *resources,
  pipeline_t *pipeline)
{
  mesh_t *mesh = &skinned_mesh->mesh;
  if (render_callback) {
    uint32_t texture_id = get_mesh_render_texture_id(scene, mesh, resources);
    (*render_callback)(scene, skinned_mesh, texture_id, pipeline);
  } else
    render_mesh(scene, mesh, render_callback, resources, pipeline);
}

static
void
render_scene_recursive(
  scene_t *scene,
  pipeline_t *pipeline,
  node_t *node,
  scene_resources_t *resources)
{
  push_matrix(pipeline);
  pre_multiply(pipeline, &node->transform);

  {
    // draw the meshes belonging to this node.
    for (uint32_t i = 0; i < node->resources.size; ++i) {
      node_resource_t *resource = cvector_as(
        &node->resources, i, node_resource_t);

      render_callback_t *render_callback = NULL;
      {
        uint32_t index = CHASHTABLE_INVALID_INDEX;
        if (resources->render_callbacks.keys.size) {
          chashmap_contains(
            &resources->render_callbacks, resource->type_id, uint32_t, index);
        }
        if (index != CHASHTABLE_INVALID_INDEX) {
          render_callback = cvector_as(
            &resources->render_callbacks.values, index, render_callback_t);
        }
      }

      if (resource->type_id == get_type_id(mesh_t)) {
        mesh_t *mesh = cvector_as(&scene->mesh_repo, resource->index, mesh_t);
        render_mesh(scene, mesh, render_callback, resources, pipeline);
      } else if (resource->type_id == get_type_id(skinned_mesh_t)) {
        skinned_mesh_t *skinned_mesh = cvector_as(
          &scene->skinned_mesh_repo, resource->index, skinned_mesh_t);
        render_skinned_mesh(
          scene, skinned_mesh, render_callback, resources, pipeline);
      } else
        assert(0);
    }

    // recurively call the child nodes.
    for (uint32_t i = 0; i < node->nodes.size; ++i) {
      uint32_t node_index = *cvector_as(&node->nodes, i, uint32_t);
      render_scene_recursive(
        scene,
        pipeline,
        cvector_as(&scene->node_repo, node_index, node_t),
        resources);
    }
  }

  pop_matrix(pipeline);
}

void
render(
  scene_t *scene,
  pipeline_t *pipeline,
  camera_t *camera,
  scene_resources_t *resources)
{
  assert(scene && pipeline && camera && resources);
  assert(scene->node_repo.size > 0 && "at least the root node must exist!");

  {
    matrix4f out;
    memset(&out, 0, sizeof(matrix4f));
    camera_view_matrix(camera, &out);
    set_matrix_mode(pipeline, MODELVIEW);
    load_identity(pipeline);
    post_multiply(pipeline, &out);

    set_lights(scene, pipeline);

    render_scene_recursive(
      scene,
      pipeline,
      cvector_as(&scene->node_repo, 0, node_t),
      resources);
  }
}

void
render_cleanup(
  scene_resources_t *resources,
  const allocator_t *allocator)
{
  assert(resources && allocator);

  chashmap_cleanup(&resources->textures, NULL);
  chashmap_cleanup(&resources->fonts, NULL);
  chashmap_cleanup(&resources->render_callbacks, NULL);
  allocator->mem_free(resources);
}

////////////////////////////////////////////////////////////////////////////////
INITIALIZER(register_texture_resource_t)
{
  vtable_t vtable;
  memset(&vtable, 0, sizeof(vtable_t));
  vtable.fn_def = NULL;
  vtable.fn_is_def = NULL;
  vtable.fn_is_equal = NULL;
  vtable.fn_serialize = NULL;
  vtable.fn_deserialize = NULL;
  vtable.fn_cleanup = texture_resource_cleanup;
  register_type(get_type_id(texture_resource_t), &vtable);
}

INITIALIZER(register_font_resource_t)
{
  vtable_t vtable;
  memset(&vtable, 0, sizeof(vtable_t));
  vtable.fn_def = NULL;
  vtable.fn_is_def = NULL;
  vtable.fn_is_equal = NULL;
  vtable.fn_serialize = NULL;
  vtable.fn_deserialize = NULL;
  vtable.fn_cleanup = font_resource_cleanup;
  register_type(get_type_id(font_resource_t), &vtable);
}
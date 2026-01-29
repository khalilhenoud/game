/**
 * @file utils.c
 * @author khalilhenoud@gmail.com
 * @brief
 * @version 0.1
 * @date 2023-07-16
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <game/levels/utils.h>
#include <entity/level/level.h>
#include <entity/misc/font.h>
#include <entity/scene/camera.h>
#include <entity/scene/light.h>
#include <entity/scene/node.h>
#include <entity/scene/scene.h>
#include <library/allocator/allocator.h>
#include <library/containers/cvector.h>
#include <library/filesystem/io.h>
#include <library/streams/binary_stream.h>
#include <library/string/cstring.h>
#include <renderer/pipeline.h>


scene_t*
load_scene(
  const char* dataset,
  const char* folder,
  const char* file,
  const allocator_t* allocator)
{
  // TODO: This is temporary, this needs to be generalized in packaged content.
  char fullpath[1024] = {0};
  snprintf(fullpath, 1024, "%s\\%s\\%s.bin", dataset, folder, file);

  scene_t *scene;
  binary_stream_t stream;

  binary_stream_def(&stream);
  binary_stream_setup(&stream, allocator);

  {
    size_t read = 0;
    uint8_t buffer[16 * 1024];
    file_handle_t file;
    file = open_file(fullpath, FILE_OPEN_MODE_READ | FILE_OPEN_MODE_BINARY);
    assert((void*)file != NULL);
    do {
      read = read_buffer(
        file,
        buffer, sizeof(uint8_t), 16 * 1024);
      binary_stream_write(&stream, buffer, 16 * 1024);
    } while (read);
    close_file(file);

    scene = scene_create(NULL, allocator);
    scene_deserialize(scene, allocator, &stream);
  }

  binary_stream_cleanup(&stream);
  return scene;
}

void
populate_default_font(font_t *font, const allocator_t *allocator)
{
  assert(font && allocator);

  {
    cstring_setup(&font->data_file, "\\font\\FontData.csv", allocator);
    cstring_setup(&font->image_file, "\\font\\ExportedFont.png", allocator);
  }
}

void
create_default_camera(scene_t *scene, camera_t *camera)
{
  if (scene->camera_repo.size)
    return;

  {
    node_t *node = NULL;
    cvector_resize(&scene->camera_repo, 1);
    camera = cvector_as(&scene->camera_repo, 0, camera_t);
    camera->position.data[0] =
    camera->position.data[1] =
    camera->position.data[2] = 0.f;

    // transform the camera to y being up.
    node = cvector_as(&scene->node_repo, 0, node_t);
    mult_set_m4f_p3f(
      &node->transform,
      &camera->position);
    camera->lookat_direction.data[0] =
    camera->lookat_direction.data[1] = 0.f;
    camera->lookat_direction.data[2] = -1.f;
    camera->up_vector.data[0] =
    camera->up_vector.data[2] = 0.f;
    camera->up_vector.data[1] = 1.f;
  }
}

void
create_default_light(
  scene_t *scene,
  const allocator_t *allocator)
{
  if (scene->light_repo.size)
    return;

  {
    light_t *light = NULL;
    cvector_resize(&scene->light_repo, 1);
    light = cvector_as(&scene->light_repo, 0, light_t);
    cstring_setup(&light->name, "test", allocator);
    vector3f_set_3f(&light->position, 0.f, 200.f, 0.f);
    vector3f_set_3f(&light->direction, -1.f, -1.f, -1.f);
    normalize_set_v3f(&light->direction);
    vector3f_set_3f(&light->up, 0.f, 0.f, 1.f);
    light->diffuse.data[0] = light->diffuse.data[1] = light->diffuse.data[2] =
    light->diffuse.data[3] = 1.f;
    light->specular = light->diffuse;
    light->ambient = light->diffuse;
    light->type = LIGHT_TYPE_DIRECTIONAL;
  }
}

// "http://stackoverflow.com/questions/12943164/replacement-for-gluperspective-with-glfrustrum"
void
setup_view_projection_pipeline(
  const level_context_t *context,
  pipeline_t *pipeline)
{
  float view_width = (float)context->viewport.width;
  float view_height = (float)context->viewport.height;
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
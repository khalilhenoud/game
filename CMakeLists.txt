cmake_minimum_required(VERSION 3.22)

# set the project name
project(game)

if (NOT HAS_STANDALONE_PREDECESSOR)
  set (HAS_STANDALONE_PREDECESSOR true)

  # all libraries built by this project are copied into the output directory.
  set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  # DOWNLOAD ALL THE SUBMODULES
  find_package(Git QUIET)
  if (GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if (GIT_SUBMODULES)
      message(STATUS "Submodule update")
      execute_process(COMMAND $(GIT_EXECUTABLE) submodule update --init --recursive
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE GIT_SUBMOD_RESULT)
      if (NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}")
      endif()
    endif()
  endif()

  # CHECK ALL THE SUBMODULES
  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/windowing/CMakeLists.txt")
    message(FATAL_ERROR "The windowing submodule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/math/CMakeLists.txt")
    message(FATAL_ERROR "The math submodule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/collision/CMakeLists.txt")
    message(FATAL_ERROR "The collision submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/library/CMakeLists.txt")
    message(FATAL_ERROR "The library submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/entity/CMakeLists.txt")
    message(FATAL_ERROR "The entity submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/loaders/CMakeLists.txt")
    message(FATAL_ERROR "The loaders submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/renderer/CMakeLists.txt")
    message(FATAL_ERROR "The renderer submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/spatial/CMakeLists.txt")
    message(FATAL_ERROR "The spatial submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/texture/CMakeLists.txt")
    message(FATAL_ERROR "The texture submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/material/CMakeLists.txt")
    message(FATAL_ERROR "The material submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/mesh/CMakeLists.txt")
    message(FATAL_ERROR "The mesh submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/level/CMakeLists.txt")
    message(FATAL_ERROR "The level submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/font/CMakeLists.txt")
    message(FATAL_ERROR "The font submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/elements/CMakeLists.txt")
    message(FATAL_ERROR "The elements submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/skeleton/CMakeLists.txt")
    message(FATAL_ERROR "The skeleton submdule was not downloaded!")
  endif()

  if (NOT EXISTS "${PROJECT_SOURCE_DIR}/external/animation/CMakeLists.txt")
    message(FATAL_ERROR "The animation submdule was not downloaded!")
  endif()

  add_subdirectory(external/windowing windowing)
  add_subdirectory(external/library library)
  add_subdirectory(external/math math)
  add_subdirectory(external/collision collision)
  add_subdirectory(external/entity entity)
  add_subdirectory(external/loaders loaders)
  add_subdirectory(external/renderer renderer)
  add_subdirectory(external/spatial spatial)
  add_subdirectory(external/texture texture)
  add_subdirectory(external/material material)
  add_subdirectory(external/mesh mesh)
  add_subdirectory(external/level level)
  add_subdirectory(external/font font)
  add_subdirectory(external/elements elements)
  add_subdirectory(external/skeleton skeleton)
  add_subdirectory(external/animation animation)
endif()

# add the executable
add_library(${PROJECT_NAME} SHARED
      ./source/memory_tracking/memory_tracking.cpp
      ./source/rendering/load_font.c
      ./source/rendering/load_image.c
      ./source/rendering/render_data.c
      ./source/rendering/render.c
      ./source/logic/player.c
      ./source/logic/camera.c
      ./source/logic/collision_utils.c
      ./source/logic/bucket_processing.c
      ./source/debug/color.c
      ./source/debug/text.c
      ./source/debug/face.c
      ./source/debug/flags.c
      ./source/levels/anim_preview.c
      ./source/levels/generic_level.c
      ./source/levels/room_select.c
      ./source/levels/utils.c
      ./source/input/input.c
      ./source/game.c
      ./include/game/internal/module.h)

target_link_libraries(${PROJECT_NAME}
						PUBLIC library
						PUBLIC math
            PUBLIC collision
            PUBLIC spatial
            PUBLIC texture
						PUBLIC material
            PUBLIC mesh
            PUBLIC level
            PUBLIC font
						PUBLIC elements
            PUBLIC skeleton
            PUBLIC animation
            PRIVATE windowing
						PRIVATE entity
						PRIVATE loaders
						PRIVATE renderer)

target_include_directories(${PROJECT_NAME} PUBLIC
							"${PROJECT_BINARY_DIR}"
							"${PROJECT_SOURCE_DIR}/include"
							)
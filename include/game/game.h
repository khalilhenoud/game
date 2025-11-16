/**
 * @file game.h
 * @author khalilhenoud@gmail.com
 * @brief 
 * @version 0.1
 * @date 2025-11-08
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef GAME_H
#define GAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <game/internal/module.h>


GAME_API
void 
game_init(
  int32_t width,
  int32_t height,
  const char *data_set);

GAME_API
void
game_cleanup();

GAME_API
uint64_t
game_update();

#ifdef __cplusplus
}
#endif

#endif
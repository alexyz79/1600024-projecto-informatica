#include "maze_logic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef STATS_GEN
#  include <time.h>
#endif

void update_neighbors(maze_solver_t* maze_solver,
                      coord new_position,
                      board_data_t board_data,
                      state_allocator_t* allocator,
                      linked_list_t* neighbors)
{
  char tmp_board[maze_solver->board_len];

  // Copia a configuração do tabuleiro atual
  memcpy(&tmp_board, board_data.board, maze_solver->board_len);

  // Iniciamos um novo tabuleiro
  maze_solver_state_t new_board;
  new_board.maze_solver = maze_solver;

  int index = new_position.row * maze_solver->cols + new_position.col;
  if(board_data.board[index] != '.')
  {
    // não podemos colocar um link na coordenada passada, este estado não
    // é valido
    return;
  }
  // Colocamos a respetiva letra no tabuleiro
  tmp_board[index] = 'c';

  // Alocamos o tabuleiro
  new_board.board_data = maze_solver_create_board(maze_solver, (const char*)&tmp_board, new_position);
  state_t* new_state = state_allocator_new(allocator, &new_board);
  linked_list_append(neighbors, new_state);
}

// Função de heurística para o puzzle 8
int heuristic(const state_t* current_state, const state_t*)
{
  // Converter os estados para puzzle_state
  maze_solver_state_t* state = (maze_solver_state_t*)(current_state->data);
  maze_solver_t* maze_solver = state->maze_solver;
  board_data_t board_data = maze_solver_wrap_board(maze_solver, state->board_data);

  int h = abs(board_data.position.col - maze_solver->exit_coord.col) + abs(board_data.position.row - maze_solver->exit_coord.row);
  return h;
}

void visit(state_t* current_state, state_allocator_t* allocator, linked_list_t* neighbors)
{
  maze_solver_state_t* state = (maze_solver_state_t*)current_state->data;
  board_data_t board_data = maze_solver_wrap_board(state->maze_solver, state->board_data);

  int up, down, left, right, col, row;

  // Lemos as coordenadas atuais e movemos cada um dos links
  row = down = up = board_data.position.row;
  col = right = left = board_data.position.col;

  down++;
  up--;
  right++;
  left--;

  // Verifica se o movimento para baixo é válido
  if(down > -1 && down < state->maze_solver->rows)
  {
    coord new_position = { col, down };
    update_neighbors(state->maze_solver, new_position, board_data, allocator, neighbors);
  }

  // Verifica se o movimento para cima é válido
  if(up > -1 && up < state->maze_solver->rows)
  {
    coord new_position = { col, up };
    update_neighbors(state->maze_solver, new_position, board_data, allocator, neighbors);
  }

  // Verifica se o movimento para a esquerda é válido
  if(left > -1 && left < state->maze_solver->cols)
  {
    coord new_position = { left, row };
    update_neighbors(state->maze_solver, new_position, board_data, allocator, neighbors);
  }

  // Verifica se o movimento para a direita é válido
  if(right > -1 && right < state->maze_solver->cols)
  {
    coord new_position = { right, row };
    update_neighbors(state->maze_solver, new_position, board_data, allocator, neighbors);
  }
}

// Verifica se um estado é um objetivo do problema number link
bool goal(const state_t* state_a, const state_t*)
{
  maze_solver_state_t* state = (maze_solver_state_t*)state_a->data;
  board_data_t board_data = maze_solver_wrap_board(state->maze_solver, state->board_data);

  return (memcmp(&board_data.position, &(state->maze_solver->exit_coord), sizeof(coord)) == 0);
}

int distance(const state_t*, const state_t*)
{
  return 1;
}

#ifdef STATS_GEN
void print_stats(const state_t* current_state, struct timespec* start_timestamp, int type)
{
  struct timespec timestamp;

  maze_solver_state_t* state = (maze_solver_state_t*)current_state->data;
  board_data_t board_data = maze_solver_wrap_board(state->maze_solver, state->board_data);

  // Print entry for video generation
  clock_gettime(CLOCK_MONOTONIC, &timestamp);
  double frametime = (timestamp.tv_sec - start_timestamp->tv_sec);
  frametime += (timestamp.tv_nsec - start_timestamp->tv_nsec) / 1000000000.0;

  char buffer[65535];
  int position = 0;
  for(int y = 0; y < state->maze_solver->rows; y++)
  {
    for(int x = 0; x < state->maze_solver->cols; x++)
    {
      int index = y * state->maze_solver->cols + x;
      if(board_data.board[index] != 'c')
      {
        continue;
      };
      if(position > 0)
      {
        position += sprintf(buffer + position, ",");
      }
      position += sprintf(buffer + position, "[%d,%d]", x, y);
    }
  }

  switch(type)
  {
    case 0:
      printf("{\"frametime\":%.9f,\"type\":\"visited\",\"position\":[%d,%d],\"path\":[%s]},\n",
             frametime,
             board_data.position.col,
             board_data.position.row,
             buffer);
      break;
    case 1:
      printf("{\"frametime\":%.9f,\"type\":\"goal\",\"position\":[%d,%d]},\n",
             frametime,
             board_data.position.col,
             board_data.position.row);
      break;
    case 2:
      printf("{\"frametime\":%.9f,\"type\":\"sucessor\",\"position\":[%d,%d],\"path\":[%s]},\n",
             frametime,
             board_data.position.col,
             board_data.position.row,
             buffer);
      break;
    default: break;
  }
}
#endif
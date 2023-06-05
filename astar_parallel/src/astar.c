#include "astar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Estrutura que contem a mensagem a ser passada nas queues
typedef struct
{
  a_star_node_t* node;
  int old_cost;
  int cost;
} a_star_message_t;

// Função utilizada na hashtable nodes para comparar se 2 nós são iguais
// è necessário comparar apenas os state_t
bool compare_a_star_nodes(const void* node_a, const void* node_b)
{
  size_t struct_size_a = ((a_star_node_t*)node_a)->state->struct_size;
  size_t struct_size_b = ((a_star_node_t*)node_b)->state->struct_size;

  // Os tamanhos tem de ser iguais
  if(struct_size_a != struct_size_b)
    return false;

  // Compara os estados
  void* state_a = ((a_star_node_t*)node_a)->state->data;
  void* state_b = ((a_star_node_t*)node_b)->state->data;
  return memcmp(state_a, state_b, struct_size_a) == 0;
}

// Função para calcular o índice do bucket na hashtable
// Tal como a comparação esta função tem de ser especifica para
// esta estrutura já que é necessário usar os dados do estado
// para gerar o índice do hash
static size_t a_star_nodes_hash(hashtable_t* hashtable, const void* data)
{
  a_star_node_t* node = (a_star_node_t*)data;

  // Cast dos dados para um array de bytes
  const unsigned char* bytes = (const unsigned char*)(node->state->data);

  // Calcula o valor hash inicial
  size_t hash_value = 0;
  for(size_t i = 0; i < node->state->struct_size; ++i)
  {
    hash_value = hash_value * 31 + bytes[i];
  }

  // Retorna o índice do bucket
  return hash_value % hashtable->capacity;
}

// Função para encontrar qual a tarefa para qual devemos enviar o state_t
static size_t assign_to_worker(const a_star_t* a_star, const state_t* state)
{
  // Cast dos dados para um array de bytes
  const unsigned char* bytes = (const unsigned char*)(state->data);

  // Calcula o valor hash inicial
  size_t hash_value = 0;
  for(size_t i = 0; i < state->struct_size; ++i)
  {
    hash_value = hash_value * 31 + bytes[i];
  }

  // Retorna o qual o worker que deve ficar com este estado
  return hash_value % a_star->num_workers;
}

// Função interna para criar um nó a colocar na fila prioritária
a_star_node_t* a_star_new_node(a_star_t* a_star, state_t* state)
{
  if(a_star == NULL)
  {
    return NULL;
  }

  a_star_node_t* node = (a_star_node_t*)allocator_alloc(a_star->node_allocator);
  node->parent = NULL;
  node->g = 0;
  node->h = 0;
  node->state = state;
  node->visited = false;

  // Indexamos este nó
  hashtable_insert(a_star->nodes, node);

  return node;
}

// Função de cada trabalhador
void* a_star_worker_function(void* arg)
{
  a_star_worker_t* worker = (a_star_worker_t*)arg;
  a_star_t* a_star = worker->a_star;

  printf("Trabalhador %d está em execução\n", worker->thread_id);

  for(;;)
  {
    // Ja temos nos na nossa lista aberta, processamos 1 nó
    if(worker->open_set->size)
    {
      // A seguinte operação pode ocorrer em O(log(N))
      // se nosAbertos é um min-heap ou uma queue prioritária
      heap_node_t top_element = min_heap_pop(worker->open_set);

      // Nó atual na nossa árvore
      a_star_node_t* current_node = (a_star_node_t*)top_element.data;
      current_node->visited = true;

      // Atualizamos a que distancia estamos
      if(a_star->solution != NULL)
      {
        if(current_node->g > a_star->solution->g)
        {
          // Este worker está a procurar por soluções que já se encontram a uma distância maior
          // do que a solução já encontrada, será que vale a pena continuar? Saimos.
          break;
        }
      }

      // Se encontramos o objetivo saímos e retornamos o nó
      if(a_star->goal_func(current_node->state, a_star->goal_state))
      {
        // Temos de informar que encontramos o nosso objetivo
        pthread_mutex_lock(&(a_star->lock));
        if(a_star->solution == NULL)
        {
          // Esta é a primeira solução encontrada nada de especial
          // a fazer
          a_star->solution = current_node;
        }
        else
        {
          // Já existe uma solução, temos de verificar se esta nova
          // solução tem um custo menor
          int existing_cost = a_star->solution->g + a_star->solution->h;
          int attempt_cost = current_node->g + current_node->h;
          if(existing_cost > attempt_cost)
          {
            a_star->solution = current_node;
          }
        }
        pthread_mutex_unlock(&(a_star->lock));
        // Esta thread ja fez o seu trabalho podemos parar
        break;
      }

      // Esta lista irá receber os vizinhos deste nó
      linked_list_t* neighbors = linked_list_create();

      // Executa a função que visita os vizinhos deste nó
      a_star->visit_func(current_node->state, a_star->state_allocator, neighbors);

      // Itera por todos os vizinhos expandidos e envia para a devida tarefa
      for(size_t i = 0; i < linked_list_size(neighbors); i++)
      {
        state_t* neighbor = (state_t*)linked_list_get(neighbors, i);
        size_t worker_id = assign_to_worker(a_star, neighbor);

        // Verifica se o nó para este estado já se encontra na nossa lista de nós
        a_star_node_t temp_node = { 0, 0, NULL, neighbor, false,PTHREAD_MUTEX_INITIALIZER};
        a_star_node_t* neighbor_node = hashtable_contains(a_star->nodes, &temp_node);

        // Preparamos a mensagem a ser enviada
        a_star_message_t* message = (a_star_message_t*)malloc(sizeof(a_star_message_t));
        if(message == NULL)
        {
          break;
        }

        message->old_cost = 0;

        if(neighbor_node != NULL)
        {
          // Este nó já existe, temos de garantir que não existe concorrência
          pthread_mutex_lock(&(neighbor_node->lock));
          // Não processamos nós visitados, estes já estão fora da fila prioritária
          if(neighbor_node->visited)
          {
            free(message);
            pthread_mutex_unlock(&(neighbor_node->lock));
            continue;
          }

          // Encontra o custo de chegar do nó a este vizinho
          int g_attempt = current_node->g + a_star->d_func(current_node->state, neighbor_node->state);

          // Se o custo for maior do que o nó já tem, não faz sentido atualizar
          // existe outro caminho mais curto para este nó
          if(g_attempt >= neighbor_node->g)
          {
            free(message);
            pthread_mutex_unlock(&(neighbor_node->lock));
            continue;
          }

          // Este nó já ex
          // O nó atual é o caminho mais curto para este vizinho, atualizamos
          neighbor_node->parent = current_node;

          // Guardamos o custo atual
          message->old_cost = neighbor_node->g + neighbor_node->h;

          // Atualizamos os parâmetros do nó
          neighbor_node->g = g_attempt;
          neighbor_node->h = a_star->h_func(neighbor_node->state, a_star->goal_state);
          pthread_mutex_unlock(&(neighbor_node->lock));
        }
        else
        {
          // Este nó ainda não existe, criamos um novo nó
          neighbor_node = a_star_new_node(a_star, neighbor);
          neighbor_node->parent = current_node;

          // Encontra o custo de chegar do nó a este vizinho e calcula a heurística para chegar ao objetivo
          neighbor_node->g = current_node->g + a_star->d_func(current_node->state, neighbor_node->state);
          neighbor_node->h = a_star->h_func(neighbor_node->state, a_star->goal_state);
        }

        // Anexamos o nó e o novo custo à mensagem
        message->node = neighbor_node;
        message->cost = neighbor_node->g + neighbor_node->h;

        // Enviamos a mensagem para o respetivo trabalhador
        channel_send(a_star->channel, worker_id, message);
      }

      // Liberta a lista de vizinhos
      linked_list_destroy(neighbors);
    }

    // Processamos todos os estados que estão no canal para esta tarefa
    // é aqui que ocorre a atualização do custo do algoritmo
    for(;;)
    {
      a_star_message_t* message = channel_receive(a_star->channel, worker->thread_id);
      if(message == NULL)
      {
        // Não existem mensagens para este trabalhado
        break;
      }

      // Retiramos os dados da mensagem e libertamos a memória;
      a_star_node_t* node = message->node;
      int old_cost = message->old_cost;
      int cost = message->cost;
      free(message);

      // Atualizamos ou inserimos este nó na nossa lista aberta
      if(old_cost > 0)
      {
        min_heap_update(worker->open_set, old_cost, cost, node);
      }
      else
      {
        min_heap_insert(worker->open_set, cost, node);
      }
    }
  }

  printf("Trabalhador %d finalizou\n", worker->thread_id);
  pthread_exit(NULL);
}

// Cria uma nova instância para resolver um problema
a_star_t* a_star_create(size_t struct_size,
                        goal_function goal_func,
                        visit_function visit_func,
                        heuristic_function h_func,
                        distance_function d_func,
                        int num_workers)
{
  a_star_t* a_star = (a_star_t*)malloc(sizeof(a_star_t));
  if(a_star == NULL)
  {
    return NULL; // Erro de alocação
  }

  // Inicializa os nossos alocadores de memória
  a_star->state_allocator = state_allocator_create(struct_size);
  if(a_star->state_allocator == NULL)
  {
    a_star_destroy(a_star);
    return NULL;
  }

  a_star->node_allocator = allocator_create(sizeof(a_star_node_t));
  if(a_star->node_allocator == NULL)
  {
    a_star_destroy(a_star);
    return NULL;
  }

  // Para mantermos controlo dos nós que já foram expandidos
  a_star->nodes = hashtable_create(sizeof(a_star_node_t), compare_a_star_nodes, a_star_nodes_hash);
  if(a_star->nodes == NULL)
  {
    a_star_destroy(a_star);
    return NULL;
  }

  // Criamos um canal para que as threads possam comunicar
  a_star->channel = channel_create(num_workers);
  if(a_star->channel == NULL)
  {
    a_star_destroy(a_star);
    return NULL;
  }

  a_star->num_workers = num_workers;
  a_star->workers = (a_star_worker_t*)malloc(num_workers * sizeof(a_star_worker_t));
  if(a_star->workers == NULL)
  {
    a_star_destroy(a_star);
    return NULL;
  }

  for(int i = 0; i < a_star->num_workers; i++)
  {
    a_star->workers[i].a_star = a_star;
    a_star->workers[i].thread_id = i;
    a_star->workers[i].open_set = min_heap_create();
  }

  // Inicializa as funções necessárias para o algoritmo funcionar
  a_star->visit_func = visit_func;
  a_star->goal_func = goal_func;
  a_star->h_func = h_func;
  a_star->d_func = d_func;
  a_star->solution = NULL;
  a_star->goal_state = NULL;

  return a_star;
}

// Liberta uma instância do algoritmo A*
void a_star_destroy(a_star_t* a_star)
{
  if(a_star == NULL)
  {
    return;
  }

  if(a_star->workers != NULL)
  {
    for(int i = 0; i < a_star->num_workers; i++)
    {
      min_heap_destroy(a_star->workers[i].open_set);
    }
  }

  if(a_star->channel != NULL)
  {
    channel_destroy(a_star->channel, true);
  }

  if(a_star->nodes != NULL)
  {
    hashtable_destroy(a_star->nodes, false);
  }

  if(a_star->state_allocator != NULL)
  {
    state_allocator_destroy(a_star->state_allocator);
  }

  if(a_star->node_allocator != NULL)
  {
    allocator_destroy(a_star->node_allocator);
  }

  // Destruímos o nosso algoritmo
  free(a_star);
}

// Resolve o problema através do uso do algoritmo A*;
a_star_node_t* a_star_solve(a_star_t* a_star, void* initial, void* goal)
{
  if(a_star == NULL)
  {
    return NULL;
  }

  // Guarda os nossos estados inicial e objetivo
  state_t* initial_state = state_allocator_new(a_star->state_allocator, initial);

  a_star_node_t* initial_node = a_star_new_node(a_star, initial_state);

  // Preparamos o nosso objetivo
  if(goal)
  {
    // Caso tenhamos passado o goal, temos de o "containerizar" num estado
    a_star->goal_state = state_allocator_new(a_star->state_allocator, goal);
  }

  // Atribui ao nó inicial um custo total de 0
  initial_node->g = 0;
  initial_node->h = a_star->h_func(initial_node->state, a_star->goal_state);

  // Preparamos a mensagem a ser enviada
  a_star_message_t* message = (a_star_message_t*)malloc(sizeof(a_star_message_t));
  if(message == NULL)
  {
    return NULL;
  }

  message->old_cost = 0;
  message->cost = initial_node->g + initial_node->h;
  message->node = initial_node;

  // Enviamos o estado inicial para o respetivo trabalhador
  size_t worker_id = assign_to_worker(a_star, initial_state);
  channel_send(a_star->channel, worker_id, initial_node);

  for(int i = 0; i < a_star->num_workers; i++)
  {
    // Iniciamos o worker
    int result = pthread_create(&(a_star->workers[i].thread), NULL, a_star_worker_function, &(a_star->workers[i]));
    if(result != 0)
    {
      printf("Erro a iniciar o trabalhador %d\n", i);
      return NULL;
    }
  }

  // Esperamos que todas os trabalhadores terminem
  for(int i = 0; i < a_star->num_workers; i++)
  {
    pthread_join(a_star->workers[i].thread, NULL);
  }

  return a_star->solution;
}

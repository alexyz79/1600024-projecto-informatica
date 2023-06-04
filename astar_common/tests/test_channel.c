#include "channel.h"
#include <check.h>
#include <stdlib.h>

// Teste de criação do canal
START_TEST(test_channel_create)
{
  size_t num_queues = 5;
  channel_t* channel = channel_create(num_queues);

  ck_assert_ptr_nonnull(channel);
  ck_assert_uint_eq(channel->num_queues, num_queues);

  channel_destroy(channel);
}
END_TEST

// Teste de envio e recebimento de mensagens no canal
START_TEST(test_channel_send_receive)
{
  size_t num_queues = 2;
  channel_t* channel = channel_create(num_queues);

  int data1 = 10;
  int data2 = 20;

  // Envia mensagens para a primeira fila
  channel_send(channel, 0, &data1);
  channel_send(channel, 0, &data2);

  // Recebe as mensagens da primeira fila
  int* received1 = (int*)channel_receive(channel, 0);
  int* received2 = (int*)channel_receive(channel, 0);

  ck_assert_ptr_nonnull(received1);
  ck_assert_ptr_nonnull(received2);
  ck_assert_int_eq(*received1, data1);
  ck_assert_int_eq(*received2, data2);

  channel_destroy(channel);
}
END_TEST

// Função principal de teste
int main(void)
{
  Suite* suite = suite_create("Channel");
  TCase* testcase = tcase_create("Core");

  // Adiciona os testes ao caso de teste
  tcase_add_test(testcase, test_channel_create);
  tcase_add_test(testcase, test_channel_send_receive);

  // Adiciona o caso de teste à suíte
  suite_add_tcase(suite, testcase);

  // Cria um corredor de teste
  SRunner* runner = srunner_create(suite);

  // Executa os testes
  srunner_run_all(runner, CK_NORMAL);

  // Armazena o número de falhas
  int num_failed = srunner_ntests_failed(runner);

  // Liberta o teste runner
  srunner_free(runner);

  // Retorna o código de saída com base no número de falhas
  return (num_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

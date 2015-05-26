#include <iostream>
#include "AMQPConsumer.h"

using namespace std;

void *AMQPConsumer::threadMain(void *pThis) {
  AMQPConsumer *pt = static_cast<AMQPConsumer*>(pThis);

  pt->connect();

  while(1) {
    pt->consume();
  }

  return NULL;
}

AMQPConsumer::AMQPConsumer() {

}

AMQPConsumer::~AMQPConsumer() {
  DLOG(INFO) << "AMQPConsumer destructor called.";
  pthread_cancel(thread);
}

void AMQPConsumer::start(string host, int port, string user, string password, 
  string routingkey, void(*callback)(void*, string), void* callbackArg) {
  this->host = host;
  this->port = port;
  this->user = user;
  this->password = password;
  this->routingkey = routingkey;
  this->callback = callback;
  this->callbackArg = callbackArg; 

    pthread_create(&thread, NULL, &AMQPConsumer::threadMain, this);
}

bool AMQPConsumer::connect() {
  int ret;

  // Initialize connection.
  amqpConn = amqp_new_connection();

  amqpSocket = amqp_tcp_socket_new(amqpConn);
  LOG_IF(FATAL, !amqpSocket) << "Error creating amqp socket.";

  ret = amqp_socket_open(amqpSocket, host.c_str(), port);
  LOG_IF(FATAL, ret) << "Error creating amqp tcp socket.";

  amqp_rpc_reply_t login = amqp_login(amqpConn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, 
    user.c_str(), password.c_str());

  LOG_IF(FATAL, AMQP_RESPONSE_NORMAL != login.reply_type) << "Failed to log into AMQP.";

  amqp_channel_open(amqpConn, 1);
  LOG_IF(FATAL, amqp_get_rpc_reply(amqpConn).reply_type != AMQP_RESPONSE_NORMAL) << "Failed to open channel.";

  // Declare queue.
  amqp_queue_declare_ok_t *r = amqp_queue_declare(amqpConn, 1, amqp_empty_bytes, 0, 0, 0, 1,
                                 amqp_empty_table);
  LOG_IF(FATAL, amqp_get_rpc_reply(amqpConn).reply_type != AMQP_RESPONSE_NORMAL) << "Failed to declare queue.";

  amqpQueueName = amqp_bytes_malloc_dup(r->queue);
  LOG_IF(FATAL, amqpQueueName.bytes == NULL) << "Out memory while copying queue name";

  // Bind queue.
  amqp_queue_bind(amqpConn, 1, amqpQueueName, amqp_cstring_bytes("amq.fanout"), 
    amqp_cstring_bytes(routingkey.c_str()), amqp_empty_table);

  LOG_IF(FATAL, amqp_get_rpc_reply(amqpConn).reply_type != AMQP_RESPONSE_NORMAL) << "Failed to bind to queue.";

  DLOG(INFO) << "Bound to queue " << (char*)amqpQueueName.bytes;

  return true;
}

void AMQPConsumer::consume() {
  amqp_envelope_t envelope;
  amqp_rpc_reply_t ret;

  amqp_basic_consume(amqpConn, 1, amqpQueueName, amqp_empty_bytes, 0, 1, 0, amqp_empty_table);
  LOG_IF(FATAL, amqp_get_rpc_reply(amqpConn).reply_type != AMQP_RESPONSE_NORMAL) << "Failed to consume.";

  // Free up memory pool.
  amqp_maybe_release_buffers(amqpConn);
  
  ret = amqp_consume_message(amqpConn, &envelope, NULL, 0);

  if (ret.reply_type != AMQP_RESPONSE_NORMAL) {
    LOG(ERROR) << "Error consuming message.";
  } else {
   string msg;
   msg.insert(0, (const char*)envelope.message.body.bytes, envelope.message.body.len);
   callback(callbackArg, msg);
  }

  amqp_destroy_envelope(&envelope);
}
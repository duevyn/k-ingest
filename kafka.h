#ifndef KAFKA_H
#define KAFKA_H

#include <stddef.h>
#include <stdint.h>
#include <librdkafka/rdkafka.h>

typedef struct kafka {
	rd_kafka_t *rk; // The Producer Instance
	rd_kafka_topic_t *rkt; // The Topic Instance
	rd_kafka_conf_t *conf; // Global Config
	char *tpc;
} kafka;

struct kafka *kafka_init(char *brokers, char *topic);
void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage,
	       void *opaque);
int kfk_produce(struct kafka *kf, const void *pyld, size_t len, char *tp);
void kfk_poll(struct kafka *kf);
void kfk_destroy(struct kafka *kf);

#endif

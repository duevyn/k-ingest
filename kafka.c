#include "kafka.h"
#include <stdlib.h>

/**
 * @brief Message delivery report callback.
 *
 * This callback is called exactly once per message, indicating if
 * the message was succesfully delivered
 * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) or permanently
 * failed delivery (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
 *
 * The callback is triggered from rd_kafka_poll() and executes on
 * the application's thread.
 */

void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage,
	       void *opaque)
{
	if (rkmessage->err)
		fprintf(stderr, "%% Message delivery failed: %s\n",
			rd_kafka_err2str(rkmessage->err));
	else
		fprintf(stderr,
			"%% Message delivered (%zd bytes, "
			"partition %" PRId32 ")\n",
			rkmessage->len, rkmessage->partition);

	/* The rkmessage is destroyed automatically by librdkafka */
}

int setconf(struct kafka *kf, char *brokers)
{
	char errstr[512]; /* librdkafka API error reporting buffer */
	if (rd_kafka_conf_set(kf->conf, "bootstrap.servers", brokers, errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		fprintf(stderr, "%s\n", errstr);
		return -1;
	}

	rd_kafka_conf_set_dr_msg_cb(kf->conf, dr_msg_cb);

	// Batch Size: 256KB (Default is usually 16KB).
	// We want to accumulate massive chunks from the Ring Buffer before sending.
	if (rd_kafka_conf_set(kf->conf, "batch.size", "262144", errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		fprintf(stderr, "[Kafka Config] %s\n", errstr);
	}

	// Linger: 5ms.
	// Wait up to 5ms to fill the batch. This trades tiny latency for huge compression wins.
	if (rd_kafka_conf_set(kf->conf, "linger.ms", "5", errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		fprintf(stderr, "[Kafka Config] %s\n", errstr);
	}

	// Compression: LZ4.
	// Fast compression with low CPU overhead. Essential for IoT/Text data.
	if (rd_kafka_conf_set(kf->conf, "compression.codec", "lz4", errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		fprintf(stderr, "[Kafka Config] %s\n", errstr);
	}

	// Acks: 1 (Leader Only).
	// Faster than 'all'. Sufficient for Mempool/Telemetry data.
	if (rd_kafka_conf_set(kf->conf, "acks", "1", errstr, sizeof(errstr)) !=
	    RD_KAFKA_CONF_OK) {
		fprintf(stderr, "[Kafka Config] %s\n", errstr);
	}
	return 0;
}

struct kafka *kafka_init(char *brokers, char *topic)
{
	struct kafka *kf = malloc(sizeof(*kf));
	kf->tpc = topic;
	char errstr[512]; /* librdkafka API error reporting buffer */
	char buf[512]; /* Message value temporary buffer */

	kf->conf = rd_kafka_conf_new();
	if (setconf(kf, brokers)) {
		return NULL;
	}
	if (!(kf->rk = rd_kafka_new(RD_KAFKA_PRODUCER, kf->conf, errstr,
				    sizeof(errstr)))) {
		fprintf(stderr, "%% Failed to create new producer: %s\n",
			errstr);
		return NULL;
	}

	return kf;
}

int kfk_produce(struct kafka *kf, const void *pyld, size_t len, char *tp)
{
	rd_kafka_resp_err_t err;

retry:
	err = rd_kafka_producev(
		kf->rk, RD_KAFKA_V_TOPIC(tp),
		RD_KAFKA_V_MSGFLAGS(
			RD_KAFKA_MSG_F_COPY), // COPY strategy (Safe for RingBuffers)
		RD_KAFKA_V_VALUE((void *)pyld, len), RD_KAFKA_V_OPAQUE(NULL),
		RD_KAFKA_V_END);

	if (err) {
		if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
			// TODO: decide how to hande full queue

			// BACKPRESSURE SIGNAL
			// Return -1 to tell Main Loop: "Stop reading from sockets, I am full"
			//
			//

			/* If the internal queue is full, wait for
                        * messages to be delivered and then retry.
                        * The internal queue represents both
                        * messages to be sent and messages that have
                        * been sent or failed, awaiting their
                        * delivery report callback to be called.
                        *
                        * The internal queue is limited by the
                        * configuration property
                        * queue.buffering.max.messages and
                        * queue.buffering.max.kbytes 
                        *
                        *
                        * */

			// rd_kafka_poll(kf->rk, 1000 /*block for max 1000ms*/);
			// goto retry;

			return -1;
		}

		fprintf(stderr, "[Kafka Produce] Failed to enqueue: %s\n",
			rd_kafka_err2str(err));
		return -1; // General error
	}
	return err;
}

void kfk_poll(struct kafka *kf)
{
	if (kf && kf->rk) {
		// Non-blocking poll to trigger callbacks
		rd_kafka_poll(kf->rk, 0);
	}
}

void kfk_destroy(struct kafka *kf)
{
	if (kf) {
		fprintf(stderr, "[Kafka] Flushing messages...\n");
		// Wait up to 10 seconds for internal queue to drain
		rd_kafka_flush(kf->rk, 10 * 1000);
		if (rd_kafka_outq_len(kf->rk) > 0)
			fprintf(stderr, "%% %d message(s) were not delivered\n",
				rd_kafka_outq_len(kf->rk));
		rd_kafka_destroy(kf->rk);
		free(kf);
		fprintf(stderr, "[Kafka] Destroyed.\n");
	}
}

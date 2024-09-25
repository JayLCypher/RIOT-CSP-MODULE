
#define ENABLE_DEBUG 1
#include "debug.h"
#include "csp.h"

extern long random(void);

#if __STDC_VERSION__ > 201710L
#define maybe_unused [[maybe_unused]]
#else
#define maybe_unused
typedef void* nullptr_t;
#define nullptr (nullptr_t)0
#endif

#define PLEXER_COUNT 5

struct packet {
	int id;
	unsigned char data[64];
};

static const char *packet_data_table[] = {
	"packet_1", "packet_2", "packet_3", "packet_4", "packet_5",
};
constexpr size_t packet_table_count = (sizeof (packet_data_table) / sizeof (*packet_data_table));
constexpr size_t packet_strlen = 9;

void *packet_plexer(void *args, channel *c) {
	(void)args;
	size_t stream_count = 0;
	channel_recv(c, &stream_count);
	if (stream_count == 0) {
		channel_close(c);
		return nullptr;
	}
	DEBUG("%s: Stream count is %zu\n", __func__, stream_count);

	channel *streams[stream_count] = {}; // VLA
	for (size_t i = 0; i != stream_count; ++i) {
		channel_recv(c, &streams[i]);
		streams[i]->creator = thread_getpid();
		DEBUG("%s: Stream %p received.\n", __func__, ((void*){0} = &streams[i]));
	}
	DEBUG("%s: Streams received.\n", __func__);

	struct packet p = {-1, {0}};
	while (true) {
		channel_recv(c, &p); // Get packet
		DEBUG("%s: Package received: {%d, %s}\n", __func__, p.id, p.data);
		// Verify packet
		if (p.id == -1) {
			// -1 is close everyone.
			for (size_t i = 0; i != stream_count; ++i) {
				channel_send(streams[i], &p, sizeof (p));
			}
			break;
		}
		if ((size_t)p.id >= stream_count) { goto defer; }
		// Pass it along
		DEBUG("%s: Sending on channel %p\n", __func__, ((void*){0} = &streams[p.id]));
		channel_send(streams[p.id], &p, sizeof(p));
		DEBUG("%s: Package sent to handler.\n", __func__);
	}
	DEBUG("%s: Finished plexing packets.\n", __func__);

defer:
	channel_close(c);
	DEBUG("%s:%zu: Thread %d terminated.\n", __func__, __LINE__, thread_getpid());
	return nullptr;
}

static void *packet_handler(void *args, channel *c)
{
	(void)args;

	struct packet p = {0};
	while (true) {
		channel_recv(c, &p);
		printf("%s ID %d: Received packet { %d, %s }\n", __func__, thread_getpid(), p.id, p.data);
		if (p.id == -1) { break; }
	}

	// HALT(__func__);
	DEBUG("%s:%zu: Thread %d terminated.\n", __func__, __LINE__, thread_getpid());
	channel_close(c);
	return nullptr;
}

static char procs_stacks[PLEXER_COUNT][THREAD_STACKSIZE_CSP] = {0};
int csp_plexer(void) {
	static channel c = {0};
	c = channel_make(&c, 1);

	MAYBE_UNUSED
	csp_ctx *plexer = GO(packet_plexer, nullptr, &c);

	constexpr size_t plexer_count = PLEXER_COUNT;
	channel_send(&c, &plexer_count, sizeof (plexer_count));

	static channel streams[PLEXER_COUNT] = {0};
	static channel *streams_ptr[PLEXER_COUNT] = {0};
	csp_ctx *procs[PLEXER_COUNT] = {0};
	for (size_t i = 0; i != PLEXER_COUNT; ++i) {
		streams[i] = channel_make(&streams[i], 1);
		streams_ptr[i] = &streams[i];
		// channel_set_buffering(&streams[i], true);
		channel_send(&c, &streams_ptr[i], sizeof (streams_ptr[i]));
		DEBUG("%s: Stream %p sent.\n", __func__, (void*){0} = &streams[i]);
		// For loops don't create new objects, need to have objects created somewhere else.
		procs[i] = csp_obj(procs_stacks[i], packet_handler, &streams[i], nullptr);
	}
	DEBUG("%s: Procs created, streams sent.\n", __func__);

	struct packet p = { -1, {0}};

	// while (true) {
	for (size_t count = 0; count != packet_table_count*PLEXER_COUNT; ++count) {
		p.id = ((unsigned long)random() % PLEXER_COUNT);
		long packet_choice = (unsigned long) random() % packet_table_count;
		for (size_t i = 0; i != packet_strlen; ++i) { p.data[i] = (const unsigned char)packet_data_table[packet_choice][i]; }
		channel_send(&c, &p, sizeof (p));
		DEBUG("%s: Package {%d, %s} sent to plexer.\n", __func__, p.id, p.data);
	}
	DEBUG("%s: Packages sent.\n", __func__);
	// HALT(__func__);

	struct packet end = { -1, {0}};
	channel_send(&c, &end, sizeof (end));

	while (true) {
		size_t count = 0;
		for (; count != PLEXER_COUNT; ++count) {
			if (csp_running(procs[count]) == false) {
				break;
			}
		}
		if (count == PLEXER_COUNT) { break; }
	}

	DEBUG("%s:%zu: Thread %d terminated.\n", __func__, __LINE__, thread_getpid());
	return 0;
}

extern void exit(int);
int main(void) {
	int ret = csp_plexer();

	puts("Press q to exit.\n");
	int ch = 0;
	while (true) {
		ch = getchar();
		switch (ch) {
			case 'q': {
				DEBUG("%s:%zu: Program exit.\n", __func__, __LINE__);
				puts("Exiting...");
				exit(ret);
			} break;
			default: break;
		}
	}
	DEBUG("%s:%zu: Thread %d terminated.\n", __func__, __LINE__, thread_getpid());
	return 0;
}

// A """simple""" example. :⁾
#include "csp.h"

void *hello(channel *c)
{
    int argc = 0;
    channel_recv(c, &argc);
    char buf[256] = {0};
    for (int i = 0; i != argc; ++i) {
        size_t str_len = 0;
        channel_recv(c, &str_len);
        if (str_len < 256) {
            channel_recv(c, buf);
        }
        else {
            channel_drop(c);
        }
    }
    channel_recv(c, nullptr); // Sync
    channel_close(c);
    return nullptr;
}

int main(const int argc, const char *argv[argc]) {
    channel c = channel_make(&c, 0);
    GO (hello, &c);
    channel_send(&c, &argc, sizeof (argc));
    for (int i = 0; i != argc; ++i) {
        size_t str_len = strlen(argv[i]);
        channel_send(&c, &str_len, sizeof (str_len));
        channel_send(&c, argv[i], str_len);
    }
    channel_send(&c, nullptr, 0); // Sync
    return 0;
}

#ifndef NETDATA_RRDPUSH_H
#define NETDATA_RRDPUSH_H

extern int default_rrdpush_enabled;
extern char *default_rrdpush_destination;
extern char *default_rrdpush_api_key;

extern int rrdpush_init();
extern void rrdset_done_push(RRDSET *st);
extern void *rrdpush_sender_thread(void *ptr);

extern int rrdpush_receiver_thread_spawn(RRDHOST *host, struct web_client *w, char *url);
extern void rrdpush_sender_thread_cleanup(RRDHOST *host);

#endif //NETDATA_RRDPUSH_H

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

/* ---- webview >= 0.12 C ABI (core/include/webview/api.h) ------------------ */
typedef void *webview_t;
extern webview_t webview_create(int debug, void *window);
extern int webview_destroy(webview_t w);
extern int webview_run(webview_t w);
extern int webview_terminate(webview_t w);
extern int webview_dispatch(webview_t w, void (*fn)(webview_t, void *), void *arg);
extern int webview_set_title(webview_t w, const char *title);
extern int webview_set_size(webview_t w, int width, int height, int hints);
extern int webview_navigate(webview_t w, const char *url);
extern int webview_set_html(webview_t w, const char *html);
extern int webview_init(webview_t w, const char *js);
extern int webview_eval(webview_t w, const char *js);
extern int webview_bind(webview_t w, const char *name,
                        void (*fn)(const char *id, const char *req, void *arg), void *arg);
extern int webview_unbind(webview_t w, const char *name);
extern int webview_return(webview_t w, const char *id, int status, const char *result);
typedef struct {
    struct { unsigned major, minor, patch; } version;
    char version_number[32];
    char pre_release[48];
    char build_metadata[48];
} hwv_version_info;
extern const hwv_version_info *webview_version(void);

#define HWV_ERR_INVALID_ARGUMENT (-2)

/* ---- data structures ------------------------------------------------------ */
typedef struct hwv_ev {
    struct hwv_ev *next;
    char *name, *id, *req;
} hwv_ev;

typedef struct hwv_bind_rec {
    struct hwv_bind_rec *next;
    struct hwv_ctx *ctx;
    char *name;
} hwv_bind_rec;

typedef struct hwv_ctx {
    webview_t w;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    hwv_ev *head, *tail;
    hwv_bind_rec *binds;
    int closed;     /* run() returned or destroy() started: no more events */
    int waiters;    /* threads currently inside hwv_next */
} hwv_ctx;

static char *dup_or_empty(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

#define CTX(h) ((hwv_ctx *)(intptr_t)(h))

/* ---- bind: JS -> queue ------------------------------------------------------ */
static void bind_cb(const char *id, const char *req, void *arg) {
    hwv_bind_rec *b = (hwv_bind_rec *)arg;
    hwv_ctx *c = b->ctx;
    hwv_ev *e = (hwv_ev *)calloc(1, sizeof *e);
    if (!e) return;
    e->name = dup_or_empty(b->name);
    e->id = dup_or_empty(id);
    e->req = dup_or_empty(req);
    pthread_mutex_lock(&c->mu);
    if (c->tail) c->tail->next = e; else c->head = e;
    c->tail = e;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->mu);
}

/* ---- lifecycle -------------------------------------------------------------- */
int64_t hwv_open(int32_t debug) {
    hwv_ctx *c = (hwv_ctx *)calloc(1, sizeof *c);
    if (!c) return 0;
    c->w = webview_create(debug, NULL);
    if (!c->w) { free(c); return 0; }
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->cv, NULL);
    return (int64_t)(intptr_t)c;
}

/* The raw webview_t, for mixing with the 1:1 `webview_*` bindings. */
int64_t hwv_handle(int64_t h) { return h ? (int64_t)(intptr_t)CTX(h)->w : 0; }

static void mark_closed(hwv_ctx *c) {
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->mu);
}

/* Blocks the calling (UI) thread until the window is closed / terminate() is
 * called, then wakes every poller with "closed". */
int32_t hwv_run(int64_t h) {
    if (!h) return HWV_ERR_INVALID_ARGUMENT;
    int32_t rc = webview_run(CTX(h)->w);
    mark_closed(CTX(h));
    return rc;
}

/* Safe from any thread. */
int32_t hwv_terminate(int64_t h) {
    if (!h) return HWV_ERR_INVALID_ARGUMENT;
    return webview_terminate(CTX(h)->w);
}

/* Wakes pollers, waits for them to leave hwv_next, then frees everything.
 * Call from the UI thread after hwv_run() returned. */
int32_t hwv_destroy(int64_t h) {
    if (!h) return HWV_ERR_INVALID_ARGUMENT;
    hwv_ctx *c = CTX(h);
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->cv);
    while (c->waiters > 0) pthread_cond_wait(&c->cv, &c->mu);
    pthread_mutex_unlock(&c->mu);
    int32_t rc = webview_destroy(c->w);
    while (c->head) {
        hwv_ev *e = c->head;
        c->head = e->next;
        free(e->name); free(e->id); free(e->req); free(e);
    }
    while (c->binds) {
        hwv_bind_rec *b = c->binds;
        c->binds = b->next;
        free(b->name); free(b);
    }
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->cv);
    free(c);
    return rc;
}

/* ---- bindings ------------------------------------------------------------------ */
static int32_t bind_impl(hwv_ctx *c, const char *name) {
    if (!c || !name || !*name) return HWV_ERR_INVALID_ARGUMENT;
    hwv_bind_rec *b = (hwv_bind_rec *)calloc(1, sizeof *b);
    if (!b) return -1;
    b->ctx = c;
    b->name = dup_or_empty(name);
    int32_t rc = webview_bind(c->w, name, bind_cb, b);
    if (rc != 0) { free(b->name); free(b); return rc; }
    pthread_mutex_lock(&c->mu);
    b->next = c->binds;
    c->binds = b;
    pthread_mutex_unlock(&c->mu);
    return 0;
}

/* Direct call: only on the UI thread (before run, or from a dispatched job).
 * From any other thread use hwv_post_bind. */
int32_t hwv_bind(int64_t h, const char *name) { return bind_impl(CTX(h), name); }

int32_t hwv_unbind(int64_t h, const char *name) {
    if (!h || !name) return HWV_ERR_INVALID_ARGUMENT;
    /* the record stays allocated until hwv_destroy: a call may still be in flight */
    return webview_unbind(CTX(h)->w, name);
}

/* Answers a JS call. status 0 = resolve(result), non-zero = reject(result).
 * `result` must be valid JSON. Safe from any thread. */
int32_t hwv_return(int64_t h, const char *id, int32_t status, const char *result) {
    if (!h || !id) return HWV_ERR_INVALID_ARGUMENT;
    return webview_return(CTX(h)->w, id, status, result ? result : "null");
}

/* ---- event queue ------------------------------------------------------------------- */
/* Waits up to timeout_ms (<0 = forever) for a JS call.
 *   > 0  pointer to an event (read it with hwv_ev_*, release with hwv_ev_free)
 *     0  timeout
 *    -1  the webview is closed and the queue is drained */
int64_t hwv_next(int64_t h, int32_t timeout_ms) {
    if (!h) return -1;
    hwv_ctx *c = CTX(h);
    struct timespec dl;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_sec += timeout_ms / 1000;
        dl.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    }
    int64_t out = 0;
    pthread_mutex_lock(&c->mu);
    c->waiters++;
    for (;;) {
        if (c->head) {
            hwv_ev *e = c->head;
            c->head = e->next;
            if (!c->head) c->tail = NULL;
            e->next = NULL;
            out = (int64_t)(intptr_t)e;
            break;
        }
        if (c->closed) { out = -1; break; }
        if (timeout_ms < 0) pthread_cond_wait(&c->cv, &c->mu);
        else if (pthread_cond_timedwait(&c->cv, &c->mu, &dl) == ETIMEDOUT) { out = 0; break; }
    }
    c->waiters--;
    pthread_cond_broadcast(&c->cv);   /* lets hwv_destroy see waiters == 0 */
    pthread_mutex_unlock(&c->mu);
    return out;
}

const char *hwv_ev_name(int64_t ev) { return ev > 0 ? ((hwv_ev *)(intptr_t)ev)->name : ""; }
const char *hwv_ev_id(int64_t ev)   { return ev > 0 ? ((hwv_ev *)(intptr_t)ev)->id   : ""; }
const char *hwv_ev_req(int64_t ev)  { return ev > 0 ? ((hwv_ev *)(intptr_t)ev)->req  : ""; }

void hwv_ev_free(int64_t ev) {
    if (ev <= 0) return;
    hwv_ev *e = (hwv_ev *)(intptr_t)ev;
    free(e->name); free(e->id); free(e->req); free(e);
}

/* ---- UI-thread operations from any thread (webview_dispatch) ---------------------- */
enum { OP_TITLE = 1, OP_SIZE, OP_NAVIGATE, OP_HTML, OP_INIT, OP_EVAL, OP_BIND, OP_UNBIND };
typedef struct { hwv_ctx *c; int op; char *s; int a, b, hint; } hwv_job;

static void job_cb(webview_t w, void *arg) {
    hwv_job *j = (hwv_job *)arg;
    switch (j->op) {
        case OP_TITLE:    webview_set_title(w, j->s); break;
        case OP_SIZE:     webview_set_size(w, j->a, j->b, j->hint); break;
        case OP_NAVIGATE: webview_navigate(w, j->s); break;
        case OP_HTML:     webview_set_html(w, j->s); break;
        case OP_INIT:     webview_init(w, j->s); break;
        case OP_EVAL:     webview_eval(w, j->s); break;
        case OP_BIND:     bind_impl(j->c, j->s); break;
        case OP_UNBIND:   webview_unbind(w, j->s); break;
    }
    free(j->s);
    free(j);
}

static int32_t post(int64_t h, int op, const char *s, int a, int b, int hint) {
    if (!h) return HWV_ERR_INVALID_ARGUMENT;
    hwv_job *j = (hwv_job *)calloc(1, sizeof *j);
    if (!j) return -1;
    j->c = CTX(h);
    j->op = op;
    j->s = dup_or_empty(s);
    j->a = a; j->b = b; j->hint = hint;
    int32_t rc = webview_dispatch(CTX(h)->w, job_cb, j);
    if (rc != 0) { free(j->s); free(j); }
    return rc;
}

int32_t hwv_post_title(int64_t h, const char *t)      { return post(h, OP_TITLE, t, 0, 0, 0); }
int32_t hwv_post_size(int64_t h, int32_t w, int32_t ht, int32_t hint) { return post(h, OP_SIZE, NULL, w, ht, hint); }
int32_t hwv_post_navigate(int64_t h, const char *url) { return post(h, OP_NAVIGATE, url, 0, 0, 0); }
int32_t hwv_post_html(int64_t h, const char *html)    { return post(h, OP_HTML, html, 0, 0, 0); }
int32_t hwv_post_init(int64_t h, const char *js)      { return post(h, OP_INIT, js, 0, 0, 0); }
int32_t hwv_post_eval(int64_t h, const char *js)      { return post(h, OP_EVAL, js, 0, 0, 0); }
int32_t hwv_post_bind(int64_t h, const char *name)    { return post(h, OP_BIND, name, 0, 0, 0); }
int32_t hwv_post_unbind(int64_t h, const char *name)  { return post(h, OP_UNBIND, name, 0, 0, 0); }

/* ---- misc --------------------------------------------------------------------------- */
const char *hwv_version(void) {
    const hwv_version_info *v = webview_version();
    return v ? v->version_number : "";
}

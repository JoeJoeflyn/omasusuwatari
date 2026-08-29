#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <wayland-client.h>
#include <cairo/cairo.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define M_PI 3.14159265358979323846
#define MAX_SPRITES 6
#define MAX_PARTICLES 30

typedef enum {
    STATE_WINDOW,
    STATE_SNEAK,
    STATE_DRAG,
    STATE_RETURN,
    STATE_POOF,
    STATE_SLEEP
} SpriteState;

typedef struct {
    double x, y;
    double vx, vy;
    double alpha;
    double size;
    double lifetime;
    double age;
    int ptype; // 0=soot, 1=poof, 2=sparkle, 3=zzz
} Particle;

typedef struct {
    int id;
    double x, y;
    double vx, vy;
    double target_x, target_y;
    double speed;
    double base_radius;
    int num_spikes;
    double spike_phases[40];
    double spike_lengths[40];
    SpriteState state;
    double state_timer;
    double look_dx, look_dy;
    double blink_timer;
    int is_blinking;
    double blink_progress;
    double scurry_phase;
    double startle_timer;
    double hop_offset;
    double prank_cooldown;
    double drag_timer;
    double last_cur_x, last_cur_y;
    int has_coal;
    Particle particles[MAX_PARTICLES];
    int num_particles;
} Sprite;

struct Buffer {
    struct wl_buffer *wl_buffer;
    void *data;
    size_t size;
    int busy;
};

struct App {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    int32_t width;
    int32_t height;
    struct Buffer buffers[2];
    int configured;

    // Hyprland cached state
    pthread_mutex_t hypr_lock;
    double cursor_x, cursor_y;
    int has_cursor;
    double cursor_idle_time;
    double last_physical_cur_x, last_physical_cur_y;
    double cursor_speed;

    double win_x, win_y, win_w, win_h;
    int has_win;

    char hypr_cmd_sock[256];

    Sprite sprites[MAX_SPRITES];
    int sprite_count;
    double start_time;
    double last_frame_time;
};

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double rand_f(double min, double max) {
    return min + ((double)rand() / (double)RAND_MAX) * (max - min);
}

// Hyprland Socket Helper
static char *hypr_send_cmd(const char *sock_path, const char *cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    struct timeval tv = { .tv_sec = 0, .tv_usec = 60000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return NULL;
    }

    if (write(sock, cmd, strlen(cmd)) < 0) {
        close(sock);
        return NULL;
    }

    char *buf = malloc(4096);
    if (!buf) { close(sock); return NULL; }
    ssize_t n = read(sock, buf, 4095);
    close(sock);
    if (n > 0) {
        buf[n] = '\0';
        return buf;
    }
    free(buf);
    return NULL;
}

static void hypr_move_cursor(const char *sock_path, int x, int y) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "eval hl.dispatch(hl.dsp.cursor.move({ x = %d, y = %d }))", x, y);
    char *res = hypr_send_cmd(sock_path, cmd);
    if (res) free(res);
}

// Background thread for Hyprland polling
static void *hypr_poller_thread(void *arg) {
    struct App *app = (struct App *)arg;
    int win_poll_counter = 0;

    while (1) {
        usleep(25000); // 40Hz
        double now = get_time_sec();

        // 1. Cursor Position
        char *cur_res = hypr_send_cmd(app->hypr_cmd_sock, "cursorpos");
        if (cur_res) {
            double cx = 0, cy = 0;
            if (sscanf(cur_res, "%lf,%lf", &cx, &cy) == 2) {
                pthread_mutex_lock(&app->hypr_lock);
                double moved = hypot(cx - app->last_physical_cur_x, cy - app->last_physical_cur_y);
                if (moved > 3.0) {
                    app->cursor_idle_time = 0.0;
                    app->cursor_speed = moved * 40.0;
                } else {
                    app->cursor_idle_time += 0.025;
                    app->cursor_speed = 0.0;
                }
                app->cursor_x = cx;
                app->cursor_y = cy;
                app->last_physical_cur_x = cx;
                app->last_physical_cur_y = cy;
                app->has_cursor = 1;
                pthread_mutex_unlock(&app->hypr_lock);
            }
            free(cur_res);
        }

        // 2. Active Window (every 200ms)
        win_poll_counter++;
        if (win_poll_counter >= 8) {
            win_poll_counter = 0;
            char *win_res = hypr_send_cmd(app->hypr_cmd_sock, "j/activewindow");
            if (win_res) {
                char *at_ptr = strstr(win_res, "\"at\":[");
                char *size_ptr = strstr(win_res, "\"size\":[");
                if (at_ptr && size_ptr) {
                    double wx = 0, wy = 0, ww = 0, wh = 0;
                    if (sscanf(at_ptr + 6, "%lf,%lf", &wx, &wy) == 2 &&
                        sscanf(size_ptr + 8, "%lf,%lf", &ww, &wh) == 2) {
                        if (ww > 40.0 && wh > 40.0) {
                            pthread_mutex_lock(&app->hypr_lock);
                            app->win_x = wx;
                            app->win_y = wy;
                            app->win_w = ww;
                            app->win_h = wh;
                            app->has_win = 1;
                            pthread_mutex_unlock(&app->hypr_lock);
                        }
                    }
                }
                free(win_res);
            }
        }
    }
    return NULL;
}

// POSIX SHM Pool Allocator
static int create_shm_file(off_t size) {
    char name[] = "/susuwatari-shm-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(name);
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }
    return -1;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct Buffer *buf = (struct Buffer *)data;
    buf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static struct Buffer *get_next_buffer(struct App *app) {
    int stride = app->width * 4;
    size_t size = stride * app->height;

    for (int i = 0; i < 2; i++) {
        if (!app->buffers[i].busy) {
            if (!app->buffers[i].wl_buffer || app->buffers[i].size != size) {
                if (app->buffers[i].wl_buffer) {
                    wl_buffer_destroy(app->buffers[i].wl_buffer);
                    munmap(app->buffers[i].data, app->buffers[i].size);
                }

                int fd = create_shm_file(size);
                if (fd < 0) return NULL;

                void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (data == MAP_FAILED) { close(fd); return NULL; }

                struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, size);
                struct wl_buffer *wl_buf = wl_shm_pool_create_buffer(pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
                wl_shm_pool_destroy(pool);
                close(fd);

                app->buffers[i].wl_buffer = wl_buf;
                app->buffers[i].data = data;
                app->buffers[i].size = size;
                wl_buffer_add_listener(wl_buf, &buffer_listener, &app->buffers[i]);
            }
            app->buffers[i].busy = 1;
            return &app->buffers[i];
        }
    }
    return NULL;
}

// Particle management
static void add_particle(Sprite *s, double x, double y, int ptype) {
    if (s->num_particles >= MAX_PARTICLES) return;
    Particle *p = &s->particles[s->num_particles++];
    p->x = x;
    p->y = y;
    p->ptype = ptype;
    p->age = 0.0;

    if (ptype == 0) { // Soot
        p->vx = rand_f(-0.8, 0.8);
        p->vy = rand_f(-0.6, 0.6);
        p->size = rand_f(1.0, 1.8);
        p->lifetime = 0.5;
        p->alpha = 0.7;
    } else if (ptype == 1) { // Poof
        double ang = rand_f(0, M_PI * 2.0);
        double spd = rand_f(1.5, 4.0);
        p->vx = cos(ang) * spd;
        p->vy = sin(ang) * spd;
        p->size = rand_f(1.5, 2.8);
        p->lifetime = 0.6;
        p->alpha = 0.9;
    } else if (ptype == 2) { // Sparkle
        p->vx = rand_f(-0.8, 0.8);
        p->vy = rand_f(-1.2, -0.4);
        p->size = 2.5;
        p->lifetime = 0.65;
        p->alpha = 1.0;
    } else { // Zzz
        p->vx = rand_f(-0.2, 0.2);
        p->vy = rand_f(-0.6, -0.3);
        p->size = 8.0;
        p->lifetime = 2.0;
        p->alpha = 0.9;
    }
}

static void init_sprites(struct App *app) {
    app->sprite_count = 5;
    for (int i = 0; i < app->sprite_count; i++) {
        Sprite *s = &app->sprites[i];
        s->id = i + 1;
        s->x = 200.0 + i * 140.0;
        s->y = 26.0;
        s->target_x = s->x;
        s->target_y = s->y;
        s->speed = rand_f(38.0, 68.0);
        s->base_radius = rand_f(5.5, 6.8);
        s->num_spikes = 32;
        for (int k = 0; k < s->num_spikes; k++) {
            s->spike_phases[k] = rand_f(0, M_PI * 2.0);
            s->spike_lengths[k] = rand_f(1.8, 4.0);
        }
        s->state = STATE_WINDOW;
        s->state_timer = rand_f(1.5, 4.0);
        s->prank_cooldown = rand_f(6.0, 16.0);
        s->has_coal = (i % 2 == 0);
        s->num_particles = 0;
    }
}

static void trigger_startle(Sprite *s, double cx) {
    if (s->startle_timer > 0.0 || s->state == STATE_POOF) return;
    s->startle_timer = 0.9;
    s->hop_offset = -6.0;
    add_particle(s, s->x, s->y - 6.0, 2);

    double dir = (s->x < cx) ? -1.0 : 1.0;
    s->vx = dir * 120.0;
    s->target_x = s->x + dir * 80.0;
    s->speed = 120.0;
    s->prank_cooldown = 12.0;
}

static void trigger_poof(Sprite *s) {
    if (s->state == STATE_POOF) return;
    s->state = STATE_POOF;
    s->state_timer = 1.4;
    for (int i = 0; i < 12; i++) {
        add_particle(s, s->x, s->y, 1);
    }
}

static void update_physics(struct App *app, double dt) {
    pthread_mutex_lock(&app->hypr_lock);
    double cx = app->cursor_x;
    double cy = app->cursor_y;
    int has_cur = app->has_cursor;
    double cur_idle = app->cursor_idle_time;
    double cur_spd = app->cursor_speed;

    double wx = app->has_win ? app->win_x : 100.0;
    double wy = app->has_win ? app->win_y : 30.0;
    double ww = app->has_win ? app->win_w : (app->width - 200.0);
    pthread_mutex_unlock(&app->hypr_lock);

    // EXACTLY ONE SUSUWATARI HEIST: Check if any sprite is already on a mission
    int heist_in_progress = 0;
    for (int j = 0; j < app->sprite_count; j++) {
        if (app->sprites[j].state == STATE_SNEAK || app->sprites[j].state == STATE_DRAG) {
            heist_in_progress = 1;
            break;
        }
    }

    for (int i = 0; i < app->sprite_count; i++) {
        Sprite *s = &app->sprites[i];
        s->state_timer -= dt;
        s->prank_cooldown -= dt;

        if (s->startle_timer > 0.0) {
            s->startle_timer -= dt;
            s->hop_offset = fmin(0.0, s->hop_offset + 20.0 * dt);
        } else {
            s->hop_offset = 0.0;
        }

        // Particle update
        for (int p = 0; p < s->num_particles; p++) {
            Particle *pt = &s->particles[p];
            pt->x += pt->vx;
            pt->y += pt->vy;
            pt->age += dt;
            pt->alpha = fmax(0.0, 1.0 - (pt->age / pt->lifetime));
            if (pt->age >= pt->lifetime) {
                s->particles[p] = s->particles[--s->num_particles];
                p--;
            }
        }

        // Fast swipe poof
        if (has_cur) {
            double d = hypot(cx - s->x, cy - s->y);
            if (cur_spd > 650.0 && d < 20.0) {
                trigger_poof(s);
            } else if (d < 24.0 && s->state == STATE_WINDOW) {
                trigger_startle(s, cx);
            }
        }

        // Poof recovery
        if (s->state == STATE_POOF) {
            if (s->state_timer <= 0.0) {
                s->state = STATE_WINDOW;
                add_particle(s, s->x, s->y, 2);
                s->state_timer = 2.0;
            }
            continue;
        }

        // Mouse Heist: STRICTLY ONE SOLO OPERATIVE AT A TIME
        if (!heist_in_progress && s->state == STATE_WINDOW && has_cur && cur_idle > 2.5 && s->prank_cooldown <= 0.0 && s->startle_timer <= 0.0) {
            s->state = STATE_SNEAK;
            s->target_x = cx;
            s->target_y = cy + 12.0;
            s->speed = 95.0;
            s->last_cur_x = cx;
            s->last_cur_y = cy;
            heist_in_progress = 1; // Claim lock so no one else joins!
        }

        if (s->state == STATE_SNEAK) {
            if (!has_cur || cur_idle < 0.15) {
                s->state = STATE_RETURN;
                s->prank_cooldown = 10.0;
            } else {
                s->target_x = cx;
                s->target_y = cy + 12.0;
                double dx = s->target_x - s->x;
                double dy = s->target_y - s->y;
                double dist = hypot(dx, dy);

                if (dist < 14.0) {
                    s->state = STATE_DRAG;
                    s->drag_timer = 2.8;
                    s->last_cur_x = cx;
                    s->last_cur_y = cy;
                    s->target_x = cx + rand_f(-120.0, 120.0);
                    s->target_y = fmax(40.0, cy - rand_f(60.0, 110.0));
                    s->speed = 46.0;
                } else {
                    s->vx = (dx / dist) * s->speed;
                    s->vy = (dy / dist) * s->speed;
                    s->x += s->vx * dt;
                    s->y += s->vy * dt;
                    s->scurry_phase += dt * 24.0;
                    s->look_dx = dx / dist;
                    s->look_dy = dy / dist;
                }
            }
        } else if (s->state == STATE_DRAG) {
            if (!has_cur || hypot(cx - s->last_cur_x, cy - s->last_cur_y) > 35.0 || s->drag_timer <= 0.0) {
                add_particle(s, s->x, s->y - 8.0, 2);
                s->state = STATE_RETURN;
                s->prank_cooldown = 12.0;
            } else {
                s->drag_timer -= dt;
                double dx = s->target_x - s->x;
                double dy = s->target_y - s->y;
                double dist = hypot(dx, dy);

                if (dist > 3.0 && s->drag_timer > 0.0) {
                    s->vx = (dx / dist) * s->speed;
                    s->vy = (dy / dist) * s->speed;
                    s->x += s->vx * dt;
                    s->y += s->vy * dt;
                    s->scurry_phase += dt * 22.0;
                    s->look_dx = dx / dist;
                    s->look_dy = -0.7;

                    hypr_move_cursor(app->hypr_cmd_sock, (int)s->x, (int)(s->y - 12.0));
                    s->last_cur_x = s->x;
                    s->last_cur_y = s->y - 12.0;
                } else {
                    add_particle(s, s->x, s->y - 8.0, 2);
                    s->state = STATE_RETURN;
                    s->prank_cooldown = 12.0;
                }
            }
        } else if (s->state == STATE_RETURN) {
            s->target_y = wy - 3.5;
            s->target_x = wx + 30.0 + (s->id * 80.0);
            double dx = s->target_x - s->x;
            double dy = s->target_y - s->y;
            double dist = hypot(dx, dy);

            if (dist < 8.0) {
                s->state = STATE_WINDOW;
                s->speed = rand_f(38.0, 68.0);
            } else {
                s->vx = (dx / dist) * 75.0;
                s->vy = (dy / dist) * 75.0;
                s->x += s->vx * dt;
                s->y += s->vy * dt;
                s->scurry_phase += dt * 25.0;
            }
        } else if (s->startle_timer > 0.0) {
            // Startled run
            double dx = s->target_x - s->x;
            if (fabs(dx) > 3.0) {
                double dir = (dx > 0) ? 1.0 : -1.0;
                s->x += dir * s->speed * dt;
                s->scurry_phase += dt * 32.0;
                s->look_dx = dir;
            }
        } else {
            // Roaming on active window top bar
            s->target_y = wy - 3.5;
            s->y = s->target_y;

            if (s->state_timer <= 0.0) {
                s->state_timer = rand_f(2.0, 4.5);
                s->target_x = wx + 30.0 + rand_f(0, fmax(30.0, ww - 60.0));
            }

            double dx = s->target_x - s->x;
            if (fabs(dx) > 3.0) {
                double dir = (dx > 0) ? 1.0 : -1.0;
                s->x += dir * s->speed * dt;
                s->scurry_phase += dt * 22.0;
                s->look_dx = dir;
                s->look_dy = 0.0;
            } else {
                s->look_dx = (has_cur && (cx - s->x) > 0) ? 0.6 : -0.6;
            }
        }

        s->x = fmax(20.0, fmin(app->width - 20.0, s->x));
    }
}

static void render_cairo(struct App *app, cairo_t *cr, double cur_time) {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for (int i = 0; i < app->sprite_count; i++) {
        Sprite *s = &app->sprites[i];

        // Draw particles
        for (int p = 0; p < s->num_particles; p++) {
            Particle *pt = &s->particles[p];
            cairo_save(cr);
            if (pt->ptype == 2) { // Sparkle
                cairo_set_source_rgba(cr, 1.0, 0.92, 0.45, pt->alpha);
                cairo_arc(cr, pt->x, pt->y, pt->size, 0, M_PI * 2.0);
                cairo_fill(cr);
            } else {
                cairo_set_source_rgba(cr, 0.08, 0.08, 0.11, pt->alpha);
                cairo_arc(cr, pt->x, pt->y, pt->size, 0, M_PI * 2.0);
                cairo_fill(cr);
            }
            cairo_restore(cr);
        }

        if (s->state == STATE_POOF) continue;

        int is_startled = (s->startle_timer > 0.0);
        int is_dragging = (s->state == STATE_DRAG);

        // Draw stick legs
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.04, 0.04, 0.06, 0.95);
        cairo_set_line_width(cr, 1.2);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        double step = sin(s->scurry_phase) * 1.8;

        cairo_move_to(cr, s->x - 3.5, s->y + s->hop_offset + 3.5);
        cairo_line_to(cr, s->x - 5.5, s->y + s->hop_offset + 7.5 + step);
        cairo_move_to(cr, s->x + 3.5, s->y + s->hop_offset + 3.5);
        cairo_line_to(cr, s->x + 5.5, s->y + s->hop_offset + 7.5 - step);
        cairo_stroke(cr);
        cairo_restore(cr);

        cairo_save(cr);
        cairo_translate(cr, s->x, s->y + s->hop_offset);

        // Charcoal outer glow
        cairo_set_source_rgba(cr, 0.9, 0.9, 1.0, 0.26);
        cairo_arc(cr, 0, 0, s->base_radius + 2.0, 0, M_PI * 2.0);
        cairo_fill(cr);

        // Organic radiating soot fuzz
        cairo_set_source_rgba(cr, 0.04, 0.04, 0.06, 0.99);
        cairo_new_path(cr);
        for (int k = 0; k < s->num_spikes; k++) {
            double angle = (k / (double)s->num_spikes) * 2.0 * M_PI;
            double jitter = sin(cur_time * 16.0 + s->spike_phases[k]) * 0.7;
            double r = s->base_radius + s->spike_lengths[k] + jitter;
            double px = cos(angle) * r;
            double py = sin(angle) * r;
            if (k == 0) cairo_move_to(cr, px, py);
            else cairo_line_to(cr, px, py);
        }
        cairo_close_path(cr);
        cairo_fill(cr);

        // Inky core
        cairo_set_source_rgba(cr, 0.07, 0.07, 0.10, 1.0);
        cairo_arc(cr, 0, 0, s->base_radius * 0.88, 0, M_PI * 2.0);
        cairo_fill(cr);

        // Carrying Hands or Coal
        if (is_dragging || is_startled) {
            cairo_set_source_rgba(cr, 0.04, 0.04, 0.06, 0.98);
            cairo_set_line_width(cr, 1.3);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, -3.0, -1.0);
            cairo_line_to(cr, -5.5, -s->base_radius - 5.0);
            cairo_move_to(cr, 3.0, -1.0);
            cairo_line_to(cr, 5.5, -s->base_radius - 5.0);
            cairo_stroke(cr);
        } else if (s->has_coal) {
            cairo_set_source_rgba(cr, 0.22, 0.22, 0.28, 1.0);
            cairo_rectangle(cr, -2.5, -s->base_radius - 3.5, 5.0, 3.5);
            cairo_fill(cr);
        }

        // Eyes
        double eye_dist = 2.8;
        double eye_y = -1.8;
        double eye_r = is_startled ? 3.1 : 2.7;

        // White Sclera
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_arc(cr, -eye_dist, eye_y, eye_r, 0, M_PI * 2.0);
        cairo_fill(cr);
        cairo_arc(cr, eye_dist, eye_y, eye_r, 0, M_PI * 2.0);
        cairo_fill(cr);

        // Pupils
        double pupil_r = is_startled ? 0.9 : 1.35;
        double px_off = s->look_dx * 1.0;
        double py_off = s->look_dy * 0.8;

        cairo_set_source_rgba(cr, 0.03, 0.03, 0.05, 1.0);
        cairo_arc(cr, -eye_dist + px_off, eye_y + py_off, pupil_r, 0, M_PI * 2.0);
        cairo_fill(cr);
        cairo_arc(cr, eye_dist + px_off, eye_y + py_off, pupil_r, 0, M_PI * 2.0);
        cairo_fill(cr);

        // Specular Sparkle Highlight
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
        cairo_arc(cr, -eye_dist + px_off - 0.5, eye_y + py_off - 0.5, 0.5, 0, M_PI * 2.0);
        cairo_fill(cr);
        cairo_arc(cr, eye_dist + px_off - 0.5, eye_y + py_off - 0.5, 0.5, 0, M_PI * 2.0);
        cairo_fill(cr);

        cairo_restore(cr);
    }
}

static void frame_callback_handler(void *data, struct wl_callback *callback, uint32_t time);

static const struct wl_callback_listener frame_listener = {
    .done = frame_callback_handler,
};

static void draw_frame(struct App *app) {
    if (!app->configured) return;

    struct Buffer *buf = get_next_buffer(app);
    if (!buf) return;

    double now = get_time_sec();
    double dt = (now - app->last_frame_time);
    if (dt < 0.001 || dt > 0.1) dt = 0.020;
    app->last_frame_time = now;

    update_physics(app, dt);

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        buf->data, CAIRO_FORMAT_ARGB32, app->width, app->height, app->width * 4);
    cairo_t *cr = cairo_create(surf);

    render_cairo(app, cr, now - app->start_time);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wl_surface_attach(app->surface, buf->wl_buffer, 0, 0);
    wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);

    struct wl_callback *callback = wl_surface_frame(app->surface);
    wl_callback_add_listener(callback, &frame_listener, app);
    wl_surface_commit(app->surface);
}

static void frame_callback_handler(void *data, struct wl_callback *callback, uint32_t time) {
    struct App *app = (struct App *)data;
    wl_callback_destroy(callback);
    usleep(18000); // Smooth ~50 FPS
    draw_frame(app);
}

// Layer Shell Listeners
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t w, uint32_t h) {
    struct App *app = (struct App *)data;
    if (w > 0) app->width = w;
    if (h > 0) app->height = h;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    app->configured = 1;
    draw_frame(app);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    exit(0);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name, const char *interface, uint32_t version) {
    struct App *app = (struct App *)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(int argc, char **argv) {
    srand(time(NULL));

    struct App app;
    memset(&app, 0, sizeof(app));
    app.width = 1536;
    app.height = 864;
    app.start_time = get_time_sec();
    app.last_frame_time = app.start_time;
    pthread_mutex_init(&app.hypr_lock, NULL);

    // Get Hyprland socket
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char *hypr_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime) xdg_runtime = "/run/user/1000";
    if (hypr_sig) {
        snprintf(app.hypr_cmd_sock, sizeof(app.hypr_cmd_sock), "%s/hypr/%s/.socket.sock", xdg_runtime, hypr_sig);
    } else {
        snprintf(app.hypr_cmd_sock, sizeof(app.hypr_cmd_sock), "%s/hypr/.socket.sock", xdg_runtime);
    }

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.layer_shell) {
        fprintf(stderr, "Compositor missing required Wayland interfaces\n");
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);

    // Empty input region for 100% click-through
    struct wl_region *empty_region = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(app.surface, empty_region);
    wl_region_destroy(empty_region);

    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "susuwatari-pets");

    zwlr_layer_surface_v1_set_size(app.layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(app.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(app.layer_surface, 0);

    zwlr_layer_surface_v1_add_listener(app.layer_surface, &layer_surface_listener, &app);
    wl_surface_commit(app.surface);

    init_sprites(&app);

    // Start background poller thread
    pthread_t poller_tid;
    pthread_create(&poller_tid, NULL, hypr_poller_thread, &app);

    while (wl_display_dispatch(app.display) != -1) {}

    return 0;
}

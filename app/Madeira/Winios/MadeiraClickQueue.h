/* Shared C implementation: used by Winios.m, the ARM64 overlay and tests.
 * g_input_q.lock protects both the original queue and click_state. No sleep,
 * worker or timer is introduced; desktop Wine already polls at <=16 ms.
 */
typedef struct {
    unsigned initialized, draining, hold_ms, held;
    double release_after[3], press_after[3];
    unsigned key_hold_ms, key_reserved;
    unsigned key_held[8];
    double key_release_after[256], key_press_after[256];
} MadeiraClickState;
_Static_assert(sizeof(MadeiraClickState) == 4200, "overlay state layout");
#ifdef MADEIRA_OVERLAY
extern MadeiraClickState click_state;
#else
static MadeiraClickState click_state;
#endif

static unsigned madeira_hold_setting(const char *name)
{
    const char *p = getenv(name);
    unsigned n = 0;
    if (!p || !*p) return 80;
    for (; *p; p++) {
        if (*p < '0' || *p > '9' || n > 250) return 80;
        n = n * 10 + (unsigned)(*p - '0');
    }
    if (!n) return 0;
    return n >= 16 && n <= 250 ? n : 80;
}

/* Inspect before removing an event. Timestamps refer to delivery to Wine,
 * not to UIKit enqueue time: a stalled guest must still see both edges.
 * A small UP interval also prevents rapid queued taps merging into a hold.
 */
static int madeira_click_ready(const winios_input_event_t *e, double now)
{
    static const unsigned downs[3] = {2, 8, 32};
    static const unsigned ups[3] = {4, 16, 64};
    if (e->type == WINIOS_EV_KEY && click_state.key_hold_ms && e->x > 0 && e->x < 256) {
        unsigned key = (unsigned)e->x, bit = 1u << (key & 31), word = key >> 5;
        int held = (click_state.key_held[word] & bit) != 0;
        if (e->flags & KEYEVENTF_KEYUP) {
            if (held && now < click_state.key_release_after[key]) return 0;
            if (held) click_state.key_press_after[key] = now + 0.030;
            click_state.key_held[word] &= ~bit;
        } else if (!held) {
            if (now < click_state.key_press_after[key]) return 0;
            click_state.key_release_after[key] = now + click_state.key_hold_ms / 1000.0;
            click_state.key_held[word] |= bit;
        }
        return 1;
    }
    if (e->type != WINIOS_EV_MOUSE || !click_state.hold_ms) return 1;
    for (unsigned i = 0; i < 3; i++) {
        unsigned both = downs[i] | ups[i];
        if ((e->flags & both) == both) continue; /* preserve combined packets */
        if ((e->flags & ups[i]) && (click_state.held & (1u << i)) &&
            now < click_state.release_after[i]) return 0;
        if ((e->flags & downs[i]) && !(click_state.held & (1u << i)) &&
            now < click_state.press_after[i]) return 0;
    }
    for (unsigned i = 0; i < 3; i++) {
        unsigned bit = 1u << i, both = downs[i] | ups[i];
        if ((e->flags & both) == both) continue;
        if ((e->flags & downs[i]) && !(click_state.held & bit)) {
            click_state.held |= bit;
            click_state.release_after[i] = now + click_state.hold_ms / 1000.0;
        }
        if (e->flags & ups[i]) {
            if (click_state.held & bit) click_state.press_after[i] = now + 0.030;
            click_state.held &= ~bit;
        }
    }
    return 1;
}

static int madeira_click_drain(void)
{
    int drained = 0, announce = 0;
    unsigned budget = WINIOS_RING_SIZE;
    pthread_mutex_lock(&g_input_q.lock);
    if (click_state.draining) {
        pthread_mutex_unlock(&g_input_q.lock);
        return 0;
    }
    click_state.draining = 1;
    if (!click_state.initialized) {
        click_state.hold_ms = madeira_hold_setting("MADEIRA_CLICK_HOLD_MS");
        click_state.key_hold_ms = madeira_hold_setting("MADEIRA_KEY_HOLD_MS");
        click_state.initialized = 1;
        announce = 1;
    }
    pthread_mutex_unlock(&g_input_q.lock);
    if (announce) {
        static const char msg[] = "[input-fix] mouse+keyboard edge pacing active; default 80 ms, gap 30 ms\n";
        write(2, msg, sizeof(msg) - 1);
    }
    for (;;) {
        winios_input_event_t e;
        double now = CACurrentMediaTime();
        pthread_mutex_lock(&g_input_q.lock);
        if (!budget-- || g_input_q.tail == g_input_q.head) {
            click_state.draining = 0;
            pthread_mutex_unlock(&g_input_q.lock);
            break;
        }
        e = g_input_q.buf[g_input_q.tail];
        if (!madeira_click_ready(&e, now)) {
            click_state.draining = 0;
            pthread_mutex_unlock(&g_input_q.lock);
            break;
        }
        g_input_q.tail = (g_input_q.tail + 1) % WINIOS_RING_SIZE;
        pthread_mutex_unlock(&g_input_q.lock);
        if (e.type == WINIOS_EV_KEY)
            winios_drv_post_key((unsigned short)e.x, e.flags);
        else
            winios_drv_post_mouse(e.x, e.y, e.flags, e.data, (void *)0);
        drained = 1;
    }
    return drained;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/uinput.h>
#include <X11/Xlib.h>

/*****************************************************************************/
/* List of modifiers: { ShiftMask, LockMask, ControlMask, Mod1Mask, Mod2Mask, 
 * Mod3Mask, Mod4Mask, Mod5Mask }
 * Mod1Mask is Alt, Superkey is Mod4Mask */
#define ACTIVATE_MOD (Mod4Mask) 
#define ACTIVATE_KEY ("w")

#define L_BUTTON ("space")
#define R_BUTTON ("Tab")
#define U_KEY    ("w")
#define D_KEY    ("s")
#define L_KEY    ("a")
#define R_KEY    ("d")
#define SCROLL_U ("e")
#define SCROLL_D ("q")
#define SLOW_KEY ("Shift_L")

// Refresh rate in microseconds.
#define REFRESH_RATE (16666)
/*****************************************************************************/

/* Contains the action response for each motion related key press, including a 
 * mask of all the keys actually pressed down. Values are represented by:
 * { 0:U_KEY, 1:D_KEY, 2:L_KEY, 3:R_KEY, 4:SCROLL_U, 5:SCROLL_D} */
struct mouse_action
{
    useconds_t last_activated[6];

    struct action_values
    {
        int move_rel_x;
        int move_rel_y;
        int scroll;
    } const action [6];

    uint8_t  prev_key_down_mask;
    uint8_t  key_down_mask;
    bool     l_button_down;
    bool     r_button_down;
    bool     l_button_up;
    bool     r_button_up;
    bool     l_button_down_held;
    bool     r_button_down_held;
    bool     is_slow;
};
typedef struct mouse_action mouse_action;

void keycodes_init(void);
void activate(void);
void deactivate(void);
void uinput_init(struct uinput_setup* usetup);
void exit_handler(__attribute__((unused))int id);
void emit(int fd, int type, int code, int val);
void uinput_state_emit(void);
void active_loop(void);

/*****************************[Static variables]******************************/

static mouse_action MOUSE_STATE = { {0, 0, 0, 0, 0, 0},
    { {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1} }
    , 0x00, 0x00, false, false, false, false, false, false, false};
    
static Display* DISPLAY;
static Window   ROOT;

static uint32_t KC_ACTIVATE_KEY; 
static uint32_t KC_L_BUTTON;
static uint32_t KC_R_BUTTON;
static uint32_t KC_U_KEY;
static uint32_t KC_D_KEY;
static uint32_t KC_L_KEY;
static uint32_t KC_R_KEY;
static uint32_t KC_SCROLL_U;
static uint32_t KC_SCROLL_D;
static uint32_t KC_SLOW_KEY;

static int      UINPUT_FD;
static bool     IS_ACTIVE; /* When active, mouse is controlled via keyboard. */

/*****************************************************************************/

void keycodes_init(void)
{
#define GET_KEYCODE(KEY) (XKeysymToKeycode(DISPLAY, XStringToKeysym((KEY))))

    KC_ACTIVATE_KEY   = GET_KEYCODE(ACTIVATE_KEY);
    KC_L_BUTTON       = GET_KEYCODE(L_BUTTON);
    KC_R_BUTTON       = GET_KEYCODE(R_BUTTON);
    KC_U_KEY          = GET_KEYCODE(U_KEY);
    KC_D_KEY          = GET_KEYCODE(D_KEY);
    KC_L_KEY          = GET_KEYCODE(L_KEY);
    KC_R_KEY          = GET_KEYCODE(R_KEY);
    KC_SCROLL_U       = GET_KEYCODE(SCROLL_U);
    KC_SCROLL_D       = GET_KEYCODE(SCROLL_D);
    KC_SLOW_KEY       = GET_KEYCODE(SLOW_KEY);

#undef GET_KEYCODE
}

void activate(void)
{
    if (!IS_ACTIVE)
    {
        IS_ACTIVE = true;

        XGrabKey(DISPLAY, KC_L_BUTTON, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_R_BUTTON, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_U_KEY, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_D_KEY, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_L_KEY, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_R_KEY, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_SCROLL_U, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_SCROLL_D, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(DISPLAY, KC_SLOW_KEY, 0, ROOT, True, GrabModeAsync, GrabModeAsync);
    }
}

void deactivate(void)
{
    if (IS_ACTIVE)
    {
        IS_ACTIVE = false;

        XUngrabKey(DISPLAY, KC_L_BUTTON, 0, ROOT);
        XUngrabKey(DISPLAY, KC_R_BUTTON, 0, ROOT);
        XUngrabKey(DISPLAY, KC_U_KEY, 0, ROOT);
        XUngrabKey(DISPLAY, KC_D_KEY, 0, ROOT);
        XUngrabKey(DISPLAY, KC_L_KEY, 0, ROOT);
        XUngrabKey(DISPLAY, KC_R_KEY, 0, ROOT);
        XUngrabKey(DISPLAY, KC_SCROLL_U, 0, ROOT);
        XUngrabKey(DISPLAY, KC_SCROLL_D, 0, ROOT);
        XUngrabKey(DISPLAY, KC_SLOW_KEY, 0, ROOT);
    }
}

void uinput_init(struct uinput_setup* usetup)
{
    if ( !(UINPUT_FD = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) ) exit(1);

    /* Enabling mouse clicks and scrolls. */
    ioctl(UINPUT_FD, UI_SET_EVBIT,  EV_KEY);
    ioctl(UINPUT_FD, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(UINPUT_FD, UI_SET_KEYBIT, BTN_RIGHT);
    /* Enabling relative mouse motion. */
    ioctl(UINPUT_FD, UI_SET_EVBIT, EV_REL);
    ioctl(UINPUT_FD, UI_SET_RELBIT, REL_X);
    ioctl(UINPUT_FD, UI_SET_RELBIT, REL_Y);
    ioctl(UINPUT_FD, UI_SET_RELBIT,  REL_WHEEL);

    memset(usetup, 0, sizeof(*usetup));
    usetup->id.bustype = BUS_USB;
    usetup->id.vendor  = 0x1234; /* Sample vendor. */
    usetup->id.product = 0x5678; /* Sample product. */
    strcpy(usetup->name, "xkeycursor");

    ioctl(UINPUT_FD, UI_DEV_SETUP, usetup);
    ioctl(UINPUT_FD, UI_DEV_CREATE);
}

void exit_handler(__attribute__((unused))int id)
{
    deactivate();

    XUngrabKey(DISPLAY, KC_ACTIVATE_KEY, ACTIVATE_MOD, ROOT);

    if (UINPUT_FD)
    {
        ioctl(UINPUT_FD, UI_DEV_DESTROY);
        close(UINPUT_FD);
    }

    exit(0);
}

static inline useconds_t curr_time(void)
{
    struct timeval tv; 
    gettimeofday(&tv, NULL);
    return 1000000*tv.tv_sec + tv.tv_usec;
}

void emit(int fd, int type, int code, int val)
{
   struct input_event ie;

   ie.type = type;
   ie.code = code;
   ie.value = val;
   /* Timestamp values below are ignored. */
   ie.time.tv_sec = 0;
   ie.time.tv_usec = 0;

   write(fd, &ie, sizeof(ie));
}

void uinput_state_emit(void)
{
    static uint64_t acc = 0;
    static int speed    = 2;
    int move_rel_x = 0;
    int move_rel_y = 0;
    int scroll     = 0;
    bool acc_inc   = false;

    useconds_t time = curr_time();
    useconds_t time_diff[6] = {time, time, time, time, time, time};

    /* Results have accumulated in MOUSE_STATE's key_down_mask, now calculate!*/
    for (uint8_t i = 0, m = 1; i < 6; ++i, m*=2)
    {
        if (MOUSE_STATE.key_down_mask & m)
        {
            move_rel_x += MOUSE_STATE.action[i].move_rel_x;
            move_rel_y += MOUSE_STATE.action[i].move_rel_y;
            scroll     += MOUSE_STATE.action[i].scroll;
            /* Set acc_inc if the event is relative mouse motion.*/
            acc_inc = acc_inc || (i < 4);
            /* Since the key has been activated, set its last_activated stamp.*/
            time_diff[i] -= MOUSE_STATE.last_activated[i];
            if (i < 4) { MOUSE_STATE.last_activated[i] = time; }
        }
    }

    if (acc_inc) ++acc;
    else { acc = 0; speed = 2; }

    for (uint8_t i = 0; i < 4; ++i)
    {
        if (!(MOUSE_STATE.prev_key_down_mask & (1 << i))
            && (MOUSE_STATE.key_down_mask & (1 << i)))
        {
            speed = (time_diff[i] < 100000) ? 12 : speed;
            printf("time_diff[%u] = %u\n", i, time_diff[i]);
            if (time_diff[i] < 100000)
            {
                printf("Boost mode enabled!\n");
            }
        }
    }
    if (speed < 12)
    {
        speed = (acc >= 60) ? 12 : 2 + acc/10;
    }

    if (move_rel_x) emit(UINPUT_FD, EV_REL, REL_X, speed*move_rel_x);
    if (move_rel_y) emit(UINPUT_FD, EV_REL, REL_Y, speed*move_rel_y);

    // For scroll we do not wish to apply it constantly at a high frequency
    // as it is too much. Instead, apply it at larger strides of time.
    bool should_scroll = false;
    for (uint8_t i = 4; i < 6; ++i)
    {
        if (MOUSE_STATE.key_down_mask & (1 << i) && time_diff[i] > 100000)
        {
            printf("should scroll = true;\n");
            should_scroll = true;
            MOUSE_STATE.last_activated[i] = time;
            
        }
    }

    if (scroll) emit(UINPUT_FD, EV_REL, REL_WHEEL, should_scroll*scroll);

    emit(UINPUT_FD, EV_SYN, SYN_REPORT, 0);

    if (MOUSE_STATE.l_button_down) 
    {
        if (!MOUSE_STATE.l_button_down_held)
        {
            printf("L_BUTTON_DOWN!\n");
            MOUSE_STATE.l_button_down_held = true;
            emit(UINPUT_FD, EV_KEY, BTN_LEFT, 1);
            emit(UINPUT_FD, EV_SYN, SYN_REPORT, 0);
        }
    }
    if (MOUSE_STATE.l_button_down && MOUSE_STATE.l_button_up) 
    {
        printf("L_BUTTON_UP!\n");
        MOUSE_STATE.l_button_up   = false;
        MOUSE_STATE.l_button_down = false;
        MOUSE_STATE.l_button_down_held = false;
        emit(UINPUT_FD, EV_KEY, BTN_LEFT, 0);
        emit(UINPUT_FD, EV_SYN, SYN_REPORT, 0);
    }

    if (MOUSE_STATE.r_button_down) 
    {
        if (!MOUSE_STATE.r_button_down_held)
        {
            printf("R_BUTTON_DOWN!\n");
            MOUSE_STATE.r_button_down_held = true;
            emit(UINPUT_FD, EV_KEY, BTN_RIGHT, 1);
            emit(UINPUT_FD, EV_SYN, SYN_REPORT, 0);
        }
    }
    if (MOUSE_STATE.r_button_down && MOUSE_STATE.r_button_up) 
    {
        printf("R_BUTTON_UP!\n");
        MOUSE_STATE.r_button_up   = false;
        MOUSE_STATE.r_button_down = false;
        MOUSE_STATE.r_button_down_held = false;
        emit(UINPUT_FD, EV_KEY, BTN_RIGHT, 0);
        emit(UINPUT_FD, EV_SYN, SYN_REPORT, 0);
    }
}

void active_loop(void)
{
    XEvent ev;
    useconds_t prev_time = curr_time();
    char keys_down[32];

    /* Blocking until the activation key is released. Since we use the same key
     * for both activation and deactivation, this prevents accidently 
     * deactivating the keyboard driven cursor. */
    XQueryKeymap(DISPLAY, keys_down); 
    while((keys_down[KC_ACTIVATE_KEY >> 3] & (1 << (KC_ACTIVATE_KEY & 0x7))))
    {
        prev_time = curr_time();
        XQueryKeymap(DISPLAY, keys_down); 
        /* Calculating the number of microseconds to sleep. */
        useconds_t time_diff = curr_time() - prev_time;
        time_diff = (REFRESH_RATE > time_diff) ? REFRESH_RATE - time_diff: 0;
        usleep(time_diff);
    }
    /* Clear input events buffer */
    while (XPending(DISPLAY)) { XNextEvent(DISPLAY, &ev); }

    while (1)
    {
        prev_time = curr_time();

        while (XPending(DISPLAY))
        {
            XNextEvent(DISPLAY, &ev);

            switch(ev.type)
            {
            case KeyPress:
                if ((ev.xkey.keycode == KC_ACTIVATE_KEY) 
                    && (ev.xkey.state == ACTIVATE_MOD))
                {
                    deactivate();
                    printf("deactivate()\n");
                    /* TODO: RESET MOUSE STATE (no keys held down) */
                    return;
                }
                else if ((ev.xkey.keycode == KC_L_BUTTON) && (!ev.xkey.state))
                {
                    MOUSE_STATE.l_button_down = true;
                    MOUSE_STATE.l_button_up = false;
                }
                else if ((ev.xkey.keycode == KC_R_BUTTON) && (!ev.xkey.state))
                {
                    MOUSE_STATE.r_button_down = true;
                    MOUSE_STATE.r_button_up = false;
                }
                else if ((ev.xkey.keycode == KC_SLOW_KEY) && (!ev.xkey.state))
                {
                    MOUSE_STATE.is_slow = true;
                }
                else if ((ev.xkey.keycode == KC_U_KEY) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x01;
                }
                else if ((ev.xkey.keycode == KC_D_KEY) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x02;
                }
                else if ((ev.xkey.keycode == KC_L_KEY) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x04;
                }
                else if ((ev.xkey.keycode == KC_R_KEY) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x08;
                }
                else if ((ev.xkey.keycode == KC_SCROLL_U) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x10;
                }
                else if ((ev.xkey.keycode == KC_SCROLL_D) && (!ev.xkey.state))
                {
                    MOUSE_STATE.key_down_mask |= 0x20;
                }

                break; /* Break out of KeyPress case. */

            }
        }

        /* We cannot rely on KeyRelease events due to a bug in X11 where 
         * not all KeyRelease events are always emitted. This occurs when 
         * multiple keys that are held down are then simultaneously released 
         * together. This bug only occurs when receiving events for 
         * system-wide grabbed keys. To combat this X11 bug, we instead 
         * query for a bit array showing the down/up state of all keys. */
        XQueryKeymap(DISPLAY, keys_down); 

        if (!(keys_down[KC_L_BUTTON >> 3] & (1 << (KC_L_BUTTON & 0x7))))
        {
            MOUSE_STATE.l_button_up = true;
        }
        if (!(keys_down[KC_R_BUTTON >> 3] & (1 << (KC_R_BUTTON & 0x7))))
        {
            MOUSE_STATE.r_button_up = true;
        }
        if (!(keys_down[KC_SLOW_KEY >> 3] & (1 << (KC_SLOW_KEY & 0x7))))
        {
            MOUSE_STATE.is_slow = false;
        }
        if (!(keys_down[KC_U_KEY >> 3] & (1 << (KC_U_KEY & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x01;
        }
        if (!(keys_down[KC_D_KEY >> 3] & (1 << (KC_D_KEY & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x02;
        }
        if (!(keys_down[KC_L_KEY >> 3] & (1 << (KC_L_KEY & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x04;
        }
        if (!(keys_down[KC_R_KEY >> 3] & (1 << (KC_R_KEY & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x08;
        }
        if (!(keys_down[KC_SCROLL_U >> 3] & (1 << (KC_SCROLL_U & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x10;
        }
        if (!(keys_down[KC_SCROLL_D >> 3] & (1 << (KC_SCROLL_D & 0x7))))
        {
            MOUSE_STATE.key_down_mask &= ~0x20;
        }

        uinput_state_emit();

        MOUSE_STATE.prev_key_down_mask = MOUSE_STATE.key_down_mask;

        useconds_t time_diff = curr_time() - prev_time;
        time_diff = (REFRESH_RATE > time_diff) ? REFRESH_RATE - time_diff: 0;
        usleep(time_diff);
    }
}

int main()
{
    struct uinput_setup usetup;
    XEvent ev;
    char keys_down[32];

    if ( !(DISPLAY = XOpenDisplay(NULL)) ) return 1;
    ROOT = DefaultRootWindow(DISPLAY);

    keycodes_init();
    uinput_init(&usetup);

    XGrabKey(DISPLAY, KC_ACTIVATE_KEY, ACTIVATE_MOD, 
        ROOT, True, GrabModeAsync, GrabModeAsync);

    signal(SIGINT,  exit_handler);
    signal(SIGQUIT, exit_handler);
    signal(SIGTSTP, exit_handler);
    
    while(1) 
    {
        XNextEvent(DISPLAY, &ev);
        if (ev.type == KeyPress)
        {
            if ((ev.xkey.keycode == KC_ACTIVATE_KEY) 
                && (ev.xkey.state == ACTIVATE_MOD))
            {
                printf("activate()\n");
                activate();
                active_loop();
                
                // We have now exited the active loop. Block until the 
                // ACTIVATE_KEY is released (this ensures we do not accidently
                // reactivate into key drive mouse mode again).
                XQueryKeymap(DISPLAY, keys_down); 
                while((keys_down[KC_ACTIVATE_KEY >> 3] & 
                    (1 << (KC_ACTIVATE_KEY & 0x7))))
                {
                    XQueryKeymap(DISPLAY, keys_down); 
                    usleep(REFRESH_RATE);
                }
                /* Clear input events buffer */
                while (XPending(DISPLAY)) { XNextEvent(DISPLAY, &ev); }
            }
        }
    }
}


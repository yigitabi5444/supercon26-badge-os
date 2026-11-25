static int keyq[16], kn, ki; // queued key events: key | 0x100 if pressed

static void key(int k, bool down)
{
    if (kn < 16) keyq[kn++] = k | down << 8;
}

static void read_buttons(void)
{
    static int was, use, knob_ref = -1;
    static int sent[2] = {-1, -1}; // A, B: 0 = undecided, KEY_x = held down, -1 = idle, -2 - KEY_x = up next poll
    static int64_t down[2];
    int h = btn_held(), now = h & ~was;
    int64_t t = esp_timer_get_time() / 1000;
    // keys go up a poll (a tic) after they went down at the earliest, or Doom would not see a quick tap
    if (use) key(KEY_USE, false), use = 0;
    for (int i = 0; i < 2; i++)
        if (sent[i] < -1) key(-2 - sent[i], false), sent[i] = -1;
    if ((demoplayback || gamestate == GS_DEMOSCREEN || gamestate == GS_FINALE) && now) {
        G_DeferedInitNew(sk_medium, 1, 1); // title or the end of the episode: no menus here, a press starts anew
        was = h;
        return;
    }
    if ((h ^ was) & 1 << BTN_PLUS) key(KEY_LEFTARROW, h & 1 << BTN_PLUS);
    if ((h ^ was) & 1 << BTN_MINUS) key(KEY_RIGHTARROW, h & 1 << BTN_MINUS);
    for (int i = 0; i < 2; i++)
        if (now & 1 << (i ? BTN_B : BTN_A)) down[i] = t, sent[i] = 0;
    if (!sent[0] && !sent[1]) key(KEY_USE, true), use = 1, sent[0] = sent[1] = -1; // both within the window
    for (int i = 0; i < 2; i++) {
        bool held = h & 1 << (i ? BTN_B : BTN_A);
        if (!sent[i] && (!held || t - down[i] >= CHORD_MS)) key(sent[i] = i ? KEY_FIRE : KEY_UPARROW, true);
        if (sent[i] > 0 && !held) sent[i] = -2 - sent[i];
    }
    was = h;

    int a = knob_angle(); // the knob turns the view 1:1: a mouse x of 2 per AS5600 step (4096 per turn)
    if (a >= 0 && knob_ref >= 0) {
        int d = ((a - knob_ref) % 4096 + 6144) % 4096 - 2048;
        if (d) D_PostEvent(&(event_t){.type = ev_mouse, .data2 = 2 * KNOB_DIR * d});
    }
    knob_ref = a;
}

// The engine drains this until it returns 0, once per tic: read the buttons at most once per drain, so a release queued
// for the next poll lands in the next tic (a press and its release in the same tic never reach the game: use, taps).
int DG_GetKey(int *pressed, unsigned char *k)
{
    static bool polled;
    if (ki == kn) {
        ki = kn = 0;
        if (polled) return polled = false, 0;
        read_buttons();
        if (!kn) return 0;
        polled = true;
    }
    *pressed = keyq[ki] >> 8;
    *k = keyq[ki++];
    return 1;
}


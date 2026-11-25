// Original Asteroids outline rendering via ABI2 boxes; GPL-2.0, see root LICENSE.
#pragma once
static const char empty[] __attribute__((aligned(4)))="";
static const char over_prefix[] __attribute__((aligned(4)))=" GAME OVER  ";
static const char space[] __attribute__((aligned(4)))=" ";
static int number_text(char *out,const char *prefix,int value,const char *suffix)
{
    int n=0,count=0;char digits[10];
    while(*prefix)out[n++]=*prefix++;
    uint32_t u=(uint32_t)value;if(value<0)out[n++]='-',u=0u-u;
    do{digits[count++]=(char)('0'+u%10);u/=10;}while(u);
    while(count)out[n++]=digits[--count];
    while(*suffix)out[n++]=*suffix++;
    out[n]=0;return n;
}
static void dot(int x,int y)
{
    int px=((x-1)%W+W)%W,py=((y-1)%PH+PH)%PH;
    int w=px+3>W?W-px:3,h=py+3>PH?PH-py:3;
    module_box(px,py+TOP,w,h,true);
    if(w<3)module_box(0,py+TOP,3-w,h,true);
    if(h<3)module_box(px,TOP,w,3-h,true);
    if(w<3&&h<3)module_box(0,TOP,3-w,3-h,true);
}
static void line(float x0, float y0, float x1, float y1)
{
    int n = fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= n; i++) dot(lroundf(x0 + (x1 - x0) * i / n), lroundf(y0 + (y1 - y0) * i / n));
}

// polygon of n corners given in the ship's frame (f forward, s to the right), heading a, at x, y
static void shape(float x, float y, float a, int n, const float (*p)[2])
{
    float c = cosf(a), s = sinf(a);
    for (int k = 0; k < n; k++) {
        const float *u = p[k], *v = p[(k + 1) % n];
        line(x + u[0] * s + u[1] * c, y - u[0] * c + u[1] * s, x + v[0] * s + v[1] * c, y - v[0] * c + v[1] * s);
    }
}

static void ship(float x, float y, float a, float k) // k: scale
{
    const float p[4][2] = {{26 * k, 0}, {-18 * k, 15 * k}, {-9 * k, 0}, {-18 * k, -15 * k}};
    shape(x, y, a, 4, p);
}

static void rocks_draw(void)
{
    module_clear();
    for (int i = 0; i < MAXR; i++) {
        rock_t *r = &g.rk[i];
        if (!r->size) continue;
        float p[NV][2];
        for (int k = 0; k < NV; k++) {
            float q = 2 * (float)M_PI * k / NV, rr = RAD[r->size] * r->r[k] / 255;
            p[k][0] = rr * cosf(q), p[k][1] = rr * sinf(q);
        }
        shape(r->x, r->y, 0, NV, p);
    }
    for (int i = 0; i < MAXB; i++)
        if (g.sh[i].life) line(g.sh[i].x, g.sh[i].y, g.sh[i].x - g.sh[i].vx * 0.02f, g.sh[i].y - g.sh[i].vy * 0.02f);
    if (!g.gone) {
        ship(g.x, g.y, g.a, 1);
        if (g.thrust) {
            const float f[3][2] = {{-13, 6}, {-30 - (g.t & 1) * 8, 0}, {-13, -6}};
            shape(g.x, g.y, g.a, 3, f);
        }
        if (g.shield) {
            float p[12][2];
            for (int k = 0; k < 12; k++) p[k][0] = 36 * cosf(k * (float)M_PI / 6), p[k][1] = 36 * sinf(k * (float)M_PI / 6);
            shape(g.x, g.y, 0, 12, p);
        }
    }
    char s[24];
    number_text(s, empty, g.score, empty);
    module_text(12, TOP + 12, 3, s, true);
    for (int i = 0; i < g.lives; i++) ship(W - 30 - i * 36, 32, 0, 0.6f);
    if (g.over) {
        int n = number_text(s, over_prefix, g.score, space), x = (W - n * 32) / 2;
        module_box(x - 8, H / 2 - 32, n * 32 + 16, 64, false);
        module_outline(x - 8, H / 2 - 32, n * 32 + 16, 64, 4);
        module_text(x, H / 2 - 16, 4, s, true);
    }
}

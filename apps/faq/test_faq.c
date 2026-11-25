// Host check: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I components/os/include apps/faq/test_faq.c -o /tmp/test-faq
// Run /tmp/test-faq; optionally pass an existing directory for reader-N.pgm previews.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "faq.c"
#include "badge/font8x8_basic.h"

uint8_t fb[PLANE];
static int frames, closes, at, nkeys, keys[32];
static jmp_buf stopped;
static char visible[64];
static struct { int x, y, scale; char s[64]; } drawn[96];
static int ndrawn;

void box(int x, int y, int w, int h, bool ink)
{
    assert(x >= 0 && y >= TOP && x + w <= W && y + h <= H);
    for (int r = y; r < y + h; r++)
        for (int c = x; c < x + w; c++)
            if (ink) fb[r * (W / 8) + c / 8] |= 0x80 >> (c & 7);
            else fb[r * (W / 8) + c / 8] &= ~(0x80 >> (c & 7));
}
void outline(int x, int y, int w, int h, int t)
{
    box(x,y,w,t,true); box(x,y+h-t,w,t,true);
    box(x,y,t,h,true); box(x+w-t,y,t,h,true);
}
void text(int x, int y, int scale, const char *s, bool ink)
{
    assert(x >= 0 && y >= TOP && y + 8 * scale <= H);
    assert(x + strlen(s) * 8 * scale <= W && ndrawn < 96 && strlen(s) < 64);
    drawn[ndrawn].x=x; drawn[ndrawn].y=y; drawn[ndrawn].scale=scale;
    strcpy(drawn[ndrawn++].s,s);
    if (x == RIGHT && scale == 3) strcpy(visible,s);
    for (; *s; s++, x += 8 * scale)
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (font8x8_basic[*s & 0x7f][r] >> c & 1)
                    box(x+c*scale,y+r*scale,scale,scale,ink);
}
void icon_base(int x, int y) { (void)x; (void)y; }
void ui_keys(const char *plus, const char *minus, const char *a, const char *b)
{
    assert(!strcmp(plus,"Topic up") && !strcmp(a,"Topic down"));
    assert(!strcmp(minus,"Read up") && !strcmp(b,"Read down"));
}
void ui_frame(int pulls) { assert(pulls==1 && frames==closes); frames++; }
void ui_close(void) { assert(frames==closes+1); closes++; }
int btn_get(int timeout_ms)
{
    assert(timeout_ms==-1 && frames==closes);
    if (at==nkeys) longjmp(stopped,1);
    ndrawn=0;
    return keys[at++];
}
static void within_lines(const char *p, int cols, int maxlines)
{
    int width=0, lines=1;
    for (; *p; p++) {
        assert((*p>=32 && *p<=126) || *p=='\n');
        if (*p=='\n') { assert(width<=cols); width=0; lines++; }
        else width++;
    }
    assert(width<=cols && lines<=maxlines);
}
static void dump(const char *dir, int topic)
{
    char path[512]; int n=snprintf(path,sizeof path,"%s/reader-%d.pgm",dir,topic+1);
    assert(n>0 && n<(int)sizeof path);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P5\n%d %d\n255\n",W,H);
    for (int i=0;i<W*H;i++) fputc(i<TOP*W ? 225 : fb[i/8]&(0x80>>(i&7)) ? 0 : 255,f);
    int rc=fclose(f); assert(!rc);
}
int main(int argc, char **argv)
{
    assert(argc<=2 && NTOPICS==10 && VISIBLE==4);
    assert(!strcmp(app_faq.name,"FAQ") && app_faq_info.launch_info);
    assert(app_faq.icon && app_faq.run && app_faq_info.info && app_faq_info.credit);
    for (int t=0;t<NTOPICS;t++) {
        within_lines(topics[t].label,17,1); within_lines(topics[t].summary,17,2);
        within_lines(topics[t].title,24,2); within_lines(topics[t].body,COLS,64);
        bool seen[64]={0}; reader_t r={t,t>=VISIBLE?t-VISIBLE+1:0,0};
        do {
            memset(fb,0xa5,sizeof fb); frames=closes=ndrawn=0;
            draw_reader(&r); assert(frames==1 && closes==1);
            bool above=false, below=false;
            for(int i=0;i<ndrawn;i++) {
                if(!strcmp(drawn[i].s,"^ More")) { above=true; assert(drawn[i].y<TOP+60); }
                if(!strncmp(drawn[i].s,"v More below ",13)) { below=true; assert(drawn[i].y==H-56); }
            }
            assert(above==(r.first>0));
            assert(below==(r.first+VISIBLE<NTOPICS));
            for (int i=0;i<TOP*W/8;i++) assert(fb[i]==0xa5);
            const char *p=topics[t].body;
            int line=0,y=BODY_Y+diagram_height(t)-r.scroll*LINE;
            for (; *p; line++,y+=LINE) {
                char s[COLS+1]; p=read_line(p,s);
                if (y>=BODY_Y && y+16<=BODY_END) {
                    bool found=false;
                    for (int i=0;i<ndrawn;i++) if(drawn[i].x==RIGHT && drawn[i].y==y &&
                        drawn[i].scale==2 && !strcmp(drawn[i].s,s)) found=true;
                    assert(found); seen[line]=true;
                }
            }
            if (argc==2 && r.scroll==0) dump(argv[1],t);
        } while (reader_step(&r,BTN_B));
        assert(r.scroll==max_scroll(t));
        const char *p=topics[t].body;
        for (int i=0;*p;i++) { char s[COLS+1]; p=read_line(p,s); assert(seen[i]); }
        while(reader_step(&r,BTN_MINUS)) {}
        assert(r.scroll==0 && !reader_step(&r,BTN_MINUS));
        printf("PASS: %s, every answer line reachable, all panes/diagram scrolls bounded\n",topics[t].label);
    }
    reader_t r={0}; assert(!reader_step(&r,BTN_PLUS) && !reader_step(&r,BTN_B));
    for (int t=1;t<NTOPICS;t++) {
        assert(reader_step(&r,BTN_A) && r.topic==t && r.scroll==0);
        assert(r.first<=t && t<r.first+VISIBLE);
        reader_step(&r,BTN_B);
    }
    assert(!reader_step(&r,BTN_A));
    for (int t=NTOPICS-2;t>=0;t--) {
        assert(reader_step(&r,BTN_PLUS) && r.topic==t && r.scroll==0);
        assert(r.first<=t && t<r.first+VISIBLE);
    }
    assert(r.first==0 && !reader_step(&r,-1));
    int seq[]={BTN_PLUS,BTN_MINUS,-1,BTN_A,BTN_A,BTN_B,BTN_B,BTN_MINUS,BTN_A};
    memcpy(keys,seq,sizeof seq); nkeys=sizeof seq/sizeof *seq; at=frames=closes=ndrawn=0;
    if (!setjmp(stopped)) app_faq.run();
    assert(at==nkeys && frames==7 && closes==7 && !strcmp(visible,"How fast refresh works"));
    puts("PASS: physical left/right controls, topic-list scrolling, answer reset, endpoints, more-above/below indicators and idle closure; no idle refresh loop");
}

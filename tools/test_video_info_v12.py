#!/usr/bin/env python3
"""Run the actual Video app with a tiny clip to check Info pauses its playback clock."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];out=Path(tempfile.mkdtemp(prefix='badge-video-info-'))
(out/'esp_attr.h').write_text('#define EXT_RAM_BSS_ATTR\n')
(out/'esp_log.h').write_text('void test_log(const char *fmt,...);\n#define ESP_LOGI(tag,...) test_log(__VA_ARGS__)\n')
(out/'stb_image.h').write_text('int stbi_zlib_decode_buffer(char *, int, const char *, int);\n')
(out/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "video.c"
uint8_t fb[PLANE];
// VID2, 8x1, 3 frames, 4 fps; the decoder mock provides one byte per frame.
const uint8_t clip[] asm("_binary_rickroll_vid_start")={
 'V','I','D','2',8,0,1,0,3,0,4,0, 1,0,0,0,0, 1,0,0,0,0, 1,0,0,0,0};
static int frames,decodes,infos,wipes,closes,buttons;
static int64_t now=1000000;
void test_log(const char *fmt,...) {}
int64_t esp_timer_get_time(void) { return now; }
void ms(int t) { assert(t>=0); now+=(int64_t)t*1000; }
void ui_keys(const char *plus,const char *minus,const char *a,const char *b) {
 assert(!strcmp(plus,"Info") && !minus && !strcmp(a,"Restart") && !strcmp(b,"Home"));
}
void icon_base(int x,int y) { (void)x; (void)y; }
void box(int x,int y,int w,int h,bool ink) { (void)x; (void)y; (void)w; (void)h; (void)ink; }
void outline(int x,int y,int w,int h,int t) { (void)x; (void)y; (void)w; (void)h; (void)t; }
void ui_wipe(void) { assert(++wipes==1); memset(fb,0,PLANE); }
void ui_close(void) { closes++; }
void ui_info(void) { infos++; now+=2000000; }
void ui_frame(int n) {
 assert(n==1); frames++;
 assert(fb[(TOP+(H-TOP-1)/2)*(W/8)+(W-8)/16]==frames);
}
int btn_get(int timeout) { assert(timeout==0); return ++buttons==1?BTN_PLUS:buttons==3?BTN_B:-1; }
int stbi_zlib_decode_buffer(char *out,int n,const char *in,int size) {
 assert(n==1 && size==1); *out=++decodes; return 1;
}
int main(void) {
 app_video.run();
 assert(frames==3 && decodes==3 && infos==1 && closes==1 && now==3500000);
 puts("PASS actual Video app: Info labeled, 2-second pause does not skip subsequent frames, Home closes panel");
}
'''
p=out/'harness.c';p.write_text(code)
subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I',str(out),'-I',str(root/'apps/video'),'-I',str(root/'components/os/include'),str(p),'-o',str(out/'video')],check=True)
subprocess.run([str(out/'video')],check=True)
print('Video Info harness:',out)

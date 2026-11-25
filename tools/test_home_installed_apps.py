#!/usr/bin/env python3
"""Exercise actual Home selection, installed-entry deduplication and caption fitting offline."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'main/home.c').read_text()
def function(name):
 start=source.index('static '+name);opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define W 960
#define H 672
#define ICON 176
#define PITCH 224
#define X0 56
#define Y0 80
#define ROW 300
#define MIN(a,b) ((a)<(b)?(a):(b))
enum { HOME_EXTRA_MAX=7 };
typedef struct {const char*name;void(*icon)(int,int);void(*run)(void);} app_t;
static void icon(int x,int y){(void)x;(void)y;}
static void run(void){}
static app_t badge={"Badge",icon,run},clock_faces={"Clock Faces",icon,run},manager={"App Manager",icon,run};
static const app_t *const apps[]={&badge,&clock_faces,&manager};
static const int napps=3;
static const app_t *provided[10];static size_t provided_count;static int scans,captions;
static size_t app_manager_home_apps(const app_t**out,size_t cap) {
 scans++;for(size_t i=0;i<provided_count&&i<cap;i++)out[i]=provided[i];return provided_count;
}
static uint8_t fb[1];
typedef struct {const char*version;} desc_t;
static const desc_t *esp_app_get_description(void){static desc_t d={"test"};return &d;}
static void text(int x,int y,int scale,const char*s,bool black){
 (void)x;(void)y;(void)black;
 if(!strncmp(s,"badge os",8))return;
 assert((int)strlen(s)*8*scale<=PITCH-24);captions++;
}
static void outline(int x,int y,int w,int h,int r){(void)x;(void)y;(void)w;(void)h;(void)r;}
static void box(int x,int y,int w,int h,bool black){(void)x;(void)y;(void)w;(void)h;(void)black;}
"""
code+=function('const app_t *home_app(').replace('static const','static const',1)+'\n'
code+=function('unsigned refresh_home_apps(')+'\n'+function('void draw_home(')+'\n'
# Keep the production optional-hook null check meaningful to the compiler.
code=code.replace('static size_t app_manager_home_apps(', 'static size_t provider(')
code=code.replace('static uint8_t fb[1];','static size_t (*app_manager_home_apps)(const app_t**,size_t)=provider;\nstatic uint8_t fb[1];')
code+=r"""
int main(void){
 const app_t *extra[HOME_EXTRA_MAX]={0};int selected=2;
 assert(refresh_home_apps(extra,&selected)==0&&selected==2);
 app_t alien={"SpaceInv",icon,run},same_badge={"Badge",icon,run},bad={"invalid",NULL,run};
 provided[0]=&alien;provided[1]=&same_badge;provided[2]=&alien;provided[3]=&bad;provided[4]=NULL;provided_count=5;
 assert(refresh_home_apps(extra,&selected)==1&&selected==2&&home_app(3,extra)==&alien);
 selected=3;assert(refresh_home_apps(extra,&selected)==1&&selected==3);
 int count=scans;draw_home(selected,extra,1);assert(scans==count&&captions==4);
 provided_count=0;assert(refresh_home_apps(extra,&selected)==0&&selected==0);
 const app_t *many[7];app_t entries[7];const char*names[]={"a","b","c","d","e","f","g"};
 for(int i=0;i<7;i++){entries[i]=(app_t){names[i],icon,run};many[i]=&entries[i];provided[i]=many[i];}
 provided_count=10;assert(refresh_home_apps(extra,&selected)==7);
 for(int i=0;i<7;i++)assert(home_app(napps+i,extra)==many[i]);
 app_manager_home_apps=NULL;selected=1;assert(refresh_home_apps(extra,&selected)==0&&selected==1);
 puts("PASS Home: empty store, installed-only append, dedup/invalid records, stable selection/removal, bounded capacity, captions and no render-time scans");
}
"""
with tempfile.TemporaryDirectory(prefix='badge-home-glue-') as d:
 p=Path(d);c=p/'test.c';exe=p/'test';c.write_text(code)
 subprocess.run(['cc','-std=c11','-fno-common','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

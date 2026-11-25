// Actual native drawing + state, with synthetic installed rows only in this harness.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "badge/font8x8_basic.h"
#include "../manager_ui.c"

static uint8_t frame[W * H / 8];
static const char *labels[4];
static jmp_buf finished;
static unsigned event, frame_count, close_count;
static bool panel_open,idle_once;
static bool record_text;
static char rendered_text[4096];
static bool audit_version_lane;
static bool audit_footer;
static bool lease_only,lease_active,drawer_busy;
static unsigned shutdowns,cleanup_polls;
static am_controller_t *lease_controller;
static bool compact_audit;
static char list_lines[H][48];
static int list_right[H],list_gap[H];
static const int browse_events[] = {BTN_PLUS, BTN_B, BTN_A, BTN_A}; // Disabled install, then Codex details.
static const int *events = browse_events;
static unsigned event_limit = sizeof browse_events / sizeof *browse_events;

static void ink_box(int x,int y,int w,int h,bool ink)
{
    if(!(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=W && y+h<=H))fprintf(stderr,"Bad rectangle %d,%d %dx%d\n",x,y,w,h);
    assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=W && y+h<=H);
    for(int r=y;r<y+h;r++) for(int c=x;c<x+w;c++) {
        uint8_t *byte=&frame[r*(W/8)+c/8],mask=0x80>>(c&7);
        if(ink)*byte|=mask;else *byte&=~mask;
    }
}
void box(int x,int y,int w,int h,bool ink){assert(!lease_only && y>=TOP);ink_box(x,y,w,h,ink);}
static void ink_text(int x,int y,int scale,const char*s,bool ink)
{
    assert(x>=0 && y>=0 && x+(int)strlen(s)*8*scale<=W && y+8*scale<=H);
    for(;*s;s++,x+=8*scale)for(int r=0;r<8;r++)for(int c=0;c<8;c++)
        if(font8x8_basic[(unsigned char)*s&127][r]>>c&1)ink_box(x+c*scale,y+r*scale,scale,scale,ink);
}
void text(int x,int y,int scale,const char*s,bool ink)
{
    assert(!lease_only && y>=TOP);
    if(compact_audit && x<list_clip.right && y>=LIST_Y && y<LIST_END) {
        assert(scale==2 && strlen(list_lines[y])+strlen(s)<sizeof list_lines[y]);
        if(list_right[y] && x>list_right[y])list_gap[y]=x-list_right[y];
        list_right[y]=x+(int)strlen(s)*8*scale;strcat(list_lines[y],s);
    }
    if(audit_footer && y==H-52)assert(x+(int)strlen(s)*8*scale<=W-16);
    if(audit_version_lane && y==LIST_END-28 && ((s[0]>='0' && s[0]<='9') || s[0]=='.'))
        assert(x+8*scale<=256);
    if(record_text){assert(strlen(rendered_text)+strlen(s)<sizeof rendered_text);strcat(rendered_text,s);}
    ink_text(x,y,scale,s,ink);
}
void ui_keys(const char*p,const char*m,const char*a,const char*b){assert(!lease_only);labels[0]=p;labels[1]=m;labels[2]=a;labels[3]=b;}
void ui_frame(int n)
{
    assert(!lease_only && n==1);frame_count++;
    ink_box(0,0,W,TOP,false);
    ink_text(14,4,2,labels[0]?labels[0]:"",true);ink_text(14,26,2,labels[2]?labels[2]:"",true);
    if(labels[1])ink_text(W-14-(int)strlen(labels[1])*16,4,2,labels[1],true);
    if(labels[3])ink_text(W-14-(int)strlen(labels[3])*16,26,2,labels[3],true);
    ink_text(350,4,2,"App Manager",true);ink_text(350,26,2,"Guest  v1.2",true);
    ink_box(0,44,W,2,true);
    panel_open=true;
}
void ui_close(void){assert(!lease_active);close_count++;panel_open=false;}
int btn_get(int timeout)
{
    if(lease_only) {
        assert(timeout==20 && lease_active && drawer_busy && shutdowns==1);
        assert(lease_controller && !lease_controller->execution_copy && lease_controller->lease.owner);
        cleanup_polls++;drawer_busy=false;return -1;
    }
    assert(timeout==-1 || timeout==800);
    if(timeout==-1)assert(!panel_open);
    if(timeout==800 && idle_once){idle_once=false;return -1;}
    if(event==event_limit)longjmp(finished,1);
    return events[event++];
}
int btn_held(void){return 0;}
void btn_flush(void){}
am_result_t am_target_start(am_controller_t *c)
{
    memset(c,0,sizeof *c);c->state=AM_UNASSIGNED;
    if(lease_only) {
        c->state=AM_FAULT;c->store.lease=&c->lease;c->lease=(am_lease_t){&c->store,1};c->store.generation=1;
        lease_controller=c;lease_active=drawer_busy=true;return AM_OK;
    }
    return AM_DISABLED;
}
am_result_t am_controller_action(am_controller_t *c,unsigned id,enum am_action a)
{(void)c;assert(id<AM_COUNT && !panel_open);(void)a;return AM_OK;}
am_result_t am_target_prepare(am_controller_t *c,am_progress_t p){(void)c;(void)p;return AM_DISABLED;}
bool wifi_connected(void){return false;}
bool am_https_clock_ready(void){return true;}
am_result_t am_controller_shutdown(am_controller_t *c)
{
    if(lease_only) {
        assert(c==lease_controller && !c->execution_copy && c->lease.owner==&c->store && c->store.lease==&c->lease);
        shutdowns++;
        if(drawer_busy){assert(shutdowns==1);return AM_BUSY;}
        assert(shutdowns==2 && cleanup_polls==1);c->store.lease=NULL;c->lease=(am_lease_t){0};c->state=AM_CLOSED;lease_active=false;
    }
    return AM_OK;
}
const char *am_controller_notice(am_result_t r){return r==AM_OK?NULL:r==AM_UP_TO_DATE?"Up to date":"Unavailable";}
void icon_base(int x,int y){(void)x;(void)y;}

static void save(const char *path)
{
    FILE *out=fopen(path,"wb");assert(out);fprintf(out,"P5\n%d %d\n255\n",W,H);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)fputc(frame[y*(W/8)+x/8]&(0x80>>(x&7))?0:255,out);
    assert(!fclose(out));
}

static int list_line(const char *s)
{
    for(int y=LIST_Y;y<LIST_END;y++)if(!strcmp(list_lines[y],s))return y;
    assert(!"Missing compact row text");return -1;
}
static void clear_list_audit(void)
{memset(list_lines,0,sizeof list_lines);memset(list_right,0,sizeof list_right);memset(list_gap,0,sizeof list_gap);}
static int size_row(unsigned id,const char *size)
{
    char line[64];snprintf(line,sizeof line,"%s%s",am_catalogue[id].name,size);
    int y=list_line(line);assert(list_right[y]==list_clip.right && list_gap[y]>=8);return y;
}
static void detail_size(am_view_t *v,const am_local_t local[AM_COUNT],const char *size)
{
    compact_audit=false;record_text=true;rendered_text[0]=0;details(v,local);record_text=false;compact_audit=true;
    assert(strstr(rendered_text,size));
    if(size[0]!='~'){char approximate[32];snprintf(approximate,sizeof approximate,"~%s",size);assert(!strstr(rendered_text,approximate));}
}
static int compact_core14(const char *output)
{
    am_home_reset();am_local_t local[AM_COUNT]={0};unsigned order[AM_COUNT];
    for(unsigned i=0;i<AM_COUNT;i++)local[i].fetch_available=true;
    unsigned installed=3;assert(!strcmp(am_catalogue[installed].id,"aliens"));
    local[installed].installed=true;local[installed].version="1.0.2";local[installed].size=4928;
    assert(am_order(local,order)==7 && order[6]==installed);
    const char *sizes[]={"~13 KiB","~17 KiB","~321 KiB","5 KiB","~13 KiB","~13 KiB","~806 KiB"};
    am_view_t v={0};compact_audit=true;
    for(unsigned selection=0;selection<AM_COUNT;selection++) {
        memset(frame,0,sizeof frame);clear_list_audit();
        v=(am_view_t){.selected=selection,.list_scroll=999};list(&v,local);
        assert(v.list_scroll==0); // All seven rows and both group headings fit, even from stale scroll.
        int last=0;unsigned missing=0;
        for(unsigned k=0;k<AM_COUNT;k++) {
            unsigned id=order[k];assert(!am_has_home(id));
            int y=size_row(id,sizes[id]);
            if(k)assert(y-last==(k==6?90:60));
            last=y;
            assert(y+40<=LIST_END);
            assert(!strcmp(list_lines[y+24],k==6?"1.0.2":"Not downloaded"));
            missing+=k<6;
            // Compare actual raster pixels: every icon is its original 32px bitmap at scale1.
            const uint8_t *icon=am_icons[am_catalogue[id].icon];
            for(int row=0;row<32;row++)for(int col=0;col<32;col++) {
                unsigned pixel=(unsigned)(y+4+row)*W+28+(unsigned)col;
                assert(!!(frame[pixel/8]&(0x80>>(pixel&7)))==!!(icon[row*4+col/8]&(0x80>>(col&7))));
            }
        }
        assert(missing==6 && list_line("Downloads")<size_row(order[0],sizes[order[0]]));
        assert(list_line("Installed")>size_row(order[5],sizes[order[5]]) && list_line("Installed")<last);
        detail_size(&v,local,sizes[selection]);
        unsigned pos=0;while(order[pos]!=selection)pos++;
        am_next(&v,local);assert(v.selected==order[(pos+1)%AM_COUNT] && !v.info_scroll);
    }
    // A verified cached package keeps its download status and version in the same compact row.
    local[0].package_available=true;local[0].package_version="1.0.0";local[0].package_size=14*1024;
    clear_list_audit();v=(am_view_t){.selected=0};list(&v,local);
    assert(list_line("Downloaded 1.0.0")>size_row(0,"14 KiB"));detail_size(&v,local,"14 KiB");
    local[0].installed=true;local[0].size=15*1024;local[0].version="1.0.1";
    clear_list_audit();list(&v,local);size_row(0,"15 KiB");detail_size(&v,local,"15 KiB");
    local[0].disabled=local[0].update_available=true;detail_size(&v,local,"15 KiB");
    assert(strstr(rendered_text,"Disabled") && strstr(rendered_text,"Update available"));local[0]=(am_local_t){.fetch_available=true};
    local[6].package_available=true;local[6].package_size=4880;local[6].package_version="1.0.0";
    clear_list_audit();v=(am_view_t){.selected=6};list(&v,local);size_row(6,"806 KiB");detail_size(&v,local,"806 KiB");
    assert(strstr(rendered_text,"Includes clip"));local[6]=(am_local_t){.fetch_available=true};
    local[installed].version="123456.123456.123456";local[installed].update_available=true;
    clear_list_audit();v=(am_view_t){.selected=installed};list(&v,local);int y=size_row(installed,"5 KiB");
    assert(strstr(list_lines[y+24],"Update") && list_gap[y+24]>=16 && list_right[y+24]<=list_clip.right);
    local[installed].version="1.0.2";local[installed].update_available=false;
    clear_list_audit();box(0,TOP,W,H-TOP,false);list(&v,local);
    compact_audit=false;details(&v,local);network_status(RIGHT,TOP+48,1);
    const am_controller_t sample={.state=AM_READY,.capacity=2490368,.used=64*1024,.store={.reserve_bytes=20*1024}};
    storage_status(&sample);ui_keys("Actions","Info up","Next app","Info down");ui_frame(1);ui_close();
    ink_box(330,24,300,20,false);ink_text(350,26,2,"Layout fixture",true);save(output);
    puts("PASS Core14 compact sizes: seven visible 60px rows/32px icons; full names, right-aligned KiB, no Update overlap; approximate unfetched hints, verified fetched/installed priority, Video clip total and matching details; actual C renderer, synthetic inventory/storage, no device");
    return 0;
}
static int fresh_core14(const char *output)
{
    am_home_reset();am_local_t local[AM_COUNT]={0};unsigned order[AM_COUNT];
    assert(am_order(local,order)==AM_COUNT);
    for(unsigned id=0;id<AM_COUNT;id++) {
        am_action_t actions[4];assert(order[id]==id && !local[id].installed && !am_has_home(id));
        assert(am_actions(id,&local[id],true,actions)==2 && actions[0].kind==AM_INSTALL && !actions[0].enabled);
    }
    am_view_t v={.selected=0};am_controller_t unprovisioned={.state=AM_UNASSIGNED};
    box(0,TOP,W,H-TOP,false);list(&v,local);details(&v,local);storage_status(&unprovisioned);
    network_status(RIGHT,TOP+48,1);
    ui_keys("Actions","Info up","Next app","Info down");ui_frame(1);ui_close();
    ink_box(330,24,300,20,false);ink_text(350,26,2,"Layout fixture",true);save(output);
    puts("PASS fresh Core14: all seven visible, none installed, unconfigured Install disabled; actual renderer with synthetic empty inventory");
    return 0;
}

int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--compact-core14"))return compact_core14(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--fresh-core14"))return fresh_core14(argv[2]);
    assert(argc==2);
    if(!strcmp(argv[1],"--lease-only")) {
        lease_only=true;memset(frame,0x5a,sizeof frame);app_manager_ui();
        assert(shutdowns==2 && cleanup_polls==1 && !lease_active && !drawer_busy && !frame_count && close_count==1);
        assert(lease_controller->state==AM_CLOSED && !lease_controller->lease.owner && !lease_controller->store.lease);
        for(size_t i=0;i<sizeof frame;i++)assert(frame[i]==0x5a);
        puts("PASS lease-only busy cleanup: no manager redraw/labels/panel close, lease retained through wait, cleanup completes before return");return 0;
    }
    unsigned native=AM_COUNT;
    for(unsigned i=0;i<AM_COUNT;i++) {
        assert(strcmp(am_catalogue[i].id,"counter"));
        if(!strcmp(am_catalogue[i].id,"aliens"))native=i;
    }
    assert(native<AM_COUNT && !strcmp(am_catalogue[native].name,"SpaceInv"));
    assert(!strcmp(am_catalogue[1].name,"Messages"));
    am_local_t local[AM_COUNT]={0};am_action_t a[4];
    unsigned profile[AM_COUNT];
    // A fresh core-only profile lists all seven, with no install source enabled.
    am_home_reset();assert(am_order(local,profile)==AM_COUNT);
    for(unsigned i=0;i<AM_COUNT;i++) {
        assert(profile[i]==i && !am_is_builtin(i,&local[i]));
        assert(am_actions(i,&local[i],true,a)==2 && a[0].kind==AM_INSTALL && !a[0].enabled);
    }
    unsigned rocks=0;for(;rocks<AM_COUNT;rocks++)if(!strcmp(am_catalogue[rocks].id,"rocks"))break;
    assert(rocks<AM_COUNT && am_downloadable(rocks,&local[rocks]));
    local[rocks].fetch_available=true;
    assert(am_order(local,profile)==AM_COUNT && am_downloadable(rocks,&local[rocks]));
    assert(am_actions(rocks,&local[rocks],true,a)==2 && a[0].kind==AM_INSTALL && a[0].enabled);
    local[rocks]=(am_local_t){0};assert(am_order(local,profile)==AM_COUNT);
    am_view_t single={.selected=native};am_next(&single,local);assert(single.selected==(native+1)%AM_COUNT);
    single.selected=native;
    record_text=true;rendered_text[0]=0;details(&single,local);record_text=false;
    assert(!strstr(rendered_text,"Built-in"));
    // Legacy Home copies keep their built-in status; absent Messages stays available for provisioning.
    for(unsigned i=0;i<AM_COUNT;i++)if(i!=1)am_home_add(am_catalogue[i].name);
    assert(am_order(local,profile)==AM_COUNT && !am_is_builtin(1,&local[1]) && am_has_home(native));
    record_text=true;rendered_text[0]=0;details(&single,local);record_text=false;
    assert(strstr(rendered_text,"Built-in copy on Home."));
    // Full21 adds Messages; native SpaceInv stays separate from its Home copy.
    am_home_add("Messages");assert(am_order(local,profile)==AM_COUNT && !am_is_builtin(native,&local[native]));
    for(unsigned i=0;i<AM_COUNT;i++)if(i!=native) {
        assert(am_is_builtin(i,&local[i]));
        assert(am_actions(i,&local[i],true,a)==1 && a[0].kind==AM_DETAILS);
    }
    assert(am_actions(2,&local[2],true,a)==1 && a[0].kind==AM_DETAILS);
    am_view_t built_in_view={.selected=2};record_text=true;rendered_text[0]=0;details(&built_in_view,local);record_text=false;
    assert(strstr(rendered_text,"Built-in") && !strstr(rendered_text,"bogus") && !strstr(rendered_text,"9999"));
    local[2]=(am_local_t){0};
    size_t n=am_actions(native,&local[native],false,a);
    assert(n==2 && a[0].kind==AM_INSTALL && !a[0].enabled);
    local[native].fetch_available=true;n=am_actions(native,&local[native],false,a);
    assert(n==2 && a[0].kind==AM_INSTALL && !a[0].enabled);
    n=am_actions(native,&local[native],true,a);assert(a[0].kind==AM_INSTALL && a[0].enabled);
    local[native].package_available=true;local[native].package_version="1.0.0";local[native].package_size=4928;
    n=am_actions(native,&local[native],true,a);assert(n==2 && a[0].kind==AM_INSTALL && a[0].enabled);
    local[native].installed=true;local[native].version="123456.123456.123456";local[native].size=4928;
    local[native].update_available=true;n=am_actions(native,&local[native],true,a);
    assert(n==4 && a[0].kind==AM_OPEN && a[1].kind==AM_UPDATE && a[2].kind==AM_UNINSTALL);
    local[native].disabled=true;n=am_actions(native,&local[native],true,a);assert(!a[0].enabled && a[2].enabled);
    record_text=true;rendered_text[0]=0;am_view_t disabled_view={.selected=native};
    details(&disabled_view,local);record_text=false;
    assert(strstr(rendered_text,"Disabled") && !strstr(rendered_text,"4928 B"));
    local[native].disabled=false;
    unsigned order[AM_COUNT];am_order(local,order);assert(order[0]==native);
    am_view_t v={.selected=native};list(&v,local);
    record_text=true;rendered_text[0]=0;details(&v,local);record_text=false;
    assert(strstr(rendered_text,local[native].version));
    for(unsigned id=0;id<AM_COUNT;id++)for(unsigned scroll=0;scroll<=720;scroll+=72) {
        v=(am_view_t){.selected=id,.info_scroll=scroll};list(&v,local);details(&v,local);storage();
    }
    local[native].installed=false;local[native].update_available=false;v=(am_view_t){.selected=native};
    local[native].package_available=false;record_text=true;rendered_text[0]=0;details(&v,local);record_text=false;
    assert(strstr(rendered_text,"Not downloaded") && !strstr(rendered_text,"RAM"));
    local[native].package_available=true;
    assert(!strcmp(action_notice(AM_INSTALL,AM_OK),"Installed"));
    local[native].installed=true;local[native].version="1.0.0";
    assert(!strcmp(action_notice(AM_UPDATE,AM_UP_TO_DATE),"Up to date"));
    assert(!strcmp(action_notice(AM_UPDATE,AM_OK),"Updated"));
    n=am_actions(native,&local[native],true,a);
    assert(a[1].kind==AM_UPDATE && !strcmp(a[1].label,"Check for update"));
    local[native].installed=false;
    // Integrated footer checks actual counters, invalid bounds and stale-bar clearing.
    am_controller_t capacity={.state=AM_READY,.capacity=128*1024,.used=88*1024,
        .store={.reserve_bytes=20*1024}};
    record_text=audit_footer=true;rendered_text[0]=0;storage_status(&capacity);
    assert(strstr(rendered_text,"Used 88 KiB") && strstr(rendered_text,"Reserved 20 KiB") &&
        strstr(rendered_text,"Available 20 KiB") && !strstr(rendered_text,"OS"));
    capacity.used=capacity.capacity;rendered_text[0]=0;storage_status(&capacity);
    assert(strstr(rendered_text,"Reserved 0 KiB") && strstr(rendered_text,"Available 0 KiB"));
    capacity.capacity=UINT64_MAX;rendered_text[0]=0;storage_status(&capacity);
    assert(strstr(rendered_text,"App store unavailable") && !strstr(rendered_text,"Used"));
    capacity.state=AM_UNASSIGNED;rendered_text[0]=0;storage_status(&capacity);
    assert(strstr(rendered_text,"App store unassigned"));
    record_text=audit_footer=false;
    capacity=(am_controller_t){.state=AM_READY,.capacity=128*1024,.used=88*1024,
        .store={.reserve_bytes=20*1024}};
    am_home_reset();for(unsigned i=0;i<AM_COUNT;i++)if(i!=1)am_home_add(am_catalogue[i].name);
    local[native].package_available=false;v=(am_view_t){.selected=native};
    box(0,TOP,W,H-TOP,false);list(&v,local);details(&v,local);storage_status(&capacity);
    network_status(RIGHT,TOP+48,1);
    ui_keys("Actions","Info up","Next app","Info down");ui_frame(1);ui_close();
    ink_box(330,24,300,20,false);ink_text(350,26,2,"Synthetic state",true);save(argv[1]);
    frame_count=close_count=0;
    event=0;idle_once=true;assert(browse_key()==browse_events[0] && !panel_open);event=0;
    if(!setjmp(finished))app_manager_ui();
    assert(frame_count>close_count); // Quick presses reuse the open session; actions still close it.
    puts("PASS: stock17/Full21/Core14 presence and truthful Home labels; contextual native actions; measured footer bounds; idle-close and quick navigation; actual C renderer under ASan/UBSan, no device.");
    return 0;
}

// Existing inbox/anchor behavior, separated from radio/storage/drawing/tasks.
// Badge OS9b73810; GPL-2.0. Original functions retained where possible.
#include "controller.h"
static int compare(const void *a,const void *b,size_t n)
{ const uint8_t *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return 1;return 0; }
static void copy(void *dst,const void *src,size_t n)
{ uint8_t *d=dst;const uint8_t *s=src;for(size_t i=0;i<n;i++)d[i]=s[i]; }
static void fill(void *dst,int value,size_t n)
{ uint8_t *d=dst;for(size_t i=0;i<n;i++)d[i]=(uint8_t)value; }
static int find(const message_state_t *s,const uint8_t id[16])
{ for(int i=0;i<s->count;i++)if(!compare(s->messages[i].id,id,16))return i;return -1; }
bool msg_view_sync(const message_state_t *s, msg_view_t *v)
{
    if (!s || !v || s->count > MSG_CAP) return false;
    bool added = false;
    for (int i = 0; i < s->count; i++) {
        bool found = false;
        for (int j = 0; j < v->known_count; j++) if (!compare(s->messages[i].id, v->known[j], 16)) found = true;
        if (!found) added = true;
    }
    v->known_count = s->count;
    for (int i = 0; i < s->count; i++) copy(v->known[i], s->messages[i].id, 16);
    if (!v->opened && added) { v->index = 0; fill(v->selected, 0, 16); }
    int at = find(s, v->selected);
    if (at >= 0) v->index = s->count - 1 - at;
    if (v->index >= s->count) v->index = s->count ? s->count - 1 : 0;
    if (s->count) copy(v->selected, s->messages[s->count - 1 - v->index].id, 16);
    else fill(v->selected, 0, 16);
    if (v->opened) {
        bool exists = false;
        for (int i = 0; i < s->count; i++) if (!compare(v->active, s->messages[i].thread, 16)) exists = true;
        if (!exists) v->opened = false;
        int anchor = find(s, v->anchor); v->offset = 0;
        if (anchor >= 0) for (int i = anchor + 1; i < s->count; i++)
            if (!compare(v->active, s->messages[i].thread, 16)) v->offset++;
    }
    return true;
}
int msg_view_visible(const message_state_t *s, const msg_view_t *v, int out[3])
{
    if (!s || !v || !out || s->count > MSG_CAP) return 0;
    int n = 0;
    if (!v->opened) {
        int first = v->index / 3 * 3;
        for (int row = first; row < s->count && row < first + 3; row++) out[n++] = s->count - 1 - row;
    } else {
        int indices[MSG_CAP], count = 0;
        for (int i = 0; i < s->count; i++) if (!compare(v->active, s->messages[i].thread, 16)) indices[count++] = i;
        int offset = v->offset < count ? v->offset : count - 1, end = count - offset;
        for (int i = end > 2 ? end - 2 : 0; i < end && count; i++) out[n++] = indices[i];
    }
    return n;
}

bool msg_view_frame(msg_view_t *v,bool available,uint8_t thread[16])
{
    static const uint8_t zero[16]={0};
    if(!v)return false;
    bool read=available && v->opened && (!compare(v->anchor,zero,16) || v->acknowledge);
    if (thread) { fill(thread,0,16);if(read)copy(thread,v->active,16); }
    v->acknowledge=false;
    return read;
}
static void phone(msg_view_t *v,msg_actions_t *out,const uint8_t *thread,bool status)
{
    out->phone=true;out->phone_has_thread=thread!=NULL;out->phone_sets_status=status;
    if(thread)copy(out->thread,thread,16);
    if(status) { v->phone_attempted=true;v->phone_menu=true; }
}
void msg_view_phone_result(msg_view_t *v,const msg_actions_t *a,bool success)
{ if(v && a && a->phone && a->phone_sets_status)v->phone_ok=success; }
void msg_view_key(msg_view_t *v,const message_state_t *s,bool available,int b,msg_actions_t *out)
{
    if(!out)return;
    fill(out,0,sizeof *out);
    if(b<0 || !v || !s || s->count>MSG_CAP)return;
    out->repaint=true;
    if(!available) {
        if(b==MSG_KEY_PLUS)out->info=true;
        if(b==MSG_KEY_B) {
            if(!v->phone_attempted) { out->exit=true;return; }
            v->phone_menu=!v->phone_menu;
        }
        return;
    }
    if(b==MSG_KEY_B && v->phone_menu) {
        v->phone_menu=false;
        if(!v->opened) { v->index=0;fill(v->selected,0,16); }
        return;
    }
    if(!v->opened) {
        if(b==MSG_KEY_PLUS && v->index>0)v->index--;
        if(b==MSG_KEY_A && v->index+1<s->count)v->index++;
        if(s->count)copy(v->selected,s->messages[s->count-1-v->index].id,16);
        if(b==MSG_KEY_MINUS && s->count) {
            copy(v->active,s->messages[s->count-1-v->index].thread,16);
            copy(v->anchor,v->selected,16);v->opened=true;v->offset=0;v->acknowledge=true;
            if(v->phone_ok)phone(v,out,v->active,false);
        }
        if(b==MSG_KEY_B)phone(v,out,NULL,true);
    } else {
        if(b==MSG_KEY_PLUS) {
            int indices[MSG_CAP],count=0;
            for(int i=0;i<s->count;i++)if(!compare(v->active,s->messages[i].thread,16))indices[count++]=i;
            if(v->offset+2<count)v->offset+=2;
            if(count)copy(v->anchor,s->messages[indices[count-v->offset-1]].id,16);
        }
        if(b==MSG_KEY_A) { v->offset=0;v->acknowledge=true;fill(v->anchor,0,16); }
        if(b==MSG_KEY_MINUS) {
            v->offset=0;v->acknowledge=true;fill(v->anchor,0,16);phone(v,out,v->active,true);
        }
        if(b==MSG_KEY_B)v->opened=false;
    }
}

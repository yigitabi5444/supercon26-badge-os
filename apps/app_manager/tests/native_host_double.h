#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
enum { NH_DRAM=1,NH_PSRAM,NH_DROM,NH_IROM };
extern atomic_uint nh_held,nh_frames_started,nh_frames_done,nh_closes,nh_wipes,nh_boxes;
extern atomic_uint nh_sem_created,nh_sem_deleted,nh_tasks_created,nh_tasks_deleted;
extern atomic_uint_fast64_t nh_time_ms;
extern atomic_bool nh_panel_blocked,nh_notify_paused,nh_suspend_paused,nh_force_on_core;
extern unsigned nh_fail_sem,nh_fail_task,nh_flushed;
extern unsigned nh_auto_b_frame;
extern unsigned nh_auto_b_poll,nh_button_polls;
extern unsigned nh_auto_chord_poll;
extern unsigned nh_resident_tasks;
extern void (*nh_resident_entry)(void *);
extern int nh_knob;
extern const char *nh_labels[4];
void nh_reset(void);
void nh_memory(const void *,size_t,unsigned,bool);
void nh_forget(const void *);
void nh_press(int);
void nh_sleep(void);
void nh_await(atomic_uint *,unsigned);

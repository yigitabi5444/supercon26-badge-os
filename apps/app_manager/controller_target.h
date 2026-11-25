#pragma once
#include "controller.h"
#include "package_source.h"
#include "package_http.h"
#include "video_source.h"
// Immutable configuration must outlive the controller. This does NOT assign a filesystem.
am_result_t am_target_start_with_source(am_controller_t *,const am_source_config_t *);

#include "txn_geometry.h"
am_result_t am_target_start_with_profile(am_controller_t *,const am_source_config_t *,const am_layout_policy_t *);
am_result_t am_target_start_with_sources(am_controller_t *,const am_source_config_t *,
                                        const am_source_config_t *,const am_layout_policy_t *);
am_result_t am_target_start_with_catalogue(am_controller_t *,const am_source_config_t *,const am_source_config_t *,
                                          const am_video_source_config_t *,const am_layout_policy_t *);
am_result_t am_target_start_with_apps(am_controller_t *,const am_source_config_t *,const am_source_config_t *,
                                     const am_video_source_config_t *,const am_source_config_t *,const am_layout_policy_t *);
am_result_t am_target_start_with_downloads(am_controller_t *,const am_source_config_t *,const am_source_config_t *,
    const am_video_source_config_t *,const am_source_config_t *,const am_source_config_t *,const am_layout_policy_t *);
am_result_t am_target_start_with_packages(am_controller_t *,const am_source_config_t *,const am_source_config_t *,
    const am_video_source_config_t *,const am_source_config_t *,const am_source_config_t *,const am_source_config_t *,const am_layout_policy_t *);
extern const am_layout_policy_t am_app_layout_policy;
am_result_t am_target_prepare(am_controller_t *,am_progress_t);
am_result_t am_target_home_inventory(unsigned,bool[AM_COUNT]); // Explicit offline read-only scan, never controller init/recovery.

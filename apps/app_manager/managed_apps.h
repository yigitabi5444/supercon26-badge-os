#pragma once
#include <stdbool.h>
#include <string.h>

// Curated identities only. Namespace strings are never derived from package input.
static inline const char *am_managed_namespace(const char *id)
{
    if(id && !strcmp(id,"aliens"))return "/appstore/aliens";
    if(id && !strcmp(id,"rocks"))return "/appstore/rocks";
    if(id && !strcmp(id,"video"))return "/appstore/video";
    if(id && !strcmp(id,"codex_board"))return "/appstore/codex_board";
    if(id && !strcmp(id,"hackaday"))return "/appstore/hackaday";
    if(id && !strcmp(id,"messaging"))return "/appstore/messaging";
    if(id && !strcmp(id,"doom"))return "/appstore/doom";
    return NULL;
}
static inline bool am_managed_id(const char *id)
{ return am_managed_namespace(id)!=NULL; }

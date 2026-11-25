// Actual adapter, with only the SDK mount and /appstore host path doubled.
#include "../txn_littlefs.h"
#include <sys/stat.h>
int home_test_stat(const char *,struct stat *);
am_result_t home_test_vfs(am_vfs_t *,const char *,uint64_t,am_io_t *);
am_result_t home_test_readonly_vfs(am_vfs_t *,const char *,uint64_t,am_io_t *);
#define stat(path,out) home_test_stat(path,out)
#define am_vfs_io home_test_vfs
#define am_vfs_readonly_io home_test_readonly_vfs
#include "../txn_littlefs.c"

/*
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <sys/capability.h>
#include "installd.h"
#include <diskusage/dirsize.h>
#include <selinux/android.h>

#define CALCULATE_DIRECTORY_REAL_SIZE

/* Directory records that are used in execution of commands. */
dir_rec_t android_data_dir;
dir_rec_t android_asec_dir;
dir_rec_t android_app_dir;
dir_rec_t android_app_private_dir;
dir_rec_t android_app_lib_dir;
dir_rec_t android_media_dir;
dir_rec_array_t android_system_dirs;

int install(const char *pkgname, uid_t uid, gid_t gid, const char *seinfo)
{
    char pkgdir[PKG_PATH_MAX];
    char libsymlink[PKG_PATH_MAX];
    char applibdir[PKG_PATH_MAX];
    struct stat libStat;

    if ((uid < AID_SYSTEM) || (gid < AID_SYSTEM)) {
        ALOGE("invalid uid/gid: %d %d\n", uid, gid);
        return -1;
    }

    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, 0)) {
        ALOGE("cannot create package path\n");
        return -1;
    }

    if (create_pkg_path(libsymlink, pkgname, PKG_LIB_POSTFIX, 0)) {
        ALOGE("cannot create package lib symlink origin path\n");
        return -1;
    }

    if (create_pkg_path_in_dir(applibdir, &android_app_lib_dir, pkgname, PKG_DIR_POSTFIX)) {
        ALOGE("cannot create package lib symlink dest path\n");
        return -1;
    }

    if (mkdir(pkgdir, 0751) < 0) {
        ALOGE("cannot create dir '%s': %s\n", pkgdir, strerror(errno));
        return -1;
    }
    if (chmod(pkgdir, 0751) < 0) {
        ALOGE("cannot chmod dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(pkgdir);
        return -1;
    }

    if (lstat(libsymlink, &libStat) < 0) {
        if (errno != ENOENT) {
            ALOGE("couldn't stat lib dir: %s\n", strerror(errno));
            return -1;
        }
    } else {
        if (S_ISDIR(libStat.st_mode)) {
            if (delete_dir_contents(libsymlink, 1, 0) < 0) {
                ALOGE("couldn't delete lib directory during install for: %s", libsymlink);
                return -1;
            }
        } else if (S_ISLNK(libStat.st_mode)) {
            if (unlink(libsymlink) < 0) {
                ALOGE("couldn't unlink lib directory during install for: %s", libsymlink);
                return -1;
            }
        }
    }

    if (symlink(applibdir, libsymlink) < 0) {
        ALOGE("couldn't symlink directory '%s' -> '%s': %s\n", libsymlink, applibdir,
                strerror(errno));
        unlink(pkgdir);
        return -1;
    }

    if (selinux_android_setfilecon2(pkgdir, pkgname, seinfo, uid) < 0) {
        ALOGE("cannot setfilecon dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(libsymlink);
        unlink(pkgdir);
        return -errno;
    }

    if (chown(pkgdir, uid, gid) < 0) {
        ALOGE("cannot chown dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(libsymlink);
        unlink(pkgdir);
        return -1;
    }

    return 0;
}

int uninstall(const char *pkgname, userid_t userid)
{
    char pkgdir[PKG_PATH_MAX];

    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, userid))
        return -1;

    /* delete contents AND directory, no exceptions */
    return delete_dir_contents(pkgdir, 1, NULL);
}

int renamepkg(const char *oldpkgname, const char *newpkgname)
{
    char oldpkgdir[PKG_PATH_MAX];
    char newpkgdir[PKG_PATH_MAX];

    if (create_pkg_path(oldpkgdir, oldpkgname, PKG_DIR_POSTFIX, 0))
        return -1;
    if (create_pkg_path(newpkgdir, newpkgname, PKG_DIR_POSTFIX, 0))
        return -1;

    if (rename(oldpkgdir, newpkgdir) < 0) {
        ALOGE("cannot rename dir '%s' to '%s': %s\n", oldpkgdir, newpkgdir, strerror(errno));
        return -errno;
    }
    return 0;
}

int fix_uid(const char *pkgname, uid_t uid, gid_t gid)
{
    char pkgdir[PKG_PATH_MAX];
    struct stat s;
    int rc = 0;

    if ((uid < AID_SYSTEM) || (gid < AID_SYSTEM)) {
        ALOGE("invalid uid/gid: %d %d\n", uid, gid);
        return -1;
    }

    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, 0)) {
        ALOGE("cannot create package path\n");
        return -1;
    }

    if (stat(pkgdir, &s) < 0) return -1;

    if (s.st_uid != 0 || s.st_gid != 0) {
        ALOGE("fixing uid of non-root pkg: %s %lu %lu\n", pkgdir, s.st_uid, s.st_gid);
        return -1;
    }

    /// M: [PMS Recovery] Installd should first change the folder to its own
    if (chown(pkgdir, AID_INSTALL, AID_INSTALL) < 0) {
        ALOGE("failed to chown '%s': %s\n", pkgdir, strerror(errno));
        return -1;
    }

    if (chmod(pkgdir, 0751) < 0) {
        ALOGE("cannot chmod dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(pkgdir);
        return -errno;
    }
    if (chown(pkgdir, uid, gid) < 0) {
        ALOGE("cannot chown dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(pkgdir);
        return -errno;
    }

    return 0;
}

int delete_user_data(const char *pkgname, userid_t userid)
{
    char pkgdir[PKG_PATH_MAX];

    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, userid))
        return -1;

    /* delete contents, excluding "lib", but not the directory itself */
    return delete_dir_contents(pkgdir, 0, "lib");
}

int make_user_data(const char *pkgname, uid_t uid, userid_t userid)
{
    char pkgdir[PKG_PATH_MAX];
    char applibdir[PKG_PATH_MAX];
    char libsymlink[PKG_PATH_MAX];
    struct stat libStat;

    // Create the data dir for the package
    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, userid)) {
        return -1;
    }
    if (create_pkg_path(libsymlink, pkgname, PKG_LIB_POSTFIX, userid)) {
        ALOGE("cannot create package lib symlink origin path\n");
        return -1;
    }
    if (create_pkg_path_in_dir(applibdir, &android_app_lib_dir, pkgname, PKG_DIR_POSTFIX)) {
        ALOGE("cannot create package lib symlink dest path\n");
        return -1;
    }

    if (mkdir(pkgdir, 0751) < 0) {
        ALOGE("cannot create dir '%s': %s\n", pkgdir, strerror(errno));
        return -errno;
    }
    if (chmod(pkgdir, 0751) < 0) {
        ALOGE("cannot chmod dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(pkgdir);
        return -errno;
    }

    if (lstat(libsymlink, &libStat) < 0) {
        if (errno != ENOENT) {
            ALOGE("couldn't stat lib dir for non-primary: %s\n", strerror(errno));
            unlink(pkgdir);
            return -1;
        }
    } else {
        if (S_ISDIR(libStat.st_mode)) {
            if (delete_dir_contents(libsymlink, 1, 0) < 0) {
                ALOGE("couldn't delete lib directory during install for non-primary: %s",
                        libsymlink);
                unlink(pkgdir);
                return -1;
            }
        } else if (S_ISLNK(libStat.st_mode)) {
            if (unlink(libsymlink) < 0) {
                ALOGE("couldn't unlink lib directory during install for non-primary: %s",
                        libsymlink);
                unlink(pkgdir);
                return -1;
            }
        }
    }

    if (symlink(applibdir, libsymlink) < 0) {
        ALOGE("couldn't symlink directory for non-primary '%s' -> '%s': %s\n", libsymlink,
                applibdir, strerror(errno));
        unlink(pkgdir);
        return -1;
    }

    if (selinux_android_setfilecon(pkgdir, pkgname, uid) < 0) {
        ALOGE("cannot setfilecon dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(libsymlink);
        unlink(pkgdir);
        return -errno;
    }

    if (chown(pkgdir, uid, uid) < 0) {
        ALOGE("cannot chown dir '%s': %s\n", pkgdir, strerror(errno));
        unlink(libsymlink);
        unlink(pkgdir);
        return -errno;
    }

    return 0;
}

int delete_user(userid_t userid)
{
    char data_path[PKG_PATH_MAX];
    if (create_user_path(data_path, userid)) {
        return -1;
    }
    if (delete_dir_contents(data_path, 1, NULL)) {
        return -1;
    }

    char media_path[PATH_MAX];
    if (create_user_media_path(media_path, userid) == -1) {
        return -1;
    }
    if (delete_dir_contents(media_path, 1, NULL) == -1) {
        return -1;
    }

    return 0;
}

int delete_cache(const char *pkgname, userid_t userid)
{
    char cachedir[PKG_PATH_MAX];

    if (create_pkg_path(cachedir, pkgname, CACHE_DIR_POSTFIX, userid))
        return -1;

        /* delete contents, not the directory, no exceptions */
    return delete_dir_contents(cachedir, 0, 0);
}

/* Try to ensure free_size bytes of storage are available.
 * Returns 0 on success.
 * This is rather simple-minded because doing a full LRU would
 * be potentially memory-intensive, and without atime it would
 * also require that apps constantly modify file metadata even
 * when just reading from the cache, which is pretty awful.
 */
int free_cache(int64_t free_size)
{
    cache_t* cache;
    int64_t avail;
    DIR *d;
    struct dirent *de;
    char tmpdir[PATH_MAX];
    char *dirpos;

    avail = data_disk_free();
    if (avail < 0) return -1;

    ALOGI("free_cache(%" PRId64 ") avail %" PRId64 "\n", free_size, avail);
    if (avail >= free_size) return 0;

    cache = start_cache_collection();

    // Collect cache files for primary user.
    if (create_user_path(tmpdir, 0) == 0) {
        //ALOGI("adding cache files from %s\n", tmpdir);
        add_cache_files(cache, tmpdir, "cache");
    }

    // Search for other users and add any cache files from them.
    snprintf(tmpdir, sizeof(tmpdir), "%s%s", android_data_dir.path,
            SECONDARY_USER_PREFIX);
    dirpos = tmpdir + strlen(tmpdir);
    d = opendir(tmpdir);
    if (d != NULL) {
        while ((de = readdir(d))) {
            if (de->d_type == DT_DIR) {
                const char *name = de->d_name;
                    /* always skip "." and ".." */
                if (name[0] == '.') {
                    if (name[1] == 0) continue;
                    if ((name[1] == '.') && (name[2] == 0)) continue;
                }
                if ((strlen(name)+(dirpos-tmpdir)) < (sizeof(tmpdir)-1)) {
                    strcpy(dirpos, name);
                    //ALOGI("adding cache files from %s\n", tmpdir);
                    add_cache_files(cache, tmpdir, "cache");
                } else {
                    ALOGW("Path exceeds limit: %s%s", tmpdir, name);
                }
            }
        }
        closedir(d);
    }

    // Collect cache files on external storage for all users (if it is mounted as part
    // of the internal storage).
    strcpy(tmpdir, android_media_dir.path);
    dirpos = tmpdir + strlen(tmpdir);
    d = opendir(tmpdir);
    if (d != NULL) {
        while ((de = readdir(d))) {
            if (de->d_type == DT_DIR) {
                const char *name = de->d_name;
                    /* skip any dir that doesn't start with a number, so not a user */
                if (name[0] < '0' || name[0] > '9') {
                    continue;
                }
                if ((strlen(name)+(dirpos-tmpdir)) < (sizeof(tmpdir)-1)) {
                    strcpy(dirpos, name);
                    if (lookup_media_dir(tmpdir, "Android") == 0
                            && lookup_media_dir(tmpdir, "data") == 0) {
                        //ALOGI("adding cache files from %s\n", tmpdir);
                        add_cache_files(cache, tmpdir, "cache");
                    }
                } else {
                    ALOGW("Path exceeds limit: %s%s", tmpdir, name);
                }
            }
        }
        closedir(d);
    }

    clear_cache_files(cache, free_size);
    finish_cache_collection(cache);

    return data_disk_free() >= free_size ? 0 : -1;
}

int move_dex(const char *src, const char *dst)
{
    char src_dex[PKG_PATH_MAX];
    char dst_dex[PKG_PATH_MAX];

    if (validate_apk_path(src)) return -1;
    if (validate_apk_path(dst)) return -1;

    if (create_cache_path(src_dex, src)) return -1;
    if (create_cache_path(dst_dex, dst)) return -1;

    ALOGV("move %s -> %s\n", src_dex, dst_dex);
    if (rename(src_dex, dst_dex) < 0) {
        ALOGE("Couldn't move %s: %s\n", src_dex, strerror(errno));
        return -1;
    } else {
        return 0;
    }
}

int rm_dex(const char *path)
{
    char dex_path[PKG_PATH_MAX];

    if (validate_apk_path(path)) return -1;
    if (create_cache_path(dex_path, path)) return -1;

    ALOGV("unlink %s\n", dex_path);
    if (unlink(dex_path) < 0) {
        ALOGE("Couldn't unlink %s: %s\n", dex_path, strerror(errno));
        return -1;
    } else {
        return 0;
    }
}

int get_size(const char *pkgname, userid_t userid, const char *apkpath,
             const char *libdirpath, const char *fwdlock_apkpath, const char *asecpath,
             int64_t *_codesize, int64_t *_datasize, int64_t *_cachesize,
             int64_t* _asecsize)
{
    DIR *d;
    int dfd;
    struct dirent *de;
    struct stat s;
    char path[PKG_PATH_MAX];
#ifdef CALCULATE_DIRECTORY_REAL_SIZE
    char path2[PKG_PATH_MAX];
#endif

    int64_t codesize = 0;
    int64_t datasize = 0;
    int64_t cachesize = 0;
    int64_t asecsize = 0;

        /* count the source apk as code -- but only if it's not
         * on the /system partition and its not on the sdcard.
         */
    if (validate_system_app_path(apkpath) &&
            strncmp(apkpath, android_asec_dir.path, android_asec_dir.len) != 0) {
        if (stat(apkpath, &s) == 0) {
            codesize += stat_size(&s);
        }
    }
        /* count the forward locked apk as code if it is given
         */
    if (fwdlock_apkpath != NULL && fwdlock_apkpath[0] != '!') {
        if (stat(fwdlock_apkpath, &s) == 0) {
            codesize += stat_size(&s);
        }
    }
        /* count the cached dexfile as code */
    if (!create_cache_path(path, apkpath)) {
        if (stat(path, &s) == 0) {
            codesize += stat_size(&s);
        }
    }

        /* add in size of any libraries */
    if (libdirpath != NULL && libdirpath[0] != '!') {
        d = opendir(libdirpath);
        if (d != NULL) {
            dfd = dirfd(d);
            codesize += calculate_dir_size(dfd);
            closedir(d);
        }
    }

        /* compute asec size if it is given
         */
    if (asecpath != NULL && asecpath[0] != '!') {
        if (stat(asecpath, &s) == 0) {
            asecsize += stat_size(&s);
        }
    }

    if (create_pkg_path(path, pkgname, PKG_DIR_POSTFIX, userid)) {
        goto done;
    }

    d = opendir(path);
    if (d == NULL) {
        goto done;
    }
    dfd = dirfd(d);

    /* most stuff in the pkgdir is data, except for the "cache"
     * directory and below, which is cache, and the "lib" directory
     * and below, which is code...
     */
    while ((de = readdir(d))) {
        const char *name = de->d_name;

        if (de->d_type == DT_DIR) {
            int subfd;
            int64_t statsize = 0;
            int64_t dirsize = 0;
                /* always skip "." and ".." */
            if (name[0] == '.') {
                if (name[1] == 0) continue;
                if ((name[1] == '.') && (name[2] == 0)) continue;
            }
            if (fstatat(dfd, name, &s, AT_SYMLINK_NOFOLLOW) == 0) {
                statsize = stat_size(&s);
            }
            subfd = openat(dfd, name, O_RDONLY | O_DIRECTORY);
            if (subfd >= 0) {
                dirsize = calculate_dir_size(subfd);
            }
            if(!strcmp(name,"lib")) {
                codesize += dirsize + statsize;
            } else if(!strcmp(name,"cache")) {

#ifdef CALCULATE_DIRECTORY_REAL_SIZE
                strcpy(path2, path);
                strcat(path2, "/cache");
                cachesize += cal_dir_size(path2);
#else
                cachesize += dirsize + statsize;
#endif
            } else {
                datasize += dirsize + statsize;
            }
        } else if (de->d_type == DT_LNK && !strcmp(name,"lib")) {
            // This is the symbolic link to the application's library
            // code.  We'll count this as code instead of data, since
            // it is not something that the app creates.
            if (fstatat(dfd, name, &s, AT_SYMLINK_NOFOLLOW) == 0) {
                codesize += stat_size(&s);
            }
        } else {
            if (fstatat(dfd, name, &s, AT_SYMLINK_NOFOLLOW) == 0) {
                datasize += stat_size(&s);
            }
        }
    }
    closedir(d);
done:
    *_codesize = codesize;
    *_datasize = datasize;
    *_cachesize = cachesize;
    *_asecsize = asecsize;
    return 0;
}


int create_cache_path(char path[PKG_PATH_MAX], const char *src)
{
    char *tmp;
    int srclen;
    int dstlen;

    srclen = strlen(src);

        /* demand that we are an absolute path */
    if ((src == 0) || (src[0] != '/') || strstr(src,"..")) {
        return -1;
    }

    if (srclen > PKG_PATH_MAX) {        // XXX: PKG_NAME_MAX?
        return -1;
    }

    dstlen = srclen + strlen(DALVIK_CACHE_PREFIX) + 
        strlen(DALVIK_CACHE_POSTFIX) + 1;
    
    if (dstlen > PKG_PATH_MAX) {
        return -1;
    }

    sprintf(path,"%s%s%s",
            DALVIK_CACHE_PREFIX,
            src + 1, /* skip the leading / */
            DALVIK_CACHE_POSTFIX);
    
    for(tmp = path + strlen(DALVIK_CACHE_PREFIX); *tmp; tmp++) {
        if (*tmp == '/') {
            *tmp = '@';
        }
    }

    return 0;
}

static void run_dexopt(int zip_fd, int odex_fd, const char* input_file_name,
    const char* output_file_name, const char* dexopt_flags)
{
    static const char* DEX_OPT_BIN = "/system/bin/dexopt";
    static const int MAX_INT_LEN = 12;      // '-'+10dig+'\0' -OR- 0x+8dig
    char zip_num[MAX_INT_LEN];
    char odex_num[MAX_INT_LEN];

    sprintf(zip_num, "%d", zip_fd);
    sprintf(odex_num, "%d", odex_fd);

    ALOGV("Running %s in=%s out=%s\n", DEX_OPT_BIN, input_file_name, output_file_name);
    execl(DEX_OPT_BIN, DEX_OPT_BIN, "--zip", zip_num, odex_num, input_file_name,
        dexopt_flags, (char*) NULL);
    ALOGE("execl(%s) failed: %s\n", DEX_OPT_BIN, strerror(errno));
}

static void run_dex2oat(int zip_fd, int oat_fd, const char* input_file_name,
    const char* output_file_name, const char* dexopt_flags)
{
    static const char* DEX2OAT_BIN = "/system/bin/dex2oat";
    static const int MAX_INT_LEN = 12;      // '-'+10dig+'\0' -OR- 0x+8dig
    char zip_fd_arg[strlen("--zip-fd=") + MAX_INT_LEN];
    char zip_location_arg[strlen("--zip-location=") + PKG_PATH_MAX];
    char oat_fd_arg[strlen("--oat-fd=") + MAX_INT_LEN];
    char oat_location_arg[strlen("--oat-name=") + PKG_PATH_MAX];

    sprintf(zip_fd_arg, "--zip-fd=%d", zip_fd);
    sprintf(zip_location_arg, "--zip-location=%s", input_file_name);
    sprintf(oat_fd_arg, "--oat-fd=%d", oat_fd);
    sprintf(oat_location_arg, "--oat-location=%s", output_file_name);

    ALOGV("Running %s in=%s out=%s\n", DEX2OAT_BIN, input_file_name, output_file_name);
    execl(DEX2OAT_BIN, DEX2OAT_BIN,
          zip_fd_arg, zip_location_arg,
          oat_fd_arg, oat_location_arg,
          (char*) NULL);
    ALOGE("execl(%s) failed: %s\n", DEX2OAT_BIN, strerror(errno));
}

static int wait_dexopt(pid_t pid, const char* apk_path)
{
    int status;
    pid_t got_pid;

    /*
     * Wait for the optimization process to finish.
     */
    while (1) {
        got_pid = waitpid(pid, &status, 0);
        if (got_pid == -1 && errno == EINTR) {
            printf("waitpid interrupted, retrying\n");
        } else {
            break;
        }
    }
    if (got_pid != pid) {
        ALOGW("waitpid failed: wanted %d, got %d: %s\n",
            (int) pid, (int) got_pid, strerror(errno));
        return 1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        ALOGV("DexInv: --- END '%s' (success) ---\n", apk_path);
        return 0;
    } else {
        ALOGW("DexInv: --- END '%s' --- status=0x%04x, process failed\n",
            apk_path, status);
        return status;      /* always nonzero */
    }
}

int dexopt(const char *apk_path, uid_t uid, int is_public)
{
    struct utimbuf ut;
    struct stat apk_stat, dex_stat;
    char out_path[PKG_PATH_MAX];
    char dexopt_flags[PROPERTY_VALUE_MAX];
    char persist_sys_dalvik_vm_lib[PROPERTY_VALUE_MAX];
    char *end;
    int res, zip_fd=-1, out_fd=-1;

    if (strlen(apk_path) >= (PKG_PATH_MAX - 8)) {
        return -1;
    }

    /* platform-specific flags affecting optimization and verification */
    property_get("dalvik.vm.dexopt-flags", dexopt_flags, "");
    ALOGV("dalvik.vm.dexopt_flags=%s\n", dexopt_flags);

    /* The command to run depend ones the value of persist.sys.dalvik.vm.lib */
    property_get("persist.sys.dalvik.vm.lib", persist_sys_dalvik_vm_lib, "libdvm.so");

    /* Before anything else: is there a .odex file?  If so, we have
     * precompiled the apk and there is nothing to do here.
     */
    sprintf(out_path, "%s%s", apk_path, ".odex");
    if (stat(out_path, &dex_stat) == 0) {
        return 0;
    }

    if (create_cache_path(out_path, apk_path)) {
        return -1;
    }

    memset(&apk_stat, 0, sizeof(apk_stat));
    stat(apk_path, &apk_stat);

    zip_fd = open(apk_path, O_RDONLY, 0);
    if (zip_fd < 0) {
        ALOGE("installd cannot open '%s' for input during dexopt\n", apk_path);
        return -1;
    }

    unlink(out_path);
    out_fd = open(out_path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (out_fd < 0) {
        ALOGE("installd cannot open '%s' for output during dexopt\n", out_path);
        goto fail;
    }
    if (fchmod(out_fd,
               S_IRUSR|S_IWUSR|S_IRGRP |
               (is_public ? S_IROTH : 0)) < 0) {
        ALOGE("installd cannot chmod '%s' during dexopt\n", out_path);
        goto fail;
    }
    if (fchown(out_fd, AID_SYSTEM, uid) < 0) {
        ALOGE("installd cannot chown '%s' during dexopt\n", out_path);
        goto fail;
    }

    ALOGV("DexInv: --- BEGIN '%s' ---\n", apk_path);

    pid_t pid;
    pid = fork();
    if (pid == 0) {
        /* child -- drop privileges before continuing */
        if (setgid(uid) != 0) {
            ALOGE("setgid(%d) failed in installd during dexopt\n", uid);
            exit(64);
        }
        if (setuid(uid) != 0) {
            ALOGE("setuid(%d) failed in installd during dexopt\n", uid);
            exit(65);
        }
        // drop capabilities
        struct __user_cap_header_struct capheader;
        struct __user_cap_data_struct capdata[2];
        memset(&capheader, 0, sizeof(capheader));
        memset(&capdata, 0, sizeof(capdata));
        capheader.version = _LINUX_CAPABILITY_VERSION_3;
        if (capset(&capheader, &capdata[0]) < 0) {
            ALOGE("capset failed: %s\n", strerror(errno));
            exit(66);
        }
        if (flock(out_fd, LOCK_EX | LOCK_NB) != 0) {
            ALOGE("flock(%s) failed: %s\n", out_path, strerror(errno));
            exit(67);
        }

        if (strncmp(persist_sys_dalvik_vm_lib, "libdvm", 6) == 0) {
            run_dexopt(zip_fd, out_fd, apk_path, out_path, dexopt_flags);
        } else if (strncmp(persist_sys_dalvik_vm_lib, "libart", 6) == 0) {
            //run_dex2oat(zip_fd, out_fd, apk_path, out_path, dexopt_flags);
            run_dexopt(zip_fd, out_fd, apk_path, out_path, dexopt_flags);
        } else {
            exit(69);   /* Unexpected persist.sys.dalvik.vm.lib value */
        }
        exit(68);   /* only get here on exec failure */
    } else {
        res = wait_dexopt(pid, apk_path);
        if (res != 0) {
            ALOGE("dexopt in='%s' out='%s' res=%d\n", apk_path, out_path, res);
            goto fail;
        }
    }

    ut.actime = apk_stat.st_atime;
    ut.modtime = apk_stat.st_mtime;
    utime(out_path, &ut);

    close(out_fd);
    close(zip_fd);
    return 0;

fail:
    if (out_fd >= 0) {
        close(out_fd);
        unlink(out_path);
    }
    if (zip_fd >= 0) {
        close(zip_fd);
    }
    return -1;
}

void mkinnerdirs(char* path, int basepos, mode_t mode, int uid, int gid,
        struct stat* statbuf)
{
    while (path[basepos] != 0) {
        if (path[basepos] == '/') {
            path[basepos] = 0;
            if (lstat(path, statbuf) < 0) {
                ALOGV("Making directory: %s\n", path);
                if (mkdir(path, mode) == 0) {
                    chown(path, uid, gid);
                } else {
                    ALOGW("Unable to make directory %s: %s\n", path, strerror(errno));
                }
            }
            path[basepos] = '/';
            basepos++;
        }
        basepos++;
    }
}

int movefileordir(char* srcpath, char* dstpath, int dstbasepos,
        int dstuid, int dstgid, struct stat* statbuf)
{
    DIR *d;
    struct dirent *de;
    int res;

    int srcend = strlen(srcpath);
    int dstend = strlen(dstpath);
    
    if (lstat(srcpath, statbuf) < 0) {
        ALOGW("Unable to stat %s: %s\n", srcpath, strerror(errno));
        return 1;
    }
    
    if ((statbuf->st_mode&S_IFDIR) == 0) {
        mkinnerdirs(dstpath, dstbasepos, S_IRWXU|S_IRWXG|S_IXOTH,
                dstuid, dstgid, statbuf);
        ALOGV("Renaming %s to %s (uid %d)\n", srcpath, dstpath, dstuid);
        if (rename(srcpath, dstpath) >= 0) {
            if (chown(dstpath, dstuid, dstgid) < 0) {
                ALOGE("cannot chown %s: %s\n", dstpath, strerror(errno));
                unlink(dstpath);
                return 1;
            }
        } else {
            ALOGW("Unable to rename %s to %s: %s\n",
                srcpath, dstpath, strerror(errno));
            return 1;
        }
        return 0;
    }

    d = opendir(srcpath);
    if (d == NULL) {
        ALOGW("Unable to opendir %s: %s\n", srcpath, strerror(errno));
        return 1;
    }

    res = 0;
    
    while ((de = readdir(d))) {
        const char *name = de->d_name;
            /* always skip "." and ".." */
        if (name[0] == '.') {
            if (name[1] == 0) continue;
            if ((name[1] == '.') && (name[2] == 0)) continue;
        }
        
        if ((srcend+strlen(name)) >= (PKG_PATH_MAX-2)) {
            ALOGW("Source path too long; skipping: %s/%s\n", srcpath, name);
            continue;
        }
        
        if ((dstend+strlen(name)) >= (PKG_PATH_MAX-2)) {
            ALOGW("Destination path too long; skipping: %s/%s\n", dstpath, name);
            continue;
        }
        
        srcpath[srcend] = dstpath[dstend] = '/';
        strcpy(srcpath+srcend+1, name);
        strcpy(dstpath+dstend+1, name);
        
        if (movefileordir(srcpath, dstpath, dstbasepos, dstuid, dstgid, statbuf) != 0) {
            res = 1;
        }
        
        // Note: we will be leaving empty directories behind in srcpath,
        // but that is okay, the package manager will be erasing all of the
        // data associated with .apks that disappear.
        
        srcpath[srcend] = dstpath[dstend] = 0;
    }
    
    closedir(d);
    return res;
}

int movefiles()
{
    DIR *d;
    int dfd, subfd;
    struct dirent *de;
    struct stat s;
    char buf[PKG_PATH_MAX+1];
    int bufp, bufe, bufi, readlen;

    char srcpkg[PKG_NAME_MAX];
    char dstpkg[PKG_NAME_MAX];
    char srcpath[PKG_PATH_MAX];
    char dstpath[PKG_PATH_MAX];
    int dstuid=-1, dstgid=-1;
    int hasspace;

    d = opendir(UPDATE_COMMANDS_DIR_PREFIX);
    if (d == NULL) {
        goto done;
    }
    dfd = dirfd(d);

        /* Iterate through all files in the directory, executing the
         * file movements requested there-in.
         */
    while ((de = readdir(d))) {
        const char *name = de->d_name;

        if (de->d_type == DT_DIR) {
            continue;
        } else {
            subfd = openat(dfd, name, O_RDONLY);
            if (subfd < 0) {
                ALOGW("Unable to open update commands at %s%s\n",
                        UPDATE_COMMANDS_DIR_PREFIX, name);
                continue;
            }
            
            bufp = 0;
            bufe = 0;
            buf[PKG_PATH_MAX] = 0;
            srcpkg[0] = dstpkg[0] = 0;
            while (1) {
                bufi = bufp;
                while (bufi < bufe && buf[bufi] != '\n') {
                    bufi++;
                }
                if (bufi < bufe) {
                    buf[bufi] = 0;
                    ALOGV("Processing line: %s\n", buf+bufp);
                    hasspace = 0;
                    while (bufp < bufi && isspace(buf[bufp])) {
                        hasspace = 1;
                        bufp++;
                    }
                    if (buf[bufp] == '#' || bufp == bufi) {
                        // skip comments and empty lines.
                    } else if (hasspace) {
                        if (dstpkg[0] == 0) {
                            ALOGW("Path before package line in %s%s: %s\n",
                                    UPDATE_COMMANDS_DIR_PREFIX, name, buf+bufp);
                        } else if (srcpkg[0] == 0) {
                            // Skip -- source package no longer exists.
                        } else {
                            ALOGV("Move file: %s (from %s to %s)\n", buf+bufp, srcpkg, dstpkg);
                            if (!create_move_path(srcpath, srcpkg, buf+bufp, 0) &&
                                    !create_move_path(dstpath, dstpkg, buf+bufp, 0)) {
                                movefileordir(srcpath, dstpath,
                                        strlen(dstpath)-strlen(buf+bufp),
                                        dstuid, dstgid, &s);
                            }
                        }
                    } else {
                        char* div = strchr(buf+bufp, ':');
                        if (div == NULL) {
                            ALOGW("Bad package spec in %s%s; no ':' sep: %s\n",
                                    UPDATE_COMMANDS_DIR_PREFIX, name, buf+bufp);
                        } else {
                            *div = 0;
                            div++;
                            if (strlen(buf+bufp) < PKG_NAME_MAX) {
                                strcpy(dstpkg, buf+bufp);
                            } else {
                                srcpkg[0] = dstpkg[0] = 0;
                                ALOGW("Package name too long in %s%s: %s\n",
                                        UPDATE_COMMANDS_DIR_PREFIX, name, buf+bufp);
                            }
                            if (strlen(div) < PKG_NAME_MAX) {
                                strcpy(srcpkg, div);
                            } else {
                                srcpkg[0] = dstpkg[0] = 0;
                                ALOGW("Package name too long in %s%s: %s\n",
                                        UPDATE_COMMANDS_DIR_PREFIX, name, div);
                            }
                            if (srcpkg[0] != 0) {
                                if (!create_pkg_path(srcpath, srcpkg, PKG_DIR_POSTFIX, 0)) {
                                    if (lstat(srcpath, &s) < 0) {
                                        // Package no longer exists -- skip.
                                        srcpkg[0] = 0;
                                    }
                                } else {
                                    srcpkg[0] = 0;
                                    ALOGW("Can't create path %s in %s%s\n",
                                            div, UPDATE_COMMANDS_DIR_PREFIX, name);
                                }
                                if (srcpkg[0] != 0) {
                                    if (!create_pkg_path(dstpath, dstpkg, PKG_DIR_POSTFIX, 0)) {
                                        if (lstat(dstpath, &s) == 0) {
                                            dstuid = s.st_uid;
                                            dstgid = s.st_gid;
                                        } else {
                                            // Destination package doesn't
                                            // exist...  due to original-package,
                                            // this is normal, so don't be
                                            // noisy about it.
                                            srcpkg[0] = 0;
                                        }
                                    } else {
                                        srcpkg[0] = 0;
                                        ALOGW("Can't create path %s in %s%s\n",
                                                div, UPDATE_COMMANDS_DIR_PREFIX, name);
                                    }
                                }
                                ALOGV("Transfering from %s to %s: uid=%d\n",
                                    srcpkg, dstpkg, dstuid);
                            }
                        }
                    }
                    bufp = bufi+1;
                } else {
                    if (bufp == 0) {
                        if (bufp < bufe) {
                            ALOGW("Line too long in %s%s, skipping: %s\n",
                                    UPDATE_COMMANDS_DIR_PREFIX, name, buf);
                        }
                    } else if (bufp < bufe) {
                        memcpy(buf, buf+bufp, bufe-bufp);
                        bufe -= bufp;
                        bufp = 0;
                    }
                    readlen = read(subfd, buf+bufe, PKG_PATH_MAX-bufe);
                    if (readlen < 0) {
                        ALOGW("Failure reading update commands in %s%s: %s\n",
                                UPDATE_COMMANDS_DIR_PREFIX, name, strerror(errno));
                        break;
                    } else if (readlen == 0) {
                        break;
                    }
                    bufe += readlen;
                    buf[bufe] = 0;
                    ALOGV("Read buf: %s\n", buf);
                }
            }
            close(subfd);
        }
    }
    closedir(d);
done:
    return 0;
}

int linklib(const char* pkgname, const char* asecLibDir, int userId)
{
    char pkgdir[PKG_PATH_MAX];
    char libsymlink[PKG_PATH_MAX];
    struct stat s, libStat;
    int rc = 0;

    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, userId)) {
        ALOGE("cannot create package path\n");
        return -1;
    }
    if (create_pkg_path(libsymlink, pkgname, PKG_LIB_POSTFIX, userId)) {
        ALOGE("cannot create package lib symlink origin path\n");
        return -1;
    }

    if (stat(pkgdir, &s) < 0) return -1;

    if (chown(pkgdir, AID_INSTALL, AID_INSTALL) < 0) {
        ALOGE("failed to chown '%s': %s\n", pkgdir, strerror(errno));
        return -1;
    }

    if (chmod(pkgdir, 0700) < 0) {
        ALOGE("linklib() 1: failed to chmod '%s': %s\n", pkgdir, strerror(errno));
        rc = -1;
        goto out;
    }

    if (lstat(libsymlink, &libStat) < 0) {
        if (errno != ENOENT) {
            ALOGE("couldn't stat lib dir: %s\n", strerror(errno));
            rc = -1;
            goto out;
        }
    } else {
        if (S_ISDIR(libStat.st_mode)) {
            if (delete_dir_contents(libsymlink, 1, 0) < 0) {
                rc = -1;
                goto out;
            }
        } else if (S_ISLNK(libStat.st_mode)) {
            if (unlink(libsymlink) < 0) {
                ALOGE("couldn't unlink lib dir: %s\n", strerror(errno));
                rc = -1;
                goto out;
            }
        }
    }

    if (symlink(asecLibDir, libsymlink) < 0) {
        ALOGE("couldn't symlink directory '%s' -> '%s': %s\n", libsymlink, asecLibDir,
                strerror(errno));
        rc = -errno;
        goto out;
    }

out:
    if (chmod(pkgdir, s.st_mode) < 0) {
        ALOGE("linklib() 2: failed to chmod '%s': %s\n", pkgdir, strerror(errno));
        rc = -errno;
    }

    if (chown(pkgdir, s.st_uid, s.st_gid) < 0) {
        ALOGE("failed to chown '%s' : %s\n", pkgdir, strerror(errno));
        return -errno;
    }

    return rc;
}

/// M: [PMS Recovery] Implement of change uid function
int change_uid_root(const char* pkgname, int userId) {
    char pkgdir[PKG_PATH_MAX];
    struct stat s;
    if (create_pkg_path(pkgdir, pkgname, PKG_DIR_POSTFIX, userId)) {
        ALOGE("cannot create package path\n");
        return -1;
    }
    if (stat(pkgdir, &s) < 0) return -1;

    if (s.st_uid != 0 || s.st_gid != 0) {
        if (chown(pkgdir, 0, 0) < 0) {
            ALOGE("failed to chown '%s': %s\n", pkgdir, strerror(errno));
            return -1;
        }
    }
    
    return 0;
}

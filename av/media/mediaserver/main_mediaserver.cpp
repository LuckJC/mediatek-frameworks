/*
**
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

#define LOG_TAG "mediaserver"
//#define LOG_NDEBUG 0
// System headers required for setgroups, etc.
#include <sys/types.h>
#include <unistd.h>
#include <grp.h>
#include <linux/rtpm_prio.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <private/android_filesystem_config.h>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <cutils/properties.h>
#include <utils/Log.h>
#include "RegisterExtensions.h"

// from LOCAL_C_INCLUDES
#include "AudioFlinger.h"
#include "CameraService.h"
#include "MediaLogService.h"
#include "MediaPlayerService.h"
#include "AudioPolicyService.h"

#ifndef ANDROID_DEFAULT_CODE
#include <memorydumper/MemoryDumper.h>
#ifdef MTK_VIDEO_HEVC_SUPPORT
#include <dlfcn.h>
#endif
#endif

using namespace android;

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    char value[PROPERTY_VALUE_MAX];
    bool doLog = (property_get("ro.test_harness", value, "0") > 0) && (atoi(value) == 1);
    pid_t childPid;
    // FIXME The advantage of making the process containing media.log service the parent process of
    // the process that contains all the other real services, is that it allows us to collect more
    // detailed information such as signal numbers, stop and continue, resource usage, etc.
    // But it is also more complex.  Consider replacing this by independent processes, and using
    // binder on death notification instead.
    if (doLog && (childPid = fork()) != 0) {
        // media.log service
        //prctl(PR_SET_NAME, (unsigned long) "media.log", 0, 0, 0);
        // unfortunately ps ignores PR_SET_NAME for the main thread, so use this ugly hack
        strcpy(argv[0], "media.log");
        sp<ProcessState> proc(ProcessState::self());
        MediaLogService::instantiate();
        ProcessState::self()->startThreadPool();
        for (;;) {
            siginfo_t info;
            int ret = waitid(P_PID, childPid, &info, WEXITED | WSTOPPED | WCONTINUED);
            if (ret == EINTR) {
                continue;
            }
            if (ret < 0) {
                break;
            }
            char buffer[32];
            const char *code;
            switch (info.si_code) {
            case CLD_EXITED:
                code = "CLD_EXITED";
                break;
            case CLD_KILLED:
                code = "CLD_KILLED";
                break;
            case CLD_DUMPED:
                code = "CLD_DUMPED";
                break;
            case CLD_STOPPED:
                code = "CLD_STOPPED";
                break;
            case CLD_TRAPPED:
                code = "CLD_TRAPPED";
                break;
            case CLD_CONTINUED:
                code = "CLD_CONTINUED";
                break;
            default:
                snprintf(buffer, sizeof(buffer), "unknown (%d)", info.si_code);
                code = buffer;
                break;
            }
            struct rusage usage;
            getrusage(RUSAGE_CHILDREN, &usage);
            ALOG(LOG_ERROR, "media.log", "pid %d status %d code %s user %ld.%03lds sys %ld.%03lds",
                    info.si_pid, info.si_status, code,
                    usage.ru_utime.tv_sec, usage.ru_utime.tv_usec / 1000,
                    usage.ru_stime.tv_sec, usage.ru_stime.tv_usec / 1000);
            sp<IServiceManager> sm = defaultServiceManager();
            sp<IBinder> binder = sm->getService(String16("media.log"));
            if (binder != 0) {
                Vector<String16> args;
                binder->dump(-1, args);
            }
            switch (info.si_code) {
            case CLD_EXITED:
            case CLD_KILLED:
            case CLD_DUMPED: {
                ALOG(LOG_INFO, "media.log", "exiting");
                _exit(0);
                // not reached
                }
            default:
                break;
            }
        }
    } else {
        // all other services
        if (doLog) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);   // if parent media.log dies before me, kill me also
            setpgid(0, 0);                      // but if I die first, don't kill my parent
        }
        sp<ProcessState> proc(ProcessState::self());
        sp<IServiceManager> sm = defaultServiceManager();
        ALOGI("ServiceManager: %p", sm.get());
        AudioFlinger::instantiate();
        MediaPlayerService::instantiate();
#ifndef ANDROID_DEFAULT_CODE
        MemoryDumper::instantiate();
#endif
        CameraService::instantiate();
        AudioPolicyService::instantiate();
        registerExtensions();

#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_VIDEO_HEVC_SUPPORT
            void *pCodecUtil = NULL,*pPerfNative = NULL;
            void (*pfn_PrepareLibrary)(int i4UID);
            pfn_PrepareLibrary = NULL;
            pCodecUtil = dlopen("/system/lib/libvcodec_utility.so", RTLD_LAZY);
            if (pCodecUtil != NULL) {
                pfn_PrepareLibrary = (void (*)(int i4UID))dlsym(pCodecUtil, "PrepareLibrary");
                if (pfn_PrepareLibrary != NULL) {
                    (*pfn_PrepareLibrary)(AID_MEDIA);
                }
                dlclose(pCodecUtil);
            }
            pPerfNative = dlopen("/system/lib/libperfservicenative.so",RTLD_LAZY);
            void (*pfn_perfuserDisableAll)(void);
            if (pPerfNative != NULL) {
                pfn_perfuserDisableAll = (void (*)(void))dlsym(pPerfNative, "PerfServiceNative_userDisableAll");
                if (pfn_perfuserDisableAll != NULL) {
                    (*pfn_perfuserDisableAll)();
                }
                dlclose(pCodecUtil);
            }
#endif
#endif

        if (AID_ROOT == getuid()) {
            ALOGI("[%s] re-adjust caps for its thread, and set uid to media", __func__);
            if (-1 == prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0)) {
                ALOGW("mediaserver prctl for set caps failed: %s", strerror(errno));
            } else {
                __user_cap_header_struct hdr;
                __user_cap_data_struct data;

                setuid(AID_MEDIA);         // change user to media

                hdr.version = _LINUX_CAPABILITY_VERSION;    // set caps again
                hdr.pid = 0;
                data.effective = (1 << CAP_SYS_NICE);
                data.permitted = (1 << CAP_SYS_NICE);
                data.inheritable = 0xffffffff;
                if (-1 == capset(&hdr, &data)) {
                    ALOGW("mediaserver cap re-setting failed, %s", strerror(errno));
                }
            }

        } else {
            ALOGI("[%s] re-adjust caps is not in root user", __func__);
        }


        ProcessState::self()->startThreadPool();
        IPCThreadState::self()->joinThreadPool();
    }
}

/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "DrmManagerService(Native)"
#include <utils/Log.h>

#include <private/android_filesystem_config.h>
#include <media/MemoryLeakTrackUtil.h>

#include <errno.h>
#include <utils/threads.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <sys/stat.h>
#include "DrmManagerService.h"
#include "DrmManager.h"

#include <drm/DrmMtkUtil.h>
#include <drm/DrmMtkDef.h>
using namespace android;

static Vector<uid_t> trustedUids;

static bool isProtectedCallAllowed() {
    // M: migration these from ICS
    // TODO
    // Following implementation is just for reference.
    // Each OEM manufacturer should implement/replace with their own solutions.
    bool result = false;

    IPCThreadState* ipcState = IPCThreadState::self();
    uid_t uid = ipcState->getCallingUid();

    for (unsigned int i = 0; i < trustedUids.size(); ++i) {
        if (trustedUids[i] == uid) {
            result = true;
            break;
        }
    }

    // M:
    // for OMA DRM v1 implementation
    // if can't authorize the process by UID, then check the process name.
    if (!result) {
        pid_t pid = ipcState->getCallingPid();
        result = DrmTrustedClient::IsDrmTrustedClient(DrmMtkUtil::getProcessName(pid));
    }

    return result;
}

void DrmManagerService::instantiate() {
    ALOGV("instantiate");
    ALOGI("try register drm server to Service Manager.");
    defaultServiceManager()->addService(String16("drm.drmManager"), new DrmManagerService());

    if (0 >= trustedUids.size()) {
        // TODO
        // Following implementation is just for reference.
        // Each OEM manufacturer should implement/replace with their own solutions.

        // Add trusted uids here
        trustedUids.push(AID_MEDIA);
        // M: migration these 3 from ICS
        trustedUids.push(AID_DRM);
        trustedUids.push(AID_SYSTEM);
        trustedUids.push(AID_ROOT);
    }
}

DrmManagerService::DrmManagerService() :
        mDrmManager(NULL) {
    ALOGV("created");
    mDrmManager = new DrmManager();
    mDrmManager->loadPlugIns();
}

DrmManagerService::~DrmManagerService() {
    ALOGV("Destroyed");
    mDrmManager->unloadPlugIns();
    delete mDrmManager; mDrmManager = NULL;
}

int DrmManagerService::addUniqueId(bool isNative) {
    return mDrmManager->addUniqueId(isNative);
}

void DrmManagerService::removeUniqueId(int uniqueId) {
    mDrmManager->removeUniqueId(uniqueId);
}

void DrmManagerService::addClient(int uniqueId) {
    mDrmManager->addClient(uniqueId);
}

void DrmManagerService::removeClient(int uniqueId) {
    mDrmManager->removeClient(uniqueId);
}

status_t DrmManagerService::setDrmServiceListener(
            int uniqueId, const sp<IDrmServiceListener>& drmServiceListener) {
    ALOGV("Entering setDrmServiceListener");
    mDrmManager->setDrmServiceListener(uniqueId, drmServiceListener);
    return DRM_NO_ERROR;
}

DrmConstraints* DrmManagerService::getConstraints(
            int uniqueId, const String8* path, const int action) {
    ALOGV("Entering getConstraints from content");
    return mDrmManager->getConstraints(uniqueId, path, action);
}

DrmMetadata* DrmManagerService::getMetadata(int uniqueId, const String8* path) {
    ALOGV("Entering getMetadata from content");
    return mDrmManager->getMetadata(uniqueId, path);
}

bool DrmManagerService::canHandle(int uniqueId, const String8& path, const String8& mimeType) {
    ALOGV("Entering canHandle");
    return mDrmManager->canHandle(uniqueId, path, mimeType);
}

DrmInfoStatus* DrmManagerService::processDrmInfo(int uniqueId, const DrmInfo* drmInfo) {
    ALOGV("Entering processDrmInfo");
    return mDrmManager->processDrmInfo(uniqueId, drmInfo);
}

DrmInfo* DrmManagerService::acquireDrmInfo(int uniqueId, const DrmInfoRequest* drmInfoRequest) {
    ALOGV("Entering acquireDrmInfo");
    return mDrmManager->acquireDrmInfo(uniqueId, drmInfoRequest);
}

status_t DrmManagerService::saveRights(
            int uniqueId, const DrmRights& drmRights,
            const String8& rightsPath, const String8& contentPath) {
    ALOGV("Entering saveRights");
    return mDrmManager->saveRights(uniqueId, drmRights, rightsPath, contentPath);
}

String8 DrmManagerService::getOriginalMimeType(int uniqueId, const String8& path, int fd) {
    ALOGV("Entering getOriginalMimeType");
    return mDrmManager->getOriginalMimeType(uniqueId, path, fd);
}

int DrmManagerService::getDrmObjectType(
           int uniqueId, const String8& path, const String8& mimeType) {
    ALOGV("Entering getDrmObjectType");
    return mDrmManager->getDrmObjectType(uniqueId, path, mimeType);
}

int DrmManagerService::checkRightsStatus(
            int uniqueId, const String8& path, int action) {
    ALOGV("Entering checkRightsStatus");
    return mDrmManager->checkRightsStatus(uniqueId, path, action);
}

status_t DrmManagerService::consumeRights(
            int uniqueId, DecryptHandle* decryptHandle, int action, bool reserve) {
    ALOGV("Entering consumeRights");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->consumeRights(uniqueId, decryptHandle, action, reserve);
}

status_t DrmManagerService::setPlaybackStatus(
            int uniqueId, DecryptHandle* decryptHandle, int playbackStatus, int64_t position) {
    ALOGV("Entering setPlaybackStatus");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->setPlaybackStatus(uniqueId, decryptHandle, playbackStatus, position);
}

bool DrmManagerService::validateAction(
            int uniqueId, const String8& path,
            int action, const ActionDescription& description) {
    ALOGV("Entering validateAction");
    return mDrmManager->validateAction(uniqueId, path, action, description);
}

status_t DrmManagerService::removeRights(int uniqueId, const String8& path) {
    ALOGV("Entering removeRights");
    return mDrmManager->removeRights(uniqueId, path);
}

status_t DrmManagerService::removeAllRights(int uniqueId) {
    ALOGV("Entering removeAllRights");
    return mDrmManager->removeAllRights(uniqueId);
}

int DrmManagerService::openConvertSession(int uniqueId, const String8& mimeType) {
    ALOGV("Entering openConvertSession");
    return mDrmManager->openConvertSession(uniqueId, mimeType);
}

DrmConvertedStatus* DrmManagerService::convertData(
            int uniqueId, int convertId, const DrmBuffer* inputData) {
    ALOGV("Entering convertData");
    return mDrmManager->convertData(uniqueId, convertId, inputData);
}

DrmConvertedStatus* DrmManagerService::closeConvertSession(int uniqueId, int convertId) {
    ALOGV("Entering closeConvertSession");
    return mDrmManager->closeConvertSession(uniqueId, convertId);
}

status_t DrmManagerService::getAllSupportInfo(
            int uniqueId, int* length, DrmSupportInfo** drmSupportInfoArray) {
    ALOGV("Entering getAllSupportInfo");
    return mDrmManager->getAllSupportInfo(uniqueId, length, drmSupportInfoArray);
}

DecryptHandle* DrmManagerService::openDecryptSession(
            int uniqueId, int fd, off64_t offset, off64_t length, const char* mime) {
    ALOGV("Entering DrmManagerService::openDecryptSession");
    if (isProtectedCallAllowed()) {
        return mDrmManager->openDecryptSession(uniqueId, fd, offset, length, mime);
    }

    return NULL;
}

DecryptHandle* DrmManagerService::openDecryptSession(
            int uniqueId, const char* uri, const char* mime) {
    ALOGV("Entering DrmManagerService::openDecryptSession with uri");
    if (isProtectedCallAllowed()) {
        return mDrmManager->openDecryptSession(uniqueId, uri, mime);
    }

    return NULL;
}

DecryptHandle* DrmManagerService::openDecryptSession(
            int uniqueId, const DrmBuffer& buf, const String8& mimeType) {
    ALOGV("Entering DrmManagerService::openDecryptSession for streaming");
    if (isProtectedCallAllowed()) {
        return mDrmManager->openDecryptSession(uniqueId, buf, mimeType);
    }

    return NULL;
}

status_t DrmManagerService::closeDecryptSession(int uniqueId, DecryptHandle* decryptHandle) {
    ALOGV("Entering closeDecryptSession");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->closeDecryptSession(uniqueId, decryptHandle);
}

status_t DrmManagerService::initializeDecryptUnit(int uniqueId, DecryptHandle* decryptHandle,
            int decryptUnitId, const DrmBuffer* headerInfo) {
    ALOGV("Entering initializeDecryptUnit");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->initializeDecryptUnit(uniqueId,decryptHandle, decryptUnitId, headerInfo);
}

status_t DrmManagerService::decrypt(
            int uniqueId, DecryptHandle* decryptHandle, int decryptUnitId,
            const DrmBuffer* encBuffer, DrmBuffer** decBuffer, DrmBuffer* IV) {
    ALOGV("Entering decrypt");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->decrypt(uniqueId, decryptHandle, decryptUnitId, encBuffer, decBuffer, IV);
}

status_t DrmManagerService::finalizeDecryptUnit(
            int uniqueId, DecryptHandle* decryptHandle, int decryptUnitId) {
    ALOGV("Entering finalizeDecryptUnit");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->finalizeDecryptUnit(uniqueId, decryptHandle, decryptUnitId);
}

ssize_t DrmManagerService::pread(int uniqueId, DecryptHandle* decryptHandle,
            void* buffer, ssize_t numBytes, off64_t offset) {
    ALOGV("Entering pread");
    // M: disable this check to match ICS
    //if (!isProtectedCallAllowed()) {
        //return DRM_ERROR_NO_PERMISSION;
    //}
    return mDrmManager->pread(uniqueId, decryptHandle, buffer, numBytes, offset);
}

status_t DrmManagerService::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    if (checkCallingPermission(String16("android.permission.DUMP")) == false) {
        snprintf(buffer, SIZE, "Permission Denial: "
                "can't dump DrmManagerService from pid=%d, uid=%d\n",
                IPCThreadState::self()->getCallingPid(),
                IPCThreadState::self()->getCallingUid());
        result.append(buffer);
    } else {
#if DRM_MEMORY_LEAK_TRACK
        bool dumpMem = false;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == String16("-m")) {
                dumpMem = true;
            }
        }
        if (dumpMem) {
            dumpMemoryAddresses(fd);
        }
#endif
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}


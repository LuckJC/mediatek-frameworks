LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftVPX.cpp

LOCAL_C_INCLUDES := \
        $(TOP)/external/libvpx/libvpx \
        $(TOP)/external/libvpx/libvpx/vpx_codec \
        $(TOP)/external/libvpx/libvpx/vpx_ports \
        frameworks/av/media/libstagefright/include \
        $(TOP)/mediatek/frameworks/av/media/libstagefright/include/omx_core

LOCAL_STATIC_LIBRARIES := \
        libvpx

LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

LOCAL_MODULE := libstagefright_soft_vpxdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

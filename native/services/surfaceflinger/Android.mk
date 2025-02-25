LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    Client.cpp \
    DisplayDevice.cpp \
    DispSync.cpp \
    EventControlThread.cpp \
    EventThread.cpp \
    FrameTracker.cpp \
    Layer.cpp \
    LayerDim.cpp \
    MessageQueue.cpp \
    SurfaceFlinger.cpp \
    SurfaceFlingerConsumer.cpp \
    SurfaceTextureLayer.cpp \
    Transform.cpp \
    DisplayHardware/FramebufferSurface.cpp \
    DisplayHardware/HWComposer.cpp \
    DisplayHardware/PowerHAL.cpp \
    DisplayHardware/VirtualDisplaySurface.cpp \
    Effects/Daltonizer.cpp \
    EventLog/EventLogTags.logtags \
    EventLog/EventLog.cpp \
    RenderEngine/Description.cpp \
    RenderEngine/Mesh.cpp \
    RenderEngine/Program.cpp \
    RenderEngine/ProgramCache.cpp \
    RenderEngine/GLExtensions.cpp \
    RenderEngine/RenderEngine.cpp \
    RenderEngine/Texture.cpp \
    RenderEngine/GLES10RenderEngine.cpp \
    RenderEngine/GLES11RenderEngine.cpp \
    RenderEngine/GLES20RenderEngine.cpp


LOCAL_CFLAGS:= -DLOG_TAG=\"SurfaceFlinger\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

ifeq ($(TARGET_BOARD_PLATFORM),omap3)
	LOCAL_CFLAGS += -DNO_RGBX_8888
endif
ifeq ($(TARGET_BOARD_PLATFORM),omap4)
	LOCAL_CFLAGS += -DHAS_CONTEXT_PRIORITY
endif
ifeq ($(TARGET_BOARD_PLATFORM),s5pc110)
	LOCAL_CFLAGS += -DHAS_CONTEXT_PRIORITY
endif

ifeq ($(TARGET_DISABLE_TRIPLE_BUFFERING),true)
	LOCAL_CFLAGS += -DTARGET_DISABLE_TRIPLE_BUFFERING
endif

ifeq ($(TARGET_FORCE_HWC_FOR_VIRTUAL_DISPLAYS),true)
    LOCAL_CFLAGS += -DFORCE_HWC_COPY_FOR_VIRTUAL_DISPLAYS
endif

ifneq ($(NUM_FRAMEBUFFER_SURFACE_BUFFERS),)
  LOCAL_CFLAGS += -DNUM_FRAMEBUFFER_SURFACE_BUFFERS=$(NUM_FRAMEBUFFER_SURFACE_BUFFERS)
endif

ifeq ($(TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK),true)
    LOCAL_CFLAGS += -DRUNNING_WITHOUT_SYNC_FRAMEWORK
endif

# See build/target/board/generic/BoardConfig.mk for a description of this setting.
ifneq ($(VSYNC_EVENT_PHASE_OFFSET_NS),)
    LOCAL_CFLAGS += -DVSYNC_EVENT_PHASE_OFFSET_NS=$(VSYNC_EVENT_PHASE_OFFSET_NS)
else
    LOCAL_CFLAGS += -DVSYNC_EVENT_PHASE_OFFSET_NS=0
endif

# See build/target/board/generic/BoardConfig.mk for a description of this setting.
ifneq ($(SF_VSYNC_EVENT_PHASE_OFFSET_NS),)
    LOCAL_CFLAGS += -DSF_VSYNC_EVENT_PHASE_OFFSET_NS=$(SF_VSYNC_EVENT_PHASE_OFFSET_NS)
else
    LOCAL_CFLAGS += -DSF_VSYNC_EVENT_PHASE_OFFSET_NS=0
endif

ifneq ($(PRESENT_TIME_OFFSET_FROM_VSYNC_NS),)
    LOCAL_CFLAGS += -DPRESENT_TIME_OFFSET_FROM_VSYNC_NS=$(PRESENT_TIME_OFFSET_FROM_VSYNC_NS)
else
    LOCAL_CFLAGS += -DPRESENT_TIME_OFFSET_FROM_VSYNC_NS=0
endif

LOCAL_CFLAGS += -fvisibility=hidden

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libdl \
	libhardware \
	libutils \
	libEGL \
	libGLESv1_CM \
	libGLESv2 \
	libbinder \
	libui \
	libgui

# --- MediaTek ---------------------------------------------------------------
MTK_PATH = ../../../../$(MTK_ROOT)/frameworks-ext/native/services/surfaceflinger

LOCAL_SRC_FILES += \
	$(MTK_PATH)/Layer.cpp \
	$(MTK_PATH)/DisplayDevice.cpp \
	$(MTK_PATH)/SurfaceFlinger.cpp \
	$(MTK_PATH)/SurfaceTextureLayer.cpp \
	$(MTK_PATH)/DisplayHardware/HWComposer.cpp \
	$(MTK_PATH)/RenderEngine/RenderEngine.cpp \
	$(MTK_PATH)/RenderEngine/GLES11RenderEngine.cpp \
	$(MTK_PATH)/RenderEngine/GLES20RenderEngine.cpp \
	$(MTK_PATH)/SurfaceFlingerWatchDog.cpp

ifneq ($(strip $(TARGET_BUILD_VARIANT)), eng)
	LOCAL_CFLAGS += -DMTK_USER_BUILD
endif

ifeq ($(MTK_EMULATOR_SUPPORT), yes)
	LOCAL_CFLAGS += -DEMULATOR_SUPPORT
endif

ifneq ($(MTK_TABLET_HARDWARE), )
	MTK_HWC_CHIP = $(shell echo $(MTK_TABLET_HARDWARE) | tr A-Z a-z )
else
	MTK_HWC_CHIP = $(shell echo $(MTK_PLATFORM) | tr A-Z a-z )
endif

ifeq ($(MTK_HWC_SUPPORT), yes)
	LOCAL_REQUIRED_MODULES += hwcomposer.$(MTK_HWC_CHIP)
endif

ifeq ($(MTK_TRIPLE_FRAMEBUFFER_SUPPORT),yes)
	LOCAL_CFLAGS += -DNUM_FRAMEBUFFER_SURFACE_BUFFERS=3
endif

ifeq ($(MTK_DX_HDCP_SUPPORT), yes)
	LOCAL_CFLAGS += -DMTK_DX_HDCP_SUPPORT
endif

LOCAL_REQUIRED_MODULES += \
	drm_disable_icon.png

LOCAL_SHARED_LIBRARIES += \
	libskia

LOCAL_C_INCLUDES := \
	$(TOP)/$(MTK_ROOT)/hardware/gralloc_extra/include \
	external/skia/include/core \
	external/skia/include/lazy
# ----------------------------------------------------------------------------

LOCAL_MODULE:= libsurfaceflinger

include $(BUILD_SHARED_LIBRARY)

###############################################################
# build surfaceflinger's executable
include $(CLEAR_VARS)

LOCAL_CFLAGS:= -DLOG_TAG=\"SurfaceFlinger\"

LOCAL_SRC_FILES:= \
	main_surfaceflinger.cpp 

LOCAL_SHARED_LIBRARIES := \
	libsurfaceflinger \
	libcutils \
	liblog \
	libbinder \
	libutils

# --- MediaTek ---------------------------------------------------------------
LOCAL_C_INCLUDES := \
	$(TOP)/$(MTK_ROOT)/kernel/include \
# ----------------------------------------------------------------------------

LOCAL_MODULE:= surfaceflinger

include $(BUILD_EXECUTABLE)

###############################################################
# uses jni which may not be available in PDK
ifneq ($(wildcard libnativehelper/include),)
include $(CLEAR_VARS)
LOCAL_CFLAGS:= -DLOG_TAG=\"SurfaceFlinger\"

LOCAL_SRC_FILES:= \
    DdmConnection.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libdl

LOCAL_MODULE:= libsurfaceflinger_ddmconnection

include $(BUILD_SHARED_LIBRARY)
endif # libnativehelper

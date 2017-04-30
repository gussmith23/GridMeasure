LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# beware: this doesn't work without the $(LOCAL_PATH).
CVROOT :=  $(LOCAL_PATH)/../../openCVnative/jni
OPENCV_LIB_TYPE:=STATIC
OPENCV_INSTALL_MODULES:=on
include $(CVROOT)/OpenCV.mk

LOCAL_MODULE := computer-vision-utils-lib

LOCAL_SRC_FILES := ../src/main/cpp/computer-vision-utils-lib.cpp

include $(BUILD_SHARED_LIBRARY)


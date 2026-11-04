LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := turnhub_game
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -Werror
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../../../shared/core/include
LOCAL_SRC_FILES := game_jni.cpp \
    ../../../../../shared/core/src/turnhub_core.cpp \
    ../../../../../shared/core/src/turnhub_offline.cpp \
    ../../../../../shared/core/src/turnhub_json.cpp
include $(BUILD_SHARED_LIBRARY)

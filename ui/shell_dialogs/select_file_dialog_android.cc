// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "select_file_dialog_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "jni/SelectFileDialog_jni.h"
#include "ui/android/window_android.h"

namespace ui {

// static
SelectFileDialogImpl* SelectFileDialogImpl::Create(Listener* listener,
                                                   SelectFilePolicy* policy) {
  return new SelectFileDialogImpl(listener, policy);
}

void SelectFileDialogImpl::OnFileSelected(JNIEnv* env,
                                          jobject java_object,
                                          jstring filepath) {
  if (listener_) {
    std::string path = base::android::ConvertJavaStringToUTF8(env, filepath);
    listener_->FileSelected(base::FilePath(path), 0, NULL);
  }

  is_running_ = false;
}

void SelectFileDialogImpl::OnFileNotSelected(
    JNIEnv* env,
    jobject java_object) {
  if (listener_)
    listener_->FileSelectionCanceled(NULL);

  is_running_ = false;
}

bool SelectFileDialogImpl::IsRunning(gfx::NativeWindow) const {
  return is_running_;
}

void SelectFileDialogImpl::ListenerDestroyed() {
  listener_ = NULL;
}

void SelectFileDialogImpl::SelectFileImpl(
    SelectFileDialog::Type type,
    const base::string16& title,
    const base::FilePath& default_path,
    const SelectFileDialog::FileTypeInfo* file_types,
    int file_type_index,
    const std::string& default_extension,
    gfx::NativeWindow owning_window,
    void* params) {
  JNIEnv* env = base::android::AttachCurrentThread();

  ScopedJavaLocalRef<jstring> capture_value;
  std::vector<base::string16> accept_types;
  if (params) {
    accept_types = *(reinterpret_cast<std::vector<base::string16>*>(params));

    // The last string in params is expected to be the string
    // with capture value.
    capture_value = base::android::ConvertUTF16ToJavaString(env,
          StringToLowerASCII(accept_types.back()));
    base::android::CheckException(env);
    accept_types.pop_back();
  } else {
    capture_value = base::android::ConvertUTF8ToJavaString(env, "filesystem");
  }

  // The rest params elements are expected to be accept_types.
  ScopedJavaLocalRef<jobjectArray> accept_types_java =
      base::android::ToJavaArrayOfStrings(env, accept_types);

  Java_SelectFileDialog_selectFile(env, java_object_.obj(),
                                   accept_types_java.obj(),
                                   capture_value.obj(),
                                   owning_window->GetJavaObject().obj());
  is_running_ = true;
}

bool SelectFileDialogImpl::RegisterSelectFileDialog(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

SelectFileDialogImpl::~SelectFileDialogImpl() {
}

SelectFileDialogImpl::SelectFileDialogImpl(Listener* listener,
                                           SelectFilePolicy* policy)
    : SelectFileDialog(listener, policy), is_running_(false) {
  JNIEnv* env = base::android::AttachCurrentThread();
  java_object_.Reset(
      Java_SelectFileDialog_create(env, reinterpret_cast<jint>(this)));
}

bool SelectFileDialogImpl::HasMultipleFileTypeChoicesImpl() {
  NOTIMPLEMENTED();
  return false;
}

SelectFileDialog* CreateAndroidSelectFileDialog(
    SelectFileDialog::Listener* listener,
    SelectFilePolicy* policy) {
  return SelectFileDialogImpl::Create(listener, policy);
}

}  // namespace ui

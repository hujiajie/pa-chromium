// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/fileapi/browser_file_system_helper.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/sequenced_worker_pool.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "webkit/browser/fileapi/external_mount_points.h"
#include "webkit/browser/fileapi/file_permission_policy.h"
#include "webkit/browser/fileapi/file_system_operation_runner.h"
#include "webkit/browser/fileapi/file_system_options.h"
#include "webkit/browser/fileapi/file_system_task_runners.h"
#include "webkit/browser/fileapi/sandbox_mount_point_provider.h"
#include "webkit/browser/quota/quota_manager.h"

namespace content {

namespace {

using fileapi::FileSystemOptions;

FileSystemOptions CreateBrowserFileSystemOptions(bool is_incognito) {
  FileSystemOptions::ProfileMode profile_mode =
      is_incognito ? FileSystemOptions::PROFILE_MODE_INCOGNITO
                   : FileSystemOptions::PROFILE_MODE_NORMAL;
  std::vector<std::string> additional_allowed_schemes;
  GetContentClient()->browser()->GetAdditionalAllowedSchemesForFileSystem(
      &additional_allowed_schemes);
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAllowFileAccessFromFiles)) {
    additional_allowed_schemes.push_back(chrome::kFileScheme);
  }
  return FileSystemOptions(profile_mode, additional_allowed_schemes);
}

}  // namespace

scoped_refptr<fileapi::FileSystemContext> CreateFileSystemContext(
    const base::FilePath& profile_path,
    bool is_incognito,
    fileapi::ExternalMountPoints* external_mount_points,
    quota::SpecialStoragePolicy* special_storage_policy,
    quota::QuotaManagerProxy* quota_manager_proxy) {

  base::SequencedWorkerPool* pool = content::BrowserThread::GetBlockingPool();
  scoped_refptr<base::SequencedTaskRunner> file_task_runner =
      pool->GetSequencedTaskRunner(pool->GetNamedSequenceToken("FileAPI"));

  scoped_ptr<fileapi::FileSystemTaskRunners> task_runners(
      new fileapi::FileSystemTaskRunners(
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO).get(),
          file_task_runner.get()));

  // Setting up additional mount point providers.
  ScopedVector<fileapi::FileSystemMountPointProvider> additional_providers;
  GetContentClient()->browser()->GetAdditionalFileSystemMountPointProviders(
      profile_path, &additional_providers);

  return new fileapi::FileSystemContext(
      task_runners.Pass(),
      external_mount_points,
      special_storage_policy,
      quota_manager_proxy,
      additional_providers.Pass(),
      profile_path,
      CreateBrowserFileSystemOptions(is_incognito));
}

bool CheckFileSystemPermissionsForProcess(
    fileapi::FileSystemContext* context, int process_id,
    const fileapi::FileSystemURL& url, int permissions,
    base::PlatformFileError* error) {
  DCHECK(error);
  *error = base::PLATFORM_FILE_OK;

  if (!url.is_valid()) {
    *error = base::PLATFORM_FILE_ERROR_INVALID_URL;
    return false;
  }

  fileapi::FileSystemMountPointProvider* mount_point_provider =
      context->GetMountPointProvider(url.type());
  if (!mount_point_provider) {
    *error = base::PLATFORM_FILE_ERROR_INVALID_URL;
    return false;
  }

  base::FilePath file_path;
  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();

  switch (mount_point_provider->GetPermissionPolicy(url, permissions)) {
    case fileapi::FILE_PERMISSION_ALWAYS_DENY:
      *error = base::PLATFORM_FILE_ERROR_SECURITY;
      return false;
    case fileapi::FILE_PERMISSION_ALWAYS_ALLOW:
      CHECK(mount_point_provider == context->sandbox_provider());
      return true;
    case fileapi::FILE_PERMISSION_USE_FILE_PERMISSION: {
      const bool success = policy->HasPermissionsForFile(
          process_id, url.path(), permissions);
      if (!success)
        *error = base::PLATFORM_FILE_ERROR_SECURITY;
      return success;
    }
    case fileapi::FILE_PERMISSION_USE_FILESYSTEM_PERMISSION: {
      const bool success = policy->HasPermissionsForFileSystem(
          process_id, url.mount_filesystem_id(), permissions);
      if (!success)
        *error = base::PLATFORM_FILE_ERROR_SECURITY;
      return success;
    }
  }
  NOTREACHED();
  *error = base::PLATFORM_FILE_ERROR_SECURITY;
  return false;
}

void SyncGetPlatformPath(fileapi::FileSystemContext* context,
                         int process_id,
                         const GURL& path,
                         base::FilePath* platform_path) {
  DCHECK(context->task_runners()->file_task_runner()->
         RunsTasksOnCurrentThread());
  DCHECK(platform_path);
  *platform_path = base::FilePath();
  fileapi::FileSystemURL url(context->CrackURL(path));
  if (!url.is_valid())
    return;

  // Make sure if this file is ok to be read (in the current architecture
  // which means roughly same as the renderer is allowed to get the platform
  // path to the file).
  base::PlatformFileError error;
  if (!CheckFileSystemPermissionsForProcess(
      context, process_id, url, fileapi::kReadFilePermissions, &error))
    return;

  context->operation_runner()->SyncGetPlatformPath(url, platform_path);

  // The path is to be attached to URLLoader so we grant read permission
  // for the file. (We first need to check if it can already be read not to
  // overwrite existing permissions)
  if (!ChildProcessSecurityPolicyImpl::GetInstance()->CanReadFile(
          process_id, *platform_path)) {
    ChildProcessSecurityPolicyImpl::GetInstance()->GrantReadFile(
        process_id, *platform_path);
  }
}

}  // namespace content

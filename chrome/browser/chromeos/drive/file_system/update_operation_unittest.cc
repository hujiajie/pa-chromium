// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/update_operation.h"

#include "chrome/browser/chromeos/drive/file_system/operation_test_base.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/google_apis/fake_drive_service.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace file_system {

class UpdateOperationTest : public OperationTestBase {
 protected:
  virtual void SetUp() OVERRIDE {
   OperationTestBase::SetUp();
   operation_.reset(new UpdateOperation(blocking_task_runner(),
                                        observer(),
                                        scheduler(),
                                        metadata(),
                                        cache()));
 }

 virtual void TearDown() OVERRIDE {
   operation_.reset();
   OperationTestBase::TearDown();
 }

 scoped_ptr<UpdateOperation> operation_;
};

TEST_F(UpdateOperationTest, UpdateFileByResourceId_PersistentFile) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  const std::string kResourceId("file:2_file_resource_id");
  const std::string kMd5("3b4382ebefec6e743578c76bbd0575ce");

  const base::FilePath kTestFile = temp_dir().Append(FILE_PATH_LITERAL("foo"));
  const std::string kTestFileContent = "I'm being uploaded! Yay!";
  google_apis::test_util::WriteStringToFile(kTestFile, kTestFileContent);

  // Pin the file so it'll be store in "persistent" directory.
  FileError error = FILE_ERROR_FAILED;
  cache()->PinOnUIThread(
      kResourceId, kMd5,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // First store a file to cache.
  error = FILE_ERROR_FAILED;
  cache()->StoreOnUIThread(
      kResourceId, kMd5, kTestFile,
      internal::FileCache::FILE_OPERATION_COPY,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Add the dirty bit.
  error = FILE_ERROR_FAILED;
  cache()->MarkDirtyOnUIThread(
      kResourceId, kMd5,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  int64 original_changestamp = fake_service()->largest_changestamp();

  // The callback will be called upon completion of
  // UpdateFileByResourceId().
  error = FILE_ERROR_FAILED;
  operation_->UpdateFileByResourceId(
      kResourceId,
      ClientContext(USER_INITIATED),
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Check that the server has received an update.
  EXPECT_LT(original_changestamp, fake_service()->largest_changestamp());

  // Check that the file size is updated to that of the updated content.
  google_apis::GDataErrorCode gdata_error = google_apis::GDATA_OTHER_ERROR;
  scoped_ptr<google_apis::ResourceEntry> server_entry;
  fake_service()->GetResourceEntry(
      kResourceId,
      google_apis::test_util::CreateCopyResultCallback(&gdata_error,
                                                       &server_entry));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(google_apis::HTTP_SUCCESS, gdata_error);
  EXPECT_EQ(static_cast<int64>(kTestFileContent.size()),
            server_entry->file_size());
}

TEST_F(UpdateOperationTest, UpdateFileByResourceId_NonexistentFile) {
  FileError error = FILE_ERROR_OK;
  operation_->UpdateFileByResourceId(
      "file:nonexistent_resource_id",
      ClientContext(USER_INITIATED),
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_NOT_FOUND, error);
}

}  // namespace file_system
}  // namespace drive

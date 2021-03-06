// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/drive/change_list_loader.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/mock_directory_change_observer.h"
#include "chrome/browser/chromeos/drive/sync_client.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/fake_drive_service.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::StrictMock;
using ::testing::_;

namespace drive {
namespace {

const int64 kLotsOfSpace = internal::kMinFreeSpace * 10;

// Counts the number of invocation, and if it increased up to |expected_counter|
// quits the current message loop.
void AsyncInitializationCallback(
    int* counter, int expected_counter, base::MessageLoop* message_loop,
    FileError error, scoped_ptr<ResourceEntry> entry) {
  if (error != FILE_ERROR_OK || !entry) {
    // If we hit an error case, quit the message loop immediately.
    // Then the expectation in the test case can find it because the actual
    // value of |counter| is different from the expected one.
    message_loop->Quit();
    return;
  }

  (*counter)++;
  if (*counter >= expected_counter)
    message_loop->Quit();
}

}  // namespace

class FileSystemTest : public testing::Test {
 protected:
  FileSystemTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_) {
  }

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile);

    // The fake object will be manually deleted in TearDown().
    fake_drive_service_.reset(new google_apis::FakeDriveService);
    fake_drive_service_->LoadResourceListForWapi(
        "chromeos/gdata/root_feed.json");
    fake_drive_service_->LoadAccountMetadataForWapi(
        "chromeos/gdata/account_metadata.json");

    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    scheduler_.reset(new JobScheduler(profile_.get(),
                                      fake_drive_service_.get()));

    scoped_refptr<base::SequencedWorkerPool> pool =
        content::BrowserThread::GetBlockingPool();
    blocking_task_runner_ =
        pool->GetSequencedTaskRunner(pool->GetSequenceToken());

    cache_.reset(new internal::FileCache(util::GetCacheRootPath(profile_.get()),
                                         blocking_task_runner_.get(),
                                         fake_free_disk_space_getter_.get()));

    mock_directory_observer_.reset(new StrictMock<MockDirectoryChangeObserver>);

    bool success = false;
    base::PostTaskAndReplyWithResult(
        blocking_task_runner_,
        FROM_HERE,
        base::Bind(&internal::FileCache::Initialize,
                   base::Unretained(cache_.get())),
        google_apis::test_util::CreateCopyResultCallback(&success));
    google_apis::test_util::RunBlockingPoolTask();
    ASSERT_TRUE(success);

    SetUpResourceMetadataAndFileSystem();
  }

  void SetUpResourceMetadataAndFileSystem() {
    resource_metadata_.reset(new internal::ResourceMetadata(
        cache_->GetCacheDirectoryPath(internal::FileCache::CACHE_TYPE_META),
        blocking_task_runner_));

    file_system_.reset(new FileSystem(profile_.get(),
                                      cache_.get(),
                                      fake_drive_service_.get(),
                                      scheduler_.get(),
                                      resource_metadata_.get(),
                                      blocking_task_runner_.get()));
    file_system_->AddObserver(mock_directory_observer_.get());
    file_system_->Initialize();

    // Disable delaying so that the sync starts immediately.
    file_system_->sync_client_for_testing()->set_delay_for_testing(
        base::TimeDelta::FromSeconds(0));

    FileError error = FILE_ERROR_FAILED;
    base::PostTaskAndReplyWithResult(
        blocking_task_runner_,
        FROM_HERE,
        base::Bind(&internal::ResourceMetadata::Initialize,
                   base::Unretained(resource_metadata_.get())),
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    ASSERT_EQ(FILE_ERROR_OK, error);
  }

  virtual void TearDown() OVERRIDE {
    ASSERT_TRUE(file_system_);
    file_system_.reset();
    scheduler_.reset();
    fake_drive_service_.reset();
    cache_.reset();
    profile_.reset(NULL);
  }

  // Loads the full resource list via FakeDriveService.
  bool LoadFullResourceList() {
    FileError error = FILE_ERROR_FAILED;
    file_system_->change_list_loader()->LoadIfNeeded(
        DirectoryFetchInfo(),
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    return error == FILE_ERROR_OK;
  }

  // Gets resource entry by path synchronously.
  scoped_ptr<ResourceEntry> GetResourceEntryByPathSync(
      const base::FilePath& file_path) {
    FileError error = FILE_ERROR_FAILED;
    scoped_ptr<ResourceEntry> entry;
    file_system_->GetResourceEntryByPath(
        file_path,
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    google_apis::test_util::RunBlockingPoolTask();

    return entry.Pass();
  }

  // Gets directory info by path synchronously.
  scoped_ptr<ResourceEntryVector> ReadDirectoryByPathSync(
      const base::FilePath& file_path) {
    FileError error = FILE_ERROR_FAILED;
    bool unused_hide_hosted_documents;
    scoped_ptr<ResourceEntryVector> entries;
    file_system_->ReadDirectoryByPath(
        file_path,
        google_apis::test_util::CreateCopyResultCallback(
            &error, &unused_hide_hosted_documents, &entries));
    google_apis::test_util::RunBlockingPoolTask();

    return entries.Pass();
  }

  // Returns true if an entry exists at |file_path|.
  bool EntryExists(const base::FilePath& file_path) {
    return GetResourceEntryByPathSync(file_path);
  }

  // Gets the resource ID of |file_path|. Returns an empty string if not found.
  std::string GetResourceIdByPath(const base::FilePath& file_path) {
    scoped_ptr<ResourceEntry> entry =
        GetResourceEntryByPathSync(file_path);
    if (entry)
      return entry->resource_id();
    else
      return "";
  }

  // Helper function to call GetCacheEntry from origin thread.
  bool GetCacheEntryFromOriginThread(const std::string& resource_id,
                                     const std::string& md5,
                                     FileCacheEntry* cache_entry) {
    bool result = false;
    cache_->GetCacheEntryOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(&result, cache_entry));
    google_apis::test_util::RunBlockingPoolTask();
    return result;
  }

  // Flag for specifying the timestamp of the test filesystem cache.
  enum SetUpTestFileSystemParam {
    USE_OLD_TIMESTAMP,
    USE_SERVER_TIMESTAMP,
  };

  // Sets up a filesystem with directories: drive/root, drive/root/Dir1,
  // drive/root/Dir1/SubDir2 and files drive/root/File1, drive/root/Dir1/File2,
  // drive/root/Dir1/SubDir2/File3. If |use_up_to_date_timestamp| is true, sets
  // the changestamp to 654321, equal to that of "account_metadata.json" test
  // data, indicating the cache is holding the latest file system info.
  bool SetUpTestFileSystem(SetUpTestFileSystemParam param) {
    // Destroy the existing resource metadata to close DB.
    resource_metadata_.reset();

    const std::string root_resource_id =
        fake_drive_service_->GetRootResourceId();
    scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
        resource_metadata(new internal::ResourceMetadata(
            cache_->GetCacheDirectoryPath(internal::FileCache::CACHE_TYPE_META),
            blocking_task_runner_));

    FileError error = FILE_ERROR_FAILED;
    base::PostTaskAndReplyWithResult(
        blocking_task_runner_,
        FROM_HERE,
        base::Bind(&internal::ResourceMetadata::Initialize,
                   base::Unretained(resource_metadata.get())),
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    resource_metadata->SetLargestChangestampOnUIThread(
        param == USE_SERVER_TIMESTAMP ? 654321 : 1,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root is already prepared by ResourceMetadata.
    base::FilePath file_path;

    // drive/root
    resource_metadata->AddEntryOnUIThread(
        util::CreateMyDriveRootEntry(root_resource_id),
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root/File1
    ResourceEntry file1;
    file1.set_title("File1");
    file1.set_resource_id("resource_id:File1");
    file1.set_parent_resource_id(root_resource_id);
    file1.mutable_file_specific_info()->set_md5("md5");
    file1.mutable_file_info()->set_is_directory(false);
    file1.mutable_file_info()->set_size(1048576);
    resource_metadata->AddEntryOnUIThread(
        file1,
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root/Dir1
    ResourceEntry dir1;
    dir1.set_title("Dir1");
    dir1.set_resource_id("resource_id:Dir1");
    dir1.set_parent_resource_id(root_resource_id);
    dir1.mutable_file_info()->set_is_directory(true);
    resource_metadata->AddEntryOnUIThread(
        dir1,
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root/Dir1/File2
    ResourceEntry file2;
    file2.set_title("File2");
    file2.set_resource_id("resource_id:File2");
    file2.set_parent_resource_id(dir1.resource_id());
    file2.mutable_file_specific_info()->set_md5("md5");
    file2.mutable_file_info()->set_is_directory(false);
    file2.mutable_file_info()->set_size(555);
    resource_metadata->AddEntryOnUIThread(
        file2,
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root/Dir1/SubDir2
    ResourceEntry dir2;
    dir2.set_title("SubDir2");
    dir2.set_resource_id("resource_id:SubDir2");
    dir2.set_parent_resource_id(dir1.resource_id());
    dir2.mutable_file_info()->set_is_directory(true);
    resource_metadata->AddEntryOnUIThread(
        dir2,
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // drive/root/Dir1/SubDir2/File3
    ResourceEntry file3;
    file3.set_title("File3");
    file3.set_resource_id("resource_id:File3");
    file3.set_parent_resource_id(dir2.resource_id());
    file3.mutable_file_specific_info()->set_md5("md5");
    file3.mutable_file_info()->set_is_directory(false);
    file3.mutable_file_info()->set_size(12345);
    resource_metadata->AddEntryOnUIThread(
        file3,
        google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
    google_apis::test_util::RunBlockingPoolTask();
    if (error != FILE_ERROR_OK)
      return false;

    // Recreate resource metadata.
    SetUpResourceMetadataAndFileSystem();

    return true;
  }

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  scoped_ptr<TestingProfile> profile_;

  scoped_ptr<internal::FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<FileSystem> file_system_;
  scoped_ptr<google_apis::FakeDriveService> fake_drive_service_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
      resource_metadata_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
  scoped_ptr<StrictMock<MockDirectoryChangeObserver> > mock_directory_observer_;
};

TEST_F(FileSystemTest, DuplicatedAsyncInitialization) {
  // "Fast fetch" will fire an OnirectoryChanged event.
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(
      Eq(base::FilePath(FILE_PATH_LITERAL("drive"))))).Times(1);

  int counter = 0;
  const GetResourceEntryCallback& callback = base::Bind(
      &AsyncInitializationCallback, &counter, 2, &message_loop_);

  file_system_->GetResourceEntryByPath(
      base::FilePath(FILE_PATH_LITERAL("drive/root")), callback);
  file_system_->GetResourceEntryByPath(
      base::FilePath(FILE_PATH_LITERAL("drive/root")), callback);
  message_loop_.Run();  // Wait to get our result
  EXPECT_EQ(2, counter);

  // Although GetResourceEntryByPath() was called twice, the resource list
  // should only be loaded once. In the past, there was a bug that caused
  // it to be loaded twice.
  EXPECT_EQ(1, fake_drive_service_->resource_list_load_count());
  // See the comment in GetMyDriveRoot test case why this is 2.
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());
}

TEST_F(FileSystemTest, GetGrandRootEntry) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(util::kDriveGrandRootSpecialResourceId, entry->resource_id());

  // Getting the grand root entry should not cause the resource load to happen.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetOtherDirEntry) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/other"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(util::kDriveOtherDirSpecialResourceId, entry->resource_id());

  // Getting the "other" directory entry should not cause the resource load to
  // happen.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetMyDriveRoot) {
  // "Fast fetch" will fire an OnirectoryChanged event.
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(
      Eq(base::FilePath(FILE_PATH_LITERAL("drive"))))).Times(1);

  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(fake_drive_service_->GetRootResourceId(), entry->resource_id());

  // Absence of "drive/root" in the local metadata triggers the "fast fetch"
  // of "drive" directory. Fetch of "drive" grand root directory has a special
  // implementation. Instead of normal GetResourceListInDirectory(), it is
  // emulated by calling GetAboutResource() so that the resource_id of
  // "drive/root" is listed.
  // Together with the normal GetAboutResource() call to retrieve the largest
  // changestamp, the method is called twice.
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());

  // After "fast fetch" is done, full resource list is fetched.
  EXPECT_EQ(1, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetExistingFile) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:2_file_resource_id", entry->resource_id());

  EXPECT_EQ(1, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(1, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetExistingDocument) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/Document 1 excludeDir-test.gdoc"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("document:5_document_resource_id", entry->resource_id());
}

TEST_F(FileSystemTest, GetNonExistingFile) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/nonexisting.file"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  EXPECT_FALSE(entry);
}

TEST_F(FileSystemTest, GetEncodedFileNames) {
  const base::FilePath kFilePath1(
      FILE_PATH_LITERAL("drive/root/Slash / in file 1.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath1);
  ASSERT_FALSE(entry);

  const base::FilePath kFilePath2 = base::FilePath::FromUTF8Unsafe(
      "drive/root/Slash \xE2\x88\x95 in file 1.txt");
  entry = GetResourceEntryByPathSync(kFilePath2);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:slash_file_resource_id", entry->resource_id());

  const base::FilePath kFilePath3 = base::FilePath::FromUTF8Unsafe(
      "drive/root/Slash \xE2\x88\x95 in directory/Slash SubDir File.txt");
  entry = GetResourceEntryByPathSync(kFilePath3);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:slash_subdir_file", entry->resource_id());
}

TEST_F(FileSystemTest, GetDuplicateNames) {
  const base::FilePath kFilePath1(
      FILE_PATH_LITERAL("drive/root/Duplicate Name.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath1);
  ASSERT_TRUE(entry);
  const std::string resource_id1 = entry->resource_id();

  const base::FilePath kFilePath2(
      FILE_PATH_LITERAL("drive/root/Duplicate Name (2).txt"));
  entry = GetResourceEntryByPathSync(kFilePath2);
  ASSERT_TRUE(entry);
  const std::string resource_id2 = entry->resource_id();

  // The entries are de-duped non-deterministically, so we shouldn't rely on the
  // names matching specific resource ids.
  const std::string file3_resource_id = "file:3_file_resource_id";
  const std::string file4_resource_id = "file:4_file_resource_id";
  EXPECT_TRUE(file3_resource_id == resource_id1 ||
              file3_resource_id == resource_id2);
  EXPECT_TRUE(file4_resource_id == resource_id1 ||
              file4_resource_id == resource_id2);
}

TEST_F(FileSystemTest, GetExistingDirectory) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root/Directory 1"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  ASSERT_EQ("folder:1_folder_resource_id", entry->resource_id());

  // The changestamp should be propagated to the directory.
  EXPECT_EQ(fake_drive_service_->largest_changestamp(),
            entry->directory_specific_info().changestamp());
}

TEST_F(FileSystemTest, GetInSubSubdir) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/Directory 1/Sub Directory Folder/"
                        "Sub Sub Directory Folder"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  ASSERT_EQ("folder:sub_sub_directory_folder_id", entry->resource_id());
}

TEST_F(FileSystemTest, GetOrphanFile) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/other/Orphan File 1.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntryByPathSync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:1_orphanfile_resource_id", entry->resource_id());
}

TEST_F(FileSystemTest, ReadDirectoryByPath_Root) {
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(
      Eq(base::FilePath(FILE_PATH_LITERAL("drive"))))).Times(1);

  // ReadDirectoryByPath() should kick off the resource list loading.
  scoped_ptr<ResourceEntryVector> entries(
      ReadDirectoryByPathSync(base::FilePath::FromUTF8Unsafe("drive")));
  // The root directory should be read correctly.
  ASSERT_TRUE(entries);
  ASSERT_EQ(2U, entries->size());

  // The found two directories should be /drive/root and /drive/other.
  bool found_other = false;
  bool found_my_drive = false;
  for (size_t i = 0; i < entries->size(); ++i) {
    const base::FilePath title =
        base::FilePath::FromUTF8Unsafe((*entries)[i].title());
    if (title == base::FilePath(util::kDriveOtherDirName)) {
      found_other = true;
    } else if (title == base::FilePath(util::kDriveMyDriveRootDirName)) {
      found_my_drive = true;
    }
  }

  EXPECT_TRUE(found_other);
  EXPECT_TRUE(found_my_drive);
}

TEST_F(FileSystemTest, ReadDirectoryByPath_NonRootDirectory) {
  // ReadDirectoryByPath() should kick off the resource list loading.
  scoped_ptr<ResourceEntryVector> entries(
      ReadDirectoryByPathSync(
          base::FilePath::FromUTF8Unsafe("drive/root/Directory 1")));
  // The non root directory should also be read correctly.
  // There was a bug (crbug.com/181487), which broke this behavior.
  // Make sure this is fixed.
  ASSERT_TRUE(entries);
  EXPECT_EQ(3U, entries->size());
}

TEST_F(FileSystemTest, LoadFileSystemFromUpToDateCache) {
  ASSERT_TRUE(SetUpTestFileSystem(USE_SERVER_TIMESTAMP));

  // Kicks loading of cached file system and query for server update.
  EXPECT_TRUE(ReadDirectoryByPathSync(util::GetDriveMyDriveRootPath()));

  // SetUpTestFileSystem and "account_metadata.json" have the same
  // changestamp (i.e. the local metadata is up-to-date), so no request for
  // new resource list (i.e., call to GetResourceList) should happen.
  EXPECT_EQ(1, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());

  // Since the file system has verified that it holds the latest snapshot,
  // it should change its state to "loaded", which admits periodic refresh.
  // To test it, call CheckForUpdates and verify it does try to check updates.
  file_system_->CheckForUpdates();
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());
}

TEST_F(FileSystemTest, LoadFileSystemFromCacheWhileOffline) {
  ASSERT_TRUE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));

  // Make GetResourceList fail for simulating offline situation. This will
  // leave the file system "loaded from cache, but not synced with server"
  // state.
  fake_drive_service_->set_offline(true);

  // Kicks loading of cached file system and query for server update.
  EXPECT_TRUE(ReadDirectoryByPathSync(util::GetDriveMyDriveRootPath()));
  // Loading of about resource should not happen as it's offline.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());

  // Tests that cached data can be loaded even if the server is not reachable.
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/File1"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1"))));
  EXPECT_TRUE(
      EntryExists(base::FilePath(FILE_PATH_LITERAL("drive/root/Dir1/File2"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/SubDir2"))));
  EXPECT_TRUE(EntryExists(
      base::FilePath(FILE_PATH_LITERAL("drive/root/Dir1/SubDir2/File3"))));

  // Since the file system has at least succeeded to load cached snapshot,
  // the file system should be able to start periodic refresh.
  // To test it, call CheckForUpdates and verify it does try to check
  // updates, which will cause directory changes.
  fake_drive_service_->set_offline(false);

  file_system_->CheckForUpdates();
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(_))
      .Times(AtLeast(1));

  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(1, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(1, fake_drive_service_->change_list_load_count());
}

TEST_F(FileSystemTest, ReadDirectoryWhileRefreshing) {
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(_))
      .Times(AtLeast(1));

  // Enter the "refreshing" state so the fast fetch will be performed.
  ASSERT_TRUE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // The list of resources in "drive/root/Dir1" should be fetched.
  EXPECT_TRUE(ReadDirectoryByPathSync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1"))));
  EXPECT_EQ(1, fake_drive_service_->directory_load_count());
}

TEST_F(FileSystemTest, GetResourceEntryExistingWhileRefreshing) {
  // Enter the "refreshing" state.
  ASSERT_TRUE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // If an entry is already found in local metadata, no directory fetch happens.
  EXPECT_TRUE(GetResourceEntryByPathSync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/File2"))));
  EXPECT_EQ(0, fake_drive_service_->directory_load_count());
}

TEST_F(FileSystemTest, GetResourceEntryNonExistentWhileRefreshing) {
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(_))
      .Times(AtLeast(1));

  // Enter the "refreshing" state so the fast fetch will be performed.
  ASSERT_TRUE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // If an entry is not found, parent directory's resource list is fetched.
  EXPECT_FALSE(GetResourceEntryByPathSync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/NonExistentFile"))));
  EXPECT_EQ(1, fake_drive_service_->directory_load_count());
}

TEST_F(FileSystemTest, CreateDirectoryByImplicitLoad) {
  // Intentionally *not* calling LoadFullResourceList(), for testing that
  // CreateDirectory ensures the resource list is loaded before it runs.

  base::FilePath existing_directory(
      FILE_PATH_LITERAL("drive/root/Directory 1"));
  FileError error = FILE_ERROR_FAILED;
  file_system_->CreateDirectory(
      existing_directory,
      true,  // is_exclusive
      false,  // is_recursive
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();

  // It should fail because is_exclusive is set to true.
  EXPECT_EQ(FILE_ERROR_EXISTS, error);
}

TEST_F(FileSystemTest, PinAndUnpin) {
  // Pinned file gets synced and it results in entry state changes.
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(
      Eq(base::FilePath(FILE_PATH_LITERAL("drive/root"))))).Times(AtLeast(1));

  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_path(FILE_PATH_LITERAL("drive/root/File 1.txt"));

  // Get the file info.
  scoped_ptr<ResourceEntry> entry(GetResourceEntryByPathSync(file_path));
  ASSERT_TRUE(entry);

  // Pin the file.
  FileError error = FILE_ERROR_FAILED;
  file_system_->Pin(file_path,
                    google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  FileCacheEntry cache_entry;
  EXPECT_TRUE(GetCacheEntryFromOriginThread(
      entry->resource_id(), std::string(), &cache_entry));
  EXPECT_TRUE(cache_entry.is_pinned());
  EXPECT_TRUE(cache_entry.is_present());

  // Unpin the file.
  error = FILE_ERROR_FAILED;
  file_system_->Unpin(file_path,
                      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  EXPECT_TRUE(GetCacheEntryFromOriginThread(
      entry->resource_id(), std::string(), &cache_entry));
  EXPECT_FALSE(cache_entry.is_pinned());
}

TEST_F(FileSystemTest, PinAndUnpin_NotSynced) {
  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_path(FILE_PATH_LITERAL("drive/root/File 1.txt"));

  // Get the file info.
  scoped_ptr<ResourceEntry> entry(GetResourceEntryByPathSync(file_path));
  ASSERT_TRUE(entry);

  // Unpin the file just after pinning. File fetch should be cancelled.
  FileError error_pin = FILE_ERROR_FAILED;
  file_system_->Pin(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error_pin));

  FileError error_unpin = FILE_ERROR_FAILED;
  file_system_->Unpin(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error_unpin));

  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error_pin);
  EXPECT_EQ(FILE_ERROR_OK, error_unpin);

  // No cache file available because the sync was cancelled by Unpin().
  FileCacheEntry cache_entry;
  EXPECT_FALSE(GetCacheEntryFromOriginThread(
      entry->resource_id(), std::string(), &cache_entry));
}

TEST_F(FileSystemTest, GetAvailableSpace) {
  FileError error = FILE_ERROR_OK;
  int64 bytes_total;
  int64 bytes_used;
  file_system_->GetAvailableSpace(
      google_apis::test_util::CreateCopyResultCallback(
          &error, &bytes_total, &bytes_used));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(GG_LONGLONG(6789012345), bytes_used);
  EXPECT_EQ(GG_LONGLONG(9876543210), bytes_total);
}

TEST_F(FileSystemTest, RefreshDirectory) {
  ASSERT_TRUE(LoadFullResourceList());

  // We'll notify the directory change to the observer.
  EXPECT_CALL(*mock_directory_observer_,
              OnDirectoryChanged(Eq(util::GetDriveMyDriveRootPath()))).Times(1);

  FileError error = FILE_ERROR_FAILED;
  file_system_->RefreshDirectory(
      util::GetDriveMyDriveRootPath(),
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);
}

TEST_F(FileSystemTest, OpenAndCloseFile) {
  ASSERT_TRUE(LoadFullResourceList());

  // The transfered file is cached and the change of "offline available"
  // attribute is notified.
  EXPECT_CALL(*mock_directory_observer_, OnDirectoryChanged(
      Eq(base::FilePath(FILE_PATH_LITERAL("drive/root"))))).Times(AtLeast(1));

  const base::FilePath kFileInRoot(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  scoped_ptr<ResourceEntry> entry(GetResourceEntryByPathSync(kFileInRoot));
  const std::string& file_resource_id = entry->resource_id();
  const std::string& md5 = entry->file_specific_info().md5();

  // Open kFileInRoot ("drive/root/File 1.txt").
  FileError error = FILE_ERROR_FAILED;
  base::FilePath file_path;
  file_system_->OpenFile(
      kFileInRoot,
      google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
  google_apis::test_util::RunBlockingPoolTask();
  const base::FilePath opened_file_path = file_path;

  // Verify that the file was properly opened.
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Try to open the already opened file.
  file_system_->OpenFile(
      kFileInRoot,
      google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
  google_apis::test_util::RunBlockingPoolTask();

  // It must fail.
  EXPECT_EQ(FILE_ERROR_IN_USE, error);

  // Verify that the file contents match the expected contents.
  const std::string kExpectedContent = "This is some test content.";
  std::string cache_file_data;
  EXPECT_TRUE(file_util::ReadFileToString(opened_file_path, &cache_file_data));
  EXPECT_EQ(kExpectedContent, cache_file_data);

  FileCacheEntry cache_entry;
  EXPECT_TRUE(GetCacheEntryFromOriginThread(file_resource_id, md5,
                                            &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());
  EXPECT_TRUE(cache_entry.is_dirty());

  base::FilePath cache_file_path;
  cache_->GetFileOnUIThread(file_resource_id, md5,
                            google_apis::test_util::CreateCopyResultCallback(
                                &error, &cache_file_path));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_EQ(cache_file_path, opened_file_path);

  // Write a new content.
  const std::string kNewContent = kExpectedContent + kExpectedContent;
  EXPECT_TRUE(google_apis::test_util::WriteStringToFile(cache_file_path,
                                                        kNewContent));

  // Close kFileInRoot ("drive/root/File 1.txt").
  file_system_->CloseFile(
      kFileInRoot,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();

  // Verify that the file was properly closed.
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Verify that the file was synced as expected.
  google_apis::GDataErrorCode gdata_error = google_apis::GDATA_FILE_ERROR;
  scoped_ptr<google_apis::ResourceEntry> gdata_entry;
  fake_drive_service_->GetResourceEntry(
      file_resource_id,
      google_apis::test_util::CreateCopyResultCallback(
          &gdata_error, &gdata_entry));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(gdata_error, google_apis::HTTP_SUCCESS);
  ASSERT_TRUE(gdata_entry);
  EXPECT_EQ(static_cast<int>(kNewContent.size()), gdata_entry->file_size());

  // Try to close the same file twice.
  file_system_->CloseFile(
      kFileInRoot,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();

  // It must fail.
  EXPECT_EQ(FILE_ERROR_NOT_FOUND, error);
}

TEST_F(FileSystemTest, MarkCacheFileAsMountedAndUnmounted) {
  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_in_root(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  scoped_ptr<ResourceEntry> entry(GetResourceEntryByPathSync(file_in_root));
  ASSERT_TRUE(entry);

  // Write to cache.
  FileError error = FILE_ERROR_FAILED;
  cache_->StoreOnUIThread(
      entry->resource_id(),
      entry->file_specific_info().md5(),
      google_apis::test_util::GetTestFilePath("chromeos/gdata/root_feed.json"),
      internal::FileCache::FILE_OPERATION_COPY,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  ASSERT_EQ(FILE_ERROR_OK, error);

  // Test for mounting.
  base::FilePath file_path;
  file_system_->MarkCacheFileAsMounted(
      file_in_root,
      google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Cannot remove a cache entry while it's being mounted.
  cache_->RemoveOnUIThread(
      entry->resource_id(),
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_IN_USE, error);

  // Test for unmounting.
  error = FILE_ERROR_FAILED;
  file_system_->MarkCacheFileAsUnmounted(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Now able to remove the cache entry.
  cache_->RemoveOnUIThread(
      entry->resource_id(),
      google_apis::test_util::CreateCopyResultCallback(&error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);
}

}   // namespace drive

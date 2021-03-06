// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "base/base_paths.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "chrome/installer/util/create_dir_work_item.h"
#include "chrome/installer/util/work_item.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
  class CreateDirWorkItemTest : public testing::Test {
   protected:
    virtual void SetUp() {
      ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    }

    base::ScopedTempDir temp_dir_;
  };
};

TEST_F(CreateDirWorkItemTest, CreatePath) {
  base::FilePath parent_dir(temp_dir_.path());
  parent_dir = parent_dir.AppendASCII("a");
  file_util::CreateDirectory(parent_dir);
  ASSERT_TRUE(file_util::PathExists(parent_dir));

  base::FilePath top_dir_to_create(parent_dir);
  top_dir_to_create = top_dir_to_create.AppendASCII("b");

  base::FilePath dir_to_create(top_dir_to_create);
  dir_to_create = dir_to_create.AppendASCII("c");
  dir_to_create = dir_to_create.AppendASCII("d");

  scoped_ptr<CreateDirWorkItem> work_item(
      WorkItem::CreateCreateDirWorkItem(dir_to_create));

  EXPECT_TRUE(work_item->Do());

  EXPECT_TRUE(file_util::PathExists(dir_to_create));

  work_item->Rollback();

  // Rollback should delete all the paths up to top_dir_to_create.
  EXPECT_FALSE(file_util::PathExists(top_dir_to_create));
  EXPECT_TRUE(file_util::PathExists(parent_dir));
}

TEST_F(CreateDirWorkItemTest, CreateExistingPath) {
  base::FilePath dir_to_create(temp_dir_.path());
  dir_to_create = dir_to_create.AppendASCII("aa");
  file_util::CreateDirectory(dir_to_create);
  ASSERT_TRUE(file_util::PathExists(dir_to_create));

  scoped_ptr<CreateDirWorkItem> work_item(
      WorkItem::CreateCreateDirWorkItem(dir_to_create));

  EXPECT_TRUE(work_item->Do());

  EXPECT_TRUE(file_util::PathExists(dir_to_create));

  work_item->Rollback();

  // Rollback should not remove the path since it exists before
  // the CreateDirWorkItem is called.
  EXPECT_TRUE(file_util::PathExists(dir_to_create));
}

TEST_F(CreateDirWorkItemTest, CreateSharedPath) {
  base::FilePath dir_to_create_1(temp_dir_.path());
  dir_to_create_1 = dir_to_create_1.AppendASCII("aaa");

  base::FilePath dir_to_create_2(dir_to_create_1);
  dir_to_create_2 = dir_to_create_2.AppendASCII("bbb");

  base::FilePath dir_to_create_3(dir_to_create_2);
  dir_to_create_3 = dir_to_create_3.AppendASCII("ccc");

  scoped_ptr<CreateDirWorkItem> work_item(
      WorkItem::CreateCreateDirWorkItem(dir_to_create_3));

  EXPECT_TRUE(work_item->Do());

  EXPECT_TRUE(file_util::PathExists(dir_to_create_3));

  // Create another directory under dir_to_create_2
  base::FilePath dir_to_create_4(dir_to_create_2);
  dir_to_create_4 = dir_to_create_4.AppendASCII("ddd");
  file_util::CreateDirectory(dir_to_create_4);
  ASSERT_TRUE(file_util::PathExists(dir_to_create_4));

  work_item->Rollback();

  // Rollback should delete dir_to_create_3.
  EXPECT_FALSE(file_util::PathExists(dir_to_create_3));

  // Rollback should not delete dir_to_create_2 as it is shared.
  EXPECT_TRUE(file_util::PathExists(dir_to_create_2));
  EXPECT_TRUE(file_util::PathExists(dir_to_create_4));
}

TEST_F(CreateDirWorkItemTest, RollbackWithMissingDir) {
  base::FilePath dir_to_create_1(temp_dir_.path());
  dir_to_create_1 = dir_to_create_1.AppendASCII("aaaa");

  base::FilePath dir_to_create_2(dir_to_create_1);
  dir_to_create_2 = dir_to_create_2.AppendASCII("bbbb");

  base::FilePath dir_to_create_3(dir_to_create_2);
  dir_to_create_3 = dir_to_create_3.AppendASCII("cccc");

  scoped_ptr<CreateDirWorkItem> work_item(
      WorkItem::CreateCreateDirWorkItem(dir_to_create_3));

  EXPECT_TRUE(work_item->Do());

  EXPECT_TRUE(file_util::PathExists(dir_to_create_3));

  RemoveDirectory(dir_to_create_3.value().c_str());
  ASSERT_FALSE(file_util::PathExists(dir_to_create_3));

  work_item->Rollback();

  // dir_to_create_3 has already been deleted, Rollback should delete
  // the rest.
  EXPECT_FALSE(file_util::PathExists(dir_to_create_1));
}

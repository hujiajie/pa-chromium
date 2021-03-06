# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from copy import deepcopy

from file_system import FileSystem, StatInfo, FileNotFoundError
from future import Future

class _AsyncFetchFuture(object):
  def __init__(self,
               unpatched_files_future,
               patched_files_future,
               dirs_value,
               patched_file_system):
    self._unpatched_files_future = unpatched_files_future
    self._patched_files_future = patched_files_future
    self._dirs_value  = dirs_value
    self._patched_file_system = patched_file_system

  def Get(self):
    files = self._unpatched_files_future.Get()
    files.update(self._patched_files_future.Get())
    files.update(
        dict((path, self._PatchDirectoryListing(path, self._dirs_value[path]))
             for path in self._dirs_value))
    return files

  def _PatchDirectoryListing(self, path, original_listing):
    added, deleted, modified = (
        self._patched_file_system._GetDirectoryListingFromPatch(path))
    if original_listing is None:
      if len(added) == 0:
        raise FileNotFoundError('Directory %s not found in the patch.' % path)
      return added
    return list((set(original_listing) | set(added)) - set(deleted))

class PatchedFileSystem(FileSystem):
  ''' Class to fetch resources with a patch applied.
  '''
  def __init__(self, host_file_system, patcher):
    self._host_file_system = host_file_system
    self._patcher = patcher

  def Read(self, paths, binary=False):
    patched_files = set()
    added, deleted, modified = self._patcher.GetPatchedFiles()
    if set(paths) & set(deleted):
      raise FileNotFoundError('Files are removed from the patch.')
    patched_files |= (set(added) | set(modified))
    dir_paths = set(path for path in paths if path.endswith('/'))
    file_paths = set(paths) - dir_paths
    patched_paths = file_paths & patched_files
    unpatched_paths = file_paths - patched_files
    return Future(delegate=_AsyncFetchFuture(
        self._host_file_system.Read(unpatched_paths, binary),
        self._patcher.Apply(patched_paths, self._host_file_system, binary),
        self._TryReadDirectory(dir_paths, binary),
        self))

  ''' Given the list of patched files, it's not possible to determine whether
  a directory to read exists in self._host_file_system. So try reading each one
  and handle FileNotFoundError.
  '''
  def _TryReadDirectory(self, paths, binary):
    value = {}
    for path in paths:
      assert path.endswith('/')
      try:
        value[path] = self._host_file_system.ReadSingle(path, binary)
      except FileNotFoundError:
        value[path] = None
    return value

  def _GetDirectoryListingFromPatch(self, path):
    assert path.endswith('/')
    def _FindChildrenInPath(files, path):
      result = []
      for f in files:
        if f.startswith(path):
          child_path = f[len(path):]
          if '/' in child_path:
            child_name = child_path[0:child_path.find('/') + 1]
          else:
            child_name = child_path
          result.append(child_name)
      return result

    added, deleted, modified = (tuple(
        _FindChildrenInPath(files, path)
        for files in self._patcher.GetPatchedFiles()))

    # A patch applies to files only. It cannot delete directories.
    deleted_files = [child for child in deleted if not child.endswith('/')]
    # However, these directories are actually modified because their children
    # are patched.
    modified += [child for child in deleted if child.endswith('/')]

    return (added, deleted_files, modified)

  def _PatchStat(self, stat_info, version, added, deleted, modified):
    assert len(added) + len(deleted) + len(modified) > 0
    assert stat_info.child_versions is not None

    # Deep copy before patching to make sure it doesn't interfere with values
    # cached in memory.
    stat_info = deepcopy(stat_info)

    stat_info.version = version
    for child in added + modified:
      stat_info.child_versions[child] = version
    for child in deleted:
      if stat_info.child_versions.get(child):
        del stat_info.child_versions[child]

    return stat_info

  def Stat(self, path):
    version = self._patcher.GetVersion()
    assert version is not None
    version = 'patched_%s' % version

    directory, filename = path.rsplit('/', 1)
    added, deleted, modified = self._GetDirectoryListingFromPatch(
        directory + '/')

    if len(added) > 0:
      # There are new files added. It's possible (if |directory| is new) that
      # self._host_file_system.Stat will throw an exception.
      try:
        stat_info = self._PatchStat(
            self._host_file_system.Stat(directory + '/'),
            version,
            added,
            deleted,
            modified)
      except FileNotFoundError:
        stat_info = StatInfo(
            version,
            dict((child, version) for child in added + modified))
    elif len(deleted) + len(modified) > 0:
      # No files were added.
      stat_info = self._PatchStat(self._host_file_system.Stat(directory + '/'),
                                  version,
                                  added,
                                  deleted,
                                  modified)
    else:
      # No changes are made in this directory.
      return self._host_file_system.Stat(path)

    if stat_info.child_versions is not None:
      if filename:
        if filename in stat_info.child_versions:
          stat_info = StatInfo(stat_info.child_versions[filename])
        else:
          raise FileNotFoundError('%s was not in child versions' % filename)
    return stat_info

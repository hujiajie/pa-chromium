# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Set of operations/utilities related to checking out the depot, and
outputting annotations on the buildbot waterfall. These are intended to be
used by the bisection scripts."""

import errno
import os
import shutil
import subprocess
import sys


GCLIENT_SPEC = """
solutions = [
  { "name"        : "src",
    "url"         : "https://chromium.googlesource.com/chromium/src.git",
    "deps_file"   : ".DEPS.git",
    "managed"     : True,
    "custom_deps" : {
      "src/data/page_cycler": "https://chrome-internal.googlesource.com/" +
                              "chrome/data/page_cycler/.git",
      "src/data/dom_perf": "https://chrome-internal.googlesource.com/" +
                           "chrome/data/dom_perf/.git",
      "src/tools/perf/data": "https://chrome-internal.googlesource.com/" +
                             "chrome/tools/perf/data/.git",
      "src/v8_bleeding_edge": "git://github.com/v8/v8.git",
    },
    "safesync_url": "",
  },
]
"""
GCLIENT_SPEC = ''.join([l for l in GCLIENT_SPEC.splitlines()])
GCLIENT_SPEC_ANDROID = GCLIENT_SPEC + "\ntarget_os = ['android']"
FILE_DEPS_GIT = '.DEPS.git'

REPO_PARAMS = [
  'https://chrome-internal.googlesource.com/chromeos/manifest-internal/',
  '--repo-url',
  'https://git.chromium.org/external/repo.git'
]

REPO_SYNC_COMMAND = 'git checkout -f $(git rev-list --max-count=1 '\
                    '--before=%d remotes/m/master)'

def OutputAnnotationStepStart(name):
  """Outputs appropriate annotation to signal the start of a step to
  a trybot.

  Args:
    name: The name of the step.
  """
  print
  print '@@@SEED_STEP %s@@@' % name
  print '@@@STEP_CURSOR %s@@@' % name
  print '@@@STEP_STARTED@@@'
  print


def OutputAnnotationStepClosed():
  """Outputs appropriate annotation to signal the closing of a step to
  a trybot."""
  print
  print '@@@STEP_CLOSED@@@'
  print


def CreateAndChangeToSourceDirectory(working_directory):
  """Creates a directory 'bisect' as a subdirectory of 'working_directory'.  If
  the function is successful, the current working directory will change to that
  of the new 'bisect' directory.

  Returns:
    True if the directory was successfully created (or already existed).
  """
  cwd = os.getcwd()
  os.chdir(working_directory)
  try:
    os.mkdir('bisect')
  except OSError, e:
    if e.errno != errno.EEXIST:
      return False
  os.chdir('bisect')
  return True


def SubprocessCall(cmd):
  """Runs a subprocess with specified parameters.

  Args:
    params: A list of parameters to pass to gclient.

  Returns:
    The return code of the call.
  """
  if os.name == 'nt':
    # "HOME" isn't normally defined on windows, but is needed
    # for git to find the user's .netrc file.
    if not os.getenv('HOME'):
      os.environ['HOME'] = os.environ['USERPROFILE']
  shell = os.name == 'nt'
  return subprocess.call(cmd, shell=shell)


def RunGClient(params):
  """Runs gclient with the specified parameters.

  Args:
    params: A list of parameters to pass to gclient.

  Returns:
    The return code of the call.
  """
  cmd = ['gclient'] + params

  return SubprocessCall(cmd)


def RunRepo(params):
  """Runs cros repo command with specified parameters.

  Args:
    params: A list of parameters to pass to gclient.

  Returns:
    The return code of the call.
  """
  cmd = ['repo'] + params

  return SubprocessCall(cmd)


def RunRepoSyncAtTimestamp(timestamp):
  """Syncs all git depots to the timestamp specified using repo forall.

  Args:
    params: Unix timestamp to sync to.

  Returns:
    The return code of the call.
  """
  repo_sync = REPO_SYNC_COMMAND % timestamp
  cmd = ['forall', '-c', REPO_SYNC_COMMAND % timestamp]
  return RunRepo(cmd)


def RunGClientAndCreateConfig(opts):
  """Runs gclient and creates a config containing both src and src-internal.

  Args:
    opts: The options parsed from the command line through parse_args().

  Returns:
    The return code of the call.
  """
  spec = GCLIENT_SPEC
  if opts.target_platform == 'android':
    spec = GCLIENT_SPEC_ANDROID

  return_code = RunGClient(
      ['config', '--spec=%s' % spec, '--git-deps'])
  return return_code


def IsDepsFileBlink():
  """Reads .DEPS.git and returns whether or not we're using blink.

  Returns:
    True if blink, false if webkit.
  """
  locals = {'Var': lambda _: locals["vars"][_],
            'From': lambda *args: None}
  execfile(FILE_DEPS_GIT, {}, locals)
  return 'blink.git' in locals['vars']['webkit_url']


def RemoveThirdPartyWebkitDirectory():
  """Removes third_party/WebKit.

  Returns:
    True on success.
  """
  try:
    path_to_dir = os.path.join(os.getcwd(), 'third_party', 'WebKit')
    if os.path.exists(path_to_dir):
      shutil.rmtree(path_to_dir)
  except OSError, e:
    if e.errno != errno.ENOENT:
      return False
  return True


def RunGClientAndSync(reset):
  """Runs gclient and does a normal sync.

  Args:
    reset: Whether to reset any changes to the depot.

  Returns:
    The return code of the call.
  """
  params = ['sync', '--verbose', '--nohooks']
  if reset:
    params.extend(['--reset', '--force', '--delete_unversioned_trees'])
  return RunGClient(params)


def SetupGitDepot(opts, reset):
  """Sets up the depot for the bisection. The depot will be located in a
  subdirectory called 'bisect'.

  Args:
    opts: The options parsed from the command line through parse_args().
    reset: Whether to reset any changes to the depot.

  Returns:
    True if gclient successfully created the config file and did a sync, False
    otherwise.
  """
  name = 'Setting up Bisection Depot'

  if opts.output_buildbot_annotations:
    OutputAnnotationStepStart(name)

  passed = False

  if not RunGClientAndCreateConfig(opts):
    passed_deps_check = True
    if os.path.isfile(os.path.join('src', FILE_DEPS_GIT)):
      cwd = os.getcwd()
      os.chdir('src')
      if not IsDepsFileBlink():
        passed_deps_check = RemoveThirdPartyWebkitDirectory()
      else:
        passed_deps_check = True
      os.chdir(cwd)

    if passed_deps_check and not RunGClientAndSync(reset):
      passed = True

  if opts.output_buildbot_annotations:
    print
    OutputAnnotationStepClosed()

  return passed


def SetupCrosRepo():
  """Sets up cros repo for bisecting chromeos.

  Returns:
    Returns 0 on success.
  """
  cwd = os.getcwd()
  try:
    os.mkdir('cros')
  except OSError, e:
    if e.errno != errno.EEXIST:
      return False
  os.chdir('cros')

  cmd = ['init', '-u'] + REPO_PARAMS

  passed = False

  if not RunRepo(cmd):
    if not RunRepo(['sync']):
      passed = True
  os.chdir(cwd)

  return passed


def SetupAndroidBuildEnvironment(opts):
  """Sets up the android build environment.

  Args:
    opts: The options parsed from the command line through parse_args().
    path_to_file: Path to the bisect script's directory.

  Returns:
    True if successful.
  """
  path_to_file = os.path.join('build', 'android', 'envsetup.sh')
  proc = subprocess.Popen(['bash', '-c', 'source %s && env' % path_to_file],
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           cwd='src')
  (out, _) = proc.communicate()

  for line in out.splitlines():
    (k, _, v) = line.partition('=')
    os.environ[k] = v
  return not proc.returncode


def SetupPlatformBuildEnvironment(opts):
  """Performs any platform specific setup.

  Args:
    opts: The options parsed from the command line through parse_args().
    path_to_file: Path to the bisect script's directory.

  Returns:
    True if successful.
  """
  if opts.target_platform == 'android':
    return SetupAndroidBuildEnvironment(opts)
  elif opts.target_platform == 'cros':
    return SetupCrosRepo()

  return True


def CreateBisectDirectoryAndSetupDepot(opts, reset=False):
  """Sets up a subdirectory 'bisect' and then retrieves a copy of the depot
  there using gclient.

  Args:
    opts: The options parsed from the command line through parse_args().
    reset: Whether to reset any changes to the depot.

  Returns:
    Returns 0 on success, otherwise 1.
  """
  if not CreateAndChangeToSourceDirectory(opts.working_directory):
    print 'Error: Could not create bisect directory.'
    print
    return 1

  if not SetupGitDepot(opts, reset):
    print 'Error: Failed to grab source.'
    print
    return 1

  return 0

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Called by the common.js module.
function domContentLoaded(name, tc, config, width, height) {
  window.webkitStorageInfo.requestQuota(window.PERSISTENT, 1024*1024,
      function(bytes) {
        common.updateStatus(
            'Allocated '+bytes+' bytes of persistant storage.');
        common.createNaClModule(name, tc, config, width, height);
        common.attachDefaultListeners();
      },
      function(e) { alert('Failed to allocate space') });
}

// Called by the common.js module.
function attachListeners() {
  document.getElementById('saveButton').addEventListener('click', saveFile);
  document.getElementById('loadButton').addEventListener('click', loadFile);
  document.getElementById('deleteButton').addEventListener('click', deleteFile);
  document.getElementById('listButton').addEventListener('click', listDir);
}

function loadFile() {
  if (common.naclModule) {
    var fileName = document.getElementById('fileName').value;

    // Package a message using a simple protocol containing:
    // instruction file_name_length file_name
    var msg = "ld " + fileName.length + " " + fileName;
    common.naclModule.postMessage(msg);
  }
}

function saveFile() {
  if (common.naclModule) {
    var fileName = document.getElementById('fileName').value;
    var fileText = document.getElementById('fileEditor').value;

    // Package a message using a simple protocol containing:
    // instruction file_name_length file_name file_contents
    var msg = "sv " + fileName.length + " " + fileName + " " + fileText;
    common.naclModule.postMessage(msg);
  }
}

function deleteFile() {
  if (common.naclModule) {
    var fileName = document.getElementById('fileName').value;

    // Package a message using a simple protocol containing:
    // instruction file_name_length file_name
    var msg = "de " + fileName.length + " " + fileName;
    common.naclModule.postMessage(msg);
  }
}

function listDir() {
  if (common.naclModule) {
    var dirName = document.getElementById('dirName').value;

    // Package a message using a simple protocol containing:
    // instruction file_name_length file_name
    var msg = "ls " + dirName.length + " " + dirName;
    common.naclModule.postMessage(msg);
  }
}

// Called by the common.js module.
function handleMessage(message_event) {
  var messageParts = message_event.data.split("|", 3);

  if (messageParts[0] == "ERR") {
    common.updateStatus(messageParts[1]);
    document.getElementById('statusField').style.color = "red";
  }
  else if(messageParts[0] == "STAT") {
    common.updateStatus(messageParts[1]);
  }
  else if (messageParts[0] == "DISP") {
    // Display the message in the file edit box
    document.getElementById('fileEditor').value = messageParts[1];
  }
  else if (messageParts[0] == "READY") {
    var statusField = document.getElementById('statusField');
    common.updateStatus(statusField.innerHTML + ' Ready!');
  }
}

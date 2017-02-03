// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_TYPES_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_TYPES_H_

#include <ostream>
#include <string>

#include "apps/ledger/src/convert/convert.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

using PageId = std::string;
using PageIdView = convert::ExtendedStringView;
using CommitId = std::string;
using CommitIdView = convert::ExtendedStringView;
using ObjectId = std::string;
using ObjectIdView = convert::ExtendedStringView;
using JournalId = std::string;
using JournalIdView = convert::ExtendedStringView;

// The priority at which the key value is downloaded, and the cache policy.
enum class KeyPriority {
  EAGER,
  LAZY,
};

// An entry in a commit.
struct Entry {
  std::string key;
  ObjectId object_id;
  KeyPriority priority;
};

bool operator==(const Entry& lhs, const Entry& rhs);
bool operator!=(const Entry& lhs, const Entry& rhs);

// A change between two commit contents.
struct EntryChange {
  Entry entry;
  bool deleted;
};

bool operator==(const EntryChange& lhs, const EntryChange& rhs);
bool operator!=(const EntryChange& lhs, const EntryChange& rhs);

enum class ChangeSource { LOCAL, SYNC };

enum class JournalType { IMPLICIT, EXPLICIT };

enum class Status {
  // User visible status.
  OK,
  IO_ERROR,
  NOT_FOUND,

  // Internal status.
  NO_SUCH_CHILD,
  FORMAT_ERROR,
  ILLEGAL_STATE,
  INTERNAL_IO_ERROR,
  OBJECT_ID_MISMATCH,
  NOT_CONNECTED_ERROR,

  // Temporary status.
  NOT_IMPLEMENTED,
};

ftl::StringView StatusToString(Status status);
std::ostream& operator<<(std::ostream& os, Status status);

}  // namespace storage
#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_TYPES_H_

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_
#define APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/app/fidl/bound_interface.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"
#include "apps/ledger/src/app/page_delegate.h"
#include "apps/ledger/src/app/page_snapshot_impl.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/cloud_sync/public/ledger_sync.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/closure.h"

namespace ledger {
// Manages a ledger page.
//
// PageManager owns all page-level objects related to a single page: page
// storage, and a set of FIDL PageImpls backed by the page storage. It is safe
// to delete it at any point - this closes all channels, deletes PageImpls and
// tears down the storage.
//
// When the set of PageImpls becomes empty, client is notified through
// |on_empty_callback|.
class PageManager {
 public:
  // Both |page_storage| and |page_sync| are owned by PageManager and are
  // deleted when it goes away.
  PageManager(coroutine::CoroutineService* coroutine_service,
              std::unique_ptr<storage::PageStorage> page_storage,
              std::unique_ptr<cloud_sync::PageSyncContext> page_sync,
              std::unique_ptr<MergeResolver> merge_resolver);
  ~PageManager();

  // Creates a new PageImpl managed by this PageManager, and binds it to the
  // request.
  void BindPage(fidl::InterfaceRequest<Page> page_request);

  // Creates a new PageSnapshotImpl managed by this PageManager, and binds it to
  // the request.
  void BindPageSnapshot(std::unique_ptr<const storage::Commit> commit,
                        fidl::InterfaceRequest<PageSnapshot> snapshot_request);

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  void CheckEmpty();
  void OnSyncBacklogDownloaded();

  coroutine::CoroutineService* coroutine_service_;
  std::unique_ptr<storage::PageStorage> page_storage_;
  std::unique_ptr<cloud_sync::PageSyncContext> page_sync_context_;
  std::unique_ptr<MergeResolver> merge_resolver_;
  callback::AutoCleanableSet<BoundInterface<PageSnapshot, PageSnapshotImpl>>
      snapshots_;
  callback::AutoCleanableSet<PageDelegate> pages_;
  ftl::Closure on_empty_callback_;

  bool sync_backlog_downloaded_;
  std::vector<fidl::InterfaceRequest<Page>> page_requests_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_

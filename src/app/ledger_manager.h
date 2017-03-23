// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_
#define APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include "apps/ledger/src/app/ledger_impl.h"
#include "apps/ledger/src/app/merging/ledger_merge_manager.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace ledger {

// Manages a ledger instance. Ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a Mojo
// LedgerImpl. It is safe to delete it at any point - this closes all channels,
// deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate {
 public:
  LedgerManager(coroutine::CoroutineService* coroutine_service,
                std::unique_ptr<storage::LedgerStorage> storage,
                std::unique_ptr<cloud_sync::LedgerSync> sync);
  ~LedgerManager();

  // Creates a new proxy for the LedgerImpl managed by this LedgerManager.
  void BindLedger(fidl::InterfaceRequest<Ledger> ledger_request);

  // LedgerImpl::Delegate:
  void CreatePage(fidl::InterfaceRequest<Page> page_request,
                  std::function<void(Status)> callback) override;
  void GetPage(convert::ExtendedStringView page_id,
               CreateIfNotFound create_if_not_found,
               fidl::InterfaceRequest<Page> page_request,
               std::function<void(Status)> callback) override;
  Status DeletePage(convert::ExtendedStringView page_id) override;
  void SetConflictResolverFactory(
      fidl::InterfaceHandle<ConflictResolverFactory> factory) override;

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  class PageManagerContainer;

  // Handles a request to retrieve a page, making a decision about whether the
  // page should be created locally based on the response from a query to the
  // cloud.
  void HandleGetPage(storage::PageId page_id,
                     cloud_sync::RemoteResponse remote_response,
                     CreateIfNotFound create_if_not_found,
                     PageManagerContainer* container);

  // Adds a new PageManagerContainer for |page_id| and configures its so that we
  // delete it from |page_managers_| automatically when the last local client
  // disconnects from the page. Returns the container.
  PageManagerContainer* AddPageManagerContainer(storage::PageIdView page_id);
  // Create a new page manager for the given storage.
  std::unique_ptr<PageManager> NewPageManager(
      std::unique_ptr<storage::PageStorage> page_storage);

  void CheckEmpty();

  coroutine::CoroutineService* coroutine_service_;
  std::unique_ptr<storage::LedgerStorage> storage_;
  std::unique_ptr<cloud_sync::LedgerSync> sync_;
  LedgerImpl ledger_impl_;
  // merge_manager_ must be destructed after page_managers_ to ensure it
  // outlives any page-specific merge resolver.
  LedgerMergeManager merge_manager_;
  fidl::BindingSet<Ledger> bindings_;

  // Mapping from page id to the manager of that page.
  callback::AutoCleanableMap<storage::PageId,
                             PageManagerContainer,
                             convert::StringViewComparator>
      page_managers_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_

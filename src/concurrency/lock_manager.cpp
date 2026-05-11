#include "onebase/concurrency/lock_manager.h"
#include "onebase/common/exception.h"

namespace onebase {

namespace {

auto CanAcquireMoreLocks(Transaction *txn) -> bool {
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  return true;
}

}  // namespace

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  if (!CanAcquireMoreLocks(txn)) {
    return false;
  }
  if (txn->IsSharedLocked(rid) || txn->IsExclusiveLocked(rid)) {
    return true;
  }

  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::SHARED);
  auto request_it = std::prev(queue.request_queue_.end());

  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &request : queue.request_queue_) {
      if (request.granted_ && request.lock_mode_ == LockMode::EXCLUSIVE) {
        return false;
      }
    }
    return true;
  });

  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(request_it);
    queue.cv_.notify_all();
    return false;
  }

  request_it->granted_ = true;
  txn->GetSharedLockSet()->insert(rid);
  return true;
}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  if (!CanAcquireMoreLocks(txn)) {
    return false;
  }
  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }
  if (txn->IsSharedLocked(rid)) {
    lock.unlock();
    return LockUpgrade(txn, rid);
  }

  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::EXCLUSIVE);
  auto request_it = std::prev(queue.request_queue_.end());

  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &request : queue.request_queue_) {
      if (request.granted_ && request.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });

  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(request_it);
    queue.cv_.notify_all();
    return false;
  }

  request_it->granted_ = true;
  txn->GetExclusiveLockSet()->insert(rid);
  return true;
}


auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  if (!CanAcquireMoreLocks(txn)) {
    return false;
  }
  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }
  if (!txn->IsSharedLocked(rid)) {
    lock.unlock();
    return LockExclusive(txn, rid);
  }

  auto &queue = lock_table_[rid];
  if (queue.upgrading_) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  queue.upgrading_ = true;

  auto request_it = queue.request_queue_.end();
  for (auto it = queue.request_queue_.begin(); it != queue.request_queue_.end(); ++it) {
    if (it->txn_id_ == txn->GetTransactionId()) {
      request_it = it;
      break;
    }
  }

  if (request_it == queue.request_queue_.end()) {
    queue.upgrading_ = false;
    return false;
  }

  request_it->lock_mode_ = LockMode::EXCLUSIVE;

  queue.cv_.wait(lock, [&]() {
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }
    for (const auto &request : queue.request_queue_) {
      if (request.granted_ && request.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });

  queue.upgrading_ = false;
  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(request_it);
    queue.cv_.notify_all();
    return false;
  }

  request_it->granted_ = true;
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->insert(rid);
  queue.cv_.notify_all();
  return true;
}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  auto table_it = lock_table_.find(rid);
  if (table_it == lock_table_.end()) {
    return false;
  }

  auto &queue = table_it->second;
  bool removed = false;
  for (auto it = queue.request_queue_.begin(); it != queue.request_queue_.end(); ++it) {
    if (it->txn_id_ == txn->GetTransactionId()) {
      queue.request_queue_.erase(it);
      removed = true;
      break;
    }
  }

  if (!removed) {
    return false;
  }

  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->erase(rid);
  if (txn->GetState() == TransactionState::GROWING) {
    txn->SetState(TransactionState::SHRINKING);
  }

  if (queue.request_queue_.empty()) {
    lock_table_.erase(table_it);
  } else {
    queue.cv_.notify_all();
  }
  return true;
}

}  // namespace onebase

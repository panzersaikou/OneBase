#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

// Owns the in-memory page frames. It maps page ids to frames, loads pages from
// disk on demand, writes dirty victims back, and asks LRUKReplacer for a victim
// when the free list is empty.

  BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
      : pool_size_(pool_size), disk_manager_(disk_manager) {
    pages_ = new Page[pool_size_];
    replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
    for (size_t i = 0; i < pool_size_; ++i) {
      free_list_.emplace_back(static_cast<frame_id_t>(i));
    }
  }

  BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

  auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
    std::scoped_lock lock(latch_);

    frame_id_t frame_id;
    if (!free_list_.empty()) {
      frame_id = free_list_.front();
      free_list_.pop_front();
    } else if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    } else {
      auto &victim = pages_[frame_id];
      if (victim.is_dirty_) {
        disk_manager_->WritePage(victim.page_id_, victim.data_);
      }

      page_table_.erase(victim.page_id_);
    }

    *page_id = disk_manager_->AllocatePage();
    auto &page = pages_[frame_id];
    page.ResetMemory();
    page.page_id_ = *page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = false;

    page_table_[*page_id] = frame_id;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return &page;
  }

  auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
    std::scoped_lock lock(latch_);

    if (auto it = page_table_.find(page_id); it != page_table_.end()) {
      auto frame_id = it->second;
      auto &page = pages_[frame_id];
      page.pin_count_++;
      replacer_->RecordAccess(frame_id);
      replacer_->SetEvictable(frame_id, false);
      return &page;
    }

    frame_id_t frame_id;
    if (!free_list_.empty()) {
      frame_id = free_list_.front();
      free_list_.pop_front();
    } else if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    } else {
      auto &victim = pages_[frame_id];
      if (victim.is_dirty_) {
        disk_manager_->WritePage(victim.page_id_, victim.data_);
      }
      page_table_.erase(victim.page_id_);
    }

    auto &page = pages_[frame_id];
    page.ResetMemory();
    disk_manager_->ReadPage(page_id, page.data_);
    page.page_id_ = page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = false;

    page_table_[page_id] = frame_id;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return &page;
  }

  auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
    std::scoped_lock lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
      return false;
    }

    auto frame_id = it->second;
    auto &page = pages_[frame_id];
    if (page.pin_count_ <= 0) {
      return false;
    }

    page.is_dirty_ = page.is_dirty_ || is_dirty;
    page.pin_count_--;
    if (page.pin_count_ == 0) {
      replacer_->SetEvictable(frame_id, true);
    }
    return true;
  }

  auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
    std::scoped_lock lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
      disk_manager_->DeallocatePage(page_id);
      return true;
    }

    auto frame_id = it->second;
    auto &page = pages_[frame_id];
    if (page.pin_count_ > 0) {
      return false;
    }

    replacer_->Remove(frame_id);
    page_table_.erase(it);
    page.ResetMemory();
    page.page_id_ = INVALID_PAGE_ID;
    page.pin_count_ = 0;
    page.is_dirty_ = false;
    free_list_.push_back(frame_id);
    disk_manager_->DeallocatePage(page_id);
    return true;
  }

  auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
    std::scoped_lock lock(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
      return false;
    }

    auto &page = pages_[it->second];
    disk_manager_->WritePage(page_id, page.data_);
    page.is_dirty_ = false;
    return true;
  }

  void BufferPoolManager::FlushAllPages() {
    std::scoped_lock lock(latch_);
    for (const auto &[page_id, frame_id] : page_table_) {
      auto &page = pages_[frame_id];
      disk_manager_->WritePage(page_id, page.data_);
      page.is_dirty_ = false;
    }
  }

}  // set namespace onebase

#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"
#include <limits>

namespace onebase {

// Tracks recent frame accesses and chooses an evictable frame using the LRU-K
// policy. BufferPoolManager marks a frame evictable only after its pin count
// drops to zero.

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock lock(latch_);

  bool found_inf = false;
  bool found_finite = false;
  frame_id_t victim_inf = INVALID_FRAME_ID;
  frame_id_t victim_finite = INVALID_FRAME_ID;
  size_t earliest_first_access = std::numeric_limits<size_t>::max();
  size_t largest_k_distance = 0;

  for (const auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }

    const auto first_access = entry.history_.empty() ? 0 : entry.history_.front();
    if (entry.history_.size() < k_) {
      if (!found_inf || first_access < earliest_first_access) {
        found_inf = true;
        earliest_first_access = first_access;
        victim_inf = fid;
      }
      continue;
    }

    const auto k_distance = current_timestamp_ - entry.history_.front();
    if (!found_finite || k_distance > largest_k_distance) {
      found_finite = true;
      largest_k_distance = k_distance;
      victim_finite = fid;
    }
  }

  const auto victim = found_inf ? victim_inf : victim_finite;
  if (victim == INVALID_FRAME_ID) {
    return false;
  }

  entries_.erase(victim);
  curr_size_--;
  *frame_id = victim;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  if (frame_id < 0 || static_cast<size_t>(frame_id) >= max_frames_) {
    throw OneBaseException("frame id out of range", ExceptionType::OUT_OF_RANGE);
  }

  std::scoped_lock lock(latch_);
  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_++);
  while (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  auto &entry = it->second;
  if (entry.is_evictable_ == set_evictable) {
    return;
  }

  curr_size_ += set_evictable ? 1 : -1;
  entry.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_) {
    throw OneBaseException("cannot remove a non-evictable frame", ExceptionType::INVALID);
  }

  entries_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() const -> size_t {
  std::scoped_lock lock(latch_);
  return curr_size_;
}

}  // namespace onebase

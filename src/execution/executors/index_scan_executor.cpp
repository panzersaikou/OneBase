#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  index_info_ = GetExecutorContext()->GetCatalog()->GetIndex(plan_->GetIndexOid());
  matching_rids_.clear();
  cursor_ = 0;

  if (index_info_ == nullptr || !index_info_->SupportsPointLookup()) {
    return;
  }

  Tuple dummy;
  auto lookup_value = plan_->GetLookupKey()->Evaluate(&dummy, &table_info_->schema_);
  auto *rids = index_info_->LookupInteger(lookup_value.GetAsInteger());
  if (rids != nullptr) {
    matching_rids_ = *rids;
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (cursor_ < matching_rids_.size()) {
    RID current_rid = matching_rids_[cursor_++];
    Tuple current = table_info_->table_->GetTuple(current_rid);
    const auto &predicate = plan_->GetPredicate();
    if (predicate != nullptr &&
        !predicate->Evaluate(&current, &table_info_->schema_).GetAsBoolean()) {
      continue;
    }
    std::vector<Value> values;
    values.reserve(table_info_->schema_.GetColumnCount());
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); ++i) {
      values.push_back(current.GetValue(&table_info_->schema_, i));
    }
    *tuple = Tuple(std::move(values));
    tuple->SetRID(current_rid);
    if (rid != nullptr) {
      *rid = current_rid;
    }
    return true;
  }
  return false;
}

}  // namespace onebase

#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (iter_ != end_) {
    Tuple current = *iter_;
    RID current_rid = iter_.GetRID();
    ++iter_;

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

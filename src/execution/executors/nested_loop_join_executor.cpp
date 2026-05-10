#include "onebase/execution/executors/nested_loop_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx,
                                                const NestedLoopJoinPlanNode *plan,
                                                std::unique_ptr<AbstractExecutor> left_executor,
                                                std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void NestedLoopJoinExecutor::Init() {
  result_tuples_.clear();
  cursor_ = 0;

  left_executor_->Init();
  Tuple left_tuple;
  RID left_rid;
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    right_executor_->Init();
    Tuple right_tuple;
    RID right_rid;
    while (right_executor_->Next(&right_tuple, &right_rid)) {
      const auto &predicate = plan_->GetPredicate();
      if (predicate != nullptr &&
          !predicate->EvaluateJoin(&left_tuple, &left_executor_->GetOutputSchema(),
                                   &right_tuple, &right_executor_->GetOutputSchema()).GetAsBoolean()) {
        continue;
      }

      std::vector<Value> values = left_tuple.GetValues();
      const auto &right_values = right_tuple.GetValues();
      values.insert(values.end(), right_values.begin(), right_values.end());
      result_tuples_.emplace_back(std::move(values));
    }
  }
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase

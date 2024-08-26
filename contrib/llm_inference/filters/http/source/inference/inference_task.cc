#include "contrib/llm_inference/filters/http/source/inference/inference_task.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

InferenceTaskMetaData::InferenceTaskMetaData(const std::string& data,bool infill, int id, InferenceTaskType type, int id_target):
  data(data), type(type),infill(infill),id(id), id_target(id_target) {}

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
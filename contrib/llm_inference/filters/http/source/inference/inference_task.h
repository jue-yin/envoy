#pragma once

#include <string>
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

struct ModelParameter {
  int n_threads = 32;
  int n_parallel = 1;
};

struct ModelChosen {
  std::string model_name;
  int first_byte_timeout = 10;
  int inference_timeout = 90;
};

// https://community.openai.com/t/openai-chat-list-of-error-codes-and-types/357791/11
enum error_type {
    ERROR_TYPE_INVALID_REQUEST,
    ERROR_TYPE_AUTHENTICATION,
    ERROR_TYPE_SERVER,
    ERROR_TYPE_NOT_FOUND,
    ERROR_TYPE_PERMISSION,
    ERROR_TYPE_UNAVAILABLE, // custom error
    ERROR_TYPE_NOT_SUPPORTED, // custom error
    NO_ERROR,
};

enum InferenceTaskType {
  InferencetasktypeTypeCompletion,
  InferencetasktypeTypeEmbeedings,
  InferencetasktypeTypeCancel,
};

struct InferenceTaskMetaData {
  InferenceTaskMetaData(const std::string&,bool,int, InferenceTaskType,int);
  std::string data;
  InferenceTaskType type;
  bool infill    = false;
  bool embedding = false;
  int id        = -1; 
  int id_target = -1;
};

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
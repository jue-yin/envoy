# Filter 配置使用说明

## 概述

本项目实现了一个 HTTP Filter，该`filter`会解析推理请求，并调用异步推理线程实现推理过程，同时给该异步线程一个回调函数，实现流式传输的大模型推理过程。此文档将指导您如何配置和使用 `filter`，以及在性能方面与 Ollama 进行对比。

## 配置使用方式

### 配置 Filter

1、在配置文件中，您需要首先设置filter级的配置，例如：

```json
- name: envoy.filters.http.llm_inference
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.llm_inference.v3.modelParameter
    n_threads : 100
    n_parallel : 5
    chat_modelpath: {
      "qwen2": "/home/yuanjq/model/qwen2-7b-instruct-q5_k_m.gguf",
      "llama3": "/home/yuanjq/model/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
    }
    embedding_modelpath: {
      "bge": "/home/yuanjq/model/bge-small-zh-v1.5-f32.gguf"
    }
```
其中
n_threads: 表示推理线程能用的最大线程数
n_parallel: 表示推理服务的最大并行请求数
chat_modelpath: 表示chat模型本地路径
embedding_modelpath: 表示embedding模型本地路径

2、在route_config中明确您对router级配置，即需要路由使用到的模型，例如：
```
route_config:
  name: route
  virtual_hosts:
  - name: llm_inference_service
    domains: ["api.openai.com"]
    routes:
    - match:
        prefix: "/v1/chat/completions"
      typed_per_filter_config:
        envoy.filters.http.llm_inference:
          "@type": type.googleapis.com/envoy.extensions.filters.http.llm_inference.v3.modelChosen
          usemodel: "qwen2"
          first_byte_timeout : 4
          inference_timeout : 90
      direct_response:
        status: 504
        body:
          inline_string: "inference timeout"
```
其中
usemodel: 表示使用的模型，模型名字与modelpath里面设置的要对应
first_byte_timeout: 表示首字节超时时间
inference_timeout: 表示总推理超时时间

### 更新 Filter
本项目可以动态地加载和卸载使用模型，您只需添加或删除chat_modelpath、embedding_modelpath里面的模型文件路径，再更新配置文件，即可动态地加载和卸载模型。需要注意的是，卸载了模型之后要确保router级配置里面使用的模型没有被卸载。


## 使用注意事项

1. **参数设置**：请根据具体场景调整 `n_threads` 、`n_parallel`的参数，以确保最佳性能。
2. **模型选用**：确保模型在本地中的路径是正确的，否则加载模型的时候会报错；同时需要用户区分该模型是否是embedding模型。
3. **并发处理**：确保服务器具有足够的内存和cpu资源，因为一般模型都有几个GB，同时模型推理是一个计算密集型任务，它需要在大量的数据上进行矩阵运算和张量操作。

## 性能对比与测试

为了评估 `filter` 的性能，现与 Ollama 进行以下对比：

### 1. 相同模型与问题

确保在相同模型和问题的条件下进行测试，使用以下步骤：

- **模型选择**：选择相同的预训练模型。
  这里我们使用alibaba的**qwen2.5-7b-instruct-q3_k_m.gguf**模型
- **输入问题**：使用相同的输入数据进行推理。
  这里我们相同的请求，要求最多生成500个词:
```
curl http://localhost:10000/v1/chat/completions \
  -H "host:api.openai.com" \
  -d '{
    "model": "qwen2.5",
    "messages": [
      {
        "role": "system",
        "content": "You are a helpful assistant."
      },
      {
        "role": "user",
        "content": "Hello! Building a website can be done in 10 simple steps:"
      }
    ],
    "stream": true,
    "n_predict": 500
  }'

```
### 2. 并发测试

在不同的并发级别下（如 1、4、8 个请求）进行性能测试，并记录以下指标：

- **资源开销**：内存使用情况。
- **响应延迟**：每个请求的响应时间。
- **推理延迟**：每个请求的推理时间。

其中，4、8个请求的时候，我们把内存使用、延迟时间求平均值作为指标
### 3. cpu核数设置与数据记录
- cpu使用8核，即n_threads = 8
- 使用性能监控工具(htop)记录资源使用情况。
- 记录时间并进行对比分析。

### 4. 对比结果
- **内存资源开销**

并发请求数     |  项目     | Ollama
-------- |-------- | -----
1  | 7.1GB | 7.1GB
4  | 7.2GB| 7.2GB
8  | 7.2GB| 7.2GB

- **响应延迟**

并发请求数     |  项目     | Ollama
-------- |-------- | -----
1  | 2633.20 ms /    34 tokens |  1336.57 ms / 15 tokens
4  | 2873.74 ms / 34 tokens | 2196.26 ms / 15 tokens
8  | 2969.98 ms / 34 tokens | 2077.51 ms / 15 tokens

- **推理延迟**

并发请求数     |  项目     | Ollama
-------- |-------- | -----
1  | 55543.16 ms |  62373.26 ms
4  | 169539.01 ms| 231860.54ms
8  | 316113.34 ms | 477764.59 ms


## 结论



通过上述方法，您可以有效地配置和使用 `filter`，并与 Ollama 在性能上进行对比。欢迎提交反馈和建议，以帮助我们持续改进项目。


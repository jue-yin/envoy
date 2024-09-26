for i in {1..9}
do
    curl -s http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer no-key" \
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
      }' > /dev/null &
done
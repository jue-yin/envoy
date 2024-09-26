for i in {1..9}
do
    curl -s http://localhost:10000/v1/chat/completions \
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
      }' > /dev/null &
done
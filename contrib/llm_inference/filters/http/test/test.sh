free -m | awk '{print "                   " ,$0}' | grep -i total > ./mem_result.txt
while true
do
    curl -s http://localhost:10000/v1/chat/completions \
      -H "host:api.openai.com" \
      -d '{
        "model": "qwen2",
        "messages": [
          {
            "role": "system",
            "content": "You are a helpful assistant."
          },
          {
            "role": "user",
            "content": "Can you introduce USA?"
          }
        ],
        "stream": true
      }' > /dev/null &
    # 获取当前日期和时间
    current_date=$(date '+%Y-%m-%d %H:%M:%S')
    
    # 使用free命令获取内存信息，并将日期和时间附加到每行
    free -m | awk -v date="$current_date" 'NR>1{print date, $0}' | grep -i mem >> ./mem_result.txt
    sleep 30
done
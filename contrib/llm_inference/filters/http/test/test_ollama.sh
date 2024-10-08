for i in {1..8}
do
curl -s localhost:11434/api/generate -d '{
"model":"qwen2.5","options":{"num_thread":8,"num_predict":500},"prompt":"Hello! Building a website can be done in 10 simple steps:","stream":true}' > /dev/null &

done
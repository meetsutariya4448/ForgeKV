# Bounded Demo

Build and start a disposable server:

```sh
cmake -S . -B build
cmake --build build
./build/forgekv-server --host 127.0.0.1 --port 7391 --data ./demo-data \
  --durability always --segment-max-bytes 1048576 --compaction-min-segments 4
```

In another terminal:

```sh
./build/forgekv-cli 127.0.0.1 7391 PING
./build/forgekv-cli 127.0.0.1 7391 PUT greeting hello
./build/forgekv-cli 127.0.0.1 7391 GET greeting
./build/forgekv-cli 127.0.0.1 7391 PUTEX session 2000 token
./build/forgekv-cli 127.0.0.1 7391 TTL session
./build/forgekv-cli 127.0.0.1 7391 STATS
./build/forgekv-bench network --host 127.0.0.1 --port 7391 --connections 4 \
  --threads 4 --requests 5000 --read-ratio 0.8 --key-count 100 --value-size 128 \
  --pipeline-depth 4 --output-prefix bench/raw/demo
```

Stop with Ctrl-C, restart the same command, and GET `greeting` to demonstrate replay. The demo does
not exercise the in-process cluster library; cluster and replication behavior is covered by focused
tests and documented separately.

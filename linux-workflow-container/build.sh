#!/usr/bin/env bash

BASE_DIR="$(pwd)"

docker build -t registry.cn-beijing.aliyuncs.com/mengli/obs-node-linux-workflow-container:latest -f "$BASE_DIR/linux-workflow-container/Dockerfile" "$BASE_DIR/linux-workflow-container"
docker push registry.cn-beijing.aliyuncs.com/mengli/obs-node-linux-workflow-container:latest
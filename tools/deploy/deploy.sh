#!/usr/bin/env bash
# ------------------------------------------------------------
# Deploy current project to a target device via rsync/ssh.
# When executed inside impl/<model_name>/, it only syncs THAT
# model directory under impl/, but syncs everything else at
# project root level.
# ------------------------------------------------------------
set -euo pipefail


source $PROJECT_ROOT/tools/color.sh
if [ $# -gt 0 ];then
  TGT=$1
  PORT=$3
else
  echo_default "Use default target Device(in env_setup.sh)..."
  echo_yellow "To use a new temp Device, set like eg: deploy.sh admin@192.168.11.55:/MyPath [-p 60000]"

  IP=$DEVICE_IP
  PORT=22
  USNAME=$DEVICE_USERNAME
  DEPLOY_DIR=$DEVICE_DEPLOY_DIR
  TGT=$USNAME@$IP:$DEPLOY_DIR
fi



usage() {
  echo "Usage: $0 [user@host:/abs/remote/path] [-p PORT]"
  exit 1
}

# ---------- parse cli ----------
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port) PORT=$2; shift 2 ;;
    -h|--help) usage ;;
    *)         TGT=$1; shift ;;
  esac
done

# ---------- env fallback ----------
if [[ -z ${PROJECT_ROOT:-} ]]; then
  echo "❌ PROJECT_ROOT is not set; make sure you sourced env_setup.sh."
  exit 1
fi

if [[ -z $TGT ]]; then
  echo "ℹ️  No target specified, falling back to env_setup.sh values."
  TGT="${DEVICE_USERNAME}@${DEVICE_IP}:${DEVICE_DEPLOY_DIR}"
fi

# ---------- path info ----------
current_dir=$(pwd)
rel_path="${current_dir#"$PROJECT_ROOT"/}"         # e.g. impl/yolov8s_person
PROJECT_NAME=$(basename "$PROJECT_ROOT")

echo "🚀 deploying [$PROJECT_ROOT] → $TGT  (port $PORT)"

# ---------- build rsync filter list ----------
FILTERS=()

#######################
# 1) 先写全局排除规则  #
#######################
FILTERS+=(--exclude='*.o')            # 当前层
FILTERS+=(--exclude='**/*.o')         # 所有子层
FILTERS+=(--exclude='/build/**')      # 整个 build 目录可选排掉
FILTERS+=(--exclude='*.pyc')
FILTERS+=(--exclude='__pycache__/')

###############################
# 2) 默认允许遍历根下一切目录 #
###############################
FILTERS+=(--include='/*')

###############################
# 3) 只同步本 impl/<model_dir> #
###############################
if [[ $rel_path == impl/* ]]; then
  model_dir=$(echo "$rel_path" | cut -d/ -f2)

  FILTERS+=(--include="/impl/")                 # 让 rsync 进 impl/
  FILTERS+=(--include="/impl/${model_dir}/")    # 进模型目录
  FILTERS+=(--include="/impl/${model_dir}/***") # 递归包含文件
  FILTERS+=(--exclude="/impl/*")                # 其他模型全部排掉
fi




# ---------- rsync ----------
rsync -av -e "ssh -p $PORT" \
      "${FILTERS[@]}" \
      "$PROJECT_ROOT/"  "$TGT/$PROJECT_NAME"

if [[ $? -eq 0 ]]; then
  echo_green "✅ Deploy succeeded."
else
  echo_red "❌ Deploy failed."
  exit 1
fi

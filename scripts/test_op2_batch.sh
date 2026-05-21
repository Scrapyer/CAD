#!/usr/bin/env zsh
set -euo pipefail

# 批量测试 OP2 文件解析
# 用法: ./scripts/test_op2_batch.sh [op2目录] [超时秒数]

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"

OP2_DIR="${1:-/Volumes/Lexar/models/pyNastran-main/models}"
TIMEOUT="${2:-10}"
BUILD_DIR="${BUILD_DIR:-build-qt6}"
VIEWER="${ROOT_DIR}/${BUILD_DIR}/FEModelViewer.app/Contents/MacOS/FEModelViewer"
LOGFILE="${ROOT_DIR}/op2_test_results.log"

if [[ ! -x "${VIEWER}" ]]; then
    echo "错误: 找不到 ${VIEWER}"
    echo "请先运行: ./scripts/build_qt6_macos.sh"
    exit 1
fi

echo "=== OP2 批量测试 ===" > "${LOGFILE}"
echo "时间: $(date)" >> "${LOGFILE}"
echo "目录: ${OP2_DIR}" >> "${LOGFILE}"
echo "" >> "${LOGFILE}"

TOTAL=0
GEOM_OK=0
RESULT_OK=0
CRASH=0
NO_GEOM=0
NO_RESULT=0

FILES=("${(@f)$(find "${OP2_DIR}" -name "*.op2" -not -name "._*" -type f 2>/dev/null | sort)}")
FILE_COUNT=${#FILES[@]}

for f in "${FILES[@]}"; do
    TOTAL=$((TOTAL + 1))
    REL="${f#${OP2_DIR}/}"

    TMPOUT=$(mktemp)
    "${VIEWER}" --parse "${f}" > "${TMPOUT}" 2>&1 &
    PID=$!
    (sleep "${TIMEOUT}" && kill "${PID}" 2>/dev/null) &
    WDOG=$!
    set +e
    wait "${PID}" 2>/dev/null
    EXIT_CODE=$?
    set -e
    kill "${WDOG}" 2>/dev/null 2>&1 || true
    wait "${WDOG}" 2>/dev/null 2>&1 || true
    OUTPUT=$(<"${TMPOUT}")
    rm -f "${TMPOUT}"

    RESULT_LINE=$(echo "${OUTPUT}" | grep "^RESULT:" | tail -1 || true)
    GEOM_STATUS=$(echo "${RESULT_LINE}" | grep -o 'geom=[A-Z]*' | cut -d= -f2 || true)
    RESULT_STATUS=$(echo "${RESULT_LINE}" | grep -o 'results=[A-Z]*' | cut -d= -f2 || true)
    NODE_COUNT=$(echo "${RESULT_LINE}" | grep -o 'nodes=[0-9]*' | cut -d= -f2 || true)
    ELEM_COUNT=$(echo "${RESULT_LINE}" | grep -o 'elems=[0-9]*' | cut -d= -f2 || true)
    SUBCASE_COUNT=$(echo "${RESULT_LINE}" | grep -o 'subcases=[0-9]*' | cut -d= -f2 || true)
    SKIP_INFO=$(echo "${OUTPUT}" | grep "skipping OES" | sort -u | head -3 || true)
    CRASH_MSG=$(echo "${OUTPUT}" | grep -i 'terminat\|abort\|segfault\|overflow\|exception' | head -1 || true)
    NODE_COUNT="${NODE_COUNT:-0}"
    ELEM_COUNT="${ELEM_COUNT:-0}"
    SUBCASE_COUNT="${SUBCASE_COUNT:-0}"

    STATUS=""
    if [[ ${EXIT_CODE} -gt 128 || -n "${CRASH_MSG}" ]]; then
        STATUS="CRASH"
        CRASH=$((CRASH + 1))
    elif [[ "${GEOM_STATUS}" == "OK" ]]; then
        GEOM_OK=$((GEOM_OK + 1))
        if [[ "${RESULT_STATUS}" == "OK" && "${SUBCASE_COUNT}" -gt 0 ]]; then
            RESULT_OK=$((RESULT_OK + 1))
            STATUS="OK(g+r:${NODE_COUNT}n/${ELEM_COUNT}e/${SUBCASE_COUNT}sc)"
        else
            STATUS="GEOM(${NODE_COUNT}n/${ELEM_COUNT}e)"
            NO_RESULT=$((NO_RESULT + 1))
        fi
    else
        STATUS="NO_GEOM"
        NO_GEOM=$((NO_GEOM + 1))
    fi

    echo "[${STATUS}] ${REL}" >> "${LOGFILE}"
    if [[ -n "${SKIP_INFO}" ]]; then
        echo "${SKIP_INFO}" | sed 's/^/  SKIP: /' >> "${LOGFILE}"
    fi
    if [[ -n "${CRASH_MSG}" ]]; then
        echo "  CRASH: ${CRASH_MSG}" >> "${LOGFILE}"
    fi

    printf "\r[%d/%d] %-40s %-50s" "${TOTAL}" "${FILE_COUNT}" "${STATUS}" "${REL}"
done

echo ""
echo "" >> "${LOGFILE}"
echo "=== 汇总 ===" | tee -a "${LOGFILE}"
echo "总文件数: ${TOTAL}" | tee -a "${LOGFILE}"
echo "几何解析成功: ${GEOM_OK}" | tee -a "${LOGFILE}"
echo "结果解析成功: ${RESULT_OK}" | tee -a "${LOGFILE}"
echo "无几何数据: ${NO_GEOM}" | tee -a "${LOGFILE}"
echo "无结果数据(仅几何): ${NO_RESULT}" | tee -a "${LOGFILE}"
echo "崩溃: ${CRASH}" | tee -a "${LOGFILE}"
echo ""
echo "详细日志: ${LOGFILE}"

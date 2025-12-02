#!/usr/bin/env bash

PMU_FILE="/proc/pmu_stats"
OUT_CSV="results.csv"

if [ ! -r "$PMU_FILE" ]; then
    echo "ERROR: $PMU_FILE not found. Did you insmod part1 module?"
    exit 1
fi

# /proc/pmu_stats 한 번 읽어서 7개 값( instruction ~ cycles )을 공백으로 출력
read_pmu() {
    local inst l1i_ref l1i_miss l1d_ref l1d_miss llc_miss cycles

    inst=$(awk '/^instructions:/     {print $2}' "$PMU_FILE")
    l1i_ref=$(awk '/^l1i_references:/ {print $2}' "$PMU_FILE")
    l1i_miss=$(awk '/^l1i_misses:/    {print $2}' "$PMU_FILE")
    l1d_ref=$(awk '/^l1d_references:/ {print $2}' "$PMU_FILE")
    l1d_miss=$(awk '/^l1d_misses:/   {print $2}' "$PMU_FILE")
    llc_miss=$(awk '/^llc_misses:/   {print $2}' "$PMU_FILE")
    cycles=$(awk '/^cycles:/         {print $2}' "$PMU_FILE")

    echo "$inst $l1i_ref $l1i_miss $l1d_ref $l1d_miss $llc_miss $cycles"
}

# 하나의 workload를 측정: baseline → cmd 실행 → after → 차이를 CSV로 기록
measure_workload() {
    local name="$1"
    shift
    local cmd=( "$@" )

    echo "===== Measuring $name: ${cmd[*]} ====="

    # baseline
    read_pmu > before.tmp

    # 워크로드 실행
    "${cmd[@]}"

    # after
    read_pmu > after.tmp

    # diff 계산
    read b_inst b_i_ref b_i_miss b_d_ref b_d_miss b_llc b_cyc < before.tmp
    read a_inst a_i_ref a_i_miss a_d_ref a_d_miss a_llc a_cyc < after.tmp

    local inst l1i_ref l1i_miss l1d_ref l1d_miss llc_miss cycles

    inst=$((a_inst - b_inst))
    l1i_ref=$((a_i_ref - b_i_ref))
    l1i_miss=$((a_i_miss - b_i_miss))
    l1d_ref=$((a_d_ref - b_d_ref))
    l1d_miss=$((a_d_miss - b_d_miss))
    llc_miss=$((a_llc - b_llc))
    cycles=$((a_cyc - b_cyc))

    echo "$name,$inst,$l1i_ref,$l1i_miss,$l1d_ref,$l1d_miss,$llc_miss,$cycles" >> "$OUT_CSV"
}

# CSV 헤더
echo "workload,instructions,l1i_ref,l1i_miss,l1d_ref,l1d_miss,llc_miss,cycles" > "$OUT_CSV"

#######################################
# 여기 아래부터 실제 워크로드들을 정의 #
#######################################

# 1) openssl speed sha256
measure_workload "openssl_sha256" openssl speed sha256

# 2) stream benchmark (현재 디렉토리에 ./stream 있다고 가정)
# 단일 쓰레드
measure_workload "stream_default" ./stream

# OpenMP 4 threads (stream이 OMP 지원 컴파일일 때)
measure_workload "stream_4threads" bash -c "OMP_NUM_THREADS=4 ./stream"

# 3) bzip2 - 서로 다른 크기의 파일
measure_workload "bzip2_small"  bzip2 -k data/small.dat
measure_workload "bzip2_medium" bzip2 -k data/medium.dat
measure_workload "bzip2_large"  bzip2 -k data/large.dat

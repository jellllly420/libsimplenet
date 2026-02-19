#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-perf}"
CXX_BIN="${CXX:-}"

IDLE_ITERATIONS="${IDLE_ITERATIONS:-200000}"
ECHO_ITERATIONS="${ECHO_ITERATIONS:-2000}"
ECHO_PAYLOAD_SIZES="${ECHO_PAYLOAD_SIZES:-64,1024,16384}"
ECHO_CONNECTIONS="${ECHO_CONNECTIONS:-8}"
ASYNC_ECHO_ITERATIONS="${ASYNC_ECHO_ITERATIONS:-${ECHO_ITERATIONS}}"
ASYNC_ECHO_PAYLOAD_SIZES="${ASYNC_ECHO_PAYLOAD_SIZES:-${ECHO_PAYLOAD_SIZES}}"
ASYNC_ECHO_CONNECTIONS="${ASYNC_ECHO_CONNECTIONS:-${ECHO_CONNECTIONS}}"
ASYNC_URING_QUEUE_DEPTH="${ASYNC_URING_QUEUE_DEPTH:-512}"
CHURN_ITERATIONS="${CHURN_ITERATIONS:-1500}"
CHURN_CONNECTION_LEVELS="${CHURN_CONNECTION_LEVELS:-16,32,64}"
PERF_REPEATS="${PERF_REPEATS:-6}"

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

is_positive_integer() {
  [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

is_numeric() {
  [[ "$1" =~ ^[0-9]+([.][0-9]+)?([eE][+-]?[0-9]+)?$ ]]
}

require_positive_integer() {
  local name="$1"
  local value="$2"
  if ! is_positive_integer "${value}"; then
    die "${name} must be a positive integer, got: ${value}"
  fi
}

parse_csv_positive_integers() {
  local raw="$1"
  local output_name="$2"

  if [[ ! "${raw}" =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]]; then
    return 1
  fi

  local IFS=','
  local parsed=()
  read -r -a parsed <<<"${raw}"
  if [[ "${#parsed[@]}" -eq 0 ]]; then
    return 1
  fi

  eval "${output_name}=(\"\${parsed[@]}\")"
}

assert_equals() {
  local expected="$1"
  local actual="$2"
  local label="$3"
  if [[ "${expected}" != "${actual}" ]]; then
    die "${label} mismatch: expected ${expected}, got ${actual}"
  fi
}

append_to_array() {
  local array_name="$1"
  local value="$2"
  eval "${array_name}+=(\"\${value}\")"
}

set_or_assert_constant() {
  local variable_name="$1"
  local value="$2"
  local label="$3"
  local current

  eval "current=\${${variable_name}:-}"
  if [[ -z "${current}" ]]; then
    printf -v "${variable_name}" '%s' "${value}"
    return
  fi

  if [[ "${current}" != "${value}" ]]; then
    die "${label} mismatch: expected ${current}, got ${value}"
  fi
}

extract_perf_line() {
  local output="$1"
  local expected_impl="$2"
  local expected_scenario="$3"

  local matches
  local count
  matches="$(printf '%s\n' "${output}" | awk -v impl="${expected_impl}" -v scenario="${expected_scenario}" '
    /^PERF,/ && index($0, "impl=" impl ",") && index($0, "scenario=" scenario ",") { print }
  ')"
  count="$(printf '%s\n' "${matches}" | sed '/^$/d' | wc -l | tr -d ' ')"

  if [[ "${count}" -ne 1 ]]; then
    return 1
  fi
  printf '%s\n' "${matches}"
}

validate_perf_line() {
  local line="$1"
  local expected_impl="$2"
  local expected_scenario="$3"
  local required_keys_csv="$4"

  printf '%s\n' "${line}" | awk -F',' \
    -v expected_impl="${expected_impl}" \
    -v expected_scenario="${expected_scenario}" \
    -v required_keys="${required_keys_csv}" '
      NR != 1 { exit 90 }
      $1 != "PERF" { exit 1 }
      {
        for (i = 2; i <= NF; ++i) {
          if (index($i, "=") == 0) {
            exit 2
          }
          split($i, kv, "=")
          key = kv[1]
          value = substr($i, length(key) + 2)
          if (key == "" || value == "") {
            exit 3
          }
          if (seen[key]++) {
            exit 4
          }
          values[key] = value
        }

        if (!("impl" in values) || values["impl"] != expected_impl) {
          exit 5
        }
        if (!("scenario" in values) || values["scenario"] != expected_scenario) {
          exit 6
        }

        required_count = split(required_keys, required, ",")
        for (j = 1; j <= required_count; ++j) {
          if (required[j] != "" && !(required[j] in values)) {
            exit 7
          }
        }
      }
    '
}

extract_perf_field() {
  local line="$1"
  local key="$2"
  local value

  if ! value="$(printf '%s\n' "${line}" | awk -F',' -v wanted_key="${key}" '
      NR != 1 { exit 90 }
      $1 != "PERF" { exit 1 }
      {
        count = 0
        for (i = 2; i <= NF; ++i) {
          if (index($i, "=") == 0) {
            exit 2
          }
          split($i, kv, "=")
          field_key = kv[1]
          field_value = substr($i, length(field_key) + 2)
          if (field_key == "" || field_value == "") {
            exit 3
          }
          if (field_key == wanted_key) {
            count++
            value = field_value
          }
        }
        if (count != 1) {
          exit 4
        }
        print value
      }
    ')"; then
    return 1
  fi

  printf '%s\n' "${value}"
}

extract_integer_field() {
  local line="$1"
  local key="$2"
  local value

  if ! value="$(extract_perf_field "${line}" "${key}")"; then
    return 1
  fi
  if ! is_positive_integer "${value}"; then
    return 1
  fi

  printf '%s\n' "${value}"
}

extract_numeric_field() {
  local line="$1"
  local key="$2"
  local value

  if ! value="$(extract_perf_field "${line}" "${key}")"; then
    return 1
  fi
  if ! is_numeric "${value}"; then
    return 1
  fi

  printf '%s\n' "${value}"
}

median_of_values() {
  if [[ "$#" -eq 0 ]]; then
    return 1
  fi

  printf '%s\n' "$@" | LC_ALL=C sort -g | awk '
    { values[NR] = $1 }
    END {
      if (NR == 0) {
        exit 1
      }
      if (NR % 2 == 1) {
        printf "%.6f\n", values[(NR + 1) / 2]
      } else {
        printf "%.6f\n", (values[NR / 2] + values[(NR / 2) + 1]) / 2.0
      }
    }
  '
}

numeric_ratio() {
  local numerator="$1"
  local denominator="$2"

  awk -v n="${numerator}" -v d="${denominator}" '
    BEGIN {
      if (d == 0) {
        exit 1
      }
      printf "%.6f\n", n / d
    }
  '
}

emit_perf_run_line() {
  local line="$1"
  local run_id="$2"
  local position="$3"

  printf 'PERF_RUN,run=%s,position=%s,%s\n' \
    "${run_id}" "${position}" "${line#PERF,}"
}

run_idle_simplenet() {
  local output
  local row
  local mode
  local iterations
  local total_ms
  local avg_ns
  local waits_per_sec
  local extra

  output="$("${BUILD_DIR}/example/simplenet_perf_reactor_wait" "${IDLE_ITERATIONS}" --mode reuse)"
  if ! row="$(printf '%s\n' "${output}" | awk -F',' '
      $1 == "reuse_path" { count++; row = $0 }
      END {
        if (count != 1) {
          exit 1
        }
        print row
      }
    ')"; then
    printf 'error: failed to parse simplenet idle output\n' >&2
    printf '%s\n' "${output}" >&2
    return 1
  fi

  IFS=',' read -r mode iterations total_ms avg_ns waits_per_sec extra <<<"${row}"
  if [[ -n "${extra:-}" || "${mode}" != "reuse_path" ]]; then
    printf 'error: malformed simplenet idle row: %s\n' "${row}" >&2
    return 1
  fi
  if ! is_positive_integer "${iterations}" || ! is_numeric "${total_ms}" || \
     ! is_numeric "${avg_ns}" || ! is_numeric "${waits_per_sec}"; then
    printf 'error: malformed numeric simplenet idle row: %s\n' "${row}" >&2
    return 1
  fi

  printf 'PERF,impl=libsimplenet,scenario=idle_wait,iterations=%s,total_ms=%s,avg_ns_per_wait=%s,waits_per_sec=%s\n' \
    "${iterations}" "${total_ms}" "${avg_ns}" "${waits_per_sec}"
}

run_idle_boost() {
  local output
  local row
  local mode
  local iterations
  local total_ms
  local avg_ns
  local waits_per_sec
  local extra

  output="$("${BUILD_DIR}/example/simplenet_perf_boost_asio_wait" "${IDLE_ITERATIONS}")"
  if ! row="$(printf '%s\n' "${output}" | awk -F',' '
      $1 == "boost_asio_poll_one_pending_wait" { count++; row = $0 }
      END {
        if (count != 1) {
          exit 1
        }
        print row
      }
    ')"; then
    printf 'error: failed to parse boost idle output\n' >&2
    printf '%s\n' "${output}" >&2
    return 1
  fi

  IFS=',' read -r mode iterations total_ms avg_ns waits_per_sec extra <<<"${row}"
  if [[ -n "${extra:-}" || "${mode}" != "boost_asio_poll_one_pending_wait" ]]; then
    printf 'error: malformed boost idle row: %s\n' "${row}" >&2
    return 1
  fi
  if ! is_positive_integer "${iterations}" || ! is_numeric "${total_ms}" || \
     ! is_numeric "${avg_ns}" || ! is_numeric "${waits_per_sec}"; then
    printf 'error: malformed numeric boost idle row: %s\n' "${row}" >&2
    return 1
  fi

  printf 'PERF,impl=boost_asio,scenario=idle_wait,iterations=%s,total_ms=%s,avg_ns_per_wait=%s,waits_per_sec=%s\n' \
    "${iterations}" "${total_ms}" "${avg_ns}" "${waits_per_sec}"
}

run_perf_program() {
  local expected_impl="$1"
  local expected_scenario="$2"
  shift 2

  local output
  local perf_line
  output="$("$@")"
  if ! perf_line="$(extract_perf_line "${output}" "${expected_impl}" "${expected_scenario}")"; then
    printf 'error: benchmark did not emit PERF line: %s\n' "$1" >&2
    printf '%s\n' "${output}" >&2
    return 1
  fi
  printf '%s\n' "${perf_line}"
}

record_idle_run() {
  local line="$1"
  local impl="$2"
  local run_id="$3"
  local position="$4"
  local iterations_var="$5"
  local total_ms_array="$6"
  local avg_ns_array="$7"
  local waits_array="$8"

  validate_perf_line "${line}" "${impl}" "idle_wait" \
    "impl,scenario,iterations,total_ms,avg_ns_per_wait,waits_per_sec" ||
    die "malformed PERF line for ${impl} idle_wait: ${line}"

  local iterations
  local total_ms
  local avg_ns
  local waits_per_sec

  iterations="$(extract_integer_field "${line}" "iterations")" ||
    die "invalid iterations in ${impl} idle_wait PERF line: ${line}"
  total_ms="$(extract_numeric_field "${line}" "total_ms")" ||
    die "invalid total_ms in ${impl} idle_wait PERF line: ${line}"
  avg_ns="$(extract_numeric_field "${line}" "avg_ns_per_wait")" ||
    die "invalid avg_ns_per_wait in ${impl} idle_wait PERF line: ${line}"
  waits_per_sec="$(extract_numeric_field "${line}" "waits_per_sec")" ||
    die "invalid waits_per_sec in ${impl} idle_wait PERF line: ${line}"

  set_or_assert_constant "${iterations_var}" "${iterations}" "${impl} idle iterations"
  append_to_array "${total_ms_array}" "${total_ms}"
  append_to_array "${avg_ns_array}" "${avg_ns}"
  append_to_array "${waits_array}" "${waits_per_sec}"

  emit_perf_run_line "${line}" "${run_id}" "${position}"
}

record_echo_run() {
  local line="$1"
  local impl="$2"
  local payload="$3"
  local run_id="$4"
  local position="$5"
  local echoes_var="$6"
  local bytes_var="$7"
  local total_ms_array="$8"
  local echoes_per_sec_array="$9"
  local mb_per_sec_array="${10}"

  validate_perf_line "${line}" "${impl}" "tcp_echo" \
    "impl,scenario,iterations,payload_size,connections,echoes,bytes,total_ms,echoes_per_sec,mb_per_sec" ||
    die "malformed PERF line for ${impl} tcp_echo: ${line}"

  local iterations
  local payload_size
  local connections
  local echoes
  local bytes
  local total_ms
  local echoes_per_sec
  local mb_per_sec

  iterations="$(extract_integer_field "${line}" "iterations")" ||
    die "invalid iterations in ${impl} tcp_echo PERF line: ${line}"
  payload_size="$(extract_integer_field "${line}" "payload_size")" ||
    die "invalid payload_size in ${impl} tcp_echo PERF line: ${line}"
  connections="$(extract_integer_field "${line}" "connections")" ||
    die "invalid connections in ${impl} tcp_echo PERF line: ${line}"
  echoes="$(extract_integer_field "${line}" "echoes")" ||
    die "invalid echoes in ${impl} tcp_echo PERF line: ${line}"
  bytes="$(extract_integer_field "${line}" "bytes")" ||
    die "invalid bytes in ${impl} tcp_echo PERF line: ${line}"
  total_ms="$(extract_numeric_field "${line}" "total_ms")" ||
    die "invalid total_ms in ${impl} tcp_echo PERF line: ${line}"
  echoes_per_sec="$(extract_numeric_field "${line}" "echoes_per_sec")" ||
    die "invalid echoes_per_sec in ${impl} tcp_echo PERF line: ${line}"
  mb_per_sec="$(extract_numeric_field "${line}" "mb_per_sec")" ||
    die "invalid mb_per_sec in ${impl} tcp_echo PERF line: ${line}"

  assert_equals "${ECHO_ITERATIONS}" "${iterations}" "${impl} tcp_echo iterations"
  assert_equals "${payload}" "${payload_size}" "${impl} tcp_echo payload_size"
  assert_equals "${ECHO_CONNECTIONS}" "${connections}" "${impl} tcp_echo connections"

  set_or_assert_constant "${echoes_var}" "${echoes}" "${impl} tcp_echo echoes"
  set_or_assert_constant "${bytes_var}" "${bytes}" "${impl} tcp_echo bytes"

  append_to_array "${total_ms_array}" "${total_ms}"
  append_to_array "${echoes_per_sec_array}" "${echoes_per_sec}"
  append_to_array "${mb_per_sec_array}" "${mb_per_sec}"

  emit_perf_run_line "${line}" "${run_id}" "${position}"
}

record_churn_run() {
  local line="$1"
  local impl="$2"
  local connections_level="$3"
  local run_id="$4"
  local position="$5"
  local total_connections_var="$6"
  local bytes_var="$7"
  local total_ms_array="$8"
  local cps_array="$9"

  validate_perf_line "${line}" "${impl}" "connection_churn" \
    "impl,scenario,iterations,connections,total_connections,bytes,total_ms,connections_per_sec" ||
    die "malformed PERF line for ${impl} connection_churn: ${line}"

  local iterations
  local connections
  local total_connections
  local bytes
  local total_ms
  local connections_per_sec

  iterations="$(extract_integer_field "${line}" "iterations")" ||
    die "invalid iterations in ${impl} connection_churn PERF line: ${line}"
  connections="$(extract_integer_field "${line}" "connections")" ||
    die "invalid connections in ${impl} connection_churn PERF line: ${line}"
  total_connections="$(extract_integer_field "${line}" "total_connections")" ||
    die "invalid total_connections in ${impl} connection_churn PERF line: ${line}"
  bytes="$(extract_integer_field "${line}" "bytes")" ||
    die "invalid bytes in ${impl} connection_churn PERF line: ${line}"
  total_ms="$(extract_numeric_field "${line}" "total_ms")" ||
    die "invalid total_ms in ${impl} connection_churn PERF line: ${line}"
  connections_per_sec="$(extract_numeric_field "${line}" "connections_per_sec")" ||
    die "invalid connections_per_sec in ${impl} connection_churn PERF line: ${line}"

  assert_equals "${CHURN_ITERATIONS}" "${iterations}" "${impl} connection_churn iterations"
  assert_equals "${connections_level}" "${connections}" "${impl} connection_churn connections"

  set_or_assert_constant "${total_connections_var}" "${total_connections}" \
    "${impl} connection_churn total_connections"
  set_or_assert_constant "${bytes_var}" "${bytes}" "${impl} connection_churn bytes"

  append_to_array "${total_ms_array}" "${total_ms}"
  append_to_array "${cps_array}" "${connections_per_sec}"

  emit_perf_run_line "${line}" "${run_id}" "${position}"
}

detect_async_io_uring_backend() {
  local output
  local status

  if output="$("${BUILD_DIR}/example/simplenet_backend_switch" io_uring 2>&1)"; then
    ASYNC_IO_URING_AVAILABLE=1
    return 0
  fi

  status=$?
  if [[ "${status}" -eq 1 && "${output}" == *"backend unavailable: io_uring"* ]]; then
    ASYNC_IO_URING_AVAILABLE=0
    return 0
  fi

  printf 'error: failed probing libsimplenet async io_uring backend\n' >&2
  printf '%s\n' "${output}" >&2
  return 1
}

record_async_run() {
  local line="$1"
  local impl="$2"
  local backend="$3"
  local payload="$4"
  local run_id="$5"
  local position="$6"
  local echoes_var="$7"
  local bytes_var="$8"
  local total_ms_array="$9"
  local echoes_per_sec_array="${10}"
  local mb_per_sec_array="${11}"

  validate_perf_line "${line}" "${impl}" "async_tcp_echo" \
    "impl,scenario,backend,iterations,payload_size,connections,echoes,bytes,total_ms,echoes_per_sec,mb_per_sec" ||
    die "malformed PERF line for ${impl} async_tcp_echo: ${line}"

  local observed_backend
  local iterations
  local payload_size
  local connections
  local echoes
  local bytes
  local total_ms
  local echoes_per_sec
  local mb_per_sec

  observed_backend="$(extract_perf_field "${line}" "backend")" ||
    die "invalid backend in ${impl} async_tcp_echo PERF line: ${line}"
  iterations="$(extract_integer_field "${line}" "iterations")" ||
    die "invalid iterations in ${impl} async_tcp_echo PERF line: ${line}"
  payload_size="$(extract_integer_field "${line}" "payload_size")" ||
    die "invalid payload_size in ${impl} async_tcp_echo PERF line: ${line}"
  connections="$(extract_integer_field "${line}" "connections")" ||
    die "invalid connections in ${impl} async_tcp_echo PERF line: ${line}"
  echoes="$(extract_integer_field "${line}" "echoes")" ||
    die "invalid echoes in ${impl} async_tcp_echo PERF line: ${line}"
  bytes="$(extract_integer_field "${line}" "bytes")" ||
    die "invalid bytes in ${impl} async_tcp_echo PERF line: ${line}"
  total_ms="$(extract_numeric_field "${line}" "total_ms")" ||
    die "invalid total_ms in ${impl} async_tcp_echo PERF line: ${line}"
  echoes_per_sec="$(extract_numeric_field "${line}" "echoes_per_sec")" ||
    die "invalid echoes_per_sec in ${impl} async_tcp_echo PERF line: ${line}"
  mb_per_sec="$(extract_numeric_field "${line}" "mb_per_sec")" ||
    die "invalid mb_per_sec in ${impl} async_tcp_echo PERF line: ${line}"

  assert_equals "${backend}" "${observed_backend}" "${impl} async_tcp_echo backend"
  assert_equals "${ASYNC_ECHO_ITERATIONS}" "${iterations}" "${impl} async_tcp_echo iterations"
  assert_equals "${payload}" "${payload_size}" "${impl} async_tcp_echo payload_size"
  assert_equals "${ASYNC_ECHO_CONNECTIONS}" "${connections}" "${impl} async_tcp_echo connections"

  if [[ "${impl}" == "libsimplenet" ]]; then
    local uring_queue_depth
    uring_queue_depth="$(extract_integer_field "${line}" "uring_queue_depth")" ||
      die "invalid uring_queue_depth in ${impl} async_tcp_echo PERF line: ${line}"
    assert_equals "${ASYNC_URING_QUEUE_DEPTH}" "${uring_queue_depth}" \
      "${impl} async_tcp_echo uring_queue_depth"
  fi

  set_or_assert_constant "${echoes_var}" "${echoes}" "${impl} async_tcp_echo echoes"
  set_or_assert_constant "${bytes_var}" "${bytes}" "${impl} async_tcp_echo bytes"

  append_to_array "${total_ms_array}" "${total_ms}"
  append_to_array "${echoes_per_sec_array}" "${echoes_per_sec}"
  append_to_array "${mb_per_sec_array}" "${mb_per_sec}"

  emit_perf_run_line "${line}" "${run_id}" "${position}"
}

run_async_pair_series() {
  local payload="$1"
  local libs_backend="$2"
  local paired_ratio_label="$3"

  local libs_total_ms=()
  local libs_echoes_per_sec=()
  local libs_mb_per_sec=()
  local boost_total_ms=()
  local boost_echoes_per_sec=()
  local boost_mb_per_sec=()
  local paired_speed_ratio=()
  local libs_echoes=""
  local libs_bytes=""
  local boost_echoes=""
  local boost_bytes=""
  local line
  local rep

  for ((rep = 1; rep <= PERF_REPEATS; ++rep)); do
    if (( rep % 2 == 1 )); then
      line="$(run_perf_program \
        libsimplenet \
        async_tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_async_echo_libsimplenet" \
        --iterations "${ASYNC_ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ASYNC_ECHO_CONNECTIONS}" \
        --backend "${libs_backend}" \
        --uring-queue-depth "${ASYNC_URING_QUEUE_DEPTH}")"
      record_async_run "${line}" "libsimplenet" "${libs_backend}" "${payload}" "${rep}" "1" \
        libs_echoes libs_bytes libs_total_ms libs_echoes_per_sec libs_mb_per_sec

      line="$(run_perf_program \
        boost_asio \
        async_tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_async_echo_boost_asio" \
        --iterations "${ASYNC_ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ASYNC_ECHO_CONNECTIONS}")"
      record_async_run "${line}" "boost_asio" "epoll" "${payload}" "${rep}" "2" \
        boost_echoes boost_bytes boost_total_ms boost_echoes_per_sec boost_mb_per_sec
    else
      line="$(run_perf_program \
        boost_asio \
        async_tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_async_echo_boost_asio" \
        --iterations "${ASYNC_ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ASYNC_ECHO_CONNECTIONS}")"
      record_async_run "${line}" "boost_asio" "epoll" "${payload}" "${rep}" "1" \
        boost_echoes boost_bytes boost_total_ms boost_echoes_per_sec boost_mb_per_sec

      line="$(run_perf_program \
        libsimplenet \
        async_tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_async_echo_libsimplenet" \
        --iterations "${ASYNC_ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ASYNC_ECHO_CONNECTIONS}" \
        --backend "${libs_backend}" \
        --uring-queue-depth "${ASYNC_URING_QUEUE_DEPTH}")"
      record_async_run "${line}" "libsimplenet" "${libs_backend}" "${payload}" "${rep}" "2" \
        libs_echoes libs_bytes libs_total_ms libs_echoes_per_sec libs_mb_per_sec
    fi
  done

  assert_equals "${libs_echoes}" "${boost_echoes}" \
    "async_tcp_echo echoes across libsimplenet/${libs_backend} vs boost_asio/epoll"
  assert_equals "${libs_bytes}" "${boost_bytes}" \
    "async_tcp_echo bytes across libsimplenet/${libs_backend} vs boost_asio/epoll"

  printf 'PERF_MEDIAN,impl=libsimplenet,scenario=async_tcp_echo,backend=%s,runs=%s,iterations=%s,payload_size=%s,connections=%s,echoes=%s,bytes=%s,uring_queue_depth=%s,total_ms=%s,echoes_per_sec=%s,mb_per_sec=%s\n' \
    "${libs_backend}" \
    "${PERF_REPEATS}" \
    "${ASYNC_ECHO_ITERATIONS}" \
    "${payload}" \
    "${ASYNC_ECHO_CONNECTIONS}" \
    "${libs_echoes}" \
    "${libs_bytes}" \
    "${ASYNC_URING_QUEUE_DEPTH}" \
    "$(median_of_values "${libs_total_ms[@]}")" \
    "$(median_of_values "${libs_echoes_per_sec[@]}")" \
    "$(median_of_values "${libs_mb_per_sec[@]}")"

  printf 'PERF_MEDIAN,impl=boost_asio,scenario=async_tcp_echo,backend=epoll,runs=%s,iterations=%s,payload_size=%s,connections=%s,echoes=%s,bytes=%s,total_ms=%s,echoes_per_sec=%s,mb_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${ASYNC_ECHO_ITERATIONS}" \
    "${payload}" \
    "${ASYNC_ECHO_CONNECTIONS}" \
    "${boost_echoes}" \
    "${boost_bytes}" \
    "$(median_of_values "${boost_total_ms[@]}")" \
    "$(median_of_values "${boost_echoes_per_sec[@]}")" \
    "$(median_of_values "${boost_mb_per_sec[@]}")"

  for ((rep = 0; rep < PERF_REPEATS; ++rep)); do
    append_to_array paired_speed_ratio \
      "$(numeric_ratio "${boost_total_ms[$rep]}" "${libs_total_ms[$rep]}")"
  done
  printf 'PERF_PAIRED_MEDIAN,scenario=async_tcp_echo,metric=total_ms,payload_size=%s,connections=%s,left_impl=libsimplenet,left_backend=%s,right_impl=boost_asio,right_backend=epoll,ratio=%s,runs=%s,value=%s\n' \
    "${payload}" \
    "${ASYNC_ECHO_CONNECTIONS}" \
    "${libs_backend}" \
    "${paired_ratio_label}" \
    "${PERF_REPEATS}" \
    "$(median_of_values "${paired_speed_ratio[@]}")"
}

run_idle_series() {
  local libs_total_ms=()
  local libs_avg_ns=()
  local libs_waits=()
  local boost_total_ms=()
  local boost_avg_ns=()
  local boost_waits=()
  local paired_speed_ratio=()
  local libs_iterations=""
  local boost_iterations=""
  local line
  local rep

  for ((rep = 1; rep <= PERF_REPEATS; ++rep)); do
    if (( rep % 2 == 1 )); then
      line="$(run_idle_simplenet)"
      record_idle_run "${line}" "libsimplenet" "${rep}" "1" \
        libs_iterations libs_total_ms libs_avg_ns libs_waits

      line="$(run_idle_boost)"
      record_idle_run "${line}" "boost_asio" "${rep}" "2" \
        boost_iterations boost_total_ms boost_avg_ns boost_waits
    else
      line="$(run_idle_boost)"
      record_idle_run "${line}" "boost_asio" "${rep}" "1" \
        boost_iterations boost_total_ms boost_avg_ns boost_waits

      line="$(run_idle_simplenet)"
      record_idle_run "${line}" "libsimplenet" "${rep}" "2" \
        libs_iterations libs_total_ms libs_avg_ns libs_waits
    fi
  done

  assert_equals "${IDLE_ITERATIONS}" "${libs_iterations}" "libsimplenet idle_wait iterations"
  assert_equals "${IDLE_ITERATIONS}" "${boost_iterations}" "boost_asio idle_wait iterations"
  assert_equals "${libs_iterations}" "${boost_iterations}" "idle_wait iterations across implementations"

  printf 'PERF_MEDIAN,impl=libsimplenet,scenario=idle_wait,runs=%s,iterations=%s,total_ms=%s,avg_ns_per_wait=%s,waits_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${libs_iterations}" \
    "$(median_of_values "${libs_total_ms[@]}")" \
    "$(median_of_values "${libs_avg_ns[@]}")" \
    "$(median_of_values "${libs_waits[@]}")"

  printf 'PERF_MEDIAN,impl=boost_asio,scenario=idle_wait,runs=%s,iterations=%s,total_ms=%s,avg_ns_per_wait=%s,waits_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${boost_iterations}" \
    "$(median_of_values "${boost_total_ms[@]}")" \
    "$(median_of_values "${boost_avg_ns[@]}")" \
    "$(median_of_values "${boost_waits[@]}")"

  for ((rep = 0; rep < PERF_REPEATS; ++rep)); do
    append_to_array paired_speed_ratio \
      "$(numeric_ratio "${boost_avg_ns[$rep]}" "${libs_avg_ns[$rep]}")"
  done
  printf 'PERF_PAIRED_MEDIAN,scenario=idle_wait,metric=avg_ns_per_wait,ratio=boost_over_libsimplenet,runs=%s,value=%s\n' \
    "${PERF_REPEATS}" \
    "$(median_of_values "${paired_speed_ratio[@]}")"
}

run_echo_series() {
  local payload="$1"

  local libs_total_ms=()
  local libs_echoes_per_sec=()
  local libs_mb_per_sec=()
  local boost_total_ms=()
  local boost_echoes_per_sec=()
  local boost_mb_per_sec=()
  local paired_speed_ratio=()
  local libs_echoes=""
  local libs_bytes=""
  local boost_echoes=""
  local boost_bytes=""
  local line
  local rep

  for ((rep = 1; rep <= PERF_REPEATS; ++rep)); do
    if (( rep % 2 == 1 )); then
      line="$(run_perf_program \
        libsimplenet \
        tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_tcp_echo_libsimplenet" \
        --iterations "${ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ECHO_CONNECTIONS}")"
      record_echo_run "${line}" "libsimplenet" "${payload}" "${rep}" "1" \
        libs_echoes libs_bytes libs_total_ms libs_echoes_per_sec libs_mb_per_sec

      line="$(run_perf_program \
        boost_asio \
        tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_tcp_echo_boost_asio" \
        --iterations "${ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ECHO_CONNECTIONS}")"
      record_echo_run "${line}" "boost_asio" "${payload}" "${rep}" "2" \
        boost_echoes boost_bytes boost_total_ms boost_echoes_per_sec boost_mb_per_sec
    else
      line="$(run_perf_program \
        boost_asio \
        tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_tcp_echo_boost_asio" \
        --iterations "${ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ECHO_CONNECTIONS}")"
      record_echo_run "${line}" "boost_asio" "${payload}" "${rep}" "1" \
        boost_echoes boost_bytes boost_total_ms boost_echoes_per_sec boost_mb_per_sec

      line="$(run_perf_program \
        libsimplenet \
        tcp_echo \
        "${BUILD_DIR}/example/simplenet_perf_tcp_echo_libsimplenet" \
        --iterations "${ECHO_ITERATIONS}" \
        --payload-size "${payload}" \
        --connections "${ECHO_CONNECTIONS}")"
      record_echo_run "${line}" "libsimplenet" "${payload}" "${rep}" "2" \
        libs_echoes libs_bytes libs_total_ms libs_echoes_per_sec libs_mb_per_sec
    fi
  done

  assert_equals "${libs_echoes}" "${boost_echoes}" "tcp_echo echoes across implementations"
  assert_equals "${libs_bytes}" "${boost_bytes}" "tcp_echo bytes across implementations"

  printf 'PERF_MEDIAN,impl=libsimplenet,scenario=tcp_echo,runs=%s,iterations=%s,payload_size=%s,connections=%s,echoes=%s,bytes=%s,total_ms=%s,echoes_per_sec=%s,mb_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${ECHO_ITERATIONS}" \
    "${payload}" \
    "${ECHO_CONNECTIONS}" \
    "${libs_echoes}" \
    "${libs_bytes}" \
    "$(median_of_values "${libs_total_ms[@]}")" \
    "$(median_of_values "${libs_echoes_per_sec[@]}")" \
    "$(median_of_values "${libs_mb_per_sec[@]}")"

  printf 'PERF_MEDIAN,impl=boost_asio,scenario=tcp_echo,runs=%s,iterations=%s,payload_size=%s,connections=%s,echoes=%s,bytes=%s,total_ms=%s,echoes_per_sec=%s,mb_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${ECHO_ITERATIONS}" \
    "${payload}" \
    "${ECHO_CONNECTIONS}" \
    "${boost_echoes}" \
    "${boost_bytes}" \
    "$(median_of_values "${boost_total_ms[@]}")" \
    "$(median_of_values "${boost_echoes_per_sec[@]}")" \
    "$(median_of_values "${boost_mb_per_sec[@]}")"

  for ((rep = 0; rep < PERF_REPEATS; ++rep)); do
    append_to_array paired_speed_ratio \
      "$(numeric_ratio "${boost_total_ms[$rep]}" "${libs_total_ms[$rep]}")"
  done
  printf 'PERF_PAIRED_MEDIAN,scenario=tcp_echo,metric=total_ms,payload_size=%s,connections=%s,ratio=boost_over_libsimplenet,runs=%s,value=%s\n' \
    "${payload}" \
    "${ECHO_CONNECTIONS}" \
    "${PERF_REPEATS}" \
    "$(median_of_values "${paired_speed_ratio[@]}")"
}

run_async_series() {
  local payload="$1"

  run_async_pair_series "${payload}" "epoll" "boost_epoll_over_libsimplenet_epoll"
  if [[ "${ASYNC_IO_URING_AVAILABLE}" == "1" ]]; then
    run_async_pair_series "${payload}" "io_uring" "boost_epoll_over_libsimplenet_io_uring"
  fi
}

run_churn_series() {
  local level="$1"

  local libs_total_ms=()
  local libs_connections_per_sec=()
  local boost_total_ms=()
  local boost_connections_per_sec=()
  local paired_speed_ratio=()
  local libs_total_connections=""
  local libs_bytes=""
  local boost_total_connections=""
  local boost_bytes=""
  local line
  local rep

  for ((rep = 1; rep <= PERF_REPEATS; ++rep)); do
    if (( rep % 2 == 1 )); then
      line="$(run_perf_program \
        libsimplenet \
        connection_churn \
        "${BUILD_DIR}/example/simplenet_perf_connection_churn_libsimplenet" \
        --iterations "${CHURN_ITERATIONS}" \
        --connections "${level}")"
      record_churn_run "${line}" "libsimplenet" "${level}" "${rep}" "1" \
        libs_total_connections libs_bytes libs_total_ms libs_connections_per_sec

      line="$(run_perf_program \
        boost_asio \
        connection_churn \
        "${BUILD_DIR}/example/simplenet_perf_connection_churn_boost_asio" \
        --iterations "${CHURN_ITERATIONS}" \
        --connections "${level}")"
      record_churn_run "${line}" "boost_asio" "${level}" "${rep}" "2" \
        boost_total_connections boost_bytes boost_total_ms boost_connections_per_sec
    else
      line="$(run_perf_program \
        boost_asio \
        connection_churn \
        "${BUILD_DIR}/example/simplenet_perf_connection_churn_boost_asio" \
        --iterations "${CHURN_ITERATIONS}" \
        --connections "${level}")"
      record_churn_run "${line}" "boost_asio" "${level}" "${rep}" "1" \
        boost_total_connections boost_bytes boost_total_ms boost_connections_per_sec

      line="$(run_perf_program \
        libsimplenet \
        connection_churn \
        "${BUILD_DIR}/example/simplenet_perf_connection_churn_libsimplenet" \
        --iterations "${CHURN_ITERATIONS}" \
        --connections "${level}")"
      record_churn_run "${line}" "libsimplenet" "${level}" "${rep}" "2" \
        libs_total_connections libs_bytes libs_total_ms libs_connections_per_sec
    fi
  done

  assert_equals "${libs_total_connections}" "${boost_total_connections}" \
    "connection_churn total_connections across implementations"
  assert_equals "${libs_bytes}" "${boost_bytes}" \
    "connection_churn bytes across implementations"

  printf 'PERF_MEDIAN,impl=libsimplenet,scenario=connection_churn,runs=%s,iterations=%s,connections=%s,total_connections=%s,bytes=%s,total_ms=%s,connections_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${CHURN_ITERATIONS}" \
    "${level}" \
    "${libs_total_connections}" \
    "${libs_bytes}" \
    "$(median_of_values "${libs_total_ms[@]}")" \
    "$(median_of_values "${libs_connections_per_sec[@]}")"

  printf 'PERF_MEDIAN,impl=boost_asio,scenario=connection_churn,runs=%s,iterations=%s,connections=%s,total_connections=%s,bytes=%s,total_ms=%s,connections_per_sec=%s\n' \
    "${PERF_REPEATS}" \
    "${CHURN_ITERATIONS}" \
    "${level}" \
    "${boost_total_connections}" \
    "${boost_bytes}" \
    "$(median_of_values "${boost_total_ms[@]}")" \
    "$(median_of_values "${boost_connections_per_sec[@]}")"

  for ((rep = 0; rep < PERF_REPEATS; ++rep)); do
    append_to_array paired_speed_ratio \
      "$(numeric_ratio "${boost_total_ms[$rep]}" "${libs_total_ms[$rep]}")"
  done
  printf 'PERF_PAIRED_MEDIAN,scenario=connection_churn,metric=total_ms,connections=%s,ratio=boost_over_libsimplenet,runs=%s,value=%s\n' \
    "${level}" \
    "${PERF_REPEATS}" \
    "$(median_of_values "${paired_speed_ratio[@]}")"
}

require_positive_integer "IDLE_ITERATIONS" "${IDLE_ITERATIONS}"
require_positive_integer "ECHO_ITERATIONS" "${ECHO_ITERATIONS}"
require_positive_integer "ECHO_CONNECTIONS" "${ECHO_CONNECTIONS}"
require_positive_integer "ASYNC_ECHO_ITERATIONS" "${ASYNC_ECHO_ITERATIONS}"
require_positive_integer "ASYNC_ECHO_CONNECTIONS" "${ASYNC_ECHO_CONNECTIONS}"
require_positive_integer "ASYNC_URING_QUEUE_DEPTH" "${ASYNC_URING_QUEUE_DEPTH}"
require_positive_integer "CHURN_ITERATIONS" "${CHURN_ITERATIONS}"
require_positive_integer "PERF_REPEATS" "${PERF_REPEATS}"
if (( PERF_REPEATS % 2 != 0 )); then
  die "PERF_REPEATS must be even to fully balance alternating run order"
fi

parse_csv_positive_integers "${ECHO_PAYLOAD_SIZES}" echo_payloads ||
  die "ECHO_PAYLOAD_SIZES must be comma-separated positive integers, got: ${ECHO_PAYLOAD_SIZES}"
parse_csv_positive_integers "${ASYNC_ECHO_PAYLOAD_SIZES}" async_echo_payloads ||
  die "ASYNC_ECHO_PAYLOAD_SIZES must be comma-separated positive integers, got: ${ASYNC_ECHO_PAYLOAD_SIZES}"
parse_csv_positive_integers "${CHURN_CONNECTION_LEVELS}" churn_levels ||
  die "CHURN_CONNECTION_LEVELS must be comma-separated positive integers, got: ${CHURN_CONNECTION_LEVELS}"

cmake_args=(
  -S "${ROOT_DIR}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE=Release
  -DSIMPLENET_BUILD_BOOST_BENCHMARKS=ON
)
if [[ -n "${CXX_BIN}" ]]; then
  cmake_args+=("-DCMAKE_CXX_COMPILER=${CXX_BIN}")
fi
cmake "${cmake_args[@]}" >/dev/null
cmake --build "${BUILD_DIR}" \
  --target \
  simplenet_backend_switch \
  simplenet_perf_reactor_wait \
  simplenet_perf_tcp_echo_libsimplenet \
  simplenet_perf_connection_churn_libsimplenet \
  simplenet_perf_async_echo_libsimplenet \
  simplenet_perf_boost_asio_wait \
  simplenet_perf_tcp_echo_boost_asio \
  simplenet_perf_connection_churn_boost_asio \
  simplenet_perf_async_echo_boost_asio \
  >/dev/null

git_sha="unknown"
if git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if git -C "${ROOT_DIR}" rev-parse --verify HEAD >/dev/null 2>&1; then
    git_sha="$(git -C "${ROOT_DIR}" rev-parse --short=12 HEAD)"
  else
    worktree_fingerprint=""
    if command -v sha256sum >/dev/null 2>&1; then
      worktree_fingerprint="$(
        git -C "${ROOT_DIR}" status --porcelain=v1 -uall \
          | sha256sum \
          | awk '{print $1}'
      )"
    elif command -v shasum >/dev/null 2>&1; then
      worktree_fingerprint="$(
        git -C "${ROOT_DIR}" status --porcelain=v1 -uall \
          | shasum -a 256 \
          | awk '{print $1}'
      )"
    fi

    if [[ -n "${worktree_fingerprint}" ]]; then
      git_sha="worktree-${worktree_fingerprint:0:12}"
    else
      git_sha="worktree-uncommitted"
    fi
  fi
fi

compiler_path="$(awk -F= '/^CMAKE_CXX_COMPILER:FILEPATH=/{print $2; exit}' "${BUILD_DIR}/CMakeCache.txt" || true)"
compiler_id="$(awk -F= '/^CMAKE_CXX_COMPILER_ID:STRING=/{print $2; exit}' "${BUILD_DIR}/CMakeCache.txt" || true)"
compiler_version="$(awk -F= '/^CMAKE_CXX_COMPILER_VERSION:STRING=/{print $2; exit}' "${BUILD_DIR}/CMakeCache.txt" || true)"
compiler_meta_file="$(printf '%s\n' "${BUILD_DIR}"/CMakeFiles/*/CMakeCXXCompiler.cmake | head -n 1)"
  if [[ -f "${compiler_meta_file}" ]]; then
  if [[ -z "${compiler_id}" ]]; then
    compiler_id="$(awk -F'\"' '/^set\(CMAKE_CXX_COMPILER_ID /{print $2; exit}' "${compiler_meta_file}" || true)"
  fi
  if [[ -z "${compiler_version}" ]]; then
    compiler_version="$(awk -F'\"' '/^set\(CMAKE_CXX_COMPILER_VERSION /{print $2; exit}' "${compiler_meta_file}" || true)"
  fi
fi
if [[ -n "${compiler_path}" && -x "${compiler_path}" && -z "${compiler_version}" ]]; then
  compiler_version="$("${compiler_path}" --version | head -n 1 || true)"
fi

kernel_name="$(uname -s)"
kernel_release="$(uname -r)"
machine_arch="$(uname -m)"
host_name="$(hostname)"
generated_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

compiler_path="${compiler_path:-unknown}"
compiler_id="${compiler_id:-unknown}"
compiler_version="${compiler_version:-unknown}"
compiler_version="${compiler_version//,/;}"
kernel_name="${kernel_name:-unknown}"
kernel_release="${kernel_release:-unknown}"
machine_arch="${machine_arch:-unknown}"
host_name="${host_name:-unknown}"

ASYNC_IO_URING_AVAILABLE=0
detect_async_io_uring_backend

async_io_uring_state="available"
if [[ "${ASYNC_IO_URING_AVAILABLE}" != "1" ]]; then
  async_io_uring_state="unavailable"
fi

printf 'PERF,suite=libsimplenet_vs_boost_asio,format=v4,repeats=%s,order=alternating_pairwise\n' "${PERF_REPEATS}"
printf 'PERF_META,git_sha=%s,generated_utc=%s,build_type=Release,cxx=%s,cxx_id=%s,cxx_version=%s,kernel=%s-%s,arch=%s,host=%s\n' \
  "${git_sha}" \
  "${generated_utc}" \
  "${compiler_path}" \
  "${compiler_id}" \
  "${compiler_version}" \
  "${kernel_name}" \
  "${kernel_release}" \
  "${machine_arch}" \
  "${host_name}"
printf 'PERF_META_ASYNC,iterations=%s,payload_sizes=%s,connections=%s,uring_queue_depth=%s,io_uring_backend=%s\n' \
  "${ASYNC_ECHO_ITERATIONS}" \
  "${ASYNC_ECHO_PAYLOAD_SIZES}" \
  "${ASYNC_ECHO_CONNECTIONS}" \
  "${ASYNC_URING_QUEUE_DEPTH}" \
  "${async_io_uring_state}"
if [[ "${ASYNC_IO_URING_AVAILABLE}" != "1" ]]; then
  printf 'PERF_SKIP,scenario=async_tcp_echo,impl=libsimplenet,backend=io_uring,reason=backend_unavailable\n'
fi

run_idle_series

for payload in "${echo_payloads[@]}"; do
  run_echo_series "${payload}"
done

for payload in "${async_echo_payloads[@]}"; do
  run_async_series "${payload}"
done

for level in "${churn_levels[@]}"; do
  run_churn_series "${level}"
done

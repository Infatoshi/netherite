#!/usr/bin/env bash
# Start/stop one isolated headless Minecraft oracle for parallel trace matrices.
# Unlike start_vnc_client.sh this never uses broad pkill: every child belongs to
# a recorded process group and gets a unique X display, QRL port, seed/save, and log.
set -euo pipefail

ACTION="${1:-}"
INSTANCE="${2:-}"
SEED="${3:-}"
if [[ ! "$INSTANCE" =~ ^[0-9]+$ ]] || [ -z "$ACTION" ]; then
    echo "usage: $0 start|stop|status INSTANCE [SEED]" >&2
    exit 2
fi
if [ "$ACTION" = start ] && ! [[ "$SEED" =~ ^-?[0-9]+$ ]]; then
    echo "start requires an integer SEED" >&2
    exit 2
fi

JAVA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$JAVA_DIR/.." && pwd)"
MC_DIR="$JAVA_DIR/Minecraft"
OUT="$REPO/magma/trace/out/oracle_pool/instance_$INSTANCE"
BASE_DISPLAY="${ORACLE_POOL_DISPLAY_BASE:-20}"
BASE_PORT="${ORACLE_POOL_PORT_BASE:-25600}"
BASE_VNC="${ORACLE_POOL_VNC_BASE:-5920}"
DISPLAY_NUM=$((BASE_DISPLAY + INSTANCE))
PORT=$((BASE_PORT + INSTANCE))
VNC_PORT=$((BASE_VNC + INSTANCE))
CONFIG="$OUT/qrl_launch.json"
GROUP_PID_FILE="$OUT/client.pgid"
XVFB_PID_FILE="$OUT/xvfb.pid"
VNC_PID_FILE="$OUT/x11vnc.pid"
GRADLE_CACHE="${ORACLE_GRADLE_CACHE:-run/gradle}"
PROJECT_CACHE="${ORACLE_PROJECT_CACHE:-}"

pid_alive() {
    local file="$1" pid
    [ -s "$file" ] || return 1
    read -r pid <"$file"
    [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null
}

pid_cmd_has() {
    local pid="$1" expected="$2"
    [ -r "/proc/$pid/cmdline" ] &&
        tr '\0' ' ' <"/proc/$pid/cmdline" | grep -Fq -- "$expected"
}

instance_java_pids() {
    local proc environ pid
    for proc in /proc/[0-9]*; do
        [ -r "$proc/comm" ] && [ -r "$proc/environ" ] || continue
        [ "$(cat "$proc/comm" 2>/dev/null)" = java ] || continue
        environ="$(tr '\0' '\n' <"$proc/environ" 2>/dev/null || true)"
        grep -Fxq "QRL_LAUNCH_JSON=$CONFIG" <<<"$environ" || continue
        grep -Fxq "QRL_PORT=$PORT" <<<"$environ" || continue
        pid="${proc##*/}"
        kill -0 "$pid" 2>/dev/null && echo "$pid"
    done
}

instance_alive() {
    local pid
    pid_alive "$GROUP_PID_FILE" && return 0
    while read -r pid; do
        [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null && return 0
    done < <(instance_java_pids)
    return 1
}

port_ready() {
    timeout 1 bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT" 2>/dev/null
}

stop_instance() {
    local pid
    local -a java_pids=()
    if [ -s "$GROUP_PID_FILE" ]; then
        read -r pid <"$GROUP_PID_FILE"
        if [[ "$pid" =~ ^[0-9]+$ ]] && pid_cmd_has "$pid" "-PrunDir=$OUT/run"; then
            kill -TERM -- "-$pid" 2>/dev/null || true
            for _ in $(seq 1 30); do
                kill -0 "$pid" 2>/dev/null || break
                sleep 0.2
            done
            kill -KILL -- "-$pid" 2>/dev/null || true
        elif [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null; then
            echo "refusing to stop stale client pid $pid (command does not match instance)" >&2
        fi
    fi
    mapfile -t java_pids < <(instance_java_pids)
    if ((${#java_pids[@]})); then
        kill -TERM "${java_pids[@]}" 2>/dev/null || true
        for _ in $(seq 1 30); do
            mapfile -t java_pids < <(instance_java_pids)
            ((${#java_pids[@]})) || break
            sleep 0.2
        done
        mapfile -t java_pids < <(instance_java_pids)
        ((${#java_pids[@]})) && kill -KILL "${java_pids[@]}" 2>/dev/null || true
    fi
    for file in "$VNC_PID_FILE" "$XVFB_PID_FILE"; do
        if [ -s "$file" ]; then
            read -r pid <"$file"
            if [ "$file" = "$VNC_PID_FILE" ]; then expected="x11vnc"; else expected="Xvfb :$DISPLAY_NUM"; fi
            if [[ "$pid" =~ ^[0-9]+$ ]] && pid_cmd_has "$pid" "$expected"; then
                kill "$pid" 2>/dev/null || true
            elif [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null; then
                echo "refusing to stop stale pid $pid (expected $expected)" >&2
            fi
        fi
    done
    rm -f "$GROUP_PID_FILE" "$VNC_PID_FILE" "$XVFB_PID_FILE"
}

case "$ACTION" in
    status)
        if instance_alive; then
            if port_ready; then state=ready; else state=starting; fi
            echo "instance=$INSTANCE state=$state display=:$DISPLAY_NUM port=$PORT out=$OUT"
            exit 0
        fi
        echo "instance=$INSTANCE state=stopped display=:$DISPLAY_NUM port=$PORT out=$OUT"
        exit 1
        ;;
    stop)
        stop_instance
        echo "instance=$INSTANCE stopped"
        exit 0
        ;;
    start)
        ;;
    *)
        echo "unknown action: $ACTION" >&2
        exit 2
        ;;
esac

mkdir -p "$OUT"
if pid_alive "$GROUP_PID_FILE"; then
    echo "instance $INSTANCE is already running" >&2
    exit 1
fi
stop_instance
mkdir -p "$OUT/run"
cp "$MC_DIR/run/options.txt" "$OUT/run/options.txt"

BASE_CONFIG="$MC_DIR/run/qrl_launch.json"
[ -s "$BASE_CONFIG" ] || {
    echo "missing $BASE_CONFIG; run mc_cli.py --no-launch first" >&2
    exit 1
}
jq --argjson port "$PORT" --argjson seed "$SEED" \
   --arg world_type "${ORACLE_POOL_WORLD_TYPE:-}" \
   --arg structures "${ORACLE_POOL_STRUCTURES:-}" \
   '.port=$port | .world.seed=$seed | .profile="oracle-pool"
    | if $world_type != "" then .world.type=$world_type else . end
    | if $structures != "" then .world.structures=($structures == "1" or $structures == "true") else . end' \
   "$BASE_CONFIG" >"$CONFIG"

setsid nohup Xvfb ":$DISPLAY_NUM" -screen 0 854x480x24 +extension GLX +render -noreset \
    >"$OUT/xvfb.log" 2>&1 </dev/null &
echo "$!" >"$XVFB_PID_FILE"
for _ in $(seq 1 50); do
    DISPLAY=":$DISPLAY_NUM" xdpyinfo >/dev/null 2>&1 && break
    pid_alive "$XVFB_PID_FILE" || {
        echo "Xvfb for instance $INSTANCE exited during startup" >&2
        tail -n 60 "$OUT/xvfb.log" >&2 || true
        exit 1
    }
    sleep 0.1
done
DISPLAY=":$DISPLAY_NUM" xdpyinfo >/dev/null 2>&1 || {
    echo "Xvfb for instance $INSTANCE did not become ready" >&2
    exit 1
}

if [ "${ORACLE_POOL_VNC:-0}" = 1 ]; then
    setsid nohup x11vnc -display ":$DISPLAY_NUM" -nopw -localhost -forever -shared \
        -rfbport "$VNC_PORT" -noxdamage >"$OUT/x11vnc.log" 2>&1 </dev/null &
    echo "$!" >"$VNC_PID_FILE"
fi

export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=":$DISPLAY_NUM"
export QRL_PORT="$PORT"
export QRL_LAUNCH_JSON="$CONFIG"
export MC_USERNAME="PoolPlayer$INSTANCE"
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=2.1
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djavax.accessibility.assistive_technologies="

(
    cd "$MC_DIR"
    gradle_args=(-g "$GRADLE_CACHE" --no-daemon --offline
        -x getVersionJson -x getAssetIndex -x getAssets)
    if [ -n "$PROJECT_CACHE" ]; then
        gradle_args+=(--project-cache-dir "$PROJECT_CACHE")
    fi
    setsid nohup ./gradlew "${gradle_args[@]}" \
        -PrunDir="$OUT/run" runClient --stacktrace \
        >"$OUT/runclient.log" 2>&1 </dev/null &
    echo "$!" >"$GROUP_PID_FILE"
)

echo "instance=$INSTANCE starting seed=$SEED display=:$DISPLAY_NUM port=$PORT out=$OUT"
if [ "${ORACLE_POOL_WAIT:-1}" = 1 ]; then
    for _ in $(seq 1 420); do
        if port_ready; then
            echo "instance=$INSTANCE ready port=$PORT"
            exit 0
        fi
        pid_alive "$GROUP_PID_FILE" || {
            echo "instance $INSTANCE exited during startup; log tail:" >&2
            tail -n 60 "$OUT/runclient.log" >&2 || true
            exit 1
        }
        sleep 1
    done
    echo "instance $INSTANCE did not bind port $PORT within 420s" >&2
    exit 1
fi

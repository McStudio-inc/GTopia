set -u
export LC_ALL=C # :/ test

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/admin_client_config.txt"

MAX_PACKET_SIZE=$((4 * 1024 * 1024))

HOST="127.0.0.1"
PORT="17999"
ACCOUNT=""
PASSWORD=""
COMMAND_TO_EXECUTE=""

CONFIG_FOUND=false

if [[ -f "$CONFIG_FILE" ]]; then
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"

        [[ -z "$line" ]] && continue
        [[ "$line" == \#* ]] && continue

        IFS='|' read -r c_host c_port c_account c_password <<< "$line"

        if [[ -n "${c_host:-}" && -n "${c_port:-}" && -n "${c_account:-}" && -n "${c_password:-}" ]]; then
            HOST="${c_host//[[:space:]]/}"
            PORT="${c_port//[[:space:]]/}"
            ACCOUNT="${c_account//[[:space:]]/}"
            PASSWORD="${c_password//[[:space:]]/}"
            CONFIG_FOUND=true
            break
        fi
    done < "$CONFIG_FILE"
fi

if [[ $# -gt 0 ]]; then
    if [[ "$CONFIG_FOUND" == false ]]; then
        if [[ $# -ge 3 ]]; then
            ACCOUNT="$1"
            PASSWORD="$2"
            shift 2
            COMMAND_TO_EXECUTE="$*"
        else
            echo "[ERROR] admin_client_config.txt missing. Usage: ./AdminClient.sh <loginName> <loginPassword> <command>" >&2
            exit 1
        fi
    else
        COMMAND_TO_EXECUTE="$*"
    fi
else
    if [[ "$CONFIG_FOUND" == false || -z "$ACCOUNT" || -z "$PASSWORD" ]]; then
        echo "admin_client_config.txt not found or invalid format."
        read -r -p "Enter Account Name: " ACCOUNT
        read -r -p "Enter Password: " PASSWORD
        echo ""
    fi
fi

if [[ -z "$ACCOUNT" || -z "$PASSWORD" ]]; then
    echo "[ERROR] Account or Password credentials are empty." >&2
    exit 1
fi

# yeah i dont trust that thing
uint32_to_bytes() {
    local value="$1"
    printf "\\$(printf '%03o' $(( value        & 255 )))"
    printf "\\$(printf '%03o' $(( (value >> 8)  & 255 )))"
    printf "\\$(printf '%03o' $(( (value >> 16) & 255 )))"
    printf "\\$(printf '%03o' $(( (value >> 24) & 255 )))"
}

send_packet() {
    local payload="$1"
    local payload_size
    payload_size=$(printf '%s' "$payload" | wc -c)
    payload_size="${payload_size//[[:space:]]/}"

    local total_size=$((payload_size + 4))

    if (( total_size < 4 || total_size > MAX_PACKET_SIZE )); then
        echo "[ERROR] Invalid packet size: $total_size" >&2
        return 1
    fi

    uint32_to_bytes "$total_size" >&3
    printf '%s' "$payload" >&3
}

receive_packet() {
    local total_size
    total_size="$(head -c 4 <&3 | od -An -t u4 2>/dev/null | tr -d ' ')"

    if [[ -z "$total_size" ]]; then
        echo "[ERROR] Connection closed by server or header unreadable." >&2
        return 1
    fi

    if (( total_size < 4 )); then
        echo "[ERROR] Invalid packet size: $total_size" >&2
        return 1
    fi

    if (( total_size > MAX_PACKET_SIZE )); then
        echo "[ERROR] Packet too large: $total_size" >&2
        return 1
    fi

    local payload_size=$((total_size - 4))
    if (( payload_size == 0 )); then
        return 0
    fi

    head -c "$payload_size" <&3
}

echo "Connecting to $HOST:$PORT..."

exec 3<>"/dev/tcp/$HOST/$PORT" || {
    echo "[ERROR] Could not connect to $HOST:$PORT" >&2
    exit 1
}

send_packet "auth $ACCOUNT $PASSWORD" || exit 1
AUTH_RESPONSE="$(receive_packet)" || exit 1

if [[ "$AUTH_RESPONSE" != "OK" ]]; then
    echo "[ERROR] Authentication failed: $AUTH_RESPONSE" >&2
    exit 1
fi

echo "Connected."
echo ""

sleep 0.5

if [[ -n "$COMMAND_TO_EXECUTE" ]]; then
    send_packet "$COMMAND_TO_EXECUTE" || exit 1
    RESPONSE="$(receive_packet)" || exit 1

    if [[ -n "$RESPONSE" ]]; then
        printf '%s\n' "$RESPONSE"
    fi

    exec 3>&-
    exec 3<&-
    exit 0
fi

while true; do
    printf "> "

    if ! IFS= read -r COMMAND; then
        break
    fi

    COMMAND="${COMMAND#"${COMMAND%%[![:space:]]*}"}"
    COMMAND="${COMMAND%"${COMMAND##*[![:space:]]}"}"

    [[ -z "$COMMAND" ]] && continue
    if [[ "$COMMAND" == "exit" || "$COMMAND" == "quit" ]]; then
        break
    fi

    send_packet "$COMMAND" || break
    RESPONSE="$(receive_packet)" || break

    if [[ -n "$RESPONSE" ]]; then
        printf '%s\n' "$RESPONSE"
    fi

    sleep 0.25
done

exec 3>&-
exec 3<&-
#!/usr/bin/env bash
# Starts the local Crossguard backend (Nakama + PostgreSQL in Docker) and waits until it answers.
# docs/backend-setup.md. Options: --down (stop), --reset (stop and DELETE the local database), --logs.
set -euo pipefail
cd "$(dirname "$0")/../Backend"

if ! docker info >/dev/null 2>&1; then
	echo "Docker is not running. Start Docker Desktop (or the docker daemon) and try again." >&2
	exit 1
fi

# First run: Backend/.env from the template, every "generate" replaced by a random key.
if [ ! -f .env ]; then
	while IFS= read -r line; do
		if [[ "$line" == *=generate ]]; then
			echo "${line%=generate}=$(od -An -N24 -tx1 /dev/urandom | tr -d ' \n')"
		else
			echo "$line"
		fi
	done < .env.example > .env
	echo "Created Backend/.env with random keys (gitignored)."
fi

COMPOSE=(docker compose -f docker-compose.yml --env-file .env)
case "${1:-}" in
	--down) "${COMPOSE[@]}" down; exit 0 ;;
	--reset) "${COMPOSE[@]}" down -v; echo "Local database deleted."; exit 0 ;;
	--logs) exec "${COMPOSE[@]}" logs -f nakama ;;
esac

"${COMPOSE[@]}" up -d --build

echo -n "Waiting for Nakama"
for _ in $(seq 1 90); do
	if curl -fsS http://127.0.0.1:7350/healthcheck >/dev/null 2>&1; then
		echo " ready."
		http_key=$(grep '^NAKAMA_HTTP_KEY=' .env | cut -d= -f2-)
		cat <<INFO

  Game API:  http://127.0.0.1:7350
  Console:   http://127.0.0.1:7351  (user/password: NAKAMA_CONSOLE_USER / NAKAMA_CONSOLE_PASSWORD in Backend/.env)
  Dedicated server flags:
    -SaveBackend=Nakama -NakamaUrl=http://127.0.0.1:7350 -NakamaHttpKey=${http_key}
  Check it: python3 Tools/backend_smoke.py    Logs: --logs    Stop: --down    Wipe database: --reset
INFO
		exit 0
	fi
	echo -n "."
	sleep 2
done
echo
echo "Nakama did not become healthy in 3 minutes. Last log lines:" >&2
"${COMPOSE[@]}" logs --tail 40 nakama >&2
exit 1

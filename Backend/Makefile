# Crossguard backend helpers (docs/backend-setup.md). Needs Docker; `test` needs Go 1.27.1+ (or GOTOOLCHAIN=auto).
COMPOSE = docker compose -f docker-compose.yml --env-file .env
NAKAMA_VERSION = 3.41.0

.PHONY: test plugin up down logs reset

# Unit tests of the Go module (no Nakama needed).
test:
	cd nakama/modules && go vet ./... && go test ./...

# Builds the plugin alone with Nakama's pluginbuilder (the compose build does this too).
plugin:
	docker run --rm -v "$(CURDIR)/nakama/modules:/backend" -w /backend \
		heroiclabs/nakama-pluginbuilder:$(NAKAMA_VERSION) build -trimpath -buildmode=plugin -o ./build/crossguard.so .

up:
	$(COMPOSE) up -d --build

down:
	$(COMPOSE) down

logs:
	$(COMPOSE) logs -f nakama

# Deletes the local database (every account and save).
reset:
	$(COMPOSE) down -v

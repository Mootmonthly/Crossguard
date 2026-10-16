# Crossguard's Nakama image: the stock server plus our Go runtime module and config.
# Built by docker-compose.yml (`docker compose build`); see docs/backend-setup.md.
#
# The plugin must be built by the pluginbuilder image of the SAME Nakama version (same Go toolchain and
# dependency versions), and modules/go.mod must require the nakama-common version that Nakama uses.
# Upgrading Nakama: bump NAKAMA_VERSION here and in docker-compose.yml, and nakama-common in go.mod
# (the "github.com/heroiclabs/nakama-common" line of Nakama's own go.mod for that tag).
ARG NAKAMA_VERSION=3.41.0

FROM heroiclabs/nakama-pluginbuilder:${NAKAMA_VERSION} AS builder
ENV CGO_ENABLED=1
WORKDIR /backend
COPY modules/go.mod modules/go.sum ./
RUN go mod download
COPY modules/*.go ./
RUN go vet ./... && go build -trimpath -buildmode=plugin -o ./crossguard.so .

FROM heroiclabs/nakama:${NAKAMA_VERSION}
COPY --from=builder /backend/crossguard.so /nakama/data/modules/crossguard.so
COPY local.yml /nakama/data/local.yml

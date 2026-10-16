module crossguard/backend

// Must match the Go version and the nakama-common version of the Nakama image (Backend/nakama/Dockerfile,
// NAKAMA_VERSION): a Go plugin only loads when every shared package is the exact version Nakama was built
// with. Nakama 3.41.0 uses nakama-common v1.48.0 and Go 1.27.1 (its go.mod).
go 1.27.1

require github.com/heroiclabs/nakama-common v1.48.0

require google.golang.org/protobuf v1.36.12 // indirect

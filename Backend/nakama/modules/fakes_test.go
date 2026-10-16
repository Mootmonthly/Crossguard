package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"errors"
	"fmt"

	"github.com/heroiclabs/nakama-common/api"
	"github.com/heroiclabs/nakama-common/runtime"
	"google.golang.org/protobuf/types/known/timestamppb"
)

// fakeLogger records messages; the embedded interface panics on anything else (not used here).
type fakeLogger struct {
	runtime.Logger
	lines []string
}

func (l *fakeLogger) log(level, format string, v ...interface{}) {
	l.lines = append(l.lines, level+": "+fmt.Sprintf(format, v...))
}
func (l *fakeLogger) Debug(format string, v ...interface{}) { l.log("debug", format, v...) }
func (l *fakeLogger) Info(format string, v ...interface{})  { l.log("info", format, v...) }
func (l *fakeLogger) Warn(format string, v ...interface{})  { l.log("warn", format, v...) }
func (l *fakeLogger) Error(format string, v ...interface{}) { l.log("error", format, v...) }

type storedObject struct {
	value           string
	version         string
	permissionRead  int
	permissionWrite int
}

// fakeNakama: users and storage in memory, with Nakama's version semantics ("" = no check, "*" = only if
// absent, otherwise must match). Only the calls the module makes are implemented.
type fakeNakama struct {
	runtime.NakamaModule
	users    map[string]*api.Account
	storage  map[string]storedObject // collection/user/key
	failRead bool
	writes   int
}

func newFakeNakama(userIDs ...string) *fakeNakama {
	nk := &fakeNakama{users: map[string]*api.Account{}, storage: map[string]storedObject{}}
	for _, id := range userIDs {
		nk.users[id] = &api.Account{User: &api.User{Id: id, Username: "user-" + id[:4]}}
	}
	return nk
}

func (nk *fakeNakama) ban(userID string) {
	nk.users[userID].DisableTime = timestamppb.New(now())
}

func storageKey(collection, userID, key string) string { return collection + "/" + userID + "/" + key }

func (nk *fakeNakama) UsersGetId(ctx context.Context, userIDs []string, facebookIDs []string) ([]*api.User, error) {
	var users []*api.User
	for _, id := range userIDs {
		if account, ok := nk.users[id]; ok {
			users = append(users, account.User)
		}
	}
	return users, nil
}

func (nk *fakeNakama) AccountGetId(ctx context.Context, userID string) (*api.Account, error) {
	if account, ok := nk.users[userID]; ok {
		return account, nil
	}
	return nil, errors.New("account not found")
}

func (nk *fakeNakama) StorageRead(ctx context.Context, reads []*runtime.StorageRead) ([]*api.StorageObject, error) {
	if nk.failRead {
		return nil, errors.New("database is down")
	}
	var objects []*api.StorageObject
	for _, read := range reads {
		if object, ok := nk.storage[storageKey(read.Collection, read.UserID, read.Key)]; ok {
			objects = append(objects, &api.StorageObject{Collection: read.Collection, Key: read.Key, UserId: read.UserID,
				Value: object.value, Version: object.version, PermissionRead: int32(object.permissionRead), PermissionWrite: int32(object.permissionWrite)})
		}
	}
	return objects, nil
}

func (nk *fakeNakama) StorageWrite(ctx context.Context, writes []*runtime.StorageWrite) ([]*api.StorageObjectAck, error) {
	var acks []*api.StorageObjectAck
	for _, write := range writes {
		id := storageKey(write.Collection, write.UserID, write.Key)
		existing, exists := nk.storage[id]
		switch {
		case write.Version == "":
		case write.Version == "*":
			if exists {
				return nil, runtime.ErrStorageRejectedVersion
			}
		default:
			if !exists || existing.version != write.Version {
				return nil, runtime.ErrStorageRejectedVersion
			}
		}
		sum := md5.Sum([]byte(write.Value))
		version := hex.EncodeToString(sum[:])
		nk.storage[id] = storedObject{value: write.Value, version: version, permissionRead: write.PermissionRead, permissionWrite: write.PermissionWrite}
		nk.writes++
		acks = append(acks, &api.StorageObjectAck{Collection: write.Collection, Key: write.Key, UserId: write.UserID, Version: version})
	}
	return acks, nil
}

// objectsIn lists the stored objects of one collection.
func (nk *fakeNakama) objectsIn(collection string) []storedObject {
	var found []storedObject
	for id, object := range nk.storage {
		if len(id) > len(collection) && id[:len(collection)+1] == collection+"/" {
			found = append(found, object)
		}
	}
	return found
}

// Contexts as Nakama builds them: a server-to-server call (HTTP key) has no user id.
func serverCtx() context.Context { return context.Background() }

func clientCtx(userID string) context.Context {
	return context.WithValue(context.Background(), runtime.RUNTIME_CTX_USER_ID, userID)
}

func errorCode(err error) int {
	var runtimeErr *runtime.Error
	if errors.As(err, &runtimeErr) {
		return runtimeErr.Code
	}
	return -1
}

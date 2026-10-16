package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"time"

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
	users     map[string]*api.Account
	storage   map[string]storedObject // collection/user/key
	failRead  bool
	failWrite bool
	writes    int
	// Called before each MultiUpdate (tests: a concurrent change between read and write).
	beforeMultiUpdate func()
}

// The save lease every user of newFakeNakama holds (writePayload sends its token).
const (
	testLeaseToken  = "test-lease-token"
	testLeaseServer = "test-server"
)

func newFakeNakama(userIDs ...string) *fakeNakama {
	nk := &fakeNakama{users: map[string]*api.Account{}, storage: map[string]storedObject{}}
	for _, id := range userIDs {
		nk.addUser(id)
	}
	return nk
}

// addUser: an account holding the test lease (seeded, not counted as a write).
func (nk *fakeNakama) addUser(id string) {
	nk.users[id] = &api.Account{User: &api.User{Id: id, Username: "user-" + id[:4]}}
	nk.seedLease(id, leaseRecord{ServerID: testLeaseServer, Token: testLeaseToken, ExpiresAtMs: now().Add(24 * time.Hour).UnixMilli()})
}

func (nk *fakeNakama) seedLease(userID string, lease leaseRecord) {
	value, _ := json.Marshal(lease)
	sum := md5.Sum(value)
	nk.storage[storageKey(collectionLeases, userID, defaultCharacterID)] = storedObject{value: string(value), version: hex.EncodeToString(sum[:])}
}

// copyStorage: for MultiUpdate's all-or-nothing.
func (nk *fakeNakama) copyStorage() map[string]storedObject {
	copied := make(map[string]storedObject, len(nk.storage))
	for key, object := range nk.storage {
		copied[key] = object
	}
	return copied
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
	// All or nothing, like Nakama's transaction.
	saved := nk.copyStorage()
	savedWrites := nk.writes
	acks, err := nk.storageWrite(writes)
	if err != nil {
		nk.storage = saved
		nk.writes = savedWrites
	}
	return acks, err
}

func (nk *fakeNakama) storageWrite(writes []*runtime.StorageWrite) ([]*api.StorageObjectAck, error) {
	if nk.failWrite {
		return nil, errors.New("database is down")
	}
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

func (nk *fakeNakama) StorageDelete(ctx context.Context, deletes []*runtime.StorageDelete) error {
	saved := nk.copyStorage()
	if err := nk.storageDelete(deletes); err != nil {
		nk.storage = saved
		return err
	}
	return nil
}

func (nk *fakeNakama) storageDelete(deletes []*runtime.StorageDelete) error {
	for _, remove := range deletes {
		id := storageKey(remove.Collection, remove.UserID, remove.Key)
		existing, exists := nk.storage[id]
		if remove.Version != "" && (!exists || existing.version != remove.Version) {
			return errors.New("Storage delete rejected - version check failed.")
		}
		delete(nk.storage, id)
	}
	return nil
}

func (nk *fakeNakama) MultiUpdate(ctx context.Context, accountUpdates []*runtime.AccountUpdate, storageWrites []*runtime.StorageWrite,
	storageDeletes []*runtime.StorageDelete, walletUpdates []*runtime.WalletUpdate, updateLedger bool) ([]*api.StorageObjectAck, []*runtime.WalletUpdateResult, error) {
	if hook := nk.beforeMultiUpdate; hook != nil {
		nk.beforeMultiUpdate = nil
		hook()
	}
	saved := nk.copyStorage()
	savedWrites := nk.writes
	acks, err := nk.storageWrite(storageWrites)
	if err == nil {
		err = nk.storageDelete(storageDeletes)
	}
	if err != nil {
		nk.storage = saved
		nk.writes = savedWrites
		return nil, nil, err
	}
	return acks, nil, nil
}

// object returns one stored value (decoded into out); false if absent.
func (nk *fakeNakama) object(collection, userID, key string, out any) bool {
	object, ok := nk.storage[storageKey(collection, userID, key)]
	if !ok {
		return false
	}
	_ = json.Unmarshal([]byte(object.value), out)
	return true
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

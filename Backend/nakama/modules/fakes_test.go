package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/heroiclabs/nakama-common/api"
	"github.com/heroiclabs/nakama-common/runtime"
	"google.golang.org/protobuf/types/known/timestamppb"
	"google.golang.org/protobuf/types/known/wrapperspb"
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
	// Groups (guilds.go mirrors guilds into them); failGroups makes every group call fail.
	groups     map[string]*fakeGroup
	groupSeq   int
	failGroups bool
	// MultiUpdate calls (attempts, refused ones included).
	multiUpdates int
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
	nk.multiUpdates++
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

// --- Groups (the guilds' mirror) ---------------------------------------------------------------------

type fakeGroup struct {
	name    string
	created time.Time
	// user id -> state (0 superadmin, 1 admin, 2 member)
	users map[string]int
}

// groupStates: the mirror of one guild, user id -> state (nil: no such group).
func (nk *fakeNakama) groupStates(groupID string) map[string]int {
	if group, ok := nk.groups[groupID]; ok {
		return group.users
	}
	return nil
}

func (nk *fakeNakama) groupCall() error {
	if nk.failGroups {
		return errors.New("group service down")
	}
	if nk.groups == nil {
		nk.groups = map[string]*fakeGroup{}
	}
	return nil
}

func (nk *fakeNakama) GroupCreate(ctx context.Context, userID, name, creatorID, langTag, description, avatarUrl string, open bool, metadata map[string]interface{}, maxCount int) (*api.Group, error) {
	if err := nk.groupCall(); err != nil {
		return nil, err
	}
	for _, group := range nk.groups {
		if group.name == name {
			return nil, runtime.ErrGroupNameInUse
		}
	}
	nk.groupSeq++
	id := fmt.Sprintf("10000000-0000-4000-8000-%012d", nk.groupSeq)
	nk.groups[id] = &fakeGroup{name: name, created: now(), users: map[string]int{userID: 0}}
	return &api.Group{Id: id, Name: name, CreateTime: timestamppb.New(now())}, nil
}

func (nk *fakeNakama) GroupDelete(ctx context.Context, id string) error {
	if err := nk.groupCall(); err != nil {
		return err
	}
	delete(nk.groups, id)
	return nil
}

func (nk *fakeNakama) GroupsList(ctx context.Context, name, langTag string, members *int, open *bool, limit int, cursor string) ([]*api.Group, string, error) {
	if err := nk.groupCall(); err != nil {
		return nil, "", err
	}
	var found []*api.Group
	for id, group := range nk.groups {
		if strings.EqualFold(group.name, name) {
			found = append(found, &api.Group{Id: id, Name: group.name, CreateTime: timestamppb.New(group.created)})
		}
	}
	return found, "", nil
}

func (nk *fakeNakama) GroupUsersList(ctx context.Context, id string, limit int, state *int, cursor string) ([]*api.GroupUserList_GroupUser, string, error) {
	if err := nk.groupCall(); err != nil {
		return nil, "", err
	}
	group, ok := nk.groups[id]
	if !ok {
		return nil, "", runtime.ErrGroupNotFound
	}
	var users []*api.GroupUserList_GroupUser
	for userID, userState := range group.users {
		users = append(users, &api.GroupUserList_GroupUser{User: &api.User{Id: userID}, State: wrapperspb.Int32(int32(userState))})
	}
	return users, "", nil
}

func (nk *fakeNakama) changeGroupUsers(groupID string, userIDs []string, change func(group *fakeGroup, userID string) error) error {
	if err := nk.groupCall(); err != nil {
		return err
	}
	group, ok := nk.groups[groupID]
	if !ok {
		return runtime.ErrGroupNotFound
	}
	for _, userID := range userIDs {
		if err := change(group, userID); err != nil {
			return err
		}
	}
	return nil
}

func (nk *fakeNakama) GroupUsersAdd(ctx context.Context, callerID, groupID string, userIDs []string) error {
	return nk.changeGroupUsers(groupID, userIDs, func(group *fakeGroup, userID string) error {
		if _, in := group.users[userID]; !in {
			group.users[userID] = 2
		}
		return nil
	})
}

func (nk *fakeNakama) superadmins(group *fakeGroup, except string) int {
	count := 0
	for userID, state := range group.users {
		if state == 0 && userID != except {
			count++
		}
	}
	return count
}

func (nk *fakeNakama) GroupUsersKick(ctx context.Context, callerID, groupID string, userIDs []string) error {
	return nk.changeGroupUsers(groupID, userIDs, func(group *fakeGroup, userID string) error {
		if group.users[userID] == 0 && nk.superadmins(group, userID) == 0 {
			return runtime.ErrGroupLastSuperadmin
		}
		delete(group.users, userID)
		return nil
	})
}

func (nk *fakeNakama) GroupUsersPromote(ctx context.Context, callerID, groupID string, userIDs []string) error {
	return nk.changeGroupUsers(groupID, userIDs, func(group *fakeGroup, userID string) error {
		if state, in := group.users[userID]; in && state > 0 {
			group.users[userID] = state - 1
		}
		return nil
	})
}

func (nk *fakeNakama) GroupUsersDemote(ctx context.Context, callerID, groupID string, userIDs []string) error {
	return nk.changeGroupUsers(groupID, userIDs, func(group *fakeGroup, userID string) error {
		state, in := group.users[userID]
		if !in || state >= 2 {
			return nil
		}
		if state == 0 && nk.superadmins(group, userID) == 0 {
			return runtime.ErrGroupLastSuperadmin
		}
		group.users[userID] = state + 1
		return nil
	})
}

// FieldMesh: tests for the identity/prefs backup (examples/companion_radio/IdentityBackup.h).
// v2.2.1: the identity file must have exactly 96 bytes. A truncated identity must never replace
// a good backup, and at boot a truncated primary file is restored from the backup like a
// missing one. Upstream IdentityStore::save() reports success even when the write failed.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Arduino.h>

// --- minimal in-memory stand-in for Adafruit_LittleFS (only what IdentityBackup uses) ---

#define FILE_O_READ  0
#define FILE_O_WRITE 1

namespace {

using Bytes = std::vector<uint8_t>;

struct MockFS;

class MockFile {
  MockFS* _fs = nullptr;
  std::string _path;
  bool _write = false;
  size_t _pos = 0;

public:
  MockFile() { }
  MockFile(MockFS* fs, const std::string& path, bool write) : _fs(fs), _path(path), _write(write) { }

  explicit operator bool() const { return _fs != nullptr; }
  size_t size() const;
  int read(uint8_t* buf, size_t n);
  size_t write(const uint8_t* buf, size_t n);
  void close() { _fs = nullptr; }
};

struct MockFS {
  std::map<std::string, Bytes> files;
  size_t write_limit = SIZE_MAX;   // bytes accepted per write() call (simulates a short write)

  bool exists(const char* path) const { return files.count(path) > 0; }
  MockFile open(const char* path, int mode) {
    if (mode == FILE_O_READ) {
      if (!exists(path)) return MockFile();
      return MockFile(this, path, false);
    }
    files[path];   // create (Adafruit FILE_O_WRITE appends to an existing file)
    return MockFile(this, path, true);
  }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
};

size_t MockFile::size() const { return _fs ? _fs->files[_path].size() : 0; }

int MockFile::read(uint8_t* buf, size_t n) {
  if (!_fs || _write) return -1;
  const Bytes& data = _fs->files[_path];
  if (_pos >= data.size()) return 0;
  size_t k = std::min(n, data.size() - _pos);
  memcpy(buf, &data[_pos], k);
  _pos += k;
  return (int)k;
}

size_t MockFile::write(const uint8_t* buf, size_t n) {
  if (!_fs || !_write) return 0;
  size_t k = std::min(n, _fs->write_limit);
  Bytes& data = _fs->files[_path];
  data.insert(data.end(), buf, buf + k);
  return k;
}

}  // namespace

#define FILESYSTEM MockFS
using File = MockFile;
#define MESH_DEBUG_PRINTLN(...) do { } while (0)
#define FM_IDBACKUP_TEST 1

#include "../../examples/companion_radio/IdentityBackup.h"

namespace {

Bytes pattern(size_t n, uint8_t seed) {
  Bytes b(n);
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 7);
  return b;
}

const Bytes GOOD_ID = pattern(FM_ID_FILE_SIZE, 1);
const Bytes OTHER_ID = pattern(FM_ID_FILE_SIZE, 99);
const Bytes PREFS = pattern(300, 5);

class IdentityBackupTest : public ::testing::Test {
protected:
  MockFS primary;
  MockFS secondary;
};

// --- restore at boot ---

TEST_F(IdentityBackupTest, RestoresMissingIdentity) {
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, primary.files[FM_ID_FILE]);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, RestoresTruncatedIdentity) {
  primary.files[FM_ID_FILE] = Bytes(GOOD_ID.begin(), GOOD_ID.begin() + 40);
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, primary.files[FM_ID_FILE]);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);   // backup not overwritten
}

TEST_F(IdentityBackupTest, RestoresEmptyIdentity) {
  primary.files[FM_ID_FILE] = Bytes();
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, primary.files[FM_ID_FILE]);
}

TEST_F(IdentityBackupTest, RestoresOversizedIdentity) {
  Bytes longer = GOOD_ID;
  longer.resize(FM_ID_FILE_SIZE + 32, 0xAA);
  primary.files[FM_ID_FILE] = longer;
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, primary.files[FM_ID_FILE]);
}

TEST_F(IdentityBackupTest, TruncatedIdentityWithoutBackupStaysAndIsNotBackedUp) {
  Bytes short_id(GOOD_ID.begin(), GOOD_ID.begin() + 40);
  primary.files[FM_ID_FILE] = short_id;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(short_id, primary.files[FM_ID_FILE]);   // nothing to restore from
  EXPECT_FALSE(secondary.exists(FM_ID_BACKUP));
}

TEST_F(IdentityBackupTest, GoodIdentityIsNeverReplacedByBackup) {
  primary.files[FM_ID_FILE] = OTHER_ID;
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(OTHER_ID, primary.files[FM_ID_FILE]);    // primary wins (e.g. imported key)
  EXPECT_EQ(OTHER_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, BrokenBackupIsRefreshedFromGoodIdentity) {
  primary.files[FM_ID_FILE] = GOOD_ID;
  secondary.files[FM_ID_BACKUP] = Bytes(GOOD_ID.begin(), GOOD_ID.begin() + 10);
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, primary.files[FM_ID_FILE]);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, FirstBackupIsCreated) {
  primary.files[FM_ID_FILE] = GOOD_ID;
  primary.files[FM_PREFS_FILE] = PREFS;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
  EXPECT_EQ(PREFS, secondary.files[FM_PREFS_BACKUP]);
}

// --- backup after saving ---

TEST_F(IdentityBackupTest, SavedIdentityIsBackedUp) {
  primary.files[FM_ID_FILE] = OTHER_ID;
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::identitySaved(&primary, &secondary);
  EXPECT_EQ(OTHER_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, FailedIdentityWriteKeepsBackup) {
  // upstream IdentityStore::save() returns true even if only part of the file was written
  primary.files[FM_ID_FILE] = Bytes(OTHER_ID.begin(), OTHER_ID.begin() + 32);
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::identitySaved(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, EmptyIdentityFileKeepsBackup) {
  primary.files[FM_ID_FILE] = Bytes();
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  IdentityBackup::identitySaved(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
}

TEST_F(IdentityBackupTest, ShortWriteToBackupKeepsOldBackup) {
  primary.files[FM_ID_FILE] = OTHER_ID;
  secondary.files[FM_ID_BACKUP] = GOOD_ID;
  secondary.write_limit = 10;   // copy to the temp file fails
  IdentityBackup::identitySaved(&primary, &secondary);
  EXPECT_EQ(GOOD_ID, secondary.files[FM_ID_BACKUP]);
  EXPECT_FALSE(secondary.exists(FM_BACKUP_TMP));
}

// --- prefs keep their behaviour (any size) ---

TEST_F(IdentityBackupTest, PrefsOfAnySizeAreBackedUp) {
  Bytes small_prefs = pattern(17, 3);
  primary.files[FM_PREFS_FILE] = small_prefs;
  IdentityBackup::prefsSaved(&primary, &secondary);
  EXPECT_EQ(small_prefs, secondary.files[FM_PREFS_BACKUP]);
}

TEST_F(IdentityBackupTest, MissingPrefsAreRestored) {
  secondary.files[FM_PREFS_BACKUP] = PREFS;
  IdentityBackup::begin(&primary, &secondary);
  EXPECT_EQ(PREFS, primary.files[FM_PREFS_FILE]);
}

TEST_F(IdentityBackupTest, NullFileSystemsAreIgnored) {
  IdentityBackup::begin(nullptr, &secondary);
  IdentityBackup::begin(&primary, nullptr);
  IdentityBackup::identitySaved(nullptr, nullptr);
  IdentityBackup::prefsSaved(nullptr, nullptr);
  EXPECT_TRUE(secondary.files.empty());
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

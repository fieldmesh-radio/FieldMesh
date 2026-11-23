// FieldMesh: tests for the local block list (examples/companion_radio/BlockList.h).
// A wrong key comparison would either let the blocked node through or drop adverts of
// nodes that were never blocked, so matching, list handling and command parsing are
// checked here.

#include <gtest/gtest.h>

#include <string>

#include "../../examples/companion_radio/BlockList.h"

namespace {

// 32-byte public key, first byte = seed, rest derived from it
struct Key {
  uint8_t b[32];
  explicit Key(uint8_t seed) {
    for (int i = 0; i < 32; i++) b[i] = (uint8_t)(seed + i * 7);
  }
};

FmBlockCmd parse(const char* text, int* entry = nullptr) {
  int dummy = -1;
  return fmBlockParse(text, entry ? entry : &dummy);
}

std::string listText(const FmBlockList& list) {
  char out[180];
  fmBlockListText(list, out, sizeof(out));
  return out;
}

}  // namespace

// ── list ─────────────────────────────────────────────────────────────────────

TEST(FieldMeshBlockList, EmptyByDefault) {
  FmBlockList list;
  EXPECT_EQ(0, list.count());
  EXPECT_FALSE(list.contains(Key(1).b));
}

TEST(FieldMeshBlockList, AddedKeyIsBlocked) {
  FmBlockList list;
  EXPECT_EQ(0, list.add(Key(1).b, "Far Away"));
  EXPECT_TRUE(list.contains(Key(1).b));
  EXPECT_FALSE(list.contains(Key(2).b));
  EXPECT_STREQ("Far Away", list.names[0]);
}

TEST(FieldMeshBlockList, OnlyTheKeyPrefixIsCompared) {
  FmBlockList list;
  Key k(1);
  list.add(k.b, "A");
  Key same_prefix(1);
  same_prefix.b[FM_BLOCK_KEY_LEN] ^= 0xFF;   // differs after the compared prefix
  EXPECT_TRUE(list.contains(same_prefix.b));
  Key other(1);
  other.b[FM_BLOCK_KEY_LEN - 1] ^= 0x01;     // differs inside the prefix
  EXPECT_FALSE(list.contains(other.b));
}

TEST(FieldMeshBlockList, AddingTwiceKeepsOneEntryAndUpdatesTheName) {
  FmBlockList list;
  EXPECT_EQ(0, list.add(Key(1).b, "Old"));
  EXPECT_EQ(0, list.add(Key(1).b, "New"));
  EXPECT_EQ(1, list.count());
  EXPECT_STREQ("New", list.names[0]);
}

TEST(FieldMeshBlockList, FullListRefusesNewEntries) {
  FmBlockList list;
  for (int i = 0; i < FM_BLOCK_MAX; i++) EXPECT_EQ(i, list.add(Key(10 + i).b, "x"));
  EXPECT_EQ(FM_BLOCK_MAX, list.count());
  EXPECT_EQ(-1, list.add(Key(99).b, "y"));
  EXPECT_FALSE(list.contains(Key(99).b));
  EXPECT_EQ(3, list.add(Key(13).b, "again"));   // existing entry still works
}

TEST(FieldMeshBlockList, AllZeroKeyIsRefused) {
  FmBlockList list;
  uint8_t zero[32] = {};
  EXPECT_EQ(-1, list.add(zero, "x"));
  EXPECT_EQ(0, list.count());
  EXPECT_FALSE(list.contains(zero));
}

TEST(FieldMeshBlockList, RemoveMovesTheRestUp) {
  FmBlockList list;
  list.add(Key(1).b, "A");
  list.add(Key(2).b, "B");
  list.add(Key(3).b, "C");
  EXPECT_TRUE(list.remove(1));
  EXPECT_EQ(2, list.count());
  EXPECT_TRUE(list.contains(Key(1).b));
  EXPECT_FALSE(list.contains(Key(2).b));
  EXPECT_TRUE(list.contains(Key(3).b));
  EXPECT_STREQ("C", list.names[1]);
  EXPECT_FALSE(list.remove(2));    // now unused
  EXPECT_FALSE(list.remove(-1));
  EXPECT_FALSE(list.remove(FM_BLOCK_MAX));
}

TEST(FieldMeshBlockList, RemoveLastSlotOfAFullList) {
  FmBlockList list;
  for (int i = 0; i < FM_BLOCK_MAX; i++) list.add(Key(10 + i).b, "x");
  EXPECT_TRUE(list.remove(FM_BLOCK_MAX - 1));
  EXPECT_EQ(FM_BLOCK_MAX - 1, list.count());
  EXPECT_FALSE(list.contains(Key(10 + FM_BLOCK_MAX - 1).b));
}

TEST(FieldMeshBlockList, ClearEmptiesTheList) {
  FmBlockList list;
  list.add(Key(1).b, "A");
  list.add(Key(2).b, "B");
  list.clear();
  EXPECT_EQ(0, list.count());
  EXPECT_FALSE(list.contains(Key(1).b));
}

TEST(FieldMeshBlockList, LongNamesAreCutOnACharacterBoundary) {
  FmBlockList list;
  list.add(Key(1).b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  EXPECT_STREQ("ABCDEFGHIJKLMNO", list.names[0]);   // 15 bytes + zero
  // 14 ASCII bytes + a 4-byte emoji: the emoji must not be split
  list.add(Key(2).b, "ABCDEFGHIJKLMN\xF0\x9F\x93\xA1");
  EXPECT_STREQ("ABCDEFGHIJKLMN", list.names[1]);
  list.add(Key(3).b, nullptr);
  EXPECT_STREQ("", list.names[2]);
}

// ── commands ─────────────────────────────────────────────────────────────────

TEST(FieldMeshBlockList, ParsesBlock) {
  EXPECT_EQ(FM_BLOCK_CMD_BLOCK, parse("!block"));
  EXPECT_EQ(FM_BLOCK_CMD_BLOCK, parse("  !Block \n"));
  EXPECT_EQ(FM_BLOCK_CMD_BLOCK, parse("!BLOCK"));
}

TEST(FieldMeshBlockList, NormalMessagesAreNotCommands) {
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("!block this guy"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("!blocked"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("block"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("hello !block"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("!unblocked"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse("!blocklist please"));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse(""));
  EXPECT_EQ(FM_BLOCK_CMD_NONE, parse(nullptr));
}

TEST(FieldMeshBlockList, ParsesListAndUnblock) {
  int entry = -1;
  EXPECT_EQ(FM_BLOCK_CMD_LIST, parse("!blocklist"));
  EXPECT_EQ(FM_BLOCK_CMD_LIST, parse(" !BlockList "));
  EXPECT_EQ(FM_BLOCK_CMD_UNBLOCK_ALL, parse("!unblock all"));
  EXPECT_EQ(FM_BLOCK_CMD_UNBLOCK_ALL, parse("!Unblock  ALL "));
  EXPECT_EQ(FM_BLOCK_CMD_UNBLOCK_ONE, parse("!unblock 1", &entry));
  EXPECT_EQ(0, entry);
  EXPECT_EQ(FM_BLOCK_CMD_UNBLOCK_ONE, parse("!unblock 8", &entry));
  EXPECT_EQ(7, entry);
}

TEST(FieldMeshBlockList, BadUnblockArgumentsAreInvalid) {
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock 0"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock 9"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock 1000"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock 2x"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock alls"));
  EXPECT_EQ(FM_BLOCK_CMD_INVALID, parse("!unblock NodeName"));
}

// ── list text ────────────────────────────────────────────────────────────────

TEST(FieldMeshBlockList, ListTextEmpty) {
  FmBlockList list;
  EXPECT_EQ("Blocked 0/8: none", listText(list));
}

TEST(FieldMeshBlockList, ListTextShowsNumberedNames) {
  FmBlockList list;
  list.add(Key(1).b, "Node A");
  list.add(Key(2).b, "Node B");
  EXPECT_EQ("Blocked 2/8: 1 Node A, 2 Node B", listText(list));
}

TEST(FieldMeshBlockList, ListTextWithoutNameShowsTheKeyPrefix) {
  FmBlockList list;
  uint8_t key[32] = { 0xA1, 0xB2, 0xC3, 0xD4, 0x01, 0x02, 0x03, 0x04 };
  list.add(key, "");
  EXPECT_EQ("Blocked 1/8: 1 a1b2c3d4", listText(list));
}

TEST(FieldMeshBlockList, FullListTextFitsIntoOneMessage) {
  FmBlockList list;
  for (int i = 0; i < FM_BLOCK_MAX; i++) list.add(Key(10 + i).b, "ABCDEFGHIJKLMNO");
  std::string text = listText(list);
  EXPECT_EQ(std::string::npos, text.find("ABCDEFGHIJKLM"));    // names cut to 12 bytes
  EXPECT_NE(std::string::npos, text.find(", 8 ABCDEFGHIJKL")); // last entry complete
  EXPECT_LE(text.size() + strlen("FieldMesh: "), 160u);   // MAX_TEXT_LEN
}

TEST(FieldMeshBlockList, ListTextNeverOverflowsTheBuffer) {
  FmBlockList list;
  for (int i = 0; i < FM_BLOCK_MAX; i++) list.add(Key(10 + i).b, "ABCDEFGHIJKLMNO");
  char small[20];
  memset(small, 'Z', sizeof(small));
  fmBlockListText(list, small, sizeof(small));
  EXPECT_EQ(sizeof(small) - 1, strlen(small));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
